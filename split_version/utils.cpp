#include "utils.h"
#include <ws2tcpip.h>
#include <iostream>

void resolve_hostname(const char* hostname, struct sockaddr_in& server) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
        struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
        server.sin_addr = addr->sin_addr;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &server.sin_addr, ip_str, INET_ADDRSTRLEN);
        std::cout << "Resolved " << hostname << " to " << ip_str << std::endl;
        freeaddrinfo(res);
    } else {
        std::cerr << "Failed to resolve hostname: " << hostname << std::endl;
    }
}
