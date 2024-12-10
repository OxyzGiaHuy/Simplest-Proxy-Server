
#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <cstring>
#include<vector>
#include<thread>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 4096


int create_server_socket() {
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return -1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return -1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return -1;
    }

    return server_socket;
}

SOCKET connect_to_target_server(const std::string &hostname, int port) {
    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        std::cerr << "getaddrinfo failed\n";
        return INVALID_SOCKET;
    }

    SOCKET target_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (target_socket == INVALID_SOCKET) {
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    if (connect(target_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Connect to target failed: " << WSAGetLastError() << std::endl;
        closesocket(target_socket);
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    freeaddrinfo(result);
    return target_socket;
}

void handle_client(SOCKET client_socket) {
    char buffer[BUFFER_SIZE];
    int received = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (received <= 0) {
        closesocket(client_socket);
        return;
    }

    buffer[received] = '\0';
    std::string hostname(buffer);

    std::cout << "Received request for hostname: " << hostname << std::endl;

    // Kết nối đến server đích
    SOCKET target_socket = connect_to_target_server(hostname, 80);
    if (target_socket == INVALID_SOCKET) {
        std::cerr << "Unable to connect to target server\n";
        closesocket(client_socket);
        return;
    }

    // Tạo một HTTP GET request để lấy dữ liệu
    std::string http_request = "GET / HTTP/1.1\r\nHost: " + hostname + "\r\nConnection: close\r\n\r\n";
    send(target_socket, http_request.c_str(), http_request.size(), 0);

    // Nhận phản hồi từ server đích và gửi lại cho client
    while ((received = recv(target_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        send(client_socket, buffer, received, 0);
    }

    closesocket(target_socket);
    closesocket(client_socket);
}

// Hàm để chạy proxy server
void runProxyServer() {
    // Thực hiện mã proxy server của bạn ở đây
    if (WSAStartup(MAKEWORD(2, 2), new WSADATA()) != 0) {
        std::cerr << "WSAStartup failed\n";
        exit(1);
    }

    SOCKET server_socket = create_server_socket();
    if (server_socket == INVALID_SOCKET) {
        exit(1);
    }

    std::cout << "Proxy server is running on port " << PORT << "...\n";

    while (true) {
        SOCKET client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        // Xử lý kết nối từ client
        handle_client(client_socket);
    }

    closesocket(server_socket);
    WSACleanup();
    std::cout << "Proxy server is running..." << std::endl;
    // proxy_server_code();
}

// Hàm để chạy client
void runClient() {
    // Thực hiện mã client của bạn ở đây
     // Khởi tạo Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
        exit(1);
    }

    // Tạo socket cho client
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
        WSACleanup();
        exit(1);
    }

    // Định nghĩa địa chỉ của proxy server
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);    // Sử dụng cổng đã định nghĩa trước đó
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    // Kết nối đến proxy server
    iResult = connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (iResult == SOCKET_ERROR) {
        std::cerr << "Connect failed: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        exit(1);
    }

    // Nhập tên miền từ người dùng
    std::string hostname;
    std::cout << "Input website: ";
    std::getline(std::cin, hostname);

    // Gửi tên miền đến proxy server
    iResult = send(clientSocket, hostname.c_str(), hostname.size(), 0);
    if (iResult == SOCKET_ERROR) {
        std::cerr << "Send failed: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        exit(1);
    }

    std::cout << "Sent require to proxy server." << std::endl;

    // Nhận phản hồi từ server và in ra màn hình
    char buffer[4096];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        std::cout << "Reply from server:\n" << buffer << std::endl;
    }

    // Đóng kết nối
    closesocket(clientSocket);
    WSACleanup();
    std::cout << "Client is running..." << std::endl;
    // client_code();
}

int main() {
    // Tạo các luồng cho proxy server và client
    std::thread proxyThread(runProxyServer);
    std::thread clientThread(runClient);

    // Đợi các luồng kết thúc
    proxyThread.join();
    clientThread.join();

    return 0;
}
