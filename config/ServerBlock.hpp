#pragma once
#include <string>
#include <vector>
#include <map>
#include "LocationBlock.hpp"

struct ServerBlock {
    std::string               host;                  // 0.0.0.0
    int                       port;                  // 8080
    std::vector<std::string>  server_names;          // localhost www.example.com
    std::map<int, std::string> error_pages;          // 404 -> /errors/404.html
    size_t                    client_max_body_size;  // bytes
    std::vector<LocationBlock> locations;

    ServerBlock() : host("0.0.0.0"), port(80), client_max_body_size(1048576) {}
};
