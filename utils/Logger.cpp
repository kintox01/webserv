#include "Logger.hpp"
#include <ctime>
#include <iostream>

static std::string timestamp() {
    time_t t = time(NULL);
    char   buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
    return std::string(buf);
}

void Logger::info(const std::string& msg) {
    std::cout << "[" << timestamp() << "] INFO  " << msg << "\n";
}

void Logger::warn(const std::string& msg) {
    std::cout << "[" << timestamp() << "] WARN  " << msg << "\n";
}

void Logger::error(const std::string& msg) {
    std::cerr << "[" << timestamp() << "] ERROR " << msg << "\n";
}
