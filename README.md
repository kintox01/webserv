*This project has been created as part of the 42 curriculum by dguerin, acharvoz.*

# webserv

## Description

`webserv` is a HTTP/1.1 server written from scratch in C++98 — no external HTTP
library, no `libevent`, nothing pulled in to do the hard part for us. Think of
it as a tiny, homemade `nginx`: it listens on one or several ports, parses raw
HTTP requests byte by byte, and answers with static files, directory listings,
uploads, or the output of CGI scripts.

The whole point of the project (and the reason it's harder than it sounds) is
the constraint set by the subject: everything has to run on a **single
thread**, using **one call to `poll()`** to watch every socket at once, and
every socket has to be non-blocking. No thread per client, no blocking
`recv()`/`send()`/`accept()` anywhere, and the only place `fork()` is allowed
is when executing a CGI script. So instead of "just" implementing HTTP, most
of the actual work went into building a correct event loop and a set of state
machines (one for parsing a request incrementally, one for tracking each
connection's lifecycle) that don't fall apart when data arrives split across
multiple TCP packets — which, in practice, happens constantly.

It supports:
- GET / POST / DELETE / HEAD, with `PUT` and `OPTIONS` accepted at the parser
  level (no dedicated semantics beyond that yet — see limitations below)
- Static file serving with correct `Content-Type` detection
- Directory listing (autoindex) and default index files
- File uploads via POST, including `Transfer-Encoding: chunked` bodies
  (decoded by the server before they reach the router or the CGI)
- CGI execution (Python by default, configurable per extension), run
  asynchronously — its pipes are driven by the same single `poll()` as every
  client socket, so a hanging script (e.g. an infinite loop) can't freeze the
  rest of the server; it gets killed and answered with a 504 after 10s
- Multiple virtual hosts on the same port, dispatched by the `Host` header
- Custom error pages, redirections, per-location method restrictions
- Keep-alive connections and configurable `client_max_body_size`
- An nginx-inspired configuration file format

## Instructions

### Requirements

A C++98-compatible compiler (`c++`/`g++`/`clang++`) and a POSIX system
(Linux or macOS). No third-party dependencies.

### Compilation

```sh
make
```

This builds the `webserv` binary from the sources in `config/`, `core/`,
`http/` and `utils/`, with `-Wall -Wextra -Werror -std=c++98`. Other targets:

```sh
make clean   # remove object files
make fclean  # remove object files and the binary
make re      # fclean + all
```

### Running it

```sh
./webserv [path/to/config.conf]
```

If no argument is given, it falls back to `webserv.conf` at the root of the
repo. A minimal config looks like this:

```nginx
server {
    listen 8080;
    server_name localhost;

    error_page 404 /errors/404.html;
    client_max_body_size 10M;

    location / {
        root ./www;
        index index.html;
        methods GET HEAD;
    }

    location /upload {
        root ./www/uploads;
        methods GET POST DELETE;
        upload_store ./www/uploads;
        autoindex on;
    }

    location /cgi-bin {
        root ./www/cgi-bin;
        methods GET POST;
        cgi .py /usr/bin/python3;
    }
}
```

Once it's running, hit it with `curl`, a browser, or a load tool like `siege`:

```sh
curl http://localhost:8080/
curl -X POST -F "file=@somefile.txt" http://localhost:8080/upload
curl http://localhost:8080/cgi-bin/hello.py
```

Stop it with `Ctrl+C` — signals are handled so the server shuts down cleanly
instead of leaving sockets or child processes behind.

### Known limitations

We'd rather be upfront about these than have someone find them during
evaluation: `PUT`/`OPTIONS` are accepted by the parser but don't have
dedicated behaviour beyond that (a `PUT` currently falls through to the
same path as a `GET`).

## Resources

Things that were actually useful while building this, beyond the subject PDF:

- [RFC 7230](https://datatracker.ietf.org/doc/html/rfc7230) — HTTP/1.1
  message syntax and routing, the reference for what a "correct" request/
  response actually looks like on the wire
- [RFC 3875](https://datatracker.ietf.org/doc/html/rfc3875) — the CGI
  specification, mainly for the environment variables and the
  header/body separation on stdout
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) —
  still the best plain-English explanation of sockets, `bind`/`listen`/
  `accept`, and non-blocking I/O
- `man poll`, `man fork`, `man execve`, `man dup2` — read (and re-read) more
  times than we'd like to admit, especially while chasing the fork/pipe/
  dup2 dance for CGI
- [nginx documentation](https://nginx.org/en/docs/) — as inspiration for the
  configuration file syntax (`server {}` / `location {}` blocks)

**On AI use:** Claude (Claude Code) was used for testing (manual endpoint
checks, the CGI timeout/infinite-loop scenario, load testing) and for writing
this README.

## Project structure

```
webserv/
├── main.cpp           entry point: loads the config, starts the server
├── config/             .conf file tokenizer + recursive-descent parser
├── core/               the event loop, socket setup, per-connection state
├── http/               request parsing, routing, CGI, response building
└── utils/              network buffer, logger, MIME type lookup
```

`core/` only ever deals with file descriptors and bytes — it has no idea
what HTTP is. `http/` is what turns those bytes into something meaningful.
Keeping that boundary clean made it a lot easier to reason about which bugs
belonged where while debugging.
