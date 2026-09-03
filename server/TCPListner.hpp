#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/select.h>
#include <iostream>
#include <unistd.h>
// #include "parseConfig.hpp"
#include <vector>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <csignal>

class TCPListner {
	private:
		int serverSocket;
		int port;
		std::vector<struct pollfd> fds;
	public:
		TCPListner(int port);
		~TCPListner();

		void startServer();
		void runServer();
		void stopServer();
} ;
