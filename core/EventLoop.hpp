#pragma once
#include <vector>
#include <map>
#include "../config/ServerBlock.hpp"
#include "Connection.hpp"

// La boucle d'evenements : le coeur du serveur.
//
// Architecture non-bloquante avec poll() :
//   - Un seul appel poll() surveille TOUS les fd (listen + clients)
//   - Quand poll() signale un fd pret, on appelle la bonne action
//   - Jamais de blocage : recv/send/accept sont tous non-bloquants
//
// Le sujet interdit d'utiliser plusieurs appels poll() ou
// de faire du select() sur des fd bloquants.
class EventLoop {
public:
    explicit EventLoop(const std::vector<ServerBlock>& servers);
    ~EventLoop();
    void run();

private:
    std::vector<ServerBlock>  _servers;      // copie locale de la config
    std::vector<int>          _listenFds;    // un fd par port d'ecoute
    std::vector<int>          _listenPorts;  // port correspondant a chaque listenFd
    std::map<int, Connection*> _connections; // fd -> connexion active

    void setupListenSockets();
    void acceptConnection(int listenFd, int port);
    void closeConnection(int fd);
    void checkTimeouts();
};
