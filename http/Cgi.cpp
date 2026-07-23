#include "Cgi.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <cctype>
#include <vector>
#include <map>

static std::string intStr(int n) {
    std::ostringstream ss; ss << n; return ss.str();
}

static void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool Cgi::start(const HttpRequest& req,
                const std::string& scriptPath,
                const std::string& interpreter,
                const ServerBlock& server,
                pid_t& pid, int& inFd, int& outFd) {
    int stdinPipe[2], stdoutPipe[2];
    if (pipe(stdinPipe) < 0) return false;
    if (pipe(stdoutPipe) < 0) {
        close(stdinPipe[0]); close(stdinPipe[1]);
        return false;
    }

    pid = fork();
    if (pid < 0) {
        close(stdinPipe[0]);  close(stdinPipe[1]);
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        return false;
    }

    if (pid == 0) {
        dup2(stdinPipe[0],  STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        close(stdinPipe[0]);  close(stdinPipe[1]);
        close(stdoutPipe[0]); close(stdoutPipe[1]);

        std::string dir, filename;
        size_t      sep = scriptPath.rfind('/');
        if (sep != std::string::npos) {
            dir      = scriptPath.substr(0, sep);
            filename = scriptPath.substr(sep + 1);
        } else {
            dir      = ".";
            filename = scriptPath;
        }
        chdir(dir.c_str());

        std::vector<std::string> envStrs;
        envStrs.push_back("REQUEST_METHOD="    + req.method);
        envStrs.push_back("QUERY_STRING="      + req.query);
        envStrs.push_back("CONTENT_LENGTH="    + intStr((int)req.body.size()));
        envStrs.push_back("CONTENT_TYPE="      + req.header("content-type"));
        envStrs.push_back("SCRIPT_FILENAME="   + filename);
        envStrs.push_back("SCRIPT_NAME="       + req.path);
        envStrs.push_back("PATH_INFO="         + req.path);
        envStrs.push_back("SERVER_NAME="       + server.host);
        envStrs.push_back("SERVER_PORT="       + intStr(server.port));
        envStrs.push_back("SERVER_PROTOCOL=HTTP/1.1");
        envStrs.push_back("GATEWAY_INTERFACE=CGI/1.1");
        envStrs.push_back("REDIRECT_STATUS=200");
        envStrs.push_back("HTTP_HOST="         + req.header("host"));

        for (std::map<std::string, std::string>::const_iterator it = req.headers.begin();
             it != req.headers.end(); ++it) {
            std::string key = "HTTP_";
            for (size_t i = 0; i < it->first.size(); i++) {
                char c = it->first[i];
                key += (c == '-') ? '_' : (char)std::toupper((unsigned char)c);
            }
            envStrs.push_back(key + "=" + it->second);
        }

        std::vector<char*> env;
        for (size_t i = 0; i < envStrs.size(); i++)
            env.push_back(const_cast<char*>(envStrs[i].c_str()));
        env.push_back(NULL);

        char* argv[3];
        argv[0] = const_cast<char*>(interpreter.c_str());
        argv[1] = const_cast<char*>(filename.c_str());
        argv[2] = NULL;

        execve(interpreter.c_str(), argv, &env[0]);
        const char* err = "Status: 500\r\nContent-Type: text/plain\r\n\r\nexecve failed\n";
        write(STDOUT_FILENO, err, strlen(err));
        exit(1);
    }

    close(stdinPipe[0]);
    close(stdoutPipe[1]);

    setNonBlocking(stdinPipe[1]);
    setNonBlocking(stdoutPipe[0]);

    inFd  = stdinPipe[1];
    outFd = stdoutPipe[0];
    return true;
}

HttpResponse Cgi::buildResponse(const std::string& output) {
    size_t sep    = output.find("\r\n\r\n");
    size_t sepLen = 4;
    if (sep == std::string::npos) {
        sep    = output.find("\n\n");
        sepLen = 2;
    }

    HttpResponse res;
    if (sep == std::string::npos) {
        res.setStatus(502);
        res.setBody("CGI: pas de separateur d'en-tetes", "text/plain");
        return res;
    }

    std::string headerPart = output.substr(0, sep);
    std::string bodyPart   = output.substr(sep + sepLen);

    int         cgiStatus   = 200;
    std::string contentType = "text/html";

    std::istringstream hs(headerPart);
    std::string        line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line = line.substr(0, line.size() - 1);
        if (line.size() > 7 && line.substr(0, 7) == "Status:") {
            cgiStatus = std::atoi(line.substr(7).c_str());
        } else if (line.size() > 13 && line.substr(0, 13) == "Content-Type:") {
            contentType = line.substr(14);
            while (!contentType.empty() && contentType[0] == ' ')
                contentType = contentType.substr(1);
        } else if (line.size() > 9 && line.substr(0, 9) == "Location:") {
            std::string loc_url = line.substr(9);
            while (!loc_url.empty() && loc_url[0] == ' ') loc_url = loc_url.substr(1);
            res.setStatus(302);
            res.setHeader("Location", loc_url);
            res.setBody("", "text/html");
            return res;
        }
    }

    res.setStatus(cgiStatus);
    res.setBody(bodyPart, contentType);
    return res;
}
