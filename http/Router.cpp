#include "Router.hpp"
#include "Cgi.hpp"
#include "../utils/MIME.hpp"
#include "../utils/Logger.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <cstdlib>

Router::Router(const ServerBlock& server) : _server(server) {}

// Trouve le bloc location qui correspond le mieux au chemin URI.
// Algorithme : on prend le prefixe le plus long qui correspond.
// Ex: pour /cgi-bin/test.py, on preferera /cgi-bin a /
const LocationBlock* Router::findLocation(const std::string& path) const {
    const LocationBlock* best    = NULL;
    size_t               bestLen = 0;
    for (size_t i = 0; i < _server.locations.size(); i++) {
        const LocationBlock& loc = _server.locations[i];
        if (path.size() >= loc.path.size()
            && path.substr(0, loc.path.size()) == loc.path) {
            if (loc.path.size() >= bestLen) {
                bestLen = loc.path.size();
                best    = &loc;
            }
        }
    }
    return best;
}

// Construit le chemin fichier a partir du root et du chemin URI.
// Previent le path traversal (../../etc/passwd) en resolvant les ".."
// sans jamais sortir du root.
std::string Router::buildFilePath(const LocationBlock& loc,
                                   const std::string& reqPath) const {
    // Enlever le prefixe de la location du chemin URI
    std::string rel = reqPath;
    if (rel.size() >= loc.path.size())
        rel = rel.substr(loc.path.size());

    // Resoudre segment par segment
    std::vector<std::string> parts;
    std::string              seg;
    std::istringstream       ss(rel);
    while (std::getline(ss, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back(); // remonter, mais pas hors du root
        } else {
            parts.push_back(seg);
        }
    }

    std::string result = loc.root;
    if (!result.empty() && result[result.size() - 1] == '/')
        result = result.substr(0, result.size() - 1);
    for (size_t i = 0; i < parts.size(); i++)
        result += "/" + parts[i];
    return result;
}

bool Router::methodAllowed(const LocationBlock& loc, const std::string& method) const {
    if (loc.methods.empty()) return true; // aucune restriction = tout est permis
    for (size_t i = 0; i < loc.methods.size(); i++)
        if (loc.methods[i] == method) return true;
    return false;
}

// Generer une reponse d'erreur, avec page personnalisee si configuree
HttpResponse Router::errorResponse(int code) {
    HttpResponse res;
    res.setStatus(code);

    std::map<int, std::string>::const_iterator it = _server.error_pages.find(code);
    if (it != _server.error_pages.end()) {
        const LocationBlock* rootLoc = findLocation("/");
        if (rootLoc && !rootLoc->root.empty()) {
            std::string filePath = rootLoc->root + it->second;
            std::ifstream f(filePath.c_str(), std::ios::binary);
            if (f.is_open()) {
                std::ostringstream buf;
                buf << f.rdbuf();
                res.setBody(buf.str(), "text/html");
                return res;
            }
        }
    }
    res.setBody(HttpResponse::defaultErrorPage(code), "text/html");
    return res;
}

// Servir un fichier statique depuis le disque
HttpResponse Router::serveFile(const std::string& fullPath) {
    struct stat st;
    if (stat(fullPath.c_str(), &st) != 0)
        return errorResponse(404);

    std::ifstream f(fullPath.c_str(), std::ios::binary);
    if (!f.is_open()) return errorResponse(403);

    std::ostringstream buf;
    buf << f.rdbuf();

    HttpResponse res;
    res.setStatus(200);
    res.setBody(buf.str(), getMimeType(fullPath));

    // En-tete Last-Modified pour le cache navigateur
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%a, %d %b %Y %H:%M:%S GMT",
             gmtime(&st.st_mtime));
    res.setHeader("Last-Modified", timeBuf);
    return res;
}

// Generer un listing HTML d'un dossier (autoindex on)
HttpResponse Router::serveDirectory(const std::string& dirPath,
                                     const LocationBlock& loc,
                                     const std::string& uriPath) {
    // Essayer d'abord le fichier index
    if (!loc.index.empty()) {
        std::string indexPath = dirPath + "/" + loc.index;
        struct stat st;
        if (stat(indexPath.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            return serveFile(indexPath);
    }
    if (!loc.autoindex) return errorResponse(403);

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return errorResponse(403);

    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><title>Index of " << uriPath
         << "</title><style>body{font-family:monospace;padding:20px}"
         << "a{display:block;margin:2px 0}</style></head>"
         << "<body><h1>Index of " << uriPath << "</h1><hr>";

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name == ".") continue;
        std::string href = uriPath;
        if (href.empty() || href[href.size() - 1] != '/') href += "/";
        href += name;
        bool isDir = (entry->d_type == DT_DIR);
        html << "<a href=\"" << href << (isDir ? "/" : "") << "\">"
             << name << (isDir ? "/" : "") << "</a>";
    }
    closedir(dir);
    html << "<hr></body></html>";

    HttpResponse res;
    res.setStatus(200);
    res.setBody(html.str(), "text/html");
    return res;
}

// Gerer l'upload de fichier (POST)
HttpResponse Router::handleUpload(const LocationBlock& loc, const HttpRequest& req) {
    if (loc.upload_store.empty()) return errorResponse(403);

    // Extraire le nom de fichier depuis Content-Disposition si present
    std::string filename;
    std::string cd = req.header("content-disposition");
    if (!cd.empty()) {
        size_t fn = cd.find("filename=");
        if (fn != std::string::npos) {
            filename = cd.substr(fn + 9);
            if (!filename.empty() && filename[0] == '"') {
                size_t end = filename.rfind('"');
                filename   = (end != std::string::npos && end > 0)
                             ? filename.substr(1, end - 1) : filename.substr(1);
            }
        }
    }

    // Nom de secours base sur le timestamp
    if (filename.empty()) {
        std::ostringstream ss;
        ss << "upload_" << time(NULL);
        filename = ss.str();
    }

    // Nettoyer le nom de fichier (pas de / ni de \)
    std::string safe;
    for (size_t i = 0; i < filename.size(); i++) {
        char c = filename[i];
        if (c != '/' && c != '\\') safe += c;
    }
    if (safe.empty()) safe = "file";

    std::string destPath = loc.upload_store;
    if (!destPath.empty() && destPath[destPath.size() - 1] != '/') destPath += "/";
    destPath += safe;

    std::ofstream f(destPath.c_str(), std::ios::binary);
    if (!f.is_open()) return errorResponse(500);
    if (!req.body.empty())
        f.write(req.body.data(), (std::streamsize)req.body.size());

    HttpResponse res;
    res.setStatus(201);
    res.setBody("Fichier uploade avec succes", "text/plain");
    return res;
}

HttpResponse Router::handleCgi(const LocationBlock& loc, const HttpRequest& req,
                                const std::string& scriptPath,
                                const std::string& interpreter) {
    Cgi cgi;
    return cgi.execute(req, scriptPath, interpreter, _server, loc);
}

// Point d'entree principal : traiter une requete et retourner une reponse
HttpResponse Router::handle(const HttpRequest& req) {
    const LocationBlock* loc = findLocation(req.path);
    if (!loc) return errorResponse(404);

    // Redirection configuree dans la location
    if (loc->redirect_code != 0) {
        HttpResponse res;
        res.setStatus(loc->redirect_code);
        if (!loc->redirect_url.empty())
            res.setHeader("Location", loc->redirect_url);
        res.setBody("", "text/html");
        return res;
    }

    // Methode non autorisee pour cette location
    if (!methodAllowed(*loc, req.method))
        return errorResponse(405);

    // Construire le chemin fichier
    std::string fullPath = buildFilePath(*loc, req.path);

    // Verifier si un handler CGI correspond a l'extension du fichier
    if (req.method == "GET" || req.method == "POST" || req.method == "HEAD") {
        size_t dot = fullPath.rfind('.');
        if (dot != std::string::npos) {
            std::string ext = fullPath.substr(dot);
            for (size_t i = 0; i < loc->cgi_handlers.size(); i++) {
                if (loc->cgi_handlers[i].extension == ext)
                    return handleCgi(*loc, req, fullPath,
                                     loc->cgi_handlers[i].interpreter);
            }
        }
    }

    // DELETE : supprimer le fichier
    if (req.method == "DELETE") {
        if (remove(fullPath.c_str()) != 0)
            return errorResponse(errno == ENOENT ? 404 : 403);
        HttpResponse res;
        res.setStatus(204);
        return res;
    }

    // POST sans CGI = upload
    if (req.method == "POST")
        return handleUpload(*loc, req);

    // GET / HEAD : servir le fichier ou le dossier
    struct stat st;
    if (stat(fullPath.c_str(), &st) != 0) return errorResponse(404);

    if (S_ISDIR(st.st_mode)) {
        // Forcer le slash final pour que les liens relatifs fonctionnent
        if (req.path.empty() || req.path[req.path.size() - 1] != '/') {
            HttpResponse res;
            res.setStatus(301);
            res.setHeader("Location", req.path + "/");
            res.setBody("", "text/html");
            return res;
        }
        return serveDirectory(fullPath, *loc, req.path);
    }

    HttpResponse res = serveFile(fullPath);
    if (req.method == "HEAD") res.body.clear(); // HEAD = memes en-tetes, pas de corps
    return res;
}
