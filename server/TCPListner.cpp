#include "TCPListner.hpp"
#include "Router.hpp"
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

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
            throw std::runtime_error(oss.str());
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

        listeningSockets[sockfd] = config;

        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        fds.push_back(pfd);
    }
}

void TCPListner::stopServer() {
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
            if (errno == EINTR) break;
            throw std::runtime_error("Poll failed");
        }

        for (int i = fds.size() - 1; i >= 0; i--) {

            if (fds[i].revents & (POLLIN | POLLOUT | POLLERR | POLLHUP | POLLNVAL)) {

                // ==========================================
                // CASE 1: Activity on a SERVER Socket
                // ==========================================
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
                    std::cout << "[DEBUG]  client on fd " << clientSocket << " connected via port " << portHit << std::endl;

                    Client Client;
                    Client.fd = clientSocket;
                    Client.config = listeningSockets[fds[i].fd];
                    clients[clientSocket] = Client;

                    struct pollfd client_fd;
                    client_fd.fd = clientSocket;
                    client_fd.events = POLLIN;
                    fds.push_back(client_fd);
                }

                // ==========================================
                // CASE 3: Activity on a CGI Pipe
                // ==========================================
                else if (cgiToClient.find(fds[i].fd) != cgiToClient.end()) {
                    int clientFd = cgiToClient[fds[i].fd];
                    Client& client = clients[clientFd];

                    if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                        char buffer[4096];
                        ssize_t bytesRead = read(fds[i].fd, buffer, sizeof(buffer));

                        if (bytesRead > 0) {
                            client.responseBuffer.append(buffer, bytesRead);
                        }
                        if (bytesRead == 0 || (bytesRead < 0 && errno != EAGAIN) || (fds[i].revents & (POLLHUP | POLLERR))) {
                            close(fds[i].fd);
                            waitpid(client.cgi_pid, NULL, 0);

                            if (client.responseBuffer.empty()) {
                                client.responseBuffer = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n";
                            } else {
                                client.responseBuffer = "HTTP/1.1 200 OK\r\n" + client.responseBuffer;
                            }

                            cgiToClient.erase(fds[i].fd);
                            fds.erase(fds.begin() + i);

                            client.state = WRITING_RESPONSE;
                            for (size_t j = 0; j < fds.size(); ++j) {
                                if (fds[j].fd == clientFd) {
                                    fds[j].events = POLLOUT;
                                    break;
                                }
                            }
                            continue;
                        }
                    }
                }

                // ==========================================
                // CASE 2: Activity on a Client Socket
                // ==========================================
                else {
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

                                if (client.requestBuffer.find("\r\n\r\n") != std::string::npos) {
                                    std::cout << "[DEBUG] Headers fully received on fd " << fds[i].fd << "!\n";

                                    if (HTTPParser::parse(client.requestBuffer, client.request)) {

                                        // --- : Check for Chunked Encoding ---
                                        if (client.request.headers["Transfer-Encoding"] == "chunked") {
                                            client.isChunked = true;
                                            client.chunkedBuffer = client.request.body; // Move any pre-read body data
                                            client.request.body.clear();
                                            client.state = READING_BODY;
                                            std::cout << "[DEBUG] Expecting chunked body on fd " << fds[i].fd << "...\n";
                                        }
                                        // --- EXISTING: Content-Length Check ---
                                        else {
                                            client.contentLength = 0;
                                            if (client.request.headers.find("Content-Length") != client.request.headers.end()) {
                                                std::istringstream iss(client.request.headers["Content-Length"]);
                                                iss >> client.contentLength;
                                            }

                                            if (client.contentLength > client.config.client_max_body_size) {
                                                client.responseBuffer = "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n";
                                                client.state = WRITING_RESPONSE;
                                                fds[i].events = POLLOUT;
                                            }
                                            else if (client.request.body.length() >= client.contentLength) {
                                                Router::handleRequest(client);
                                                if (client.state == WRITING_RESPONSE) {
                                                    fds[i].events = POLLOUT;
                                                } else if (client.state == READING_CGI) {
                                                    struct pollfd cgi_pfd;
                                                    cgi_pfd.fd = client.cgi_fd;
                                                    cgi_pfd.events = POLLIN;
                                                    fds.push_back(cgi_pfd);
                                                    cgiToClient[client.cgi_fd] = client.fd;
                                                    fds[i].events = 0;
                                                }
                                            }
                                            else {
                                                std::cout << "[DEBUG] Waiting for body data on fd " << fds[i].fd << "...\n";
                                                client.state = READING_BODY;
                                            }
                                        }
                                    } else {
                                        client.responseBuffer = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                                        client.state = WRITING_RESPONSE;
                                        fds[i].events = POLLOUT;
                                    }
                                }
                            }

                            // PHASE B: Reading the body
                            else if (client.state == READING_BODY) {
                                // PATH 1: Chunked Encoding
                                if (client.isChunked) {
                                    client.chunkedBuffer.append(buffer, bytesRead);

                                    bool done = false;
                                    while (!client.chunkedBuffer.empty()) {
                                        size_t pos = client.chunkedBuffer.find("\r\n");
                                        if (pos == std::string::npos) break; // Need more data for hex size

                                        std::string hexStr = client.chunkedBuffer.substr(0, pos);
                                        size_t chunkSize = 0;
                                        std::stringstream ss;
                                        ss << std::hex << hexStr;
                                        ss >> chunkSize;

                                        if (chunkSize == 0) {
                                            done = true; // Received 0\r\n, body is complete
                                            break;
                                        }

                                        // Check if the full chunk + trailing \r\n is in the buffer
                                        if (client.chunkedBuffer.length() >= pos + 2 + chunkSize + 2) {
                                            client.request.body.append(client.chunkedBuffer.substr(pos + 2, chunkSize));
                                            client.chunkedBuffer.erase(0, pos + 2 + chunkSize + 2);
                                        } else {
                                            break; // Wait for the rest of the chunk
                                        }
                                    }

                                    if (client.request.body.length() > client.config.client_max_body_size) {
                                        client.responseBuffer = "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n";
                                        client.state = WRITING_RESPONSE;
                                        fds[i].events = POLLOUT;
                                    } else if (done) {
                                        std::cout << "[DEBUG] Chunked body complete on fd " << fds[i].fd << "!\n";
                                        Router::handleRequest(client);
                                        if (client.state == WRITING_RESPONSE) {
                                            fds[i].events = POLLOUT;
                                        } else if (client.state == READING_CGI) {
                                            struct pollfd cgi_pfd;
                                            cgi_pfd.fd = client.cgi_fd;
                                            cgi_pfd.events = POLLIN;
                                            fds.push_back(cgi_pfd);
                                            cgiToClient[client.cgi_fd] = client.fd;
                                            fds[i].events = 0;
                                        }
                                    }
                                }
                                // PATH 2: Standard Content-Length
                                else {
                                    client.request.body.append(buffer, bytesRead);

                                    if (client.request.body.length() >= client.contentLength) {
                                        std::cout << "[DEBUG] Full body (" << client.contentLength << " bytes) received on fd " << fds[i].fd << "!\n";
                                        Router::handleRequest(client);

                                        if (client.state == WRITING_RESPONSE) {
                                            fds[i].events = POLLOUT;
                                        } else if (client.state == READING_CGI) {
                                            struct pollfd cgi_pfd;
                                            cgi_pfd.fd = client.cgi_fd;
                                            cgi_pfd.events = POLLIN;
                                            fds.push_back(cgi_pfd);
                                            cgiToClient[client.cgi_fd] = client.fd;
                                            fds[i].events = 0;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // --- STATE: WRITING RESPONSE ---
                    else if (fds[i].revents & POLLOUT) {
                        Client& client = clients[fds[i].fd];

                        if (client.bytesSent >= client.responseBuffer.length() && client.file_fd != -1) {
                            char fileBuf[8192];
                            ssize_t r = read(client.file_fd, fileBuf, sizeof(fileBuf));
                            if (r > 0) {
                                client.responseBuffer.clear();
                                client.responseBuffer.append(fileBuf, r);
                                client.bytesSent = 0;
                            } else {
                                close(client.file_fd);
                                client.file_fd = -1;
                            }
                        }

                        size_t remaining = client.responseBuffer.length() - client.bytesSent;

                        if (remaining > 0) {
                            ssize_t sent = send(fds[i].fd, client.responseBuffer.c_str() + client.bytesSent, remaining, 0);

                            if (sent > 0) {
                                client.bytesSent += sent;
                                std::cout << "[DEBUG] Sent " << sent << " bytes to fd " << fds[i].fd << std::endl;
                            }
                            else if (sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
                                std::cerr << "[ERROR] Send failed on fd " << fds[i].fd << std::endl;
                                if (client.file_fd != -1) close(client.file_fd);
                                clients.erase(fds[i].fd);
                                close(fds[i].fd);
                                fds.erase(fds.begin() + i);
                                continue;
                            }
                        }

                        if (client.bytesSent >= client.responseBuffer.length() && client.file_fd == -1) {
                            std::cout << "[DEBUG] Response fully delivered. Closing fd " << fds[i].fd << std::endl;
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
