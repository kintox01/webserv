#include "Connection.hpp"
#include "../http/Router.hpp"
#include "../http/HttpResponse.hpp"
#include "../utils/Logger.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <ctime>

Connection::Connection(int fd, int port, const std::vector<ServerBlock>& servers)
    : fd(fd), port(port), state(CONN_READING),
      lastActivity(time(NULL)), writePos(0), _servers(servers), _shouldClose(false) {}

Connection::~Connection() {}

bool Connection::timedOut() const {
    return difftime(time(NULL), lastActivity) > 60.0; // 60 secondes d'inactivite
}

// Choisir le ServerBlock qui correspond a la requete en cours.
// On compare le header "Host" avec les server_names configures.
// Si aucun ne correspond, on prend le premier sur ce port (serveur par defaut).
const ServerBlock* Connection::selectServer() const {
    std::string host = request.header("host");
    size_t colon = host.find(':');
    if (colon != std::string::npos)
        host = host.substr(0, colon); // enlever le port du Host

    const ServerBlock* defaultSrv = NULL;
    for (size_t i = 0; i < _servers.size(); i++) {
        if (_servers[i].port != port) continue;
        if (!defaultSrv) defaultSrv = &_servers[i]; // premier serveur sur ce port

        for (size_t j = 0; j < _servers[i].server_names.size(); j++) {
            if (_servers[i].server_names[j] == host)
                return &_servers[i]; // correspondance exacte
        }
    }
    return defaultSrv;
}

// Construire la reponse HTTP et la placer dans writeBuffer
void Connection::buildAndSend() {
    const ServerBlock* srv = selectServer();
    HttpResponse       res;

    if (srv) {
        Router router(*srv);
        res = router.handle(request);
    } else {
        res.setStatus(500);
        res.setBody("No server configured", "text/plain");
    }

    // Gestion du keep-alive
    _shouldClose = !request.keepAlive();
    res.setHeader("Connection", _shouldClose ? "close" : "keep-alive");

    writeBuffer = res.build();
    writePos    = 0;
    state       = CONN_WRITING;

    request.reset(); // preparer pour la prochaine requete (keep-alive)
}

// Lire les donnees du client et alimenter le parser
bool Connection::onReadable() {
    char    buf[8192];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);

    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return true; // pas de donnees disponibles, reessayer
        return false;    // connexion fermee ou erreur
    }
    lastActivity = time(NULL);

    // Chercher la limite max du corps pour ce port
    size_t maxBody = 1048576; // 1 MB par defaut
    for (size_t i = 0; i < _servers.size(); i++) {
        if (_servers[i].port == port) {
            maxBody = _servers[i].client_max_body_size;
            break;
        }
    }

    bool complete = request.feed(buf, (size_t)n, maxBody);

    if (request.state == PARSE_ERROR) {
        // Erreur de parsing : envoyer une reponse d'erreur et fermer
        HttpResponse res;
        res.setStatus(request.error_code);
        res.setBody(HttpResponse::defaultErrorPage(request.error_code), "text/html");
        res.setHeader("Connection", "close");
        _shouldClose = true;
        writeBuffer  = res.build();
        writePos     = 0;
        state        = CONN_WRITING;
        return true;
    }

    if (complete)
        buildAndSend(); // requete complete -> router -> reponse

    return true;
}

// Envoyer le contenu de writeBuffer au client
bool Connection::onWritable() {
    if (writePos >= writeBuffer.size()) {
        state = CONN_READING; // buffer vide, retourner en lecture
        return true;
    }

    ssize_t n = send(fd,
                     writeBuffer.c_str() + writePos,
                     writeBuffer.size() - writePos,
                     0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false; // erreur reseau
    }
    lastActivity  = time(NULL);
    writePos     += (size_t)n;

    if (writePos >= writeBuffer.size()) {
        // Tout envoye : decider si on ferme ou on garde la connexion ouverte
        state = _shouldClose ? CONN_CLOSING : CONN_READING;
    }
    return true;
}
