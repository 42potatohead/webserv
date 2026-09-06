#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include "../config/Config.hpp"
#include "HTTPRequest.hpp"

enum ClientState {
    READING_HEADERS,
    READING_BODY,
    READING_CGI,
    WRITING_RESPONSE
};

struct Client {
    int fd;
    ServerConfig config;
    std::string requestBuffer;      // Holds incoming header data
    std::string responseBuffer;     // Holds outgoing data
    ClientState state;              // What the client is currently doing
    size_t bytesSent;               // Tracks partial sends

    HTTPRequest request;            // The parsed request
    size_t contentLength;           // The expected body size (from headers)
    int cgi_fd;                     // <-- File descriptor for reading CGI output
    pid_t cgi_pid;                  // <-- Process ID of the script
    int cgi_in_fd;                  // <-- NEW: Input pipe (writing to CGI)
    size_t cgiBytesSent;            // <-- NEW: Tracker for POST body sent to CGI
    int file_fd;                    // <-- Tracks the open file for large GET requests
    bool isChunked;               // <-- Indicates if the request uses chunked transfer encoding
    std::string chunkedBuffer;    // <-- Buffer for chunked transfer encoding

    Client() : fd(-1), state(READING_HEADERS), bytesSent(0), contentLength(0),
               isChunked(false), cgi_fd(-1), cgi_pid(-1), file_fd(-1), cgi_in_fd(-1), cgiBytesSent(0) {}

};

#endif
