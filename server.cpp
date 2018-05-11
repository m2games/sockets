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

const void* get_in_addr(const sockaddr* const sa)
{
    if(sa->sa_family == AF_INET)
        return &( ( (sockaddr_in*)sa )->sin_addr );

    return &( ( (sockaddr_in6*)sa )->sin6_addr );
}

int main()
{
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
    // @TODO(matiTechno): Client structure, keepalive, research select()
    int clients[10];
    int numClients = 0;
    while(true)
    {
        // handle new client
        {
            sockaddr_storage clientAddr;
            socklen_t clientAddrSize = sizeof(clientAddr);
            const int client = accept(sockfd, (sockaddr*)&clientAddr, &clientAddrSize);

            if(client == -1)
            {
                if(errno != EAGAIN || errno != EWOULDBLOCK)
                    break;
            }
            else
            {
                // set non-blocking
                if(fcntl(client, F_SETFL, O_NONBLOCK) == -1)
                {
                    close(client);
                    perror("fcntl() on client failed");
                }
                else
                {
                    // replace with getSize()
                    assert(numClients < int(sizeof(clients) / sizeof(int))); 
                    clients[numClients] = client;
                    ++numClients;

                    // print client ip
                    char name[INET6_ADDRSTRLEN];
                    inet_ntop(clientAddr.ss_family, get_in_addr( (sockaddr*)&clientAddr ),
                              name, sizeof(name));
                    printf("accepted connection from %s\n", name);
                }

            }
        }

        // receive and send data
        for(int i = 0; i < numClients; ++i)
        {
            bool remove = false;
            int client = clients[i];
            char buffer[512];
            const int rc = recv(client, buffer, sizeof(buffer) - 1, 0);

            if(rc == -1)
            {
                if(errno != EAGAIN || errno != EWOULDBLOCK)
                {
                    remove = true;
                    perror("recv() failed");
                }
            }
            else if(rc == 0)
            {
                remove = true;
                printf("client has closed the connection\n");
            }
            else
            {
                // @TODO(matiTechno): what if msg is incomplete? (protocol)
                buffer[rc] = '\0';
                printf("received msg:\n%s\n", buffer);

                // send html page

                const char msg[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n\r\n"

                "<!DOCTYPE html>"
                "<html>"
                "<body>"
                "<h1>Welcome to the cavetiles server!</h1>"
                "<p><a href=\"https://github.com/m2games\">company</a></p>"
                "</body>"
                "</html>";

                // shadowing
                const int rc = send(client, msg, sizeof(msg), 0);

                if(rc == -1)
                {
                    remove = true;
                    perror("send() failed");
                }
                else if(rc != sizeof(msg))
                {
                    // @TODO(matiTechno)
                    printf("WARNING: only part of the msg has been sent!\n");
                }

                // without this browser does not display html
                remove = true;
            }

            if(remove)
            {
                printf("removing client\n");
                close(client);
                clients[i] = clients[numClients - 1];
                --numClients;
                --i;
            }
        }

        // sleep for some time
        sleep(1);
    }
    
    for(int i = 0; i < numClients; ++i)
        close(clients[i]);

    close(sockfd);
    printf("ending program with no critical errors\n");
    return 0;
}
