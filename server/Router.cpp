#include "Router.hpp"
#include <fstream>
#include <sys/stat.h>
#include <algorithm>
#include <cstdio> // <-- Add this for remove()

std::string Router::buildResponse(int statusCode, const std::string& statusMessage, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << " " << statusMessage << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;
    return response.str();
}

std::string Router::getErrorPage(int statusCode, const ServerConfig& config) {
    // Check if the server config has a custom error page for this code
    std::map<int, std::string>::const_iterator it = config.error_pages.find(statusCode);
    if (it != config.error_pages.end()) {
        std::ifstream file(it->second.c_str());
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            return buildResponse(statusCode, "Error", ss.str());
        }
    }
    
    // Fallback to default HTML error pages
    std::ostringstream oss;
    oss << "<html><body><h1>" << statusCode << " Error</h1></body></html>";
    return buildResponse(statusCode, "Error", oss.str());
}

void Router::handleRequest(Client& client) {
    const HTTPRequest& req = client.request;
    const ServerConfig& config = client.config;
    
    // 1. Find the longest matching location block
    const LocationConfig* bestMatch = NULL;
    size_t longestMatch = 0;

    for (size_t i = 0; i < config.locations.size(); ++i) {
        const std::string& path = config.locations[i].path;
        if (req.uri.find(path) == 0) {
            if (path.length() > longestMatch) {
                longestMatch = path.length();
                bestMatch = &config.locations[i];
            }
        }
    }

    if (!bestMatch) {
        client.responseBuffer = getErrorPage(404, config);
        return;
    }

    // 2. Check Allowed Methods
    if (!bestMatch->allow_methods.empty()) {
        bool methodAllowed = false;
        for (size_t i = 0; i < bestMatch->allow_methods.size(); ++i) {
            if (req.method == bestMatch->allow_methods[i]) {
                methodAllowed = true;
                break;
            }
        }
        if (!methodAllowed) {
            client.responseBuffer = getErrorPage(405, config); 
            return;
        }
    }

    // 3. Handle Redirects (Return directive)
    if (bestMatch->redirect.first != 0) {
        std::ostringstream response;
        response << "HTTP/1.1 " << bestMatch->redirect.first << " Moved\r\n";
        response << "Location: " << bestMatch->redirect.second << "\r\n";
        response << "Connection: close\r\n\r\n";
        client.responseBuffer = response.str();
        return;
    }

    // Prepare a clean suffix (e.g., if URI is /upload/file.txt and location is /upload, suffix is file.txt)
    std::string suffix = req.uri.substr(bestMatch->path.length());
    if (!suffix.empty() && suffix[0] == '/') {
        suffix = suffix.substr(1);
    }

    // ==========================================
    // 4. Handle POST Requests (File Uploads)
    // ==========================================
    if (req.method == "POST") {
        if (bestMatch->upload_store.empty()) {
            client.responseBuffer = getErrorPage(403, config); // Forbidden if upload_store isn't set
            return;
        }
        if (suffix.empty()) {
            client.responseBuffer = buildResponse(400, "Bad Request", "Missing filename in URI for upload.");
            return;
        }

        // Combine upload_store path and the requested filename
        std::string savePath = bestMatch->upload_store;
        if (savePath[savePath.length()-1] != '/') savePath += "/";
        savePath += suffix;

        // Write the buffered body to disk
        std::ofstream outfile(savePath.c_str(), std::ios::binary);
        if (!outfile.is_open()) {
            client.responseBuffer = getErrorPage(500, config); // Internal Server Error
            return;
        }
        
        outfile.write(req.body.c_str(), req.body.length());
        outfile.close();

        client.responseBuffer = buildResponse(201, "Created", "<html><body><h1>201 Created</h1><p>File uploaded successfully.</p></body></html>");
        return;
    }

    // ==========================================
    // 5. Handle DELETE Requests
    // ==========================================
    if (req.method == "DELETE") {
        std::string targetPath = bestMatch->root;
        if (targetPath[targetPath.length()-1] != '/') targetPath += "/";
        targetPath += suffix;

        if (remove(targetPath.c_str()) == 0) {
            client.responseBuffer = buildResponse(200, "OK", "<html><body><h1>200 OK</h1><p>File deleted successfully.</p></body></html>");
        } else {
            client.responseBuffer = getErrorPage(404, config);
        }
        return;
    }

    // ==========================================
    // 6. Handle GET Requests (Serving Files)
    // ==========================================
    std::string resolvedPath = bestMatch->root;
    if (resolvedPath[resolvedPath.length()-1] != '/') resolvedPath += "/";
    resolvedPath += suffix;

    struct stat fileStat;
    if (stat(resolvedPath.c_str(), &fileStat) != 0) {
        client.responseBuffer = getErrorPage(404, config); 
        return;
    }

    if (S_ISDIR(fileStat.st_mode)) {
        if (!bestMatch->index.empty()) {
            if (resolvedPath[resolvedPath.length()-1] != '/') resolvedPath += "/";
            resolvedPath += bestMatch->index;
            if (stat(resolvedPath.c_str(), &fileStat) != 0 || S_ISDIR(fileStat.st_mode)) {
                 client.responseBuffer = getErrorPage(403, config); 
                 return;
            }
        } else if (bestMatch->autoindex) {
            std::string autoindexHtml = generateAutoindex(resolvedPath, req.uri);
            if (autoindexHtml.empty()) {
                client.responseBuffer = getErrorPage(403, config); // Forbidden (e.g., lack of read permissions)
            } else {
                client.responseBuffer = buildResponse(200, "OK", autoindexHtml);
            }
            return;
        } else {
            client.responseBuffer = getErrorPage(403, config); 
            return;
        }
    }

    // ==========================================
    // 6.5. Handle CGI Execution
    // ==========================================
    // Check if the requested file has an extension
    size_t dotPos = resolvedPath.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string ext = resolvedPath.substr(dotPos);
        
        // If the extension is in our location's cgi_map
        std::map<std::string, std::string>::const_iterator cgiIt = bestMatch->cgi_map.find(ext);
        if (cgiIt != bestMatch->cgi_map.end()) {
            std::string cgiExecutable = cgiIt->second; // e.g., "/usr/bin/php-cgi"
            client.responseBuffer = executeCGI(resolvedPath, cgiExecutable, req);
            return;
        }
    }


    std::ifstream file(resolvedPath.c_str(), std::ios::binary);
    if (!file.is_open()) {
        client.responseBuffer = getErrorPage(403, config);
        return;
    }

    std::ostringstream fileStream;
    fileStream << file.rdbuf();
    client.responseBuffer = buildResponse(200, "OK", fileStream.str());
}

std::string Router::generateAutoindex(const std::string& dirPath, const std::string& uri) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return ""; // Return empty string if directory cannot be opened
    }

    std::ostringstream html;
    html << "<html>\n<head><title>Index of " << uri << "</title></head>\n<body>\n";
    html << "<h1>Index of " << uri << "</h1>\n<hr><pre>\n";

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        
        // Hide the current directory "." link, but keep ".." for navigation
        if (name == ".") {
            continue;
        }

        // Build the hyperlink
        html << "<a href=\"" << uri;
        if (!uri.empty() && uri[uri.length() - 1] != '/') {
            html << "/";
        }
        html << name << "\">" << name << "</a>\n";
    }

    html << "</pre><hr>\n</body>\n</html>";
    closedir(dir);
    
    return html.str();
}

std::string Router::executeCGI(const std::string& scriptPath, const std::string& cgiExec, const HTTPRequest& req) {
    int pipe_in[2];  // Parent writes, child reads
    int pipe_out[2]; // Child writes, parent reads

    if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0) {
        return "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n";
    }

    pid_t pid = fork();
    if (pid < 0) {
        return "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n";
    }

    if (pid == 0) { // --- CHILD PROCESS ---
        close(pipe_in[1]); // Close write end of input pipe
        dup2(pipe_in[0], STDIN_FILENO);
        close(pipe_in[0]);

        close(pipe_out[0]); // Close read end of output pipe
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_out[1]);

        // Setup environment variables (CGI standard)
        std::vector<std::string> env_strings;
        env_strings.push_back("REQUEST_METHOD=" + req.method);
        env_strings.push_back("SERVER_PROTOCOL=HTTP/1.1");
        env_strings.push_back("SCRIPT_FILENAME=" + scriptPath);
        
        // Pass Content-Length for POST requests
        std::map<std::string, std::string>::const_iterator it = req.headers.find("Content-Length");
        if (it != req.headers.end()) env_strings.push_back("CONTENT_LENGTH=" + it->second);
        
        it = req.headers.find("Content-Type");
        if (it != req.headers.end()) env_strings.push_back("CONTENT_TYPE=" + it->second);

        // Convert vector to char** for execve
        std::vector<char*> envp;
        for (size_t i = 0; i < env_strings.size(); ++i) {
            envp.push_back(const_cast<char*>(env_strings[i].c_str()));
        }
        envp.push_back(NULL);

        // Setup arguments
        char* argv[] = { const_cast<char*>(cgiExec.c_str()), const_cast<char*>(scriptPath.c_str()), NULL };

        execve(cgiExec.c_str(), argv, &envp[0]);
        exit(1); // Exit if execve fails
    } 
    else { // --- PARENT PROCESS ---
        close(pipe_in[0]);
        close(pipe_out[1]);

        // Send POST body to CGI script
        if (!req.body.empty()) {
            write(pipe_in[1], req.body.c_str(), req.body.length());
        }
        close(pipe_in[1]); // Send EOF to child so it knows body is done

        // Wait for script to finish
        int status;
        waitpid(pid, &status, 0);

        // Read CGI output
        char buffer[4096];
        ssize_t bytesRead;
        std::string cgiOutput;
        while ((bytesRead = read(pipe_out[0], buffer, sizeof(buffer))) > 0) {
            cgiOutput.append(buffer, bytesRead);
        }
        close(pipe_out[0]);

        // CGI scripts output their own headers (e.g., Content-Type). 
        // We just need to prepend the HTTP status line.
        return "HTTP/1.1 200 OK\r\n" + cgiOutput;
    }
}