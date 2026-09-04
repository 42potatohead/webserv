#include "TCPListner.hpp"
#include "Router.hpp"
#include <sstream>

volatile sig_atomic_t serverRunning = 1;

void handleSignal(int signum) {
    std::cout << "\n[DEBUG] Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    serverRunning = 0;
}

TCPListner::TCPListner(const std::vector<ServerConfig>& configs)
    : configs(configs) {
    std::cout << "[DEBUG] TCPListner constructed with " << configs.size() << " configurations." << std::endl;
}

TCPListner::~TCPListner() {
    std::cout << "[DEBUG] TCPListner destructor called" << std::endl;
    stopServer();
}

void TCPListner::startServer() {
    signal(SIGINT, handleSignal);  
    signal(SIGTERM, handleSignal); 

    for (size_t i = 0; i < configs.size(); ++i) {
        const ServerConfig& config = configs[i];
        std::cout << "[DEBUG] Booting server on port " << config.port << std::endl;

        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        int opt = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(sockfd);
            throw std::runtime_error("setsockopt failed");
        }

        sockaddr_in serverAddress;
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(config.port);
        // In a real scenario, you'd use inet_pton with config.host instead of INADDR_ANY
        serverAddress.sin_addr.s_addr = INADDR_ANY; 

        int flags = fcntl(sockfd, F_GETFL, 0);
        if (flags < 0 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(sockfd);
            throw std::runtime_error("fcntl failed to set non-blocking on server socket");
        }

        if (bind(sockfd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
            std::cerr << "Error: Bind failed" << std::endl;
            close(sockfd);
            std::ostringstream oss;
            oss << "Bind failed on port " << config.port;
            throw std::runtime_error(oss.str()); // C++98 way to combine string + int
        }
        std::cout << "[DEBUG] Bind successful on port " << config.port << std::endl;

        if (listen(sockfd, SOMAXCONN) < 0) {
            std::cerr << "Error: Listen failed" << std::endl;
            close(sockfd);
            std::ostringstream oss;
            oss << "Listen failed on port " << config.port;
            throw std::runtime_error(oss.str());
        }
        std::cout << "[DEBUG] Port " << config.port << " listening on fd " << sockfd << std::endl;

        // Register the server socket
        listeningSockets[sockfd] = config;
        
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        fds.push_back(pfd);
    }
}

void TCPListner::stopServer() {
    // Closes all client sockets AND server sockets (they are all in fds)
    for (size_t i = 0; i < fds.size(); i++) {
        if (fds[i].fd != -1) {
            std::cout << "[DEBUG] Closing socket fd " << fds[i].fd << std::endl;
            close(fds[i].fd);
        }
    }
    fds.clear();
    listeningSockets.clear();
}

void TCPListner::runServer() {
    std::cout << "[DEBUG] Entering main event loop..." << std::endl;

    while (serverRunning) {
        int activity = poll(fds.data(), fds.size(), -1);
        if (activity < 0) {
            if (errno == EINTR) break; // Clean shutdown on Ctrl+C
            throw std::runtime_error("Poll failed");
        }

        for (int i = fds.size() - 1; i >= 0; i--) {
            
            // ---> FIX 1: Added POLLOUT here so the loop actually sees write events <---
            if (fds[i].revents & (POLLIN | POLLOUT | POLLERR | POLLHUP | POLLNVAL)) {

                // CASE 1: Activity on a SERVER Socket (New Connection)
                // If the fd is a key in our listeningSockets map, it's a server socket
                if (listeningSockets.find(fds[i].fd) != listeningSockets.end()) {
                    int clientSocket = accept(fds[i].fd, NULL, NULL);
                    if (clientSocket < 0) {
                        if (errno != EWOULDBLOCK && errno != EAGAIN) {
                            std::cerr << "Error: Accept failed" << std::endl;
                        }
                        continue;
                    }

                    int clientFlags = fcntl(clientSocket, F_GETFL, 0);
                    if (clientFlags < 0 || fcntl(clientSocket, F_SETFL, clientFlags | O_NONBLOCK) < 0) {
                        close(clientSocket);
                        continue;
                    }

                    int portHit = listeningSockets[fds[i].fd].port;
                    std::cout << "[DEBUG] New client on fd " << clientSocket << " connected via port " << portHit << std::endl;

                    // Initialize the Client context
                    Client newClient;
                    newClient.fd = clientSocket;
                    newClient.config = listeningSockets[fds[i].fd]; 
                    clients[clientSocket] = newClient;              

                    struct pollfd client_fd;
                    client_fd.fd = clientSocket;
                    client_fd.events = POLLIN;
                    fds.push_back(client_fd);
                }

                // CASE 2: Activity on a Client Socket -> Incoming Data, Outgoing Data, or Disconnect
                else {
                    // ---> FIX 2: Check for errors or hangups first <---
                    if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        std::cout << "[DEBUG] Client disconnected (Error/HUP) on fd " << fds[i].fd << std::endl;
                        clients.erase(fds[i].fd);
                        close(fds[i].fd);
                        fds.erase(fds.begin() + i);
                        continue;
                    }

                    // --- STATE: READING INCOMING DATA ---
                    if (fds[i].revents & POLLIN) {
                        char buffer[4096];
                        ssize_t bytesRead = recv(fds[i].fd, buffer, sizeof(buffer), 0);

                        if (bytesRead <= 0) {
                            std::cout << "[DEBUG] Client disconnected on fd " << fds[i].fd << std::endl;
                            clients.erase(fds[i].fd);
                            close(fds[i].fd);
                            fds.erase(fds.begin() + i);
                        }
                        else {
                            Client& client = clients[fds[i].fd];

                            // PHASE A: Still reading headers
                            if (client.state == READING_HEADERS) {
                                client.requestBuffer.append(buffer, bytesRead);

                                // Check if headers are complete
                                if (client.requestBuffer.find("\r\n\r\n") != std::string::npos) {
                                    std::cout << "[DEBUG] Headers fully received on fd " << fds[i].fd << "!\n";
                                    
                                    if (HTTPParser::parse(client.requestBuffer, client.request)) {
                                        
                                        // Find Content-Length safely
                                        client.contentLength = 0;
                                        if (client.request.headers.find("Content-Length") != client.request.headers.end()) {
                                            std::istringstream iss(client.request.headers["Content-Length"]);
                                            iss >> client.contentLength;
                                        }

                                        // Enforce max body size from config
                                        if (client.contentLength > client.config.client_max_body_size) {
                                            client.responseBuffer = "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n";
                                            client.state = WRITING_RESPONSE;
                                            fds[i].events = POLLOUT;
                                        } 
                                        // Check if we already have the full body (or if there is no body)
                                        else if (client.request.body.length() >= client.contentLength) {
                                            Router::handleRequest(client);
                                            client.state = WRITING_RESPONSE;
                                            fds[i].events = POLLOUT;
                                        } 
                                        // Otherwise, change state to wait for the rest of the body
                                        else {
                                            std::cout << "[DEBUG] Waiting for body data on fd " << fds[i].fd << "...\n";
                                            client.state = READING_BODY;
                                        }
                                    } else {
                                        client.responseBuffer = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                                        client.state = WRITING_RESPONSE;
                                        fds[i].events = POLLOUT;
                                    }
                                }
                            }
                            
                            // PHASE B: Reading the body (e.g., POST data)
                            else if (client.state == READING_BODY) {
                                // Append directly to the request's body string
                                client.request.body.append(buffer, bytesRead);

                                // Check if we've reached the expected length
                                if (client.request.body.length() >= client.contentLength) {
                                    std::cout << "[DEBUG] Full body (" << client.contentLength << " bytes) received on fd " << fds[i].fd << "!\n";
                                    Router::handleRequest(client);
                                    client.state = WRITING_RESPONSE;
                                    fds[i].events = POLLOUT;
                                }
                            }
                        }
                    }

                    // --- STATE: WRITING RESPONSE ---
                    else if (fds[i].revents & POLLOUT) {
                        Client& client = clients[fds[i].fd];
                        
                        // Calculate how much is left to send
                        size_t remaining = client.responseBuffer.length() - client.bytesSent;
                        
                        // Attempt to send the remaining bytes
                        ssize_t sent = send(fds[i].fd, client.responseBuffer.c_str() + client.bytesSent, remaining, 0);

                        if (sent > 0) {
                            client.bytesSent += sent;
                            std::cout << "[DEBUG] Sent " << sent << " bytes to fd " << fds[i].fd << std::endl;

                            // If we sent everything, close the connection (HTTP/1.0 style)
                            if (client.bytesSent >= client.responseBuffer.length()) {
                                std::cout << "[DEBUG] Response fully delivered. Closing fd " << fds[i].fd << std::endl;
                                clients.erase(fds[i].fd);
                                close(fds[i].fd);
                                fds.erase(fds.begin() + i);
                            }
                        }
                        else if (sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
                            std::cerr << "[ERROR] Send failed on fd " << fds[i].fd << std::endl;
                            clients.erase(fds[i].fd);
                            close(fds[i].fd);
                            fds.erase(fds.begin() + i);
                        }
                    }
                }
            }
        }
    }
}