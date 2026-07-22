#pragma once
#include <string>
#include <map>
#include "../utils/Buffer.hpp"

// Etats du parser de requete HTTP
enum ParseState {
    PARSE_REQUEST_LINE, // on lit la premiere ligne (GET /index.html HTTP/1.1)
    PARSE_HEADERS,      // on lit les en-tetes
    PARSE_BODY,         // on lit le corps (Content-Length)
    PARSE_CHUNK_SIZE,   // Transfer-Encoding: chunked - taille du prochain morceau
    PARSE_CHUNK_DATA,   // donnees du morceau en cours
    PARSE_CHUNK_CRLF,   // \r\n final du morceau
    PARSE_CHUNK_TRAILER,// en-tetes optionnels apres le dernier morceau (taille 0)
    PARSE_COMPLETE,     // requete entierement recue
    PARSE_ERROR         // erreur de parsing
};

class HttpRequest {
public:
    std::string                        method;         // GET, POST, DELETE
    std::string                        uri;            // /page?foo=bar
    std::string                        path;           // /page
    std::string                        query;          // foo=bar
    std::string                        version;        // HTTP/1.1
    std::map<std::string, std::string> headers;        // cles en minuscules
    std::string                        body;
    size_t                             content_length;
    ParseState                         state;
    int                                error_code;     // 400, 413, etc.
    size_t                             chunk_remaining; // octets restants dans le morceau en cours

    HttpRequest();
    void        reset();

    // Alimenter le parser avec les donnees recues du reseau.
    // Renvoie true quand la requete est complete.
    bool        feed(const char* data, size_t len, size_t maxBody);

    // Recuperer un en-tete (insensible a la casse)
    std::string header(const std::string& name) const;

    // La connexion doit-elle rester ouverte apres cette requete ?
    bool        keepAlive() const;

private:
    Buffer _buf;

    bool parseRequestLine(const std::string& line);
    bool parseHeader(const std::string& line);
    void parseUri(const std::string& raw);
};
