#include "Server.hpp"
#include "EventLoop.hpp"

Server::Server(const std::vector<ServerBlock>& servers) : _servers(servers) {}

void Server::run() {
    EventLoop loop(_servers);
    loop.run();
}
