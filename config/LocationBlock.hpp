#pragma once
#include <string>
#include <vector>

struct CgiEntry {
    std::string extension;   // ex: ".py"
    std::string interpreter; // ex: "/usr/bin/python3"
};

struct LocationBlock {
    std::string              path;          // /api, /cgi-bin, /
    std::string              root;          // dossier racine sur le disque
    std::string              index;         // fichier index par defaut
    std::vector<std::string> methods;       // GET POST DELETE (vide = tous)
    bool                     autoindex;     // listage du dossier
    int                      redirect_code; // 301, 302, 0 = pas de redirect
    std::string              redirect_url;
    std::string              upload_store;  // dossier pour les uploads
    std::vector<CgiEntry>    cgi_handlers;

    LocationBlock() : autoindex(false), redirect_code(0) {}
};
