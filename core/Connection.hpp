#pragma once
#include <string>
#include <vector>
#include <ctime>
#include <sys/types.h>
#include "../config/ServerBlock.hpp"
#include "../http/HttpRequest.hpp"
#include "../http/Router.hpp"

enum ConnState {
    CONN_READING,  // on attend des donnees du client
    CONN_CGI,      // requete complete, un script CGI tourne en tache de fond
    CONN_WRITING,  // on envoie la reponse
    CONN_CLOSING   // la connexion doit etre fermee
};

// Represente une connexion TCP avec un client.
// Machine a etats : READING -> [CGI ->] WRITING -> READING (keep-alive)
// ou READING -> [CGI ->] WRITING -> CLOSING (Connection: close)
//
// Le CGI n'est jamais attendu de maniere synchrone : ses pipes (stdin/stdout)
// sont geres par l'EventLoop exactement comme les sockets clients, via le
// meme et unique poll(). Voir onCgiReadable()/onCgiWritable().
class Connection {
public:
    int         fd;           // descripteur de fichier du socket client
    int         port;         // port sur lequel la connexion est arrivee
    ConnState   state;
    time_t      lastActivity;

    HttpRequest request;       // parser de la requete en cours
    std::string writeBuffer;   // reponse a envoyer
    size_t      writePos;      // position dans writeBuffer

    // Etat du CGI en cours (valide uniquement si state == CONN_CGI)
    pid_t       cgiPid;
    int         cgiStdin;      // extremite d'ecriture (corps requete -> CGI), -1 si fermee
    int         cgiStdout;     // extremite de lecture (sortie CGI), -1 si fermee
    std::string cgiBody;       // copie du corps de la requete a transmettre au CGI
    size_t      cgiBodyPos;    // combien de cgiBody a deja ete ecrit
    std::string cgiOutput;     // sortie du CGI accumulee
    time_t      cgiStart;      // pour detecter un script qui boucle indefiniment

    Connection(int fd, int port, const std::vector<ServerBlock>& servers);
    ~Connection();

    bool onReadable();     // appele quand poll() signale POLLIN sur le socket client
    bool onWritable();     // appele quand poll() signale POLLOUT sur le socket client
    bool onCgiReadable();  // appele quand poll() signale POLLIN sur cgiStdout
    bool onCgiWritable();  // appele quand poll() signale POLLOUT sur cgiStdin
    bool timedOut() const;
    bool cgiTimedOut() const;
    void abortCgi(int statusCode); // tue le CGI (timeout) et prepare une reponse d'erreur

private:
    const std::vector<ServerBlock>& _servers;
    bool                            _shouldClose; // fermer apres l'envoi ?

    const ServerBlock* selectServer() const;
    void               buildAndSend();
    bool               startCgi(const ServerBlock& srv, const Router::CgiRequest& cgiReq);
    void               finishCgi();
    void               finalizeResponse(HttpResponse& res);
};
