#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/select.h>
#include <iostream>
#include <unistd.h>
#include "../config/Config.hpp"
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <sstream>

class TCPListner {
	private:
		std::vector<ServerConfig> configs;
        std::map<int, ServerConfig> listeningSockets;
        std::vector<struct pollfd> fds;
	public:
		TCPListner(const std::vector<ServerConfig>& configs);
		~TCPListner();

		void startServer();
		void runServer();
		void stopServer();
} ;
