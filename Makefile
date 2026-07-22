NAME        = webserv
CXX         = c++
CXXFLAGS    = -Wall -Wextra -Werror -std=c++98

SRCS        = main.cpp \
              config/Config.cpp \
              core/Server.cpp \
              core/EventLoop.cpp \
              core/Connection.cpp \
              http/HttpRequest.cpp \
              http/HttpResponse.cpp \
              http/Router.cpp \
              http/Cgi.cpp \
              utils/Logger.cpp \
              utils/MIME.cpp

OBJS        = $(SRCS:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(NAME) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
