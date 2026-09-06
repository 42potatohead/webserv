#ifndef ROUTER_HPP
#define ROUTER_HPP

#include "Client.hpp"
#include <string>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <cstdlib>

class Router {
public:
    // Takes the client context, routes the request, and populates client.responseBuffer
    static void handleRequest(Client& client);

private:
    static std::string buildResponse(int statusCode, const std::string& statusMessage, const std::string& body);
    static std::string getErrorPage(int statusCode, const ServerConfig& config);
    static std::string getMimeType(const std::string& path);
    static std::string generateAutoindex(const std::string& dirPath, const std::string& uri);
    static std::string executeCGI(const std::string& scriptPath, const std::string& cgiExec, const HTTPRequest& req);
    static std::string getMimeType(const std::string& path);

};

#endif
