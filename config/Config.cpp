#include "Config.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cctype>

ConfigParser::ConfigParser(const std::string& filename) : pos(0) {
    tokenize(filename);
}

void ConfigParser::tokenize(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        throw std::runtime_error("Could not open config file: " + filename);
    }

    std::string currentToken = "";
    char c;
    
    while (file.get(c)) {
        if (c == '#') { // Skip comments
            while (file.get(c) && c != '\n') {}
            continue;
        }

        if (std::isspace(c)) { // Handle whitespace
            if (!currentToken.empty()) {
                tokens.push_back(currentToken);
                currentToken = "";
            }
            continue;
        }

        if (c == '{' || c == '}' || c == ';') { // Structural symbols
            if (!currentToken.empty()) {
                tokens.push_back(currentToken);
                currentToken = "";
            }
            tokens.push_back(std::string(1, c));
            continue;
        }

        currentToken += c;
    }
    
    if (!currentToken.empty()) {
        tokens.push_back(currentToken);
    }
}

std::string ConfigParser::peek() { 
    return (pos < tokens.size()) ? tokens[pos] : ""; 
}

std::string ConfigParser::consume() { 
    return (pos < tokens.size()) ? tokens[pos++] : ""; 
}

void ConfigParser::expect(const std::string& expected) {
    std::string actual = consume();
    if (actual != expected) {
        throw std::runtime_error("Syntax error: expected '" + expected + "' but got '" + actual + "'");
    }
}

void ConfigParser::parseLocationBlock(ServerConfig& server) {
    LocationConfig loc;
    loc.path = consume(); 
    expect("{");

    while (peek() != "}" && peek() != "") {
        std::string directive = consume();

        if (directive == "root") {
            loc.root = consume();
            expect(";");
        } 
        else if (directive == "allow_methods") {
            while (peek() != ";") {
                loc.allow_methods.push_back(consume());
            }
            expect(";");
        } 
        else if (directive == "autoindex") {
            loc.autoindex = (consume() == "on");
            expect(";");
        } 
        else if (directive == "index") {
            loc.index = consume();
            expect(";");
        } 
        else if (directive == "return") {
            int code = std::stoi(consume());
            std::string url = consume();
            loc.redirect = std::make_pair(code, url);
            expect(";");
        } 
        else if (directive == "upload_store") {
            loc.upload_store = consume();
            expect(";");
        } 
        else if (directive == "cgi_pass") {
            std::string ext = consume();
            std::string path = consume();
            loc.cgi_map[ext] = path;
            expect(";");
        } 
        else {
            throw std::runtime_error("Unknown location directive: " + directive);
        }
    }
    expect("}");
    server.locations.push_back(loc);
}

void ConfigParser::parseServerBlock() {
    ServerConfig server;
    expect("{");

    while (peek() != "}" && peek() != "") {
        std::string directive = consume();

        if (directive == "listen") {
            std::string val = consume();
            size_t colon = val.find(':');
            if (colon != std::string::npos) {
                server.host = val.substr(0, colon);
                server.port = std::stoi(val.substr(colon + 1));
            } else {
                server.port = std::stoi(val);
            }
            expect(";");
        } 
        else if (directive == "client_max_body_size") {
            server.client_max_body_size = std::stoull(consume());
            expect(";");
        } 
        else if (directive == "error_page") {
            std::vector<int> codes;
            while (peek() != ";" && std::isdigit(peek()[0])) {
                codes.push_back(std::stoi(consume()));
            }
            std::string page = consume();
            for (size_t i = 0; i < codes.size(); ++i) {
                server.error_pages[codes[i]] = page;
            }
            expect(";");
        } 
        else if (directive == "location") {
            parseLocationBlock(server);
        } 
        else {
            throw std::runtime_error("Unknown server directive: " + directive);
        }
    }
    expect("}");
    servers.push_back(server);
}

void ConfigParser::debugPrintTokens() {
    std::cout << "[DEBUG] Tokens found: " << tokens.size() << std::endl;
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::cout << "[" << tokens[i] << "] ";
    }
    std::cout << std::endl;
}

std::vector<ServerConfig> ConfigParser::parse() {
    pos = 0; 
    servers.clear();
    while (peek() != "") {
        std::string directive = consume();
        if (directive == "server") {
            parseServerBlock();
        } else {
            throw std::runtime_error("Global scope can only contain 'server' blocks. Found: " + directive);
        }
    }
    return servers;
}