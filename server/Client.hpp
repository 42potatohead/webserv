#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include "../config/Config.hpp"
#include "HTTPRequest.hpp"

enum ClientState {
    READING_HEADERS,
    READING_BODY,
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

    Client() : fd(-1), state(READING_HEADERS), bytesSent(0), contentLength(0) {}
};

#endif