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

    sockaddr_storage clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);
    const int clientSockfd = accept(sockfd, (sockaddr*)&clientAddr, &clientAddrSize);
    close(sockfd);

    if(clientSockfd == -1)
    {
        perror("accept() failed");
        return 0;
    }

    // print client name
    {
        char name[INET6_ADDRSTRLEN];
        inet_ntop(clientAddr.ss_family, get_in_addr( (sockaddr*)&clientAddr ), name,
                  sizeof(name));
        printf("accepted connection from %s\n", name);
    }

    // receive data
    char buffer[512];
    const int rc = recv(clientSockfd, buffer, sizeof(buffer) - 1, 0);

    if(rc == -1)
    {
        perror("recv() failed");
    }
    else if(rc == 0)
    {
        printf("client has closed the connection\n");
    }
    else
    {
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
        const int rc = send(clientSockfd, msg, sizeof(msg), 0);

        // @TODO(matiTechno): handle the case when not full data is sent
        if(rc == -1)
            perror("send() failed");
    }

    printf("ending program with no critical errors\n");
    close(clientSockfd);
    return 0;
}
