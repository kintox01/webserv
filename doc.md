# Webserv — Documentation complète du projet

## 1. Vue d'ensemble : qu'est-ce que le projet fait ?

Tu construis un **serveur HTTP/1.1** en C++98, c'est-à-dire un programme qui écoute sur un port réseau et répond aux requêtes de navigateurs ou d'outils comme `curl`. C'est essentiellement un mini-nginx fait à la main.

```
                    ┌────────────────────────────────────────┐
                    │                WEBSERV                 │
                    │                                        │
  Navigateur ──────►│  Reçoit la requête HTTP                │
  curl ─────────────│  Lit le fichier ou exécute le CGI      │──► Répond HTTP
  siege ────────────│  Gère plusieurs clients en même temps  │
                    └────────────────────────────────────────┘
```

---

## 2. Architecture des fichiers

```
webserv/
├── main.cpp                ← point d'entrée
│
├── config/
│   ├── Config.cpp/hpp      ← lecture du fichier .conf
│   ├── ServerBlock.hpp     ← structure d'un bloc "server {}"
│   └── LocationBlock.hpp   ← structure d'un bloc "location {}"
│
├── core/
│   ├── Server.cpp/hpp      ← façade simple : charge la config, lance la boucle
│   ├── EventLoop.cpp/hpp   ← boucle principale avec poll()
│   └── Connection.cpp/hpp  ← une connexion TCP avec un client
│
├── http/
│   ├── HttpRequest.cpp/hpp ← parser de requête HTTP
│   ├── HttpResponse.cpp/hpp← constructeur de réponse HTTP
│   ├── Router.cpp/hpp      ← aiguillage GET/POST/DELETE + fichiers + CGI
│   └── Cgi.cpp/hpp         ← exécution d'un script via fork/execve
│
└── utils/
    ├── Buffer.hpp          ← buffer réseau efficace
    ├── Logger.cpp/hpp      ← logs avec timestamp
    └── MIME.cpp/hpp        ← extension → Content-Type
```

---

## 3. Le démarrage : main.cpp

```
./webserv webserv.conf
```

```
main()
  │
  ├── Config::load("webserv.conf")   ← lit et parse le fichier de config
  │       │
  │       └── produit un vector<ServerBlock>
  │
  ├── Server server(config.servers())
  │
  └── server.run()                   ← entre dans la boucle infinie
```

C'est juste un point d'entrée. Il attrape toutes les exceptions avec try/catch pour afficher un message propre en cas d'erreur de config ou de port déjà utilisé.

---

## 4. Le fichier de configuration (config/)

### Ce que fait le tokenizer

Le fichier `webserv.conf` est d'abord découpé en **tokens** (morceaux) :

```
server { listen 8080; server_name localhost; location / { root ./www; } }
```
devient :
```
["server"] ["{"] ["listen"] ["8080"] [";"] ["server_name"] ["localhost"] [";"]
["location"] ["/"] ["{"] ["root"] ["./www"] [";"] ["}"] ["}"]
```

### Ce que le parser en fait

Il lit les tokens un par un avec un parseur récursif :

```
parse()
  └── while (tokens disponibles)
        └── parseServer()
              ├── lit "listen", "server_name", "error_page", ...
              └── parseLocation()  ← pour chaque bloc location {}
```

### Structures résultantes

**ServerBlock** (un bloc `server {}`) :
```
host:               "0.0.0.0"
port:               8080
server_names:       ["localhost"]
error_pages:        {404: "/errors/404.html", 500: "/errors/500.html"}
client_max_body_size: 10485760 (10M)
locations:          [LocationBlock, LocationBlock, ...]
```

**LocationBlock** (un bloc `location {}`) :
```
path:           "/"
root:           "./www"
index:          "index.html"
methods:        ["GET", "HEAD"]
autoindex:      false
upload_store:   ""
redirect_code:  0
cgi_handlers:   [{extension:".py", interpreter:"/usr/bin/python3"}]
```

---

## 5. La boucle événementielle (core/EventLoop.cpp) — le cœur du serveur

C'est la partie la plus importante. Le sujet impose : **un seul appel à poll()**, **aucun fork() dans la boucle principale**, **tout non-bloquant**.

### Pourquoi poll() ?

Un serveur doit gérer **plusieurs clients en même temps**. Deux approches classiques :
- **Multi-thread** : un thread par client → complexe, interdit par le sujet
- **I/O multiplexé** : un seul thread surveille tous les fds avec poll() → c'est ce qu'on fait

`poll()` prend une liste de file descriptors et dit "lesquels sont prêts à lire/écrire ?" Il attend (bloque) jusqu'à ce qu'un événement arrive ou que le timeout expire.

### Cycle de vie des sockets

```
          socket()      bind()       listen()
               │            │            │
               ▼            ▼            ▼
            fd créé    lié au port   prêt à accepter
                                         │
                                    accept() ──► nouveau fd client
```

### La boucle principale

```
EventLoop::run()
│
└── BOUCLE INFINIE (tant que g_running == true)
      │
      ├── 1. Construire le tableau pollfd
      │       ├── listen sockets  (POLLIN)    ← nouvelles connexions
      │       └── client sockets  (POLLIN ou POLLOUT selon l'état)
      │
      ├── 2. poll(&fds[0], n, 5000)           ← attendre max 5 secondes
      │
      ├── 3. Traiter les événements
      │       ├── POLLIN sur listen fd  → acceptConnection()
      │       ├── POLLIN sur client fd  → conn->onReadable()
      │       └── POLLOUT sur client fd → conn->onWritable()
      │
      └── 4. Fermer les connexions mortes/timeout
```

### Schéma avec plusieurs clients simultanés

```
poll() surveille ces fds en même temps :

  fd 3 (listen:8080) ──── POLLIN → accept → crée fd 7 (client A)
  fd 7 (client A)    ──── POLLIN → lit la requête
  fd 8 (client B)    ──── POLLOUT → envoie la réponse
  fd 9 (client C)    ──── rien (timeout)

Un seul poll() → un seul thread → zéro blocage
```

---

## 6. La connexion (core/Connection.cpp)

Chaque client a son objet `Connection`. C'est une **machine à états** :

```
                ┌─────────────────────────────────────────┐
                │                                         │
                ▼                                         │
          CONN_READING                             keep-alive ?
         (recv les données)                           OUI ─► CONN_READING
                │                                         │
          requête complète ?                         NON ─► CONN_CLOSING
                │
                ▼
          buildAndSend()
         (Router → réponse)
                │
                ▼
          CONN_WRITING
         (send les données)
```

**Pourquoi une machine à états ?**
Parce que recv() et send() ne lisent/écrivent pas forcément tout en une fois (réseau fragmenté). On doit retenir où on en est entre deux appels à poll().

### onReadable() — quand il y a des données à lire

```
onReadable()
  │
  ├── recv() → lit jusqu'à 8192 octets
  ├── request.feed(buf, n, maxBody) ← parser incrémental
  │
  ├── si PARSE_ERROR : envoyer 400/413 et fermer
  └── si requête complète : buildAndSend()
```

### onWritable() — quand on peut écrire

```
onWritable()
  │
  ├── send(writeBuffer + writePos, ...)
  ├── writePos += n_envoyé
  └── si tout envoyé :
        ├── _shouldClose → CONN_CLOSING
        └── keep-alive  → CONN_READING
```

---

## 7. Le parser HTTP (http/HttpRequest.cpp)

Une requête HTTP ressemble à ça :

```
GET /index.html HTTP/1.1\r\n
Host: localhost:8080\r\n
Connection: keep-alive\r\n
\r\n
```

Pour un POST avec corps :
```
POST /upload HTTP/1.1\r\n
Host: localhost\r\n
Content-Length: 1234\r\n
\r\n
[1234 octets de données]
```

### Machine à états du parser

```
PARSE_REQUEST_LINE
      │  lit "GET /index.html HTTP/1.1\r\n"
      ▼
PARSE_HEADERS
      │  lit les en-têtes un par un
      │  ligne vide → fin des en-têtes
      ▼
PARSE_BODY (si POST avec Content-Length)
      │  accumule body.size() == content_length octets
      ▼
PARSE_COMPLETE  ─── feed() retourne true
```

**Gestion du cas fragmenté :** Si recv() ne donne que la moitié de la première ligne, feed() stocke dans _buf et attend le prochain appel. La ligne sera assemblée au prochain poll().

---

## 8. Le routeur (http/Router.cpp)

Le routeur reçoit une requête complète et décide quoi faire.

### Sélection de la location (longest prefix match)

```
Requête : GET /cgi-bin/hello.py

Locations configurées :
  "/"         (longueur 1)
  "/upload"   (longueur 7)
  "/cgi-bin"  (longueur 8)  ← GAGNANTE (préfixe le plus long)
```

### Arbre de décision complet

```
Router::handle(request)
      │
      ├── findLocation(path) ──── pas trouvé → 404
      │
      ├── redirect_code != 0 ──── 301/302 avec header Location:
      │
      ├── méthode non autorisée ── 405
      │
      ├── buildFilePath() ──── construit le chemin sur le disque
      │                        (protège contre ../../etc/passwd)
      │
      ├── extension .py/.php ? ── handleCgi() → fork/execve
      │
      ├── DELETE ──────────────── remove(path) → 204 ou 404
      │
      ├── POST (sans CGI) ──────── handleUpload() → 201
      │
      └── GET / HEAD
            ├── c'est un dossier ?
            │     ├── pas de slash final → redirect 301
            │     ├── index.html existe → serveFile()
            │     └── autoindex on → serveDirectory() (listing HTML)
            └── c'est un fichier → serveFile() → 200
```

### Protection path traversal

```
root = "./www"
URI  = /../../etc/passwd

buildFilePath() résout segment par segment :
  ".."  → pop (stack vide, on reste à la racine)
  ".."  → pop (stack vide)
  "etc" → push
  "passwd" → push

Résultat : "./www/etc/passwd"  ← jamais sorti de ./www
```

---

## 9. Le CGI (http/Cgi.cpp)

CGI = Common Gateway Interface. Au lieu de servir un fichier statique, le serveur **exécute un programme** et sert sa sortie.

### Flux d'exécution

```
Requête POST /cgi-bin/hello.py
              │
              ▼
         fork()
        /       \
PARENT           ENFANT
│                │
│                ├── dup2(stdinPipe[0], STDIN)   ← corps POST → stdin du script
│                ├── dup2(stdoutPipe[1], STDOUT) ← print() du script → réponse
│                ├── setenv(REQUEST_METHOD, QUERY_STRING, ...)
│                └── execve("/usr/bin/python3", ["python3", "hello.py"])
│
├── write(stdinPipe[1], req.body)  ← envoie le corps POST au script
├── poll(stdoutPipe[0], 10000ms)   ← lit la sortie (avec timeout)
└── waitpid()                      ← attendre la fin du processus
```

### Variables d'environnement CGI (RFC 3875)

| Variable        | Exemple                                  |
|-----------------|------------------------------------------|
| REQUEST_METHOD  | POST                                     |
| QUERY_STRING    | name=alice&age=25                        |
| CONTENT_LENGTH  | 42                                       |
| CONTENT_TYPE    | application/x-www-form-urlencoded        |
| SCRIPT_FILENAME | /absolute/path/to/hello.py               |
| SERVER_NAME     | localhost                                |
| SERVER_PORT     | 8080                                     |
| HTTP_HOST       | localhost:8080                           |

### Format de sortie d'un script CGI

```
Content-Type: text/html\r\n
\r\n
<html>...</html>
```

ou avec un statut custom :

```
Status: 404\r\n
Content-Type: text/plain\r\n
\r\n
Not found
```

---

## 10. La réponse HTTP (http/HttpResponse.cpp)

`build()` sérialise la réponse en texte pur :

```
HTTP/1.1 200 OK\r\n
Date: Mon, 18 May 2026 10:00:00 GMT\r\n
Server: webserv/1.0\r\n
Content-Type: text/html\r\n
Content-Length: 42\r\n
Connection: keep-alive\r\n
\r\n
<html>Hello World</html>
```

---

## 11. Flux complet d'une requête — de bout en bout

```
curl http://localhost:8080/index.html
```

```
1. RÉSEAU
   client ──TCP SYN──► listen socket fd=3

2. EVENT LOOP
   poll() détecte POLLIN sur fd=3
   accept() → crée fd=7 (client socket)
   new Connection(fd=7, port=8080)

3. LECTURE
   poll() détecte POLLIN sur fd=7
   conn->onReadable()
     recv() → "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n"
     request.feed() → PARSE_COMPLETE
     buildAndSend()

4. ROUTAGE
   selectServer() → vérifie Host: localhost → ServerBlock port 8080
   Router::handle()
     findLocation("/index.html") → location "/"
     buildFilePath() → "./www/index.html"
     stat() → c'est un fichier régulier
     serveFile("./www/index.html")
       open + read → contenu HTML
       getMimeType(".html") → "text/html"
     HttpResponse 200

5. ÉCRITURE
   res.build() → texte HTTP complet → writeBuffer
   state = CONN_WRITING
   poll() détecte POLLOUT sur fd=7
   conn->onWritable()
     send() → envoie writeBuffer
     tout envoyé → state = CONN_READING (keep-alive)

6. PROCHAINE REQUÊTE ou TIMEOUT
   Si le client envoie une autre requête → on recommence depuis 3
   Si 60s sans activité → closeConnection(fd=7)
```

---

## 12. Les virtual hosts

Le même serveur peut héberger **plusieurs sites web** sur le même port, distingués par le header `Host:` :

```
config :
  server { listen 8080; server_name site1.com; location / { root ./site1; } }
  server { listen 8080; server_name site2.com; location / { root ./site2; } }

curl -H "Host: site1.com" http://localhost:8080/  → sert ./site1/index.html
curl -H "Host: site2.com" http://localhost:8080/  → sert ./site2/index.html
```

Un seul socket listen par port → selectServer() choisit le bon ServerBlock selon Host:.

---

## 13. Points clés pour l'évaluation

| Point                 | Implémentation                                      |
|-----------------------|-----------------------------------------------------|
| poll() unique         | EventLoop.cpp — un seul appel par itération         |
| Non-bloquant          | setNonBlocking() sur tous les fds                   |
| Keep-alive            | _shouldClose + CONN_READING après envoi             |
| CGI                   | fork() + execve() + pipes                           |
| Upload                | handleUpload() → ofstream                           |
| Autoindex             | serveDirectory() → HTML généré                      |
| Error pages           | errorResponse() → fichier custom ou page générique  |
| client_max_body_size  | vérification dans feed() → 413                      |
| Path traversal        | buildFilePath() résout les ..                       |
| Virtual hosts         | selectServer() compare Host: aux server_name        |
| Timeout               | timedOut() → 60s sans activité                      |
| SIGPIPE ignoré        | signal(SIGPIPE, SIG_IGN) dans EventLoop             |
| SO_REUSEADDR          | setsockopt() pour éviter "Address in use"           |
