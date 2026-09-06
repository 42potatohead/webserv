#pragma once

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cctype>
#include <cstdlib> // For atoi
#include <sstream> // For istringstream

struct LocationConfig {
    std::string path;                                     // e.g., "/kapouet"
    std::string root;                                     // e.g., "/tmp/www"
    std::vector<std::string> allow_methods;               // e.g., ["GET", "POST"]
    std::string index;                                    // e.g., "index.html"
    bool autoindex;                                       // true/false
    
    std::pair<int, std::string> redirect;                 // <301, "/new-url">
    std::string upload_store;                             // Directory for saved files
    std::map<std::string, std::string> cgi_map;           // <".php", "/usr/bin/php-cgi">

    LocationConfig() : autoindex(false), redirect(0, "") {}
};

struct ServerConfig {
    std::string host;                                     // e.g., "127.0.0.1"
    int port;                                             // e.g., 8125
    size_t client_max_body_size;                          // In bytes
    
    std::map<int, std::string> error_pages;               // <404, "/404.html">
    std::vector<LocationConfig> locations;                // List of route rules

    ServerConfig() : host("0.0.0.0"), port(80), client_max_body_size(1048576) {}
};

class ConfigParser {
private:
    std::vector<std::string> tokens;
    size_t pos;
    std::vector<ServerConfig> servers;

    // Lexer
    void tokenize(const std::string& filename);

    // Parser Helpers
    std::string peek();
    std::string consume();
    void expect(const std::string& expected);

    // Block Parsers
    void parseLocationBlock(ServerConfig& server);
    void parseServerBlock();

public:
    ConfigParser(const std::string& filename);
    void debugPrintTokens();
    std::vector<ServerConfig> parse();
};
