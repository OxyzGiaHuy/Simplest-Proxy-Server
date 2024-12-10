#include <iostream>
#include <string>
#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 8192

void open_in_browser(const std::string& fileName) {
    // Mở file HTML trong trình duyệt mặc định
    ShellExecute(0,"open", fileName.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void handle_client(SOCKET clientSocket) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    // Nhận URL từ client
    int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (bytesReceived <= 0) {
        std::cerr << "Failed to receive data from client.\n";
        closesocket(clientSocket);
        return;
    }

    std::string url(buffer);
    std::cout << "Received URL from client: " << url << std::endl;

    // Phân tích hostname từ URL
    std::string hostname = url;
    if (hostname.find("http://") == 0) {
        hostname = hostname.substr(7);  // Bỏ "http://"
    }
    size_t slashPos = hostname.find('/');
    std::string path = "/";
    if (slashPos != std::string::npos) {
        path = hostname.substr(slashPos);  // Đường dẫn
        hostname = hostname.substr(0, slashPos);  // Tên host
    }

    // Kết nối đến server mục tiêu
    SOCKET targetSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (targetSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket to target.\n";
        closesocket(clientSocket);
        return;
    }

    struct sockaddr_in targetAddr;
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(80);

    struct hostent* host = gethostbyname(hostname.c_str());
    if (!host) {
        std::cerr << "Failed to resolve hostname: " << hostname << "\n";
        closesocket(targetSocket);
        closesocket(clientSocket);
        return;
    }

    memcpy(&targetAddr.sin_addr, host->h_addr, host->h_length);

    if (connect(targetSocket, (struct sockaddr*)&targetAddr, sizeof(targetAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to target server.\n";
        closesocket(targetSocket);
        closesocket(clientSocket);
        return;
    }

    // Gửi HTTP request đến server mục tiêu
    std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + hostname + "\r\nConnection: close\r\n\r\n";
    send(targetSocket, request.c_str(), request.length(), 0);

    // Nhận phản hồi từ server mục tiêu
    std::ofstream outputFile("output.html");  // Lưu dữ liệu vào file
    while ((bytesReceived = recv(targetSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        outputFile.write(buffer, bytesReceived);
    }
    outputFile.close();

    // Mở file trong trình duyệt
    open_in_browser("output.html");

    // Đóng kết nối
    closesocket(targetSocket);
    closesocket(clientSocket);
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock.\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create server socket.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed.\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed.\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Proxy server is running on port " << PORT << "...\n";

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Failed to accept connection.\n";
            continue;
        }

        handle_client(clientSocket);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
