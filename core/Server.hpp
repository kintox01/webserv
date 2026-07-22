#pragma once
#include <vector>
#include "../config/ServerBlock.hpp"

class Server {
public:
    explicit Server(const std::vector<ServerBlock>& servers);
    void run();

private:
    std::vector<ServerBlock> _servers;
};
