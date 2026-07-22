#pragma once
#include <string>
#include <sys/types.h>
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "../config/ServerBlock.hpp"

// Gere l'execution d'un script CGI (Python, PHP, etc.)
//
// Le lancement et la communication (stdin/stdout) sont deux etapes separees :
// start() ne fait que fork() + brancher les pipes en non-bloquant, sans jamais
// lire ni ecrire dessus. La lecture/ecriture reelle des pipes est ensuite pilotee
// par l'unique poll() de l'EventLoop, exactement comme pour les sockets clients
// (le sujet impose que TOUT descripteur en attente de donnees, pipes inclus,
// passe par ce poll() unique).
class Cgi {
public:
    // Fork + configure l'environnement CGI (RFC 3875) + execve.
    // En cas de succes, remplit pid/inFd/outFd (les extremites cote parent des
    // pipes, deja en O_NONBLOCK) et renvoie true. N'attend jamais le process
    // et ne touche pas aux pipes.
    static bool start(const HttpRequest& req,
                       const std::string& scriptPath,
                       const std::string& interpreter,
                       const ServerBlock& server,
                       pid_t& pid, int& inFd, int& outFd);

    // Une fois la sortie du script entierement recue (EOF sur son stdout),
    // separe les en-tetes CGI (Status/Content-Type/Location) du corps et
    // construit la HttpResponse correspondante.
    static HttpResponse buildResponse(const std::string& rawOutput);
};
