# *This project has been created as part of the 42 curriculum by ataan, zabu-bak.*

# restFulQuacks (webserv)

## Description

restFulQuacks is a lightweight HTTP/1.1 web server written in C++ as part of the 42 curriculum. The project aims to provide a practical implementation of a configurable web server, including request parsing, routing, response generation, and non-blocking client handling.

The server reads location-based rules from a configuration file and supports serving static files, custom error pages, directory autoindexing, HTTP redirects, file uploads and deletion, and CGI execution for configured file extensions. It is designed to handle multiple clients through an event-driven socket loop while respecting request methods and configurable body-size limits.


## Instructions

### Requirements

- A Unix-like environment (Linux or WSL)
- A C++ compiler with C++98 support
- GNU Make
- Python 3 for the example CGI scripts

### Compilation

From the project root, run:

```bash
make
```

To perform a clean rebuild:

```bash
make re
```

To remove compiled object files and the executable:

```bash
make fclean
```

### Execution

Start the server with the default configuration:

```bash
./webserv
```

Or provide a custom configuration file:

```bash
./webserv path/to/config.conf
```

The default configuration listens on `127.0.0.1:8083`. Once the server is running, requests can be sent with a browser or a tool such as `curl`:

```bash
curl -i http://127.0.0.1:8083/
```

Routes, allowed methods, document roots, redirects, uploads, CGI handlers, and error pages are configured in `config/default.conf` or in the custom configuration file supplied at startup.


## Resources

- [HTTP Semantics - RFC 9110](https://www.rfc-editor.org/rfc/rfc9110)
- [HTTP/1.1 Message Syntax and Routing - RFC 9112](https://www.rfc-editor.org/rfc/rfc9112)
- [MDN Web Docs: HTTP](https://developer.mozilla.org/en-US/docs/Web/HTTP)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [Linux `poll` documentation](https://man7.org/linux/man-pages/man2/poll.2.html)
- [Linux `socket` documentation](https://man7.org/linux/man-pages/man2/socket.2.html)
- [Common Gateway Interface - RFC 3875](https://www.rfc-editor.org/rfc/rfc3875)
- [cppreference: C++ language and library reference](https://en.cppreference.com/)

# AI Usage

AI assistance was used as a development and documentation aid for the following tasks:

- Explaining concepts, including webservers, poll mechanism,  request methods, redirects, status codes, query strings, CGI ...
- Helped troubleshooting and locating problems.
- Code reviews.
- Drafting and refining this README.

The project authors reviewed the suggestions, ran the build and request checks, and made the final decisions and integrations.