#include "./server/TCPListner.hpp"

int main(int ac, char **av)
{
    try {
        TCPListner listner(8125);
        listner.startServer();
		listner.runServer();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
