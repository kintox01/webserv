#pragma once
#include <string>
#include <vector>
#include <ctime>
#include <sys/types.h>
#include "../config/ServerBlock.hpp"
#include "../http/HttpRequest.hpp"
#include "../http/Router.hpp"

enum ConnState {
    CONN_READING,
    CONN_CGI,
    CONN_WRITING,
    CONN_CLOSING
};

class Connection {
public:
    int         fd;
    int         port;
    ConnState   state;
    time_t      lastActivity;

    HttpRequest request;
    std::string writeBuffer;
    size_t      writePos;

    pid_t       cgiPid;
    int         cgiStdin;
    int         cgiStdout;
    std::string cgiBody;
    size_t      cgiBodyPos;
    std::string cgiOutput;
    time_t      cgiStart;

    Connection(int fd, int port, const std::vector<ServerBlock>& servers);
    ~Connection();

    bool onReadable();
    bool onWritable();
    bool onCgiReadable();
    bool onCgiWritable();
    bool timedOut() const;
    bool cgiTimedOut() const;
    void abortCgi(int statusCode);

private:
    const std::vector<ServerBlock>& _servers;
    bool                            _shouldClose;

    const ServerBlock* selectServer() const;
    void               buildAndSend();
    bool               startCgi(const ServerBlock& srv, const Router::CgiRequest& cgiReq);
    void               finishCgi();
    void               finalizeResponse(HttpResponse& res);
};
