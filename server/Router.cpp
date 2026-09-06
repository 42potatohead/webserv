#include "Router.hpp"
#include <fstream>
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>  // For remove()
#include <cstring>
#include <cstdlib>
#include <fcntl.h> // For fcntl
#include <unistd.h>

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

std::string Router::getMimeType(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) return "application/octet-stream"; // Default for unknown binary

    std::string ext = path.substr(dotPos);

    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".txt") return "text/plain";
    if (ext == ".ico") return "image/x-icon";

    return "application/octet-stream";
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
        client.state = WRITING_RESPONSE;
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
            client.state = WRITING_RESPONSE;
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
        client.state = WRITING_RESPONSE;
        return;
    }

    // Prepare a clean suffix
    std::string suffix = req.uri.substr(bestMatch->path.length());
    if (!suffix.empty() && suffix[0] == '/') {
        suffix = suffix.substr(1);
    }

    // ==========================================
    // 4. Handle POST Requests (File Uploads)
    // ==========================================
    if (req.method == "POST" && !bestMatch->upload_store.empty()) {
        if (suffix.empty()) {
            client.responseBuffer = buildResponse(400, "Bad Request", "Missing filename in URI for upload.");
            client.state = WRITING_RESPONSE;
            return;
        }

        std::string savePath = bestMatch->upload_store;
        if (savePath[savePath.length()-1] != '/') savePath += "/";
        savePath += suffix;

        std::ofstream outfile(savePath.c_str(), std::ios::binary);
        if (!outfile.is_open()) {
            client.responseBuffer = getErrorPage(500, config);
            client.state = WRITING_RESPONSE;
            return;
        }

        outfile.write(req.body.c_str(), req.body.length());
        outfile.close();

        client.responseBuffer = buildResponse(201, "Created", "<html><body><h1>201 Created</h1><p>File uploaded successfully.</p></body></html>");
        client.state = WRITING_RESPONSE;
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
        client.state = WRITING_RESPONSE;
        return;
    }

    // ==========================================
    // 6. Resolve Path for GET and CGI
    // ==========================================
    std::string resolvedPath = bestMatch->root;
    if (resolvedPath[resolvedPath.length()-1] != '/') resolvedPath += "/";
    resolvedPath += suffix;

    // ==========================================
    // 7. Handle CGI Execution (Non-Blocking)
    // ==========================================
    size_t dotPos = resolvedPath.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string ext = resolvedPath.substr(dotPos);
        std::map<std::string, std::string>::const_iterator cgiIt = bestMatch->cgi_map.find(ext);

        if (cgiIt != bestMatch->cgi_map.end()) {
            std::string cgiExec = cgiIt->second;

            int pipe_in[2];
            int pipe_out[2];
            if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0) {
                client.responseBuffer = getErrorPage(500, config);
                client.state = WRITING_RESPONSE;
                return;
            }

            pid_t pid = fork();
            if (pid < 0) {
                client.responseBuffer = getErrorPage(500, config);
                client.state = WRITING_RESPONSE;
                return;
            }

            if (pid == 0) {
                // --- CHILD PROCESS ---
                close(pipe_in[1]); dup2(pipe_in[0], STDIN_FILENO); close(pipe_in[0]);
                close(pipe_out[0]); dup2(pipe_out[1], STDOUT_FILENO); close(pipe_out[1]);

                std::vector<std::string> env_strings;
                env_strings.push_back("REQUEST_METHOD=" + req.method);
                env_strings.push_back("SERVER_PROTOCOL=HTTP/1.1");
                env_strings.push_back("SCRIPT_FILENAME=" + resolvedPath);

                if (req.headers.count("Content-Length")) {
                    env_strings.push_back("CONTENT_LENGTH=" + req.headers.find("Content-Length")->second);
                }
                if (req.headers.count("Content-Type")) {
                    env_strings.push_back("CONTENT_TYPE=" + req.headers.find("Content-Type")->second);
                }

                std::vector<char*> envp;
                for (size_t i = 0; i < env_strings.size(); ++i) {
                    envp.push_back(const_cast<char*>(env_strings[i].c_str()));
                }
                envp.push_back(NULL);

                char* argv[] = { const_cast<char*>(cgiExec.c_str()), const_cast<char*>(resolvedPath.c_str()), NULL };
                execve(cgiExec.c_str(), argv, &envp[0]);
                exit(1);
            } else {
                // --- PARENT PROCESS ---
                close(pipe_in[0]);
                close(pipe_out[1]);

                // Make the output pipe non-blocking
                fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);
                client.cgi_fd = pipe_out[0];
                client.cgi_pid = pid;

                // Write POST body to script
                if (!req.body.empty()) {
                    fcntl(pipe_in[1], F_SETFL, O_NONBLOCK);
                    client.cgi_in_fd = pipe_in[1]; // Save pipe to event loop
                }
                else {
                    close(pipe_in[1]); // No body to send, close the input pipe
                    client.cgi_in_fd = -1;
                }
                client.state = READING_CGI;
                return; // We exit handleRequest immediately without waiting!
            }
        }
    }

    // ==========================================
    // 8. Handle Standard GET Requests (Serving Files/Directories)
    // ==========================================
    struct stat fileStat;
    if (stat(resolvedPath.c_str(), &fileStat) != 0) {
        client.responseBuffer = getErrorPage(404, config);
        client.state = WRITING_RESPONSE;
        return;
    }

    if (S_ISDIR(fileStat.st_mode)) {
        if (!bestMatch->index.empty()) {
            if (resolvedPath[resolvedPath.length()-1] != '/') resolvedPath += "/";
            resolvedPath += bestMatch->index;
            // here
            if (stat(resolvedPath.c_str(), &fileStat) != 0 || S_ISDIR(fileStat.st_mode)) {
                 client.responseBuffer = getErrorPage(404, config);
                 client.state = WRITING_RESPONSE;
                 return;
            }
        } else if (bestMatch->autoindex) {
            std::string autoindexHtml = generateAutoindex(resolvedPath, req.uri);
            if (autoindexHtml.empty()) {
                client.responseBuffer = getErrorPage(403, config);
            } else {
                client.responseBuffer = buildResponse(200, "OK", autoindexHtml);
            }
            client.state = WRITING_RESPONSE;
            return;
        } else {
            client.responseBuffer = getErrorPage(403, config);
            client.state = WRITING_RESPONSE;
            return;
        }
    }

// ==========================================
    // (Keep your existing S_ISDIR block above this)
    // ==========================================

    // Use open() instead of std::ifstream so we can pass the fd to the event loop
    int fd = open(resolvedPath.c_str(), O_RDONLY);
    if (fd < 0) {
        client.responseBuffer = getErrorPage(403, config);
        client.state = WRITING_RESPONSE;
        return;
    }

    // Hand the file descriptor to the Client context
    client.file_fd = fd;

    // Generate ONLY the headers here. The body is streamed in TCPListner.cpp.
    std::ostringstream headers;
    headers << "HTTP/1.1 200 OK\r\n";
    headers << "Content-Type: " << getMimeType(resolvedPath) << "\r\n";
    headers << "Content-Length: " << fileStat.st_size << "\r\n";
    headers << "Connection: close\r\n\r\n";

    client.responseBuffer = headers.str();
    client.state = WRITING_RESPONSE;
}
