#include <iostream>
#include <sstream>
#include "config/Config.hpp"
#include "core/Server.hpp"
#include "utils/Logger.hpp"

int main(int argc, char* argv[]) {
    std::string configPath = "webserv.conf";
    if (argc >= 2) configPath = argv[1];

    try {
        Config config;
        config.load(configPath);

        std::ostringstream ss;
        ss << config.servers().size() << " serveur(s) configure(s)";
        Logger::info(ss.str());

        Server server(config.servers());
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Erreur fatale: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
