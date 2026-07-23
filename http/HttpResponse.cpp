#include "HttpResponse.hpp"
#include <sstream>
#include <ctime>

HttpResponse::HttpResponse() : status(200) {}

void HttpResponse::setStatus(int code) { status = code; }
void HttpResponse::setHeader(const std::string& n, const std::string& v) { headers[n] = v; }

void HttpResponse::setBody(const std::string& content, const std::string& type) {
    body = content;
    std::ostringstream ss;
    ss << content.size();
    headers["Content-Type"]   = type;
    headers["Content-Length"] = ss.str();
}

std::string HttpResponse::statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown";
    }
}

std::string HttpResponse::defaultErrorPage(int code) {
    std::ostringstream ss;
    std::string text = statusText(code);
    ss << "<!DOCTYPE html><html><head><title>" << code << " " << text
       << "</title></head><body><h1>" << code << " " << text
       << "</h1></body></html>";
    return ss.str();
}

std::string HttpResponse::build() const {
    std::ostringstream ss;

    ss << "HTTP/1.1 " << status << " " << statusText(status) << "\r\n";

    time_t t = time(NULL);
    char   dateBuf[64];
    strftime(dateBuf, sizeof(dateBuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&t));
    ss << "Date: "   << dateBuf      << "\r\n";
    ss << "Server: webserv/1.0\r\n";

    bool hasCL = headers.count("Content-Length") > 0;
    bool hasCT = headers.count("Content-Type")   > 0;

    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it) {
        ss << it->first << ": " << it->second << "\r\n";
    }

    if (!hasCL) ss << "Content-Length: " << body.size() << "\r\n";
    if (!hasCT && !body.empty()) ss << "Content-Type: text/html\r\n";

    ss << "\r\n";
    ss << body;
    return ss.str();
}
