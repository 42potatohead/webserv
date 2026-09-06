#include "./server/TCPListner.hpp"
#include "./config/Config.hpp"

int main(int ac, char **av) {
    try {
        if (ac > 2) {
            throw std::runtime_error("Usage: ./webserv [config_file]");
        }
        
        const char* configPath = "config/default.conf";
        if (ac == 2) {
            configPath = av[1];
        }

        ConfigParser parser(configPath);
        std::vector<ServerConfig> configs = parser.parse();
        TCPListner server(configs);
        server.startServer();
        server.runServer();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

