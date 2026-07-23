#pragma once
#include <string>
#include "../config/ServerBlock.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

class Router {
public:
    struct CgiRequest {
        std::string scriptPath;
        std::string interpreter;
    };

    explicit Router(const ServerBlock& server);

    bool handle(const HttpRequest& req, HttpResponse& res, CgiRequest& cgiOut);

private:
    const ServerBlock& _server;

    const LocationBlock* findLocation(const std::string& path) const;
    std::string          buildFilePath(const LocationBlock& loc,
                                       const std::string& reqPath) const;
    bool                 methodAllowed(const LocationBlock& loc,
                                       const std::string& method) const;

    HttpResponse errorResponse(int code);
    HttpResponse serveFile(const std::string& fullPath);
    HttpResponse serveDirectory(const std::string& dirPath,
                                const LocationBlock& loc,
                                const std::string& uriPath);
    HttpResponse handleUpload(const LocationBlock& loc, const HttpRequest& req);
};
