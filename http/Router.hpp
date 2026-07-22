#pragma once
#include <string>
#include "../config/ServerBlock.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

// Le Router analyse une requete et decide quoi faire.
// Il cherche le bloc location qui correspond le mieux (plus long prefixe)
// puis decide : servir un fichier, uploader, supprimer, rediriger, ou
// lancer un CGI.
//
// Le CGI n'est jamais execute par le Router lui-meme : handle() se contente
// de reperer le script a lancer et laisse Connection s'en charger de maniere
// asynchrone (fork immediat, puis lecture/ecriture pilotees par le poll()
// unique de l'EventLoop).
class Router {
public:
    struct CgiRequest {
        std::string scriptPath;
        std::string interpreter;
    };

    explicit Router(const ServerBlock& server);

    // Renvoie true si la reponse est prete immediatement (res est rempli).
    // Renvoie false si un CGI doit etre lance de maniere asynchrone (cgiOut
    // est rempli a la place, res est alors ignore).
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
