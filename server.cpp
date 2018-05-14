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
#include <time.h>
#include <netinet/tcp.h>

#include "Array.hpp"

double getTimeSec()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

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
            perror("setsockopt() (SO_REUSEADDR) failed");
            return 0;
        }

        if(setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &option, sizeof(option)) == -1)
        {
            close(sockfd);
            perror("setsockopt() (TCP_NODELAY) failed");
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
    double currentTime = getTimeSec();
    float timer = 0.f;
    FixedArray<Client, 10> clients;
    FixedArray<Msg, 50> msgQue;
    while(gExitLoop == false)
    {
        // 1) update clients
        {
            const double newTime = getTimeSec(); // calculate this
            timer += newTime - currentTime;
            currentTime = newTime;

            if(timer > 5.f)
            {
                timer = 0.f;

                for(int i = 0; i < clients.size(); ++i)
                {
                    Client& client = clients[i];
                    {
                        if(client.alive == false || client.status == ClientStatus::Waiting)
                        {
                            printf("client '%s' (%s) will be removed (no PONG or init msg)\n",
                                   client.name, getStatusStr(client.status));
                            client.remove = true;
                        }
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
            // this is a mess :D (a little bit)
            else
            {
                // @TODO(matiTechno): what if msg is incomplete? (protocol)
                // or two packets have been joined together under the hood
                // (I don't know if it is possible)
                // THIS IS CRITICAL
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
                        bool ok = true;
                        int nameBufSize = sizeof(client.name);
                        // shadowing
                        for(const Client& client: clients)
                        {
                            if(client.status != ClientStatus::Waiting &&
                               strncmp(buffer, client.name, nameBufSize - 1) == 0)
                            {
                                ok = false;
                                break;
                            }
                        }

                        if(ok)
                        {
                            client.status = ClientStatus::Player;
                            memcpy(client.name, buffer, nameBufSize - 1);
                            client.name[nameBufSize - 1] = '\0';
                            Msg msg;
                            snprintf(msg.buf, sizeof(msg.buf), "'%s' has joined the game!",
                                     client.name);

                            for(int j = 0; j < clients.size(); ++j)
                            {
                                if(clients[j].status == ClientStatus::Player)
                                {
                                    msg.clientIdx = j;
                                    msgQue.pushBack(msg);
                                }
                            }
                        }
                        else
                        {
                            // @TODO(matiTechno): client will be disconnected in the update
                            msgQue.pushBack(Msg{i, "NAME"});
                        }

                    }
                }
                else // status == Player
                {
                    if(strncmp(buffer, "PONG", 4) == 0)
                        client.alive = true;

                    else if(strncmp(buffer, "PING", 4) == 0)
                        msgQue.pushBack(Msg{i, "PONG"});

                    else if(strncmp(buffer, "CHAT", 4) == 0)
                    {
                        // forward msg to all players
                        // this might be not efficient
                        Msg msg;
                        // might truncate I guess
                        snprintf(msg.buf, sizeof(msg.buf), "%s: %s\n", client.name,
                                 buffer + 5);

                        for(int j = 0; j < clients.size(); ++j)
                        {
                            if(clients[i].status == ClientStatus::Player)
                            {
                                msg.clientIdx = j;
                                msgQue.pushBack(msg);
                            }
                        }
                    }
                }
            }
        }

        // 4) inform players if someone will leave the game
        for(const Client& client: clients)
        {
            if(client.remove && client.status == ClientStatus::Player)
            {
                Msg msg;
                snprintf(msg.buf, sizeof(msg), "'%s' has left", client.name);

                for(int i = 0; i < clients.size(); ++i)
                {
                    if(clients[i].status == ClientStatus::Player)
                    {
                        msg.clientIdx = i;
                        msgQue.pushBack(msg);
                    }
                }
            }
        }

        // 5) send data
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

        // 6) remove some clients
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

        // 7) sleep for 10 ms
        usleep(10000);
    }
    
    for(Client& client: clients)
        close(client.sockfd);

    close(sockfd);
    printf("ending program with no critical errors\n");
    return 0;
}
