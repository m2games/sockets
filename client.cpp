#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

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

    const char request[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const int rc = send(sockfd, request, sizeof(request), 0);

    // @TODO(matiTechno): handle the case when not full data is sent
    if(rc == -1)
        perror("send() failed");

    // receive data
    else
    {
        char buffer[512];
        int rc = recv(sockfd, buffer, sizeof(buffer) - 1, 0);

        if(rc == -1)
        {
            perror("recv() failed");
        }
        else if(rc == 0)
        {
            printf("server has closed the connection\n");
        }
        else
        {
            buffer[rc] = '\0';
            printf("received msg:\n%s\n", buffer);
        }
    }

    printf("ending program with no critical errors\n");
    close(sockfd);
    return 0;
}
