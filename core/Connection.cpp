#include "Connection.hpp"
#include "../http/HttpResponse.hpp"
#include "../http/Cgi.hpp"
#include "../utils/Logger.hpp"
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <ctime>

static const double CGI_TIMEOUT_SECONDS = 10.0;

Connection::Connection(int fd, int port, const std::vector<ServerBlock>& servers)
    : fd(fd), port(port), state(CONN_READING),
      lastActivity(time(NULL)), writePos(0),
      cgiPid(-1), cgiStdin(-1), cgiStdout(-1), cgiBodyPos(0), cgiStart(0),
      _servers(servers), _shouldClose(false) {}

Connection::~Connection() {
    if (cgiStdin  != -1) close(cgiStdin);
    if (cgiStdout != -1) close(cgiStdout);
    if (cgiPid > 0) {
        kill(cgiPid, SIGKILL);
        waitpid(cgiPid, NULL, 0);
    }
}

bool Connection::timedOut() const {
    return difftime(time(NULL), lastActivity) > 60.0; // 60 secondes d'inactivite
}

bool Connection::cgiTimedOut() const {
    return state == CONN_CGI && difftime(time(NULL), cgiStart) > CGI_TIMEOUT_SECONDS;
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

// Appliquer l'en-tete Connection puis placer la reponse dans writeBuffer
void Connection::finalizeResponse(HttpResponse& res) {
    res.setHeader("Connection", _shouldClose ? "close" : "keep-alive");
    writeBuffer = res.build();
    writePos    = 0;
    state       = CONN_WRITING;
}

// Construire la reponse HTTP (ou lancer un CGI) a partir de la requete complete
void Connection::buildAndSend() {
    const ServerBlock* srv = selectServer();
    _shouldClose = !request.keepAlive(); // a capturer AVANT tout reset() de request

    if (!srv) {
        HttpResponse res;
        res.setStatus(500);
        res.setBody("No server configured", "text/plain");
        request.reset();
        finalizeResponse(res);
        return;
    }

    Router            router(*srv);
    Router::CgiRequest cgiReq;
    HttpResponse       res;

    if (router.handle(request, res, cgiReq)) {
        request.reset(); // preparer pour la prochaine requete (keep-alive)
        finalizeResponse(res);
        return;
    }

    if (!startCgi(*srv, cgiReq)) {
        HttpResponse err;
        err.setStatus(502);
        err.setBody(HttpResponse::defaultErrorPage(502), "text/html");
        request.reset();
        finalizeResponse(err);
    }
    // si startCgi() a reussi : state == CONN_CGI, request.reset() deja fait,
    // la suite est pilotee par onCgiReadable()/onCgiWritable().
}

// Lancer le script CGI en tache de fond (fork immediat, pipes non-bloquants).
bool Connection::startCgi(const ServerBlock& srv, const Router::CgiRequest& cgiReq) {
    pid_t pid; int inFd, outFd;
    if (!Cgi::start(request, cgiReq.scriptPath, cgiReq.interpreter, srv, pid, inFd, outFd))
        return false;

    cgiPid     = pid;
    cgiStdin   = inFd;
    cgiStdout  = outFd;
    cgiBody    = request.body; // copie : request va etre reset() juste apres
    cgiBodyPos = 0;
    cgiOutput.clear();
    cgiStart   = time(NULL);
    state      = CONN_CGI;

    if (cgiBody.empty()) {
        // rien a envoyer au script : fermer tout de suite son entree standard
        close(cgiStdin);
        cgiStdin = -1;
    }

    request.reset();
    return true;
}

// Le CGI a fini d'ecrire sa sortie (EOF) : recuperer le process et construire la reponse
void Connection::finishCgi() {
    if (cgiStdin != -1) { close(cgiStdin); cgiStdin = -1; }
    if (cgiPid > 0) {
        int status;
        waitpid(cgiPid, &status, 0); // le script vient de fermer stdout, il termine
        cgiPid = -1;
    }
    HttpResponse res = Cgi::buildResponse(cgiOutput);
    finalizeResponse(res);
}

// Timeout CGI (script qui boucle a l'infini, par exemple) : on tue le process
// et on repond une erreur, sans jamais laisser tout le serveur en attente.
void Connection::abortCgi(int statusCode) {
    if (cgiPid > 0) {
        kill(cgiPid, SIGKILL);
        waitpid(cgiPid, NULL, 0);
        cgiPid = -1;
    }
    if (cgiStdin  != -1) { close(cgiStdin);  cgiStdin  = -1; }
    if (cgiStdout != -1) { close(cgiStdout); cgiStdout = -1; }

    HttpResponse res;
    res.setStatus(statusCode);
    res.setBody(HttpResponse::defaultErrorPage(statusCode), "text/html");
    finalizeResponse(res);
}

// Lire les donnees du client et alimenter le parser.
// Le retour de recv() est verifie explicitement pour les deux cas distincts
// (0 = connexion fermee par le pair, negatif = erreur) ; errno n'est jamais
// consulte pour decider du comportement, comme l'exige le sujet.
bool Connection::onReadable() {
    char    buf[8192];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);

    if (n < 0) return false; // erreur reseau -> fermer la connexion
    if (n == 0) return false; // le client a ferme la connexion -> fermer

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
        _shouldClose = true;
        finalizeResponse(res);
        return true;
    }

    if (complete)
        buildAndSend(); // requete complete -> router -> reponse (ou lancement CGI)

    return true;
}

// Envoyer le contenu de writeBuffer au client (une seule tentative d'ecriture).
bool Connection::onWritable() {
    if (writePos >= writeBuffer.size()) {
        state = CONN_READING; // buffer vide, retourner en lecture
        return true;
    }

    ssize_t n = send(fd, writeBuffer.c_str() + writePos, writeBuffer.size() - writePos, 0);

    if (n < 0) return false; // erreur reseau -> fermer la connexion
    if (n == 0) return true; // rien envoye cette fois, on retente au prochain poll()

    lastActivity  = time(NULL);
    writePos     += (size_t)n;

    if (writePos >= writeBuffer.size()) {
        // Tout envoye : decider si on ferme ou on garde la connexion ouverte
        state = _shouldClose ? CONN_CLOSING : CONN_READING;
    }
    return true;
}

// Une seule lecture de la sortie du CGI par appel, comme pour les sockets.
bool Connection::onCgiReadable() {
    char    buf[4096];
    ssize_t n = read(cgiStdout, buf, sizeof(buf));

    if (n > 0) {
        cgiOutput.append(buf, (size_t)n);
        return true; // attendre la suite au prochain poll()
    }

    // n == 0 (EOF, le script a ferme son stdout) ou n < 0 (erreur) :
    // dans les deux cas le CGI n'a plus rien a nous dire, on finalise.
    close(cgiStdout);
    cgiStdout = -1;
    finishCgi();
    return true;
}

// Une seule ecriture vers le stdin du CGI par appel.
bool Connection::onCgiWritable() {
    if (cgiStdin == -1) return true;

    size_t remaining = cgiBody.size() - cgiBodyPos;
    if (remaining == 0) {
        close(cgiStdin);
        cgiStdin = -1;
        return true;
    }

    ssize_t n = write(cgiStdin, cgiBody.data() + cgiBodyPos, remaining);

    if (n < 0) {
        // le script a peut-etre ferme son stdin plus tot (il n'a pas besoin du
        // corps) : on arrete d'essayer d'ecrire mais on continue de lire sa sortie.
        close(cgiStdin);
        cgiStdin = -1;
        return true;
    }
    if (n == 0) return true; // rien ecrit cette fois, on retente au prochain poll()

    cgiBodyPos += (size_t)n;
    if (cgiBodyPos >= cgiBody.size()) {
        close(cgiStdin);
        cgiStdin = -1;
    }
    return true;
}
