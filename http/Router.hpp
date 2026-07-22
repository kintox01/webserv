#pragma once
#include <string>
#include "../config/ServerBlock.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

// Le Router analyse une requete et produit une reponse.
// Il cherche le bloc location qui correspond le mieux (plus long prefixe)
// puis decide quoi faire : servir un fichier, lancer un CGI, uploader, etc.
class Router {
public:
    explicit Router(const ServerBlock& server);
    HttpResponse handle(const HttpRequest& req);

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
    HttpResponse handleCgi(const LocationBlock& loc, const HttpRequest& req,
                           const std::string& scriptPath,
                           const std::string& interpreter);
};
