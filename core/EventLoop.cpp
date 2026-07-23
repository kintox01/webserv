#include "EventLoop.hpp"
#include "../utils/Logger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <csignal>
#include <algorithm>
#include <set>

static volatile bool g_running = true;
static void          sigHandler(int) { g_running = false; }

static void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

EventLoop::EventLoop(const std::vector<ServerBlock>& servers)
    : _servers(servers) {
    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGPIPE, SIG_IGN);
    setupListenSockets();
}

EventLoop::~EventLoop() {
    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it) {
        close(it->first);
        delete it->second;
    }
    for (size_t i = 0; i < _listenFds.size(); i++)
        close(_listenFds[i]);
}

void EventLoop::setupListenSockets() {
    std::set<int> seenPorts;

    for (size_t i = 0; i < _servers.size(); i++) {
        int port = _servers[i].port;
        if (seenPorts.count(port)) continue;
        seenPorts.insert(port);

        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags    = AI_NUMERICHOST | AI_NUMERICSERV;

        std::ostringstream portStr;
        portStr << port;

        struct addrinfo* res;
        int gaiErr = getaddrinfo(_servers[i].host.c_str(), portStr.str().c_str(), &hints, &res);
        if (gaiErr != 0) {
            std::ostringstream ss;
            ss << "getaddrinfo() failed for " << _servers[i].host << ":" << port
               << ": " << gai_strerror(gaiErr);
            throw std::runtime_error(ss.str());
        }

        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) {
            freeaddrinfo(res);
            throw std::runtime_error("socket() failed");
        }

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setNonBlocking(fd);

        if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
            std::ostringstream ss;
            ss << "bind() failed on port " << port << ": " << strerror(errno);
            freeaddrinfo(res);
            close(fd);
            throw std::runtime_error(ss.str());
        }
        freeaddrinfo(res);
        if (listen(fd, SOMAXCONN) < 0) {
            close(fd);
            throw std::runtime_error("listen() failed");
        }

        _listenFds.push_back(fd);
        _listenPorts.push_back(port);

        std::ostringstream ss;
        ss << "Serveur en ecoute sur " << _servers[i].host << ":" << port;
        Logger::info(ss.str());
    }
}

void EventLoop::acceptConnection(int listenFd, int port) {
    struct sockaddr_in addr;
    socklen_t          addrLen = sizeof(addr);
    int clientFd = accept(listenFd, (struct sockaddr*)&addr, &addrLen);
    if (clientFd < 0) return;

    setNonBlocking(clientFd);
    _connections[clientFd] = new Connection(clientFd, port, _servers);
}

void EventLoop::closeConnection(int fd) {
    std::map<int, Connection*>::iterator it = _connections.find(fd);
    if (it != _connections.end()) {
        delete it->second;
        _connections.erase(it);
    }
    close(fd);
}

void EventLoop::checkTimeouts() {
    std::vector<int> toClose;
    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it) {
        if (it->second->cgiTimedOut())
            it->second->abortCgi(504);
        if (it->second->timedOut() || it->second->state == CONN_CLOSING)
            toClose.push_back(it->first);
    }
    for (size_t i = 0; i < toClose.size(); i++)
        closeConnection(toClose[i]);
}

enum PollKind { POLL_LISTEN, POLL_CLIENT, POLL_CGI_OUT, POLL_CGI_IN };

void EventLoop::run() {
    while (g_running) {
        std::vector<struct pollfd> fds;
        std::vector<PollKind>      kinds;
        std::vector<int>           owners;

        for (size_t i = 0; i < _listenFds.size(); i++) {
            struct pollfd pfd;
            pfd.fd = _listenFds[i]; pfd.events = POLLIN; pfd.revents = 0;
            fds.push_back(pfd); kinds.push_back(POLL_LISTEN); owners.push_back(-1);
        }

        for (std::map<int, Connection*>::iterator it = _connections.begin();
             it != _connections.end(); ++it) {
            Connection* conn = it->second;
            struct pollfd pfd;
            pfd.revents = 0;

            if (conn->state == CONN_READING) {
                pfd.fd = it->first; pfd.events = POLLIN;
                fds.push_back(pfd); kinds.push_back(POLL_CLIENT); owners.push_back(it->first);
            } else if (conn->state == CONN_WRITING) {
                pfd.fd = it->first; pfd.events = POLLOUT;
                fds.push_back(pfd); kinds.push_back(POLL_CLIENT); owners.push_back(it->first);
            } else if (conn->state == CONN_CGI) {
                if (conn->cgiStdout != -1) {
                    pfd.fd = conn->cgiStdout; pfd.events = POLLIN; pfd.revents = 0;
                    fds.push_back(pfd); kinds.push_back(POLL_CGI_OUT); owners.push_back(it->first);
                }
                if (conn->cgiStdin != -1) {
                    pfd.fd = conn->cgiStdin; pfd.events = POLLOUT; pfd.revents = 0;
                    fds.push_back(pfd); kinds.push_back(POLL_CGI_IN); owners.push_back(it->first);
                }
            }
        }

        if (fds.empty()) { checkTimeouts(); continue; }
        int ready = poll(&fds[0], (nfds_t)fds.size(), 5000);
        if (ready < 0) {
            if (errno == EINTR) continue;
            Logger::error(std::string("poll() error: ") + strerror(errno));
            break;
        }
        if (ready == 0) {
            checkTimeouts();
            continue;
        }

        std::vector<int> toClose;
        for (size_t i = 0; i < fds.size(); i++) {
            if (fds[i].revents == 0) continue;

            if (kinds[i] == POLL_LISTEN) {
                if (fds[i].revents & POLLIN)
                    acceptConnection(fds[i].fd, _listenPorts[i]);
                continue;
            }

            std::map<int, Connection*>::iterator it = _connections.find(owners[i]);
            if (it == _connections.end()) continue;
            Connection* conn = it->second;

            if (kinds[i] == POLL_CLIENT) {
                if (fds[i].revents & (POLLERR | POLLHUP)) { toClose.push_back(owners[i]); continue; }
                if ((fds[i].revents & POLLIN)  && conn->state == CONN_READING) {
                    if (!conn->onReadable()) { toClose.push_back(owners[i]); continue; }
                }
                if ((fds[i].revents & POLLOUT) && conn->state == CONN_WRITING) {
                    if (!conn->onWritable()) { toClose.push_back(owners[i]); continue; }
                }
            } else if (kinds[i] == POLL_CGI_OUT) {
                if (fds[i].revents & (POLLIN | POLLHUP | POLLERR))
                    conn->onCgiReadable();
            } else if (kinds[i] == POLL_CGI_IN) {
                if (fds[i].revents & (POLLOUT | POLLERR))
                    conn->onCgiWritable();
            }

            if (conn->state == CONN_CLOSING || conn->timedOut())
                toClose.push_back(owners[i]);
        }

        std::sort(toClose.begin(), toClose.end());
        toClose.erase(std::unique(toClose.begin(), toClose.end()), toClose.end());
        for (size_t i = 0; i < toClose.size(); i++)
            closeConnection(toClose[i]);

        checkTimeouts();
    }
    Logger::info("Serveur arrete.");
}
