#include "Cgi.hpp"
#include "../utils/Logger.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <cctype>
#include <vector>

static std::string intStr(int n) {
    std::ostringstream ss; ss << n; return ss.str();
}

HttpResponse Cgi::execute(const HttpRequest& req,
                           const std::string& scriptPath,
                           const std::string& interpreter,
                           const ServerBlock& server,
                           const LocationBlock& loc) {
    // Deux pipes : un pour l'stdin du CGI (corps POST), un pour son stdout
    int stdinPipe[2], stdoutPipe[2];
    if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0) {
        HttpResponse res; res.setStatus(500);
        res.setBody("pipe() failed: " + std::string(strerror(errno)), "text/plain");
        return res;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdinPipe[0]);  close(stdinPipe[1]);
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        HttpResponse res; res.setStatus(500);
        res.setBody("fork() failed", "text/plain");
        return res;
    }

    if (pid == 0) {
        // --- Processus enfant : executer le script CGI ---
        dup2(stdinPipe[0],  STDIN_FILENO);  // stdin  = lecture du pipe
        dup2(stdoutPipe[1], STDOUT_FILENO); // stdout = ecriture dans le pipe
        close(stdinPipe[1]);
        close(stdoutPipe[0]);

        // Variables d'environnement CGI (RFC 3875)
        std::vector<std::string> envStrs;
        envStrs.push_back("REQUEST_METHOD="    + req.method);
        envStrs.push_back("QUERY_STRING="      + req.query);
        envStrs.push_back("CONTENT_LENGTH="    + intStr((int)req.body.size()));
        envStrs.push_back("CONTENT_TYPE="      + req.header("content-type"));
        envStrs.push_back("SCRIPT_FILENAME="   + scriptPath);
        envStrs.push_back("SCRIPT_NAME="       + req.path);
        envStrs.push_back("PATH_INFO="         + req.path);
        envStrs.push_back("SERVER_NAME="       + server.host);
        envStrs.push_back("SERVER_PORT="       + intStr(server.port));
        envStrs.push_back("SERVER_PROTOCOL=HTTP/1.1");
        envStrs.push_back("GATEWAY_INTERFACE=CGI/1.1");
        envStrs.push_back("REDIRECT_STATUS=200"); // requis par PHP-CGI
        envStrs.push_back("HTTP_HOST="         + req.header("host"));

        // Convertir les en-tetes HTTP en variables HTTP_*
        for (std::map<std::string, std::string>::const_iterator it = req.headers.begin();
             it != req.headers.end(); ++it) {
            std::string key = "HTTP_";
            for (size_t i = 0; i < it->first.size(); i++) {
                char c = it->first[i];
                key += (c == '-') ? '_' : (char)std::toupper((unsigned char)c);
            }
            envStrs.push_back(key + "=" + it->second);
        }

        std::vector<char*> env;
        for (size_t i = 0; i < envStrs.size(); i++)
            env.push_back(const_cast<char*>(envStrs[i].c_str()));
        env.push_back(NULL);

        // Convertir le chemin du script en chemin absolu AVANT chdir
        // (un chemin relatif devient invalide apres chdir)
        char absScriptPath[4096];
        if (realpath(scriptPath.c_str(), absScriptPath) == NULL) {
            strncpy(absScriptPath, scriptPath.c_str(), sizeof(absScriptPath) - 1);
            absScriptPath[sizeof(absScriptPath) - 1] = '\0';
        }

        // Mettre a jour SCRIPT_FILENAME avec le chemin absolu
        for (size_t k = 0; k < envStrs.size(); k++) {
            if (envStrs[k].substr(0, 16) == "SCRIPT_FILENAME=")
                envStrs[k] = std::string("SCRIPT_FILENAME=") + absScriptPath;
        }

        // Se placer dans le repertoire du script
        std::string dir = std::string(absScriptPath);
        size_t      sep = dir.rfind('/');
        if (sep != std::string::npos) chdir(dir.substr(0, sep).c_str());

        char* argv[3];
        argv[0] = const_cast<char*>(interpreter.c_str());
        argv[1] = absScriptPath; // chemin absolu valide meme apres chdir
        argv[2] = NULL;

        execve(interpreter.c_str(), argv, &env[0]);
        // Si execve echoue, signaler l'erreur via stdout (sera parsee comme CGI)
        const char* err = "Status: 500\r\nContent-Type: text/plain\r\n\r\nexecve failed\n";
        write(STDOUT_FILENO, err, strlen(err));
        exit(1);
    }

    // --- Processus parent ---
    close(stdinPipe[0]);
    close(stdoutPipe[1]);

    // Envoyer le corps de la requete au CGI via son stdin
    if (!req.body.empty()) {
        const char* ptr       = req.body.data();
        size_t      remaining = req.body.size();
        while (remaining > 0) {
            ssize_t written = write(stdinPipe[1], ptr, remaining);
            if (written <= 0) break;
            ptr       += (size_t)written;
            remaining -= (size_t)written;
        }
    }
    close(stdinPipe[1]);

    // Lire la sortie CGI avec un timeout de 10 secondes
    std::string output;
    char        buf[4096];
    struct pollfd pfd;
    pfd.fd     = stdoutPipe[0];
    pfd.events = POLLIN;

    while (true) {
        int r = poll(&pfd, 1, 10000); // timeout 10s
        if (r <= 0) break;            // timeout ou erreur
        ssize_t n = read(stdoutPipe[0], buf, sizeof(buf));
        if (n <= 0) break;
        output.append(buf, (size_t)n);
    }
    close(stdoutPipe[0]);

    int wstatus;
    waitpid(pid, &wstatus, 0);

    // Separer les en-tetes CGI du corps (\r\n\r\n ou \n\n)
    size_t sep    = output.find("\r\n\r\n");
    size_t sepLen = 4;
    if (sep == std::string::npos) {
        sep    = output.find("\n\n");
        sepLen = 2;
    }

    HttpResponse res;
    if (sep == std::string::npos) {
        res.setStatus(502);
        res.setBody("CGI: pas de separateur d'en-tetes", "text/plain");
        return res;
    }

    std::string headerPart = output.substr(0, sep);
    std::string bodyPart   = output.substr(sep + sepLen);

    // Parser les en-tetes CGI
    int         cgiStatus   = 200;
    std::string contentType = "text/html";

    std::istringstream hs(headerPart);
    std::string        line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line = line.substr(0, line.size() - 1);
        if (line.size() > 7 && line.substr(0, 7) == "Status:") {
            cgiStatus = std::atoi(line.substr(7).c_str());
        } else if (line.size() > 13 && line.substr(0, 13) == "Content-Type:") {
            contentType = line.substr(14);
            while (!contentType.empty() && contentType[0] == ' ')
                contentType = contentType.substr(1);
        } else if (line.size() > 9 && line.substr(0, 9) == "Location:") {
            std::string loc_url = line.substr(9);
            while (!loc_url.empty() && loc_url[0] == ' ') loc_url = loc_url.substr(1);
            res.setStatus(302);
            res.setHeader("Location", loc_url);
            res.setBody("", "text/html");
            (void)loc;
            return res;
        }
    }

    res.setStatus(cgiStatus);
    res.setBody(bodyPart, contentType);
    (void)loc;
    return res;
}
