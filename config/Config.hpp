#pragma once
#include <string>
#include <vector>
#include "ServerBlock.hpp"

class Config {
public:
    Config();

    void                            load(const std::string& path);
    const std::vector<ServerBlock>& servers() const;

private:
    std::vector<ServerBlock> _servers;
    std::vector<std::string> _tokens;
    size_t                   _pos;

    std::vector<std::string> tokenize(const std::string& content);
    void                     parse();
    ServerBlock              parseServer();
    LocationBlock            parseLocation();

    std::string              next();
    std::string              peek() const;
    void                     expect(const std::string& tok);
    bool                     atEnd() const;

    static size_t parseSize(const std::string& s);
};
