#pragma once
#include <string>
#include <sys/types.h>
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "../config/ServerBlock.hpp"

class Cgi {
public:
    static bool start(const HttpRequest& req,
                       const std::string& scriptPath,
                       const std::string& interpreter,
                       const ServerBlock& server,
                       pid_t& pid, int& inFd, int& outFd);

    static HttpResponse buildResponse(const std::string& rawOutput);
};
