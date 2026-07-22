#pragma once
#include <string>
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "../config/ServerBlock.hpp"
#include "../config/LocationBlock.hpp"

// Gere l'execution d'un script CGI (Python, PHP, etc.)
// Le CGI est le mecanisme qui permet au serveur d'executer un programme
// et de renvoyer sa sortie au client.
//
// Protocole :
//   1. fork() -> enfant
//   2. Configurer les variables d'environnement CGI (REQUEST_METHOD, etc.)
//   3. Relier stdin au corps de la requete, stdout a la reponse
//   4. execve() l'interpreteur avec le script en argument
//   5. Le parent lit la sortie CGI (status + en-tetes + corps)
//   6. Construire une HttpResponse a partir de cette sortie
class Cgi {
public:
    HttpResponse execute(const HttpRequest& req,
                         const std::string& scriptPath,
                         const std::string& interpreter,
                         const ServerBlock& server,
                         const LocationBlock& loc);
};
