#include <iostream>
#include <winsock2.h>
#include <thread>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")
#define PORT 8888
#define BUFFER_SIZE 4096

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

void handle_client(SOCKET client_socket) {
    char buffer[BUFFER_SIZE];
    int recv_size;

    // Receive client request
    recv_size = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (recv_size == SOCKET_ERROR) {
        std::cerr << "Recv failed. Error Code: " << WSAGetLastError() << std::endl;
        closesocket(client_socket);
        return;
    }

    buffer[recv_size] = '\0'; // Null-terminate the request
    std::cout << "Request received:\n" << buffer << std::endl;

    // If CONNECT method, forward the request and establish a tunnel
    if (strncmp(buffer, "CONNECT", 7) == 0) {
        std::cerr << "Handling CONNECT request..." << std::endl;

        // Extract hostname and port from CONNECT request
        char hostname[256];
        int port;
        
        sscanf(buffer, "CONNECT %255[^:]:%d", hostname, &port);
        std::cout << "CONNECT request to " << hostname << ":" << port << std::endl;

        // Establish connection to the target server
        struct sockaddr_in target_addr{0};
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(port);
        resolve_hostname(hostname, target_addr);
        
        SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(target_socket, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
            std::cerr << "Failed to connect to target: " << hostname << ":" << port << " " <<  WSAGetLastError() << std::endl;
            send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
            closesocket(client_socket);
            return;
        }

        // Send connection established response to the client
        const char* connection_established = "HTTP/1.1 200 Connection Established\r\n\r\n";
        send(client_socket, connection_established, strlen(connection_established), 0);

        // Relay data between client and server (this will forward encrypted data for HTTPS)
        fd_set fdset;
        char relay_buffer[BUFFER_SIZE];
        int bytes_read;

        while (1) {
            FD_ZERO(&fdset);
            FD_SET(client_socket, &fdset);
            FD_SET(target_socket, &fdset);

            int activity = select(0, &fdset, NULL, NULL, NULL);
            if (activity <= 0) break;

            // Forward data from client to target
            if (FD_ISSET(client_socket, &fdset)) {
                bytes_read = recv(client_socket, relay_buffer, sizeof(relay_buffer), 0);
                if (bytes_read <= 0) break;
                send(target_socket, relay_buffer, bytes_read, 0);
            }

            // Forward data from target to client
            if (FD_ISSET(target_socket, &fdset)) {
                bytes_read = recv(target_socket, relay_buffer, sizeof(relay_buffer), 0);
                if (bytes_read <= 0) break;
                send(client_socket, relay_buffer, bytes_read, 0);
            }
        }

        // Close connections
        closesocket(target_socket);
        closesocket(client_socket);
        return;
    }

    // Handle HTTP traffic (e.g., GET requests)
    if (strncmp(buffer, "GET", 3) == 0 || strncmp(buffer, "POST", 4) == 0) {
        char method[10], url[256], protocol[10];
        sscanf(buffer, "%s %s %s", method, url, protocol);

        std::cout << "HTTP " << method << " request for " << url << std::endl;

        char hostname[256] = {0};
        sscanf(url, "http://%255[^/]", hostname);

        struct sockaddr_in target_addr = {0};
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(80);
        resolve_hostname(hostname, target_addr);

        SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(target_socket, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
            std::cerr << "Failed to connect to HTTP target: " << hostname << std::endl;
            send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
            closesocket(client_socket);
            return;
        }

        send(target_socket, buffer, recv_size, 0);

        // Relay response back to client
        while ((recv_size = recv(target_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            send(client_socket, buffer, recv_size, 0);
        }

        closesocket(target_socket);
    }

    closesocket(client_socket);
}



int main() {
    WSADATA wsa;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server, client;
    int c;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "Failed to initialize Winsock. Error Code: " << WSAGetLastError() << std::endl;
        return 1;
    }
    std::cout << "Winsock initialized.\n";

    // Create a socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Could not create socket. Error Code: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }
    std::cout << "Socket created.\n";

    // Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    // Bind the socket
    if (bind(server_socket, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        std::cerr << "Bind failed. Error Code: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    std::cout << "Bind done.\n";

    // Listen for incoming connections
    listen(server_socket, 3);
    std::cout << "Waiting for incoming connections...\n";

    // Accept and handle client connections
    c = sizeof(struct sockaddr_in);
    while ((client_socket = accept(server_socket, (struct sockaddr*)&client, &c)) != INVALID_SOCKET) {
        std::cout << "Connection accepted.\n";
        std::thread client_thread(handle_client, client_socket);
        client_thread.detach(); // Handle the client in a separate thread
    }

    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Accept failed. Error Code: " << WSAGetLastError() << std::endl;
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}