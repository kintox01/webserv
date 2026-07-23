#pragma once
#include <string>
#include <map>
#include "../utils/Buffer.hpp"

enum ParseState {
    PARSE_REQUEST_LINE,
    PARSE_HEADERS,
    PARSE_BODY,
    PARSE_CHUNK_SIZE,
    PARSE_CHUNK_DATA,
    PARSE_CHUNK_CRLF,
    PARSE_CHUNK_TRAILER,
    PARSE_COMPLETE,
    PARSE_ERROR
};

class HttpRequest {
public:
    std::string                        method;
    std::string                        uri;
    std::string                        path;
    std::string                        query;
    std::string                        version;
    std::map<std::string, std::string> headers;
    std::string                        body;
    size_t                             content_length;
    ParseState                         state;
    int                                error_code;
    size_t                             chunk_remaining;

    HttpRequest();
    void        reset();

    bool        feed(const char* data, size_t len, size_t maxBody);

    std::string header(const std::string& name) const;

    bool        keepAlive() const;

private:
    Buffer _buf;

    bool parseRequestLine(const std::string& line);
    bool parseHeader(const std::string& line);
    void parseUri(const std::string& raw);
};
