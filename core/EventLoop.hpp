#pragma once
#include <vector>
#include <map>
#include "../config/ServerBlock.hpp"
#include "Connection.hpp"

class EventLoop {
public:
    explicit EventLoop(const std::vector<ServerBlock>& servers);
    ~EventLoop();
    void run();

private:
    std::vector<ServerBlock>  _servers;
    std::vector<int>          _listenFds;
    std::vector<int>          _listenPorts;
    std::map<int, Connection*> _connections;

    void setupListenSockets();
    void acceptConnection(int listenFd, int port);
    void closeConnection(int fd);
    void checkTimeouts();
};
