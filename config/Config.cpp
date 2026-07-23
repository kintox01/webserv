#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>

Config::Config() : _pos(0) {}

const std::vector<ServerBlock>& Config::servers() const { return _servers; }

std::vector<std::string> Config::tokenize(const std::string& content) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < content.size()) {
        if (std::isspace((unsigned char)content[i])) { i++; continue; }
        if (content[i] == '#') {
            while (i < content.size() && content[i] != '\n') i++;
            continue;
        }
        if (content[i] == '{' || content[i] == '}' || content[i] == ';') {
            tokens.push_back(std::string(1, content[i++]));
            continue;
        }
        if (content[i] == '"' || content[i] == '\'') {
            char        quote = content[i++];
            std::string tok;
            while (i < content.size() && content[i] != quote)
                tok += content[i++];
            if (i < content.size()) i++;
            tokens.push_back(tok);
            continue;
        }
        std::string tok;
        while (i < content.size()
               && !std::isspace((unsigned char)content[i])
               && content[i] != '{' && content[i] != '}'
               && content[i] != ';'  && content[i] != '#') {
            tok += content[i++];
        }
        if (!tok.empty()) tokens.push_back(tok);
    }
    return tokens;
}

std::string Config::peek() const {
    if (_pos < _tokens.size()) return _tokens[_pos];
    return "";
}

std::string Config::next() {
    if (_pos >= _tokens.size())
        throw std::runtime_error("Config: fin de fichier inattendue");
    return _tokens[_pos++];
}

void Config::expect(const std::string& tok) {
    std::string got = next();
    if (got != tok)
        throw std::runtime_error("Config: attendu '" + tok + "', obtenu '" + got + "'");
}

bool Config::atEnd() const { return _pos >= _tokens.size(); }

size_t Config::parseSize(const std::string& s) {
    if (s.empty()) return 0;
    size_t val = (size_t)std::atol(s.c_str());
    char   last = s[s.size() - 1];
    if      (last == 'k' || last == 'K') val *= 1024;
    else if (last == 'm' || last == 'M') val *= 1024 * 1024;
    else if (last == 'g' || last == 'G') val *= 1024 * 1024 * 1024;
    return val;
}

void Config::load(const std::string& path) {
    std::ifstream f(path.c_str());
    if (!f.is_open())
        throw std::runtime_error("Impossible d'ouvrir le fichier: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    _tokens = tokenize(ss.str());
    _pos    = 0;
    parse();
}

void Config::parse() {
    while (!atEnd()) {
        if (peek() == "server") {
            next();
            _servers.push_back(parseServer());
        } else {
            throw std::runtime_error("Config: attendu 'server', obtenu '" + peek() + "'");
        }
    }
    if (_servers.empty())
        throw std::runtime_error("Config: aucun bloc 'server' defini");
}

ServerBlock Config::parseServer() {
    ServerBlock s;
    expect("{");
    while (!atEnd() && peek() != "}") {
        std::string dir = next();
        if (dir == "listen") {
            std::string val = next();
            size_t colon = val.find(':');
            if (colon != std::string::npos) {
                s.host = val.substr(0, colon);
                s.port = std::atoi(val.substr(colon + 1).c_str());
            } else {
                s.port = std::atoi(val.c_str());
            }
            expect(";");
        } else if (dir == "server_name") {
            while (!atEnd() && peek() != ";")
                s.server_names.push_back(next());
            expect(";");
        } else if (dir == "error_page") {
            int         code = std::atoi(next().c_str());
            std::string page = next();
            s.error_pages[code] = page;
            expect(";");
        } else if (dir == "client_max_body_size") {
            s.client_max_body_size = parseSize(next());
            expect(";");
        } else if (dir == "location") {
            s.locations.push_back(parseLocation());
        } else {
            while (!atEnd() && peek() != ";" && peek() != "}") next();
            if (!atEnd() && peek() == ";") expect(";");
        }
    }
    expect("}");
    return s;
}

LocationBlock Config::parseLocation() {
    LocationBlock loc;
    loc.path = next();
    expect("{");
    while (!atEnd() && peek() != "}") {
        std::string dir = next();
        if (dir == "root") {
            loc.root = next(); expect(";");
        } else if (dir == "index") {
            loc.index = next(); expect(";");
        } else if (dir == "methods" || dir == "allow_methods") {
            while (!atEnd() && peek() != ";")
                loc.methods.push_back(next());
            expect(";");
        } else if (dir == "autoindex") {
            loc.autoindex = (next() == "on"); expect(";");
        } else if (dir == "return") {
            loc.redirect_code = std::atoi(next().c_str());
            if (!atEnd() && peek() != ";") loc.redirect_url = next();
            expect(";");
        } else if (dir == "upload_store") {
            loc.upload_store = next(); expect(";");
        } else if (dir == "cgi" || dir == "cgi_pass") {
            CgiEntry ce;
            ce.extension    = next();
            ce.interpreter  = next();
            loc.cgi_handlers.push_back(ce);
            expect(";");
        } else {
            while (!atEnd() && peek() != ";" && peek() != "}") next();
            if (!atEnd() && peek() == ";") expect(";");
        }
    }
    expect("}");
    return loc;
}
