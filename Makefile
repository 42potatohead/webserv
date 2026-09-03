CC = c++
NAME = webserv
CFLAGS = -std=c++98
SRCS = main.cpp ./server/TCPListner.cpp
# ./server/Socket.cpp ./server/Request.cpp ./server/Response.cpp
OBJS = $(SRCS:.cpp=.o)
all: $(NAME)
$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $(NAME) $(OBJS)
%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -f $(OBJS)
fclean: clean
	rm -f $(NAME)
re: fclean all
.PHONY: all clean fclean re
