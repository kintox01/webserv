#pragma once
#include <string>
#include <vector>
#include <ctime>
#include "../config/ServerBlock.hpp"
#include "../http/HttpRequest.hpp"

enum ConnState {
    CONN_READING,  // on attend des donnees du client
    CONN_WRITING,  // on envoie la reponse
    CONN_CLOSING   // la connexion doit etre fermee
};

// Represente une connexion TCP avec un client.
// Contient une machine a etats : READING -> WRITING -> READING (keep-alive)
// ou READING -> WRITING -> CLOSING (Connection: close)
class Connection {
public:
    int         fd;           // descripteur de fichier du socket client
    int         port;         // port sur lequel la connexion est arrivee
    ConnState   state;
    time_t      lastActivity;

    HttpRequest request;       // parser de la requete en cours
    std::string writeBuffer;   // reponse a envoyer
    size_t      writePos;      // position dans writeBuffer

    Connection(int fd, int port, const std::vector<ServerBlock>& servers);
    ~Connection();

    bool onReadable();  // appele quand poll() signale POLLIN
    bool onWritable();  // appele quand poll() signale POLLOUT
    bool timedOut() const;

private:
    const std::vector<ServerBlock>& _servers;
    bool                            _shouldClose; // fermer apres l'envoi ?

    const ServerBlock* selectServer() const;
    void               buildAndSend();
};
