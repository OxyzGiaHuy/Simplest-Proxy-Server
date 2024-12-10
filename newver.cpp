#include <iostream>
#include <string>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#pragma comment(lib, "Ws2_32.lib")

#define BUFFER_SIZE 8192
#define PORT 8080

void handle_http(SOCKET client_socket, const std::string& request) {
    size_t host_start = request.find("Host: ") + 6;
    size_t host_end = request.find("\r\n", host_start);

    if (host_start == std::string::npos || host_end == std::string::npos) {
        std::cerr << "Invalid HTTP request.\n";
        closesocket(client_socket);
        return;
    }

    std::string host = request.substr(host_start, host_end - host_start);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(80);

    hostent* remoteHost = gethostbyname(host.c_str());
    if (!remoteHost) {
        std::cerr << "Failed to resolve host: " << host << "\n";
        closesocket(client_socket);
        return;
    }
    std::memcpy(&serverAddr.sin_addr, remoteHost->h_addr, remoteHost->h_length);

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect(server_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to server.\n";
        closesocket(client_socket);
        return;
    }

    send(server_socket, request.c_str(), request.length(), 0);

    char buffer[BUFFER_SIZE];
    int bytes_received;
    while ((bytes_received = recv(server_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        send(client_socket, buffer, bytes_received, 0);
    }

    closesocket(server_socket);
    closesocket(client_socket);
}

void handle_https(SOCKET client_socket, const std::string& host, int port) {
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    hostent* remoteHost = gethostbyname(host.c_str());
    if (!remoteHost) {
        std::cerr << "Failed to resolve host: " << host << "\n";
        closesocket(client_socket);
        return;
    }
    std::memcpy(&serverAddr.sin_addr, remoteHost->h_addr, remoteHost->h_length);

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect(server_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to HTTPS server.\n";
        closesocket(client_socket);
        return;
    }
    std::string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_socket, response.c_str(), response.length(), 0);

    char buffer[BUFFER_SIZE];
    int bytes_received;

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);
        FD_SET(server_socket, &readfds);

        int activity = select(0, &readfds, nullptr, nullptr, nullptr);
        if (activity <= 0) break;

        if (FD_ISSET(client_socket, &readfds)) {
            bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (bytes_received <= 0) break;
            send(server_socket, buffer, bytes_received, 0);
        }

        if (FD_ISSET(server_socket, &readfds)) {
            bytes_received = recv(server_socket, buffer, BUFFER_SIZE, 0);
            if (bytes_received <= 0) break;
            send(client_socket, buffer, bytes_received, 0);
        }
    }

    closesocket(server_socket);
    closesocket(client_socket);
}

void handle_client(SOCKET client_socket) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    int received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (received <= 0) {
        closesocket(client_socket);
        return;
    }
    if (received <= 0) {
        std::cerr << "Failed to receive data from client.\n";
        closesocket(client_socket);
        return;
    }
    std::string url(buffer);
    std::cout << "Received URL from client: " << url << std::endl;

    std::string request(buffer);

    if (request.find("CONNECT") == 0) {
        size_t host_start = request.find(' ') + 1;
        size_t host_end = request.find(':', host_start);
        std::string host = request.substr(host_start, host_end - host_start);
        int port = std::stoi(request.substr(host_end + 1, request.find(' ', host_end) - host_end - 1));
        handle_https(client_socket, host, port);
    } else {
        handle_http(client_socket, request);
    }
    
}

int main() {
    WSADATA wsaData;
    SOCKET server_socket;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(server_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << "\n";
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << "\n";
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Proxy server running on port " << PORT << "...\n";

    while (true) {
        SOCKET client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << "\n";
            continue;
        }

        std::thread(handle_client, client_socket).detach();
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}
