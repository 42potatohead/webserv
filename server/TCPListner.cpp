#include "TCPListner.hpp"

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
            close(sockfd);
            throw std::runtime_error("Bind failed on port " + std::to_string(config.port));
        }

        // Listen immediately during boot
        if (listen(sockfd, SOMAXCONN) < 0) {
            close(sockfd);
            throw std::runtime_error("Listen failed on port " + std::to_string(config.port));
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
            if (fds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {

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

                    struct pollfd client_fd;
                    client_fd.fd = clientSocket;
                    client_fd.events = POLLIN;
                    fds.push_back(client_fd);
                }

                // CASE 2: Activity on a CLIENT Socket
                else {
                    if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        std::cout << "[DEBUG] Client disconnected (Error/HUP) on fd " << fds[i].fd << std::endl;
                        close(fds[i].fd);
                        fds.erase(fds.begin() + i);
                        continue;
                    }
                    if (fds[i].revents & POLLIN) {
                        char buffer[1024];
                        ssize_t bytesRead = recv(fds[i].fd, buffer, sizeof(buffer) - 1, 0);

                        if (bytesRead <= 0) {
                            std::cout << "[DEBUG] Client disconnected on fd " << fds[i].fd << std::endl;
                            close(fds[i].fd);
                            fds.erase(fds.begin() + i);
                        }
                        else {
                            buffer[bytesRead] = '\0';
                            std::cout << "[DEBUG] Received " << bytesRead << " bytes from fd " << fds[i].fd << std::endl;
                            std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello Server!";
                            send(fds[i].fd, response.c_str(), response.length(), 0);
                        }
                    }
                }
            }
        }
    }
}