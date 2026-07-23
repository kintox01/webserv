#pragma once
#include <string>
#include <vector>

struct CgiEntry {
    std::string extension;
    std::string interpreter;
};

struct LocationBlock {
    std::string              path;
    std::string              root;
    std::string              index;
    std::vector<std::string> methods;
    bool                     autoindex;
    int                      redirect_code;
    std::string              redirect_url;
    std::string              upload_store;
    std::vector<CgiEntry>    cgi_handlers;

    LocationBlock() : autoindex(false), redirect_code(0) {}
};
