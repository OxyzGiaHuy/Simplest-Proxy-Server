
#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")


#define PORT 8888
#define BUFFER_SIZE 4096

int main() {
    // Khởi tạo Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
        return 1;
    }

    // Tạo socket cho client
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Định nghĩa địa chỉ của proxy server
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &(serverAddr.sin_addr)); // Lắng nghe trên tất cả các địa chỉ IP của máy
    serverAddr.sin_port = htons(PORT);       // Sử dụng cổng đã định nghĩa trước đó

    // Kết nối đến proxy server
    iResult = connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (iResult == SOCKET_ERROR) {
        std::cerr << "Connect failed: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
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
        return 1;
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
    return 0;
}
