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

template<typename T>
T min(T l, T r) {return l > r ? r : l;}

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
    if(cmd)
    {
        const char* cmdStr = getCmdStr(cmd);
        int len = strlen(cmdStr) + strlen(payload) + 2; // ' ' + '\0'
        int prevSize = buffer.size();
        buffer.resize(prevSize + len);
        assert(snprintf(buffer.data() + prevSize, len, "%s %s", cmdStr, payload) == len - 1);
    }
    // special case for http response
    else
    {
        int len = strlen(payload) + 1;
        int prevSize = buffer.size();
        buffer.resize(prevSize + len);
        memcpy(buffer.data() + prevSize, payload, len);
    }
}

enum class ClientStatus
{
    Waiting,
    Browser,
    Player,
    PlayerRename
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
        case ClientStatus::PlayerRename: return "PlayerRename";
    }
    assert(false);
}

static volatile int gExitLoop = false;
void sigHandler(int) {gExitLoop = true;}

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

        const int option = 1;
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1)
        {
            close(sockfd);
            perror("setsockopt() (SO_REUSEADDR) failed");
            return 0;
        }

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

    constexpr int maxClients = 10;
    FixedArray<Client, maxClients> clients;
    Array<char> sendBufs[maxClients];
    Array<char> recvBufs[maxClients];
    int recvBufsNumUsed[maxClients];

    for(int i = 0; i < maxClients; ++i)
    {
        sendBufs[i].reserve(500);
        recvBufs[i].resize(500);
    }

    double currentTime = getTimeSec();
    float timer = 0.f;

    // server loop
    // note: don't change the order of operations
    // (some logic is based on this)
    while(gExitLoop == false)
    {
        // update clients
        {
            const double newTime = getTimeSec();
            timer += newTime - currentTime;
            currentTime = newTime;

            if(timer > 5.f)
            {
                timer = 0.f;

                for(int i = 0; i < clients.size(); ++i)
                {
                    Client& client = clients[i];

                    if(client.alive == false)
                    {
                        printf("client '%s' (%s) will be removed (no PONG or init msg)\n",
                               client.name, getStatusStr(client.status));
                        client.remove = true;
                    }
                    else if(client.status != ClientStatus::Waiting)
                        addMsg(sendBufs[i], Cmd::Ping);

                    client.alive = false;
                }
            }
        }

        // handle new client
        if(clients.size() < clients.maxSize())
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
                const int option = 1;

                if(fcntl(clientSockfd, F_SETFL, O_NONBLOCK) == -1)
                {
                    close(clientSockfd);
                    perror("fcntl() on client failed");
                }
                else if(setsockopt(clientSockfd, IPPROTO_TCP, TCP_NODELAY, &option,
                                   sizeof(option)) == -1)
                {
                    close(clientSockfd);
                    perror("setsockopt() (TCP_NODELAY) on client failed");
                }
                else
                {
                    clients.pushBack(Client());
                    clients.back().sockfd = clientSockfd;
                    sendBufs[clients.size() - 1].clear();
                    recvBufsNumUsed[clients.size() - 1] = 0;

                    // print client ip
                    char ipStr[INET6_ADDRSTRLEN];
                    inet_ntop(clientAddr.ss_family, get_in_addr( (sockaddr*)&clientAddr ),
                              ipStr, sizeof(ipStr));
                    printf("accepted connection from %s\n", ipStr);
                }

            }
        }

        // receive
        for(int i = 0; i < clients.size(); ++i)
        {
            Array<char>& recvBuf = recvBufs[i];
            int& recvBufNumUsed = recvBufsNumUsed[i];
            Client& client = clients[i];

            while(true)
            {
                const int numFree = recvBuf.size() - recvBufNumUsed;
                const int rc = recv(client.sockfd, recvBuf.data() + recvBufNumUsed,
                                    numFree, 0);

                if(rc == -1)
                {
                    if(errno != EAGAIN || errno != EWOULDBLOCK)
                    {
                        perror("recv() failed");
                        client.remove = true;
                    }
                    break;
                }
                else if(rc == 0)
                {
                    printf("client has closed the connection\n");
                    client.remove = true;
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
                        printf("recvBuf big size issue, removing client: '%s' (%s)\n",
                               client.name, getStatusStr(client.status));
                        client.remove = true;
                        break;
                    }
                }
            }
        }

        // process received data
        for(int i = 0; i < clients.size(); ++i)
        {
            Array<char>& sendBuf = sendBufs[i];
            Array<char>& recvBuf = recvBufs[i];
            int& recvBufNumUsed = recvBufsNumUsed[i];
            Client& client = clients[i];

            const char* end = recvBuf.data();
            const char* begin;

            // special case for http
            if(recvBufNumUsed >= 3)
            {
                const char* const cmd = "GET";
                if(strncmp(cmd, recvBuf.data(), strlen(cmd)) == 0)
                {
                    client.status = ClientStatus::Browser;
                    addMsg(sendBuf, Cmd::_nil,
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/html\r\n\r\n"
                            "<!DOCTYPE html>"
                            "<html>"
                            "<body>"
                            "<h1>Welcome to the cavetiles server!</h1>"
                            "<p><a href=\"https://github.com/m2games\">company</a></p>"
                            "</body>"
                            "</html>");
                    continue;
                }
            }

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

                printf("'%s' (%s) received msg: '%s'\n", client.name,
                                                         getStatusStr(client.status), begin);

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
                        client.alive = true;
                        break;

                    case Cmd::Name:
                    {
                        bool ok = true;

                        for(const Client& other: clients)
                        {
                            if(other.status == ClientStatus::Player &&
                               strcmp(other.name, begin) == 0)
                            {
                                ok = false;
                                break;
                            }
                        }

                        if(ok)
                        {
                            client.status = ClientStatus::Player;
                            const int maxSize = sizeof(client.name);

                            memcpy(client.name, begin, min(maxSize, int(strlen(begin)) + 1));
                            client.name[maxSize - 1] = '\0';

                            for(int i = 0; i < clients.size(); ++i)
                            {
                                if(clients[i].status == ClientStatus::Player)
                                {
                                    char msg[64];
                                    snprintf(msg, sizeof(msg), "'%s' has joined the game!",
                                             client.name);

                                    addMsg(sendBufs[i], Cmd::Chat, msg);
                                }
                            }
                        }
                        else
                        {
                            client.status = ClientStatus::PlayerRename;
                            addMsg(sendBuf, Cmd::Name);
                        }

                        break;
                    }

                    case Cmd::Chat:
                        for(int i = 0; i < clients.size(); ++i)
                        {
                            if(clients[i].status == ClientStatus::Player)
                            {
                                char msg[512];
                                snprintf(msg, sizeof(msg), "%s: %s", client.name, begin);
                                addMsg(sendBufs[i], Cmd::Chat, msg);
                            }
                        }
                        break;
                }
            }

            const int numToFree = end - recvBuf.data();
            memmove(recvBuf.data(), recvBuf.data() + numToFree, recvBufNumUsed - numToFree);
            recvBufNumUsed -= numToFree;
        }

        // inform players if someone will leave the game
        for(const Client& client: clients)
        {
            if(client.remove && client.status == ClientStatus::Player)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "'%s' has left", client.name);

                for(int i = 0; i < clients.size(); ++i)
                {
                    if(clients[i].status == ClientStatus::Player && !clients[i].remove)
                    {
                        addMsg(sendBufs[i], Cmd::Chat, buf);
                    }
                }
            }
        }

        // send
        for(int i = 0; i < clients.size(); ++i)
        {
            if(clients[i].remove)
                continue;

            Array<char>& buf = sendBufs[i];
            if(buf.size())
            {
                const int rc = send(clients[i].sockfd, buf.data(), buf.size(), 0);

                if(rc == -1)
                {
                    perror("send() failed");
                    clients[i].remove = true;
                }
                else
                    buf.erase(0, rc);
            }
        }

        // remove some clients
        for(int i = 0; i < clients.size(); ++i)
        {
            Client& client = clients[i];
            if(client.remove || client.status == ClientStatus::Browser)
            {
                printf("removing client '%s' (%s)\n", client.name,
                       getStatusStr(client.status));

                close(client.sockfd);

                client = clients.back();

                const int lastIdx = clients.size() - 1;
                sendBufs[i].swap(sendBufs[lastIdx]);
                recvBufs[i].swap(recvBufs[lastIdx]);
                recvBufsNumUsed[i] = recvBufsNumUsed[lastIdx];

                clients.popBack();
                --i;
            }
        }

        // sleep for 10 ms
        usleep(10000);
    }
    
    for(Client& client: clients)
        close(client.sockfd);

    close(sockfd);
    printf("end of the main function\n");
    return 0;
}
