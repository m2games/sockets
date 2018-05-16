all: 
	g++ -std=c++11 -Wall -Wextra -pedantic -g main.cpp -o socket
	g++ -std=c++11 -Wall -Wextra -pedantic -g client.cpp -o client
	g++ -std=c++11 -Wall -Wextra -pedantic -g server.cpp -o server
