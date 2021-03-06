#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
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

struct Cmd
{
    enum
    {
        _nil,
        Ping,
        Pong,
        Name,
        Chat,
        _count
    };
};

const char* getCmdStr(int cmd)
{
    switch(cmd)
    {
        case Cmd::Ping: return "PING";
        case Cmd::Pong: return "PONG";
        case Cmd::Name: return "NAME";
        case Cmd::Chat: return "CHAT";
    }
    assert(false);
}

void addMsg(Array<char>& buffer, int cmd, const char* payload = "")
{
    const char* cmdStr = getCmdStr(cmd);
    int len = strlen(cmdStr) + strlen(payload) + 2; // ' ' + '\0'
    int prevSize = buffer.size();
    buffer.resize(prevSize + len);
    assert(snprintf(buffer.data() + prevSize, len, "%s %s", cmdStr, payload) == len - 1);
}

static volatile int gExitLoop = false;
void sigHandler(int) {gExitLoop = true;}

// returns socket descriptior, -1 if failed
// if succeeded you have to free the socket yourself
int connect()
{
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* list;
    {
        // this blocks
        const int ec = getaddrinfo("localhost", "3000", &hints, &list);
        if(ec != 0)
        {
            printf("getaddrinfo() failed: %s\n", gai_strerror(ec));
            return -1;
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

        if(connect(sockfd, it->ai_addr, it->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("connect() failed");
            continue;
        }

        char name[INET6_ADDRSTRLEN];
        inet_ntop(it->ai_family, get_in_addr(it->ai_addr), name, sizeof(name));
        printf("connected to %s\n", name);
        break;
    }
    freeaddrinfo(list);

    if(it == nullptr)
    {
        printf("connection procedure failed\n");
        return -1;
    }

    if(fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1)
    {
        close(sockfd);
        perror("fcntl() failed");
        return -1;
    }

    {
        const int option = 1;
        if(setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &option, sizeof(option)) == -1)
        {
            close(sockfd);
            perror("setsockopt() (TCP_NODELAY) failed");
            return -1;
        }
    }

    return sockfd;
}

int main(int argc, const char* const * const argv)
{
    if(argc != 2)
    {
        printf("usage: client <name>\n");
        return 0;
    }

    signal(SIGINT, sigHandler);

    // remove code duplication between client.cpp and server.cpp
    // change the delimiter to \r\n ?

    Array<char> sendBuf, recvBuf;
    int recvBufNumUsed = 0;
    sendBuf.reserve(500);
    recvBuf.resize(500);
    bool serverAlive;
    double currentTime = getTimeSec();
    const float timerAliveMax = 5.f;
    const float timerReconnectMax = 5.f;
    float timerAlive, timerSend, timerReconnect = timerReconnectMax;
    bool hasToReconnect = true;
    int sockfd = -1;

    while(gExitLoop == false)
    {
        // time managment
        {
            double newTime = getTimeSec();
            const float dt = newTime - currentTime;
            currentTime = newTime;
            timerAlive += dt;
            timerSend += dt;
            timerReconnect += dt;
        }

        if(hasToReconnect)
        {
            if(timerReconnect >= timerReconnectMax)
            {
                timerReconnect = 0.f;

                if(sockfd != -1)
                    close(sockfd);

                sockfd = connect();

                if(sockfd != -1)
                {
                    serverAlive = true;
                    timerAlive = timerAliveMax;
                    timerSend = 5.f;
                    hasToReconnect = false;
                    sendBuf.clear();

                    // send the player name
                    {
                        char name[20];
                        int maxNameSize = sizeof(name) - 1;

                        if(int(strlen(argv[1])) > maxNameSize)
                        {
                            printf("WARNING: max player name size is %d, truncating\n",
                                    maxNameSize);
                        }

                        snprintf(name, sizeof(name), "%s", argv[1]);
                        addMsg(sendBuf, Cmd::Name, name);
                    }
                }
            }
        }

        // update
        if(!hasToReconnect)
        {
            if(timerAlive > timerAliveMax)
            {
                timerAlive = 0.f;

                if(serverAlive)
                {
                    serverAlive = false;
                    addMsg(sendBuf, Cmd::Ping);
                }
                else
                {
                    hasToReconnect = true;
                    printf("no PONG response from server, will try to reconnect\n");
                }
            }

            if(timerSend > 10.f)
            {
                timerSend = 0.f;
                addMsg(sendBuf, Cmd::Chat, "I send a random message every 10s!");
            }
        }

        // receive
        if(!hasToReconnect)
        {
            while(true)
            {
                const int numFree = recvBuf.size() - recvBufNumUsed;
                const int rc = recv(sockfd, recvBuf.data() + recvBufNumUsed, numFree, 0);

                if(rc == -1)
                {
                    if(errno != EAGAIN || errno != EWOULDBLOCK)
                    {
                        perror("recv() failed");
                        hasToReconnect = true;
                    }
                    break;
                }
                else if(rc == 0)
                {
                    printf("server has closed the connection\n");
                    hasToReconnect = true;
                    break;
                }
                else
                {
                    recvBufNumUsed += rc;

                    if(recvBufNumUsed < recvBuf.size())
                        break;

                    recvBuf.resize(recvBuf.size() * 2);
                    if(recvBuf.size() > 10000)
                    {
                        printf("recvBuf big size issue, exiting\n");
                        gExitLoop = true;
                        break;
                    }
                }
            }
        }

        // process received data
        {
            const char* end = recvBuf.data();
            const char* begin;

            while(true)
            {
                begin = end;
                {
                    const char* tmp = (const char*)memchr((const void*)end, '\0',
                                       recvBuf.data() + recvBufNumUsed - end);

                    if(tmp == nullptr) break;
                    end = tmp;
                }

                ++end;

                //printf("received msg: '%s'\n", begin);

                int cmd = 0;
                for(int i = 1; i < Cmd::_count; ++i)
                {
                    const char* const cmdStr = getCmdStr(i);
                    const int cmdLen = strlen(cmdStr);

                    if(cmdLen > int(strlen(begin)))
                        continue;

                    if(strncmp(begin, cmdStr, cmdLen) == 0)
                    {
                        cmd = i;
                        begin += cmdLen + 1; // ' '
                        break;
                    }
                }

                switch(cmd)
                {
                    case 0:
                        printf("WARNING unknown command received: '%s'\n", begin);
                        break;

                    case Cmd::Ping:
                        addMsg(sendBuf, Cmd::Pong);
                        break;

                    case Cmd::Pong:
                        serverAlive = true;
                        break;

                    case Cmd::Name:
                        gExitLoop = true;
                        printf("name already in use, try something different\n");
                        break;

                    case Cmd::Chat:
                        printf("%s\n", begin);
                        break;
                }
            }

            const int numToFree = end - recvBuf.data();
            memmove(recvBuf.data(), recvBuf.data() + numToFree, recvBufNumUsed - numToFree);
            recvBufNumUsed -= numToFree;
        }

        // send
        if(!hasToReconnect && sendBuf.size())
        {
            const int rc = send(sockfd, sendBuf.data(), sendBuf.size(), 0);

            if(rc == -1)
            {
                perror("send() failed");
                hasToReconnect = true;
            }
            else
                sendBuf.erase(0, rc);
        }
        
        // sleep for 10 ms
        usleep(10000);
    }

    if(sockfd != -1)
        close(sockfd);

    printf("end of the main function\n");
    return 0;
}
