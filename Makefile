all: 
	g++ -Wall -Wextra -pedantic -g main.cpp -o socket
	g++ -Wall -Wextra -pedantic -g client.cpp -o client
	g++ -Wall -Wextra -pedantic -g server.cpp -o server
