// sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h> // close
//
#include <stdio.h>

struct Address
{
    void setIp(unsigned char a, unsigned char b, unsigned char c, unsigned char d)
    {
        ip = (a << 24) | (b << 16) | (c << 8) | d;
    }

    unsigned int ip;
    unsigned short port;
};

struct Socket
{
    bool open(unsigned short port = 0)
    {
        const int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        
        if(handle <= 0)
        {
            printf("failed to create socket\n");
            return false;
        }
        
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if(bind(handle, (const sockaddr*)&addr, sizeof(sockaddr_in)) < 0)
        {
            printf("failed to bind socket\n");
            close();
            return false;
        }

        const int nonBlocking = 1;

        if(fcntl(handle, F_SETFL, O_NONBLOCK, nonBlocking) == -1)
        {
            printf("failed to set non-blocking\n");
            close();
            return false;
        }

        return true;
    }

    void close() {::close(handle);}

    bool send(const Address& destination, const void* data, int size)
    {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(destination.ip);
        addr.sin_port = htons(destination.port);

        const int sentBytes = sendto(handle, (const char*)data, size, 0, (sockaddr*)&addr,
                                     sizeof(sockaddr_in));

        if(sentBytes != size)
        {
            printf("failed to send packet\n");
            return false;
        }

        return true;
    }

    int receive(Address& sender, void* data, int size)
    {
        sockaddr_in addr;
        socklen_t length = sizeof(addr);
        const int bytes = recvfrom(handle, (char*)data, size, 0, (sockaddr*)&addr, &length);

        if(bytes <= 0)
            return 0;

        else if(bytes > size)
        {
            printf("socket - ignoring packet with size greater than max buffer size");
            return 0;
        }
        
        sender.ip = ntohl(addr.sin_addr.s_addr);
        sender.port = ntohs(addr.sin_port);
        return bytes;
    }

    int handle;
};

int main()
{
    const int port = 30000;

    Socket socket;
    if(!socket.open(port))
    {
        return 0;
    }

    // send packet
    {
        Address addr;
        addr.setIp(127, 0, 0, 1);
        addr.port = port;
        char data[] = {"test!testkwrj;ekrjegjl;gjew;lkfjewfrk;le"};
        socket.send(addr, data, sizeof(data)); 
    }

    while(true)
    {
        Address sender;
        unsigned char buffer[256];
        const int bytesRead = socket.receive(sender, buffer, sizeof(buffer));

        if(!bytesRead)
            continue;

        printf("\nserver received data (port %d)!: ", sender.port);
        fwrite(buffer, bytesRead, 1, stdout);
        break;
    }

    socket.close();
    return 0;
}
