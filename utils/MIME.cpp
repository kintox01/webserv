#include "MIME.hpp"
#include <map>

std::string getMimeType(const std::string& path) {
    static std::map<std::string, std::string> types;
    if (types.empty()) {
        types[".html"]  = "text/html";
        types[".htm"]   = "text/html";
        types[".css"]   = "text/css";
        types[".js"]    = "application/javascript";
        types[".json"]  = "application/json";
        types[".xml"]   = "application/xml";
        types[".png"]   = "image/png";
        types[".jpg"]   = "image/jpeg";
        types[".jpeg"]  = "image/jpeg";
        types[".gif"]   = "image/gif";
        types[".svg"]   = "image/svg+xml";
        types[".ico"]   = "image/x-icon";
        types[".txt"]   = "text/plain";
        types[".pdf"]   = "application/pdf";
        types[".zip"]   = "application/zip";
        types[".tar"]   = "application/x-tar";
        types[".mp4"]   = "video/mp4";
        types[".webm"]  = "video/webm";
        types[".mp3"]   = "audio/mpeg";
        types[".ogg"]   = "audio/ogg";
        types[".py"]    = "text/x-python";
        types[".sh"]    = "text/x-shellscript";
    }
    size_t dot = path.rfind('.');
    if (dot != std::string::npos) {
        std::map<std::string, std::string>::const_iterator it = types.find(path.substr(dot));
        if (it != types.end()) return it->second;
    }
    return "application/octet-stream";
}
