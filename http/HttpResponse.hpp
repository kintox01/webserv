#pragma once
#include <string>
#include <map>

class HttpResponse {
public:
    int                                status;
    std::map<std::string, std::string> headers;
    std::string                        body;

    HttpResponse();

    void        setStatus(int code);
    void        setHeader(const std::string& name, const std::string& value);
    void        setBody(const std::string& content, const std::string& type = "text/html");

    // Construit la reponse HTTP brute (status line + headers + body)
    std::string build() const;

    static std::string statusText(int code);
    static std::string defaultErrorPage(int code);
};
