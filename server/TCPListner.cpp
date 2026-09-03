#include "TCPListner.hpp"

volatile sig_atomic_t serverRunning = 1;

void handleSignal(int signum) {
	std::cout << "\n[DEBUG] Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
	serverRunning = 0;
}

TCPListner::TCPListner(int port)
	: serverSocket(-1), port(port) {
	std::cout << "[DEBUG] TCPListner constructed for port " << port << std::endl;
}

TCPListner::~TCPListner() {
	std::cout << "[DEBUG] TCPListner destructor called" << std::endl;
	stopServer();
}

void TCPListner::startServer() {
	signal(SIGINT, handleSignal);  // Catches Ctrl+C
	signal(SIGTERM, handleSignal); // Catches termination requests
	std::cout << "[DEBUG] Starting server on port " << port << std::endl;
	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket < 0) {
		std::cerr << "Error: Socket creation failed" << std::endl;
		throw std::runtime_error("Socket creation failed");
	}

	int opt = 1;
	if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		std::cerr << "Error: setsockopt failed" << std::endl;
		throw std::runtime_error("setsockopt failed");
	}
	std::cout << "[DEBUG] Socket created with fd " << serverSocket << std::endl;
	sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(port);
	serverAddress.sin_addr.s_addr = INADDR_ANY;

	// Get current socket flags
	int flags = fcntl(serverSocket, F_GETFL, 0);
	if (flags < 0) {
		std::cerr << "Error: fcntl(F_GETFL) failed" << std::endl;
		throw std::runtime_error("fcntl failed");
	}

	// Set the O_NONBLOCK flag
	if (fcntl(serverSocket, F_SETFL, flags | O_NONBLOCK) < 0) {
		std::cerr << "Error: fcntl(F_SETFL) failed to set non-blocking" << std::endl;
		throw std::runtime_error("fcntl failed");
	}
	if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
		std::cerr << "Error: Bind failed" << std::endl;
		throw std::runtime_error("Bind failed");
	}
	std::cout << "[DEBUG] Bind successful on port " << port << std::endl;
}

void TCPListner::stopServer() {
	// Loop through all active sockets (server and clients) and close them
	for (size_t i = 0; i < fds.size(); i++) {
		if (fds[i].fd != -1) {
			std::cout << "[DEBUG] Closing socket fd " << fds[i].fd << std::endl;
			close(fds[i].fd);
		}
	}
	fds.clear(); // Empty the vector

	// Safety fallback for the server socket just in case it wasn't in the vector yet
	if (serverSocket != -1) {
		close(serverSocket);
		serverSocket = -1;
	}
}

void TCPListner::runServer() {
	std::cout << "[DEBUG] Listening for incoming connections" << std::endl;
	if (listen(serverSocket, SOMAXCONN) < 0) {
		std::cerr << "Error: Listen failed" << std::endl;
		throw std::runtime_error("Listen failed");
	}
	std::cout << "[DEBUG] Listen successful" << std::endl;
	struct pollfd server_fd;
	server_fd.fd = serverSocket;
	server_fd.events = POLLIN; // We want to know when there is data to read
	fds.push_back(server_fd);

	while (serverRunning) {
		// poll() blocks until at least one fd is ready. -1 means wait indefinitely.
		int activity = poll(fds.data(), fds.size(), -1);
		if (activity < 0) {
			if (errno == EINTR) {
				// We were woken up by a signal (like Ctrl+C)
				// Break out of the while loop to shut down cleanly
				break;
			}
			std::cerr << "Error: Poll failed" << std::endl;
			throw std::runtime_error("Poll failed");
		}

		// Iterate backwards through the vector.
		// We do this so if we delete a disconnected client, it doesn't mess up our loop index.
		for (int i = fds.size() - 1; i >= 0; i--) {

			// Check if this specific socket has data ready to read (POLLIN)
			if (fds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {

				// CASE 1: Activity on the Server Socket -> New Client Connection
				if (fds[i].fd == serverSocket) {
					int clientSocket = accept(serverSocket, NULL, NULL);
					if (clientSocket < 0) {
						if (errno != EWOULDBLOCK && errno != EAGAIN) {
							std::cerr << "Error: Accept failed" << std::endl;
						}
						continue;
					}
					int clientFlags = fcntl(clientSocket, F_GETFL, 0);
					if (clientFlags < 0 || fcntl(clientSocket, F_SETFL, clientFlags | O_NONBLOCK) < 0) {
						std::cerr << "Error: Failed to set client socket to non-blocking" << std::endl;
						close(clientSocket);
						continue;
					}

					std::cout << "[DEBUG] New client connected on fd " << clientSocket << std::endl;

					// Add the new client to our polling list
					struct pollfd client_fd;
					client_fd.fd = clientSocket;
					client_fd.events = POLLIN;
					fds.push_back(client_fd);
				}

				// CASE 2: Activity on a Client Socket -> Incoming Data or Disconnect
				else {
					if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
						std::cout << "[DEBUG] Client disconnected (Error/HUP) on fd " << fds[i].fd << std::endl;
						close(fds[i].fd);
						fds.erase(fds.begin() + i);
						continue;
					}
					if (fds[i].revents & POLLIN) {
						// Read data from the client
						char buffer[1024];
						ssize_t bytesRead = recv(fds[i].fd, buffer, sizeof(buffer) - 1, 0);

						if (bytesRead <= 0) {
							// 0 means graceful disconnect, < 0 means error
							std::cout << "[DEBUG] Client disconnected on fd " << fds[i].fd << std::endl;
							close(fds[i].fd);
							fds.erase(fds.begin() + i); // Remove from our polling list
						}
						else {
							// We received actual data!
							buffer[bytesRead] = '\0';
							std::cout << "[DEBUG] Received " << bytesRead
									<< " bytes from fd " << fds[i].fd << ": " << buffer << std::endl;

							std::string response = "Hello from server!\n";
							send(fds[i].fd, response.c_str(), response.length(), 0);
						}
					}
				}
			}
		}
	}
}
