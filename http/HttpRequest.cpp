#include "HttpRequest.hpp"
#include <sstream>
#include <cctype>
#include <cstdlib>

static std::string toLower(const std::string& s) {
    std::string r = s;
    for (size_t i = 0; i < r.size(); i++)
        r[i] = (char)std::tolower((unsigned char)r[i]);
    return r;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

HttpRequest::HttpRequest()
    : content_length(0), state(PARSE_REQUEST_LINE), error_code(0), chunk_remaining(0) {}

void HttpRequest::reset() {
    method.clear(); uri.clear(); path.clear(); query.clear(); version.clear();
    headers.clear(); body.clear();
    content_length  = 0;
    state           = PARSE_REQUEST_LINE;
    error_code      = 0;
    chunk_remaining = 0;
    _buf.clear();
}

std::string HttpRequest::header(const std::string& name) const {
    std::map<std::string, std::string>::const_iterator it = headers.find(toLower(name));
    return (it != headers.end()) ? it->second : "";
}

bool HttpRequest::keepAlive() const {
    std::string conn = toLower(header("connection"));
    if (version == "HTTP/1.1") return conn != "close";
    return conn == "keep-alive";
}

void HttpRequest::parseUri(const std::string& raw) {
    uri = raw;
    size_t q = raw.find('?');
    if (q != std::string::npos) {
        path  = raw.substr(0, q);
        query = raw.substr(q + 1);
    } else {
        path  = raw;
        query = "";
    }
}

bool HttpRequest::parseRequestLine(const std::string& line) {
    std::istringstream ss(line);
    if (!(ss >> method >> uri >> version)) return false;
    // Methodes acceptees
    if (method != "GET"    && method != "POST"   && method != "DELETE"
     && method != "HEAD"   && method != "PUT"    && method != "OPTIONS")
        return false;
    parseUri(uri);
    return true;
}

bool HttpRequest::parseHeader(const std::string& line) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    std::string name  = toLower(trim(line.substr(0, colon)));
    std::string value = trim(line.substr(colon + 1));
    headers[name] = value;
    return true;
}

// Le coeur du parser : on l'appelle a chaque fois qu'on recoit des donnees.
// La requete HTTP ressemble a :
//   GET /index.html HTTP/1.1\r\n
//   Host: localhost\r\n
//   \r\n                        <- ligne vide = fin des en-tetes
//   [corps pour POST]
bool HttpRequest::feed(const char* data, size_t len, size_t maxBody) {
    _buf.append(data, len);

    // Lire ligne par ligne tant qu'on est dans les en-tetes
    while (state == PARSE_REQUEST_LINE || state == PARSE_HEADERS) {
        size_t pos = _buf.find("\r\n");
        if (pos == std::string::npos) {
            // Pas encore de fin de ligne : attendre plus de donnees
            if (_buf.size() > 8192) { state = PARSE_ERROR; error_code = 400; }
            break;
        }
        std::string line(_buf.data(), pos);
        _buf.consume(pos + 2); // consommer la ligne + \r\n

        if (state == PARSE_REQUEST_LINE) {
            if (!parseRequestLine(line)) { state = PARSE_ERROR; error_code = 400; break; }
            state = PARSE_HEADERS;
        } else {
            if (line.empty()) {
                // Ligne vide = fin des en-tetes
                std::string te = toLower(header("transfer-encoding"));

                if (method == "GET" || method == "DELETE" || method == "HEAD") {
                    state = PARSE_COMPLETE;
                } else if (te.find("chunked") != std::string::npos) {
                    // RFC 3875 : le serveur doit "un-chunk" le corps avant de le
                    // transmettre au CGI, qui attend un corps brut termine par EOF.
                    state = PARSE_CHUNK_SIZE;
                } else {
                    std::string cl = header("content-length");
                    if (!cl.empty())
                        content_length = (size_t)std::atol(cl.c_str());
                    if (content_length > maxBody) { state = PARSE_ERROR; error_code = 413; break; }
                    state = (content_length > 0) ? PARSE_BODY : PARSE_COMPLETE;
                }
                break;
            }
            parseHeader(line);
        }
    }

    // Lire le corps (Content-Length)
    if (state == PARSE_BODY) {
        size_t need = content_length - body.size();
        size_t have = _buf.size();
        size_t take = (have < need) ? have : need;
        if (take > 0) {
            body.append(_buf.data(), take);
            _buf.consume(take);
        }
        if (body.size() >= content_length)
            state = PARSE_COMPLETE;
    }

    // Lire le corps chunke (Transfer-Encoding: chunked)
    while (state == PARSE_CHUNK_SIZE  || state == PARSE_CHUNK_DATA
        || state == PARSE_CHUNK_CRLF  || state == PARSE_CHUNK_TRAILER) {
        if (state == PARSE_CHUNK_SIZE) {
            size_t pos = _buf.find("\r\n");
            if (pos == std::string::npos) {
                if (_buf.size() > 4096) { state = PARSE_ERROR; error_code = 400; }
                break;
            }
            std::string line(_buf.data(), pos);
            _buf.consume(pos + 2);

            size_t semi = line.find(';'); // ignorer les extensions de morceau
            if (semi != std::string::npos) line = line.substr(0, semi);

            char* endp = NULL;
            long  sz   = std::strtol(line.c_str(), &endp, 16);
            if (sz < 0 || endp == line.c_str()) { state = PARSE_ERROR; error_code = 400; break; }
            if (body.size() + (size_t)sz > maxBody) { state = PARSE_ERROR; error_code = 413; break; }

            chunk_remaining = (size_t)sz;
            state = (chunk_remaining == 0) ? PARSE_CHUNK_TRAILER : PARSE_CHUNK_DATA;

        } else if (state == PARSE_CHUNK_DATA) {
            size_t have = _buf.size();
            size_t take = (have < chunk_remaining) ? have : chunk_remaining;
            if (take > 0) {
                body.append(_buf.data(), take);
                _buf.consume(take);
                chunk_remaining -= take;
            }
            if (chunk_remaining > 0) break; // attendre le reste du morceau
            state = PARSE_CHUNK_CRLF;

        } else if (state == PARSE_CHUNK_CRLF) {
            if (_buf.size() < 2) break; // attendre le \r\n final du morceau
            _buf.consume(2);
            state = PARSE_CHUNK_SIZE;

        } else { // PARSE_CHUNK_TRAILER
            size_t pos = _buf.find("\r\n");
            if (pos == std::string::npos) break;
            std::string line(_buf.data(), pos);
            _buf.consume(pos + 2);
            if (line.empty()) { state = PARSE_COMPLETE; break; } // fin des trailers
            // sinon : en-tete de fin ignore, on continue a consommer les trailers
        }
    }

    return state == PARSE_COMPLETE;
}
