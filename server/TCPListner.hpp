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
#include "Client.hpp"
#include "Router.hpp"

class TCPListner {
	private:
		std::vector<ServerConfig> configs;
        std::map<int, ServerConfig> listeningSockets;
        std::vector<struct pollfd> fds;
		std::map<int, Client> clients;
		std::map<int, int> cgiToClient;
	public:
		TCPListner(const std::vector<ServerConfig>& configs);
		~TCPListner();

		void startServer();
		void runServer();
		void stopServer();
} ;
