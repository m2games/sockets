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

struct Msg
{
    char buf[256];
};

int main(int argc, const char* const * const argv)
{
    if(argc != 2)
    {
        printf("usage: client <name>\n");
        return 0;
    }

    signal(SIGINT, sigHandler);

    // @TODO(matiTechno): client should be able to reconnect - move all the connect code
    // to the main loop
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
        return 0;
    }

    // set non-blocking
    if(fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1)
    {
        close(sockfd);
        perror("fcntl() failed");
        return 0;
    }

    bool serverAlive = true;
    FixedArray<Msg, 10> sendQue;
    double currentTime = getTimeSec();
    float timerAlive = 0.f, timerSend = 0.f;

    // send the client name first
    sendQue.pushBack(Msg{});
    strncpy(sendQue.back().buf, argv[1], sizeof(sendQue.back().buf));

    //@TODO(matiTechno): never exit the loop on error (always try to reconnect)
    while(gExitLoop == false)
    {
        double newTime = getTimeSec();
        const float dt = newTime - currentTime;
        currentTime = newTime;
        timerAlive += dt;
        timerSend += dt;

        if(timerAlive > 5.f)
        {
            timerAlive = 0.f;

            if(serverAlive)
            {
                serverAlive = false;
                sendQue.pushBack(Msg{"PING"});
            }
            else
            {
                gExitLoop = true;
                printf("no PONG response from server\n");
            }
        }

        if(timerSend > 10.f)
        {
            timerSend = 0.f;
            sendQue.pushBack(Msg{"CHAT I send a random message every 10 s!"});
        }

        // receive
        {
            char buffer[512];

            // we need some protocol for this, see server.c receive() code for more notes
            int rc = recv(sockfd, buffer, sizeof(buffer) - 1, 0);

            if(rc == -1)
            {
                if(errno != EAGAIN || errno != EWOULDBLOCK)
                {
                    perror("recv() failed");
                    gExitLoop = true;
                }
            }
            else if(rc == 0)
            {
                printf("server has closed the connection\n");
                gExitLoop = true;
            }
            else
            {
                buffer[rc] = '\0';

                if(strncmp(buffer, "PING", 4) == 0)
                    sendQue.pushBack(Msg{"PONG"});

                else if(strncmp(buffer, "PONG", 4) == 0)
                    serverAlive = true;

                else if(strncmp(buffer, "NAME", 4) == 0)
                {
                    gExitLoop = true;
                    printf("name already in use, try something different\n");
                }

                else
                    printf("received msg:\n%s\n", buffer);
            }
        }

        // send
        // @TODO(matiTechno): handle the case when not full data is sent
        // note: we are sending the entire buffer (even if not used)
        {
            for(const Msg& msg: sendQue)
            {
                const int msgSize = sizeof(msg.buf);
                const int rc = send(sockfd, msg.buf, msgSize, 0);

                if(rc == -1)
                {
                    close(sockfd);
                    perror("send() failed");
                    return 0;
                }
                else if(rc != msgSize)
                {
                    printf("WARNING: only part of the msg has been sent!\n");
                }
            }
        }
        sendQue.clear();

        // sleep for 10 ms
        usleep(10000);
    }

    printf("ending program with no critical errors\n");
    close(sockfd);
    return 0;
}
