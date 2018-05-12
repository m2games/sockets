#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#include "Array.hpp"

const void* get_in_addr(const sockaddr* const sa)
{
    if(sa->sa_family == AF_INET)
        return &( ( (sockaddr_in*)sa )->sin_addr );

    return &( ( (sockaddr_in6*)sa )->sin6_addr );
}

static volatile int gExitLoop = false;

void sigHandler(int)
{
    gExitLoop = true;
}

enum class ClientStatus
{
    Waiting,
    Browser,
    Player
};

struct Client
{
    ClientStatus status = ClientStatus::Waiting;
    char name[20] = "dummy";
    int sockfd;
    bool remove = false;

    bool alive = true;
};

const char* getStatusStr(ClientStatus code)
{
    switch(code)
    {
        case ClientStatus::Waiting: return "Waiting";
        case ClientStatus::Browser: return "Browser";
        case ClientStatus::Player:  return "Player";
    }
    assert(false);
}

struct Msg
{
    int clientIdx;
    char buf[256];
};

int main()
{
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* list;
    {
        // this blocks
        const int ec = getaddrinfo(nullptr, "3000", &hints, &list);
        if(ec != 0)
        {
            printf("getaddrinfo() failed: %s\n", gai_strerror(ec));
            return 0;
        }
    }

    int sockfd;

    const addrinfo* it;
    for(it = list; it != nullptr; it = it->ai_next)
    {
        sockfd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(sockfd == -1)
        {
            perror("socket() failed");
            continue;
        }

        // reuse the port
        const int option = 1;
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1)
        {
            close(sockfd);
            perror("setsockopt() failed");
            return 0;
        }

        // set non-blocking
        if(fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1)
        {
            close(sockfd);
            perror("fcntl() failed");
            return 0;
        }

        if(bind(sockfd, it->ai_addr, it->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("bind() failed");
            continue;
        }

        break;
    }
    freeaddrinfo(list);

    if(it == nullptr)
    {
        printf("binding procedure failed\n");
        return 0;
    }

    if(listen(sockfd, 5) == -1)
    {
        perror("listen() failed");
        close(sockfd);
        return 0;
    }

    // server loop
    // note: don't change the order of operations
    // (some logic is based on this)
    float currentTime = 0.f;
    float timer = 0.f;
    FixedArray<Client, 10> clients;
    FixedArray<Msg, 50> msgQue;
    while(gExitLoop == false)
    {
        // 1) update clients
        {
            const float newTime = currentTime + 1.f; // calculate this
            const float dt = newTime - currentTime;
            currentTime = newTime;
            timer += dt;

            if(timer > 5.f)
            {
                timer = 0.f;

                for(int i = 0; i < clients.size(); ++i)
                {
                    Client& client = clients[i];
                    {
                        if(client.alive == false || client.status == ClientStatus::Waiting)
                            client.remove = true;
                        else
                        {
                            client.alive = false;
                            msgQue.pushBack(Msg{i, "PING"});
                        }
                    }
                }
            }
        }

        // 2) handle new client
        {
            sockaddr_storage clientAddr;
            socklen_t clientAddrSize = sizeof(clientAddr);
            const int clientSockfd = accept(sockfd, (sockaddr*)&clientAddr, &clientAddrSize);

            if(clientSockfd == -1)
            {
                if(errno != EAGAIN || errno != EWOULDBLOCK)
                {
                    perror("accept()");
                    break;
                }
            }
            else
            {
                // set non-blocking
                if(fcntl(clientSockfd, F_SETFL, O_NONBLOCK) == -1)
                {
                    close(clientSockfd);
                    perror("fcntl() on client failed");
                }
                else
                {
                    clients.pushBack(Client()); // secured by the assert() in pushBack()
                    clients.back().sockfd = clientSockfd;

                    // print client ip
                    char ipStr[INET6_ADDRSTRLEN];
                    inet_ntop(clientAddr.ss_family, get_in_addr( (sockaddr*)&clientAddr ),
                              ipStr, sizeof(ipStr));
                    printf("accepted connection from %s\n", ipStr);
                }

            }
        }

        // 3) receive data
        for(int i = 0; i < clients.size(); ++i)
        {
            Client& client = clients[i];
            // when set to 256 firefox has problems with the connection :D
            char buffer[512];
            const int rc = recv(client.sockfd, buffer, sizeof(buffer) - 1, 0);

            if(rc == -1)
            {
                if(errno != EAGAIN || errno != EWOULDBLOCK)
                {
                    client.remove = true;
                    perror("recv() failed");
                }
            }
            else if(rc == 0)
            {
                client.remove = true;
                printf("client has closed the connection\n");
            }
            else
            {
                // @TODO(matiTechno): what if msg is incomplete? (protocol)
                // or two packets have been joined together under the hood
                // (I don't know if it is possible)
                buffer[rc] = '\0';
                printf("received msg from '%s' (%s):\n%s\n", client.name,
                       getStatusStr(client.status), buffer);

                if(client.status == ClientStatus::Waiting)
                {
                    // this is terrible I guess :D
                    if(strncmp(buffer, "GET", 3) == 0)
                    {
                        client.status = ClientStatus::Browser;
                        // without this browser does not display html
                        client.remove = true;

                        msgQue.pushBack(Msg{i,
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html\r\n\r\n"
                        "<!DOCTYPE html>"
                        "<html>"
                        "<body>"
                        "<h1>Welcome to the cavetiles server!</h1>"
                        "<p><a href=\"https://github.com/m2games\">company</a></p>"
                        "</body>"
                        "</html>"});
                    }
                    else
                    {
                        // player name, name collisions, ...
                        client.status = ClientStatus::Player;
                        msgQue.pushBack(Msg{i, "Hello cavetiles player!"});
                    }
                }
                else // status == Player
                {
                    if(strncmp(buffer, "PONG", 4) == 0)
                        client.alive = true;

                    else if(strncmp(buffer, "PING", 4) == 0)
                        msgQue.pushBack(Msg{i, "PONG"});
                }
            }
        }

        // 4) send data
        for(Msg& msg: msgQue)
        {
            Client& client = clients[msg.clientIdx];
            // note: we send the entire buffer
            const int rc = send(client.sockfd, msg.buf, sizeof(msg.buf), 0);
            if(rc == -1)
            {
                client.remove = true;
                perror("send() failed");
            }
            else if(rc != sizeof(msg.buf))
            {
                // @TODO(matiTechno)
                printf("WARNING: only part of the msg has been sent!\n");
            }
        }
        msgQue.clear();

        // 5) remove some clients
        for(int i = 0; i < clients.size(); ++i)
        {
            Client& client = clients[i];
            if(client.remove)
            {
                printf("removing client '%s' (%s)\n", client.name,
                       getStatusStr(client.status));

                close(client.sockfd);
                client = clients.back();
                clients.popBack();
                --i;
            }
        }

        // 6) sleep for 10 ms
        usleep(10000);
    }
    
    for(Client& client: clients)
        close(client.sockfd);

    close(sockfd);
    printf("ending program with no critical errors\n");
    return 0;
}
