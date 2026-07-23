#pragma once
#include <string>
#include <vector>
#include <map>
#include "LocationBlock.hpp"

struct ServerBlock {
    std::string               host;
    int                       port;
    std::vector<std::string>  server_names;
    std::map<int, std::string> error_pages;
    size_t                    client_max_body_size;
    std::vector<LocationBlock> locations;

    ServerBlock() : host("0.0.0.0"), port(80), client_max_body_size(1048576) {}
};
