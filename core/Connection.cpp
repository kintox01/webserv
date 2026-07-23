#include "Connection.hpp"
#include "../http/HttpResponse.hpp"
#include "../http/Cgi.hpp"
#include "../utils/Logger.hpp"
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <ctime>

static const double CGI_TIMEOUT_SECONDS = 10.0;

Connection::Connection(int fd, int port, const std::vector<ServerBlock>& servers)
    : fd(fd), port(port), state(CONN_READING),
      lastActivity(time(NULL)), writePos(0),
      cgiPid(-1), cgiStdin(-1), cgiStdout(-1), cgiBodyPos(0), cgiStart(0),
      _servers(servers), _shouldClose(false) {}

Connection::~Connection() {
    if (cgiStdin  != -1) close(cgiStdin);
    if (cgiStdout != -1) close(cgiStdout);
    if (cgiPid > 0) {
        kill(cgiPid, SIGKILL);
        waitpid(cgiPid, NULL, 0);
    }
}

bool Connection::timedOut() const {
    return difftime(time(NULL), lastActivity) > 60.0;
}

bool Connection::cgiTimedOut() const {
    return state == CONN_CGI && difftime(time(NULL), cgiStart) > CGI_TIMEOUT_SECONDS;
}

const ServerBlock* Connection::selectServer() const {
    std::string host = request.header("host");
    size_t colon = host.find(':');
    if (colon != std::string::npos)
        host = host.substr(0, colon);

    const ServerBlock* defaultSrv = NULL;
    for (size_t i = 0; i < _servers.size(); i++) {
        if (_servers[i].port != port) continue;
        if (!defaultSrv) defaultSrv = &_servers[i];

        for (size_t j = 0; j < _servers[i].server_names.size(); j++) {
            if (_servers[i].server_names[j] == host)
                return &_servers[i];
        }
    }
    return defaultSrv;
}

void Connection::finalizeResponse(HttpResponse& res) {
    res.setHeader("Connection", _shouldClose ? "close" : "keep-alive");
    writeBuffer = res.build();
    writePos    = 0;
    state       = CONN_WRITING;
}

void Connection::buildAndSend() {
    const ServerBlock* srv = selectServer();
    _shouldClose = !request.keepAlive();

    if (!srv) {
        HttpResponse res;
        res.setStatus(500);
        res.setBody("No server configured", "text/plain");
        request.reset();
        finalizeResponse(res);
        return;
    }

    Router            router(*srv);
    Router::CgiRequest cgiReq;
    HttpResponse       res;

    if (router.handle(request, res, cgiReq)) {
        request.reset();
        finalizeResponse(res);
        return;
    }

    if (!startCgi(*srv, cgiReq)) {
        HttpResponse err;
        err.setStatus(502);
        err.setBody(HttpResponse::defaultErrorPage(502), "text/html");
        request.reset();
        finalizeResponse(err);
    }
}

bool Connection::startCgi(const ServerBlock& srv, const Router::CgiRequest& cgiReq) {
    pid_t pid; int inFd, outFd;
    if (!Cgi::start(request, cgiReq.scriptPath, cgiReq.interpreter, srv, pid, inFd, outFd))
        return false;

    cgiPid     = pid;
    cgiStdin   = inFd;
    cgiStdout  = outFd;
    cgiBody    = request.body;
    cgiBodyPos = 0;
    cgiOutput.clear();
    cgiStart   = time(NULL);
    state      = CONN_CGI;

    if (cgiBody.empty()) {
        close(cgiStdin);
        cgiStdin = -1;
    }

    request.reset();
    return true;
}

void Connection::finishCgi() {
    if (cgiStdin != -1) { close(cgiStdin); cgiStdin = -1; }
    if (cgiPid > 0) {
        int status;
        waitpid(cgiPid, &status, 0);
        cgiPid = -1;
    }
    HttpResponse res = Cgi::buildResponse(cgiOutput);
    finalizeResponse(res);
}

void Connection::abortCgi(int statusCode) {
    if (cgiPid > 0) {
        kill(cgiPid, SIGKILL);
        waitpid(cgiPid, NULL, 0);
        cgiPid = -1;
    }
    if (cgiStdin  != -1) { close(cgiStdin);  cgiStdin  = -1; }
    if (cgiStdout != -1) { close(cgiStdout); cgiStdout = -1; }

    HttpResponse res;
    res.setStatus(statusCode);
    res.setBody(HttpResponse::defaultErrorPage(statusCode), "text/html");
    finalizeResponse(res);
}

bool Connection::onReadable() {
    char    buf[8192];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);

    if (n < 0) return false;
    if (n == 0) return false;

    lastActivity = time(NULL);

    size_t maxBody = 1048576;
    for (size_t i = 0; i < _servers.size(); i++) {
        if (_servers[i].port == port) {
            maxBody = _servers[i].client_max_body_size;
            break;
        }
    }

    bool complete = request.feed(buf, (size_t)n, maxBody);

    if (request.state == PARSE_ERROR) {
        HttpResponse res;
        res.setStatus(request.error_code);
        res.setBody(HttpResponse::defaultErrorPage(request.error_code), "text/html");
        _shouldClose = true;
        finalizeResponse(res);
        return true;
    }

    if (complete)
        buildAndSend();

    return true;
}

bool Connection::onWritable() {
    if (writePos >= writeBuffer.size()) {
        state = CONN_READING;
        return true;
    }

    ssize_t n = send(fd, writeBuffer.c_str() + writePos, writeBuffer.size() - writePos, 0);

    if (n < 0) return false;
    if (n == 0) return true;

    lastActivity  = time(NULL);
    writePos     += (size_t)n;

    if (writePos >= writeBuffer.size()) {
        state = _shouldClose ? CONN_CLOSING : CONN_READING;
    }
    return true;
}

bool Connection::onCgiReadable() {
    char    buf[4096];
    ssize_t n = read(cgiStdout, buf, sizeof(buf));

    if (n > 0) {
        cgiOutput.append(buf, (size_t)n);
        return true;
    }

    close(cgiStdout);
    cgiStdout = -1;
    finishCgi();
    return true;
}

bool Connection::onCgiWritable() {
    if (cgiStdin == -1) return true;

    size_t remaining = cgiBody.size() - cgiBodyPos;
    if (remaining == 0) {
        close(cgiStdin);
        cgiStdin = -1;
        return true;
    }

    ssize_t n = write(cgiStdin, cgiBody.data() + cgiBodyPos, remaining);

    if (n < 0) {
        close(cgiStdin);
        cgiStdin = -1;
        return true;
    }
    if (n == 0) return true;

    cgiBodyPos += (size_t)n;
    if (cgiBodyPos >= cgiBody.size()) {
        close(cgiStdin);
        cgiStdin = -1;
    }
    return true;
}
