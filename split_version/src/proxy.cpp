#include "../include/proxy.h"
#include "../include/config.h"
#include "../include/utils.h"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <array>
#include <queue>
#include <set>
#include <algorithm>
#include <sstream>
#include <regex>
#include <winuser.h>

void addToHostRunning(const std::string &hostname)
{
    std::lock_guard<std::mutex> lock(active_hosts_mutex);
    if (active_hosts.find(hostname) == active_hosts.end())
    {
        active_hosts.insert(hostname);
        SendMessageA(hwndHostRunning, LB_ADDSTRING, 0, (LPARAM)hostname.c_str());
    }
}

void removeFromHostRunning(const std::string &hostname)
{
    std::lock_guard<std::mutex> lock(active_hosts_mutex);
    active_hosts.erase(hostname);

    // Remove from listbox
    for (int i = 0; i < SendMessage(hwndHostRunning, LB_GETCOUNT, 0, 0); ++i)
    {
        char buffer[256];
        SendMessageA(hwndHostRunning, LB_GETTEXT, i, (LPARAM)buffer);
        if (std::string(buffer) == hostname)
        {
            SendMessage(hwndHostRunning, LB_DELETESTRING, i, 0);
            break;
        }
    }
}

// Function to handle CONNECT requests
void handleHttpsRequest(SOCKET client_socket, const std::string &request)
{
    // Extract hostname and port from CONNECT request
    char hostname[256] = {0};
    int port = 0;

    if (sscanf(request.c_str(), "CONNECT %255[^:]:%d", hostname, &port) != 2)
    {
        std::cerr << "Error parsing CONNECT request\n";
        send(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 26, 0);
        closesocket(client_socket);
        return;
    }

    // Check blacklist
    if (is_blacklisted(hostname))
    {
        std::string response = "HTTP/1.1 403 Forbidden\r\n\r\n";
        send(client_socket, response.c_str(), response.length(), 0);
        closesocket(client_socket);
        return;
    }
    addToHostRunning(hostname);

    // Establish connection to the target server
    struct sockaddr_in target_addr{0};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    resolve_hostname(hostname, target_addr);
    std::string s(hostname);

    SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (target_socket == INVALID_SOCKET)
    {
        std::cerr << "Failed to create target socket\n";
        send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
        closesocket(client_socket);
        removeFromHostRunning(hostname);
        return;
    }
    if (connect(target_socket, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
    {
        std::cerr << "Failed to connect to target: " << hostname << ":" << port << " Error: " << WSAGetLastError() << std::endl;
        send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
        closesocket(client_socket);
        if (target_socket != INVALID_SOCKET)
        {
            closesocket(target_socket);
        }
        removeFromHostRunning(hostname);
        return;
    }

    // Send connection established response to the client
    const char *connection_established = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_socket, connection_established, strlen(connection_established), 0);

    // Relay data between client and server (this will forward encrypted data for HTTPS)
    fd_set fdset;
    std::array<char, BUFFER_SIZE> relay_buffer;
    logMessage("Connecting to " + s + "\n");
    while (running)
    {

        FD_ZERO(&fdset);
        FD_SET(client_socket, &fdset);
        FD_SET(target_socket, &fdset);

        int activity = select(0, &fdset, NULL, NULL, NULL);
        if (activity <= 0)
            break;

        if (is_blacklisted(hostname))
            break;
        // Forward data from client to target
        if (FD_ISSET(client_socket, &fdset))
        {
            int recv_size = recv(client_socket, relay_buffer.data(), relay_buffer.size(), 0);
            if (recv_size <= 0)
                break;
            send(target_socket, relay_buffer.data(), recv_size, 0);
        }

        // Forward data from target to client
        if (FD_ISSET(target_socket, &fdset))
        {
            int recv_size = recv(target_socket, relay_buffer.data(), relay_buffer.size(), 0);
            if (recv_size <= 0)
                break;
            send(client_socket, relay_buffer.data(), recv_size, 0);
        }
    }

    // Close connections
    logMessage("Disconnect to " + s + "\n");
    if (target_socket != INVALID_SOCKET)
    {
        closesocket(target_socket);
    }

    closesocket(client_socket);
    removeFromHostRunning(hostname);
    return;
}

// Function to handle HTTP requests
void handleHttpRequest(SOCKET client_socket, const std::string &request)
{
    char method[10] = {0}, url[256] = {0}, protocol[10] = {0};
    if (sscanf(request.c_str(), "%9s %255s %9s", method, url, protocol) != 3)
    {
        std::cerr << "Error parsing HTTP request\n";
        send(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 26, 0);
        closesocket(client_socket);
        return;
    }

    std::string hostname;
    int port;

    if (!parseHostHeader(request, hostname, port))
    {
        std::cerr << "Could not find Host header\r\n";
        send(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 26, 0);
        closesocket(client_socket);
        return;
    }

    // Check blacklist
    if (is_blacklisted(hostname))
    {
        std::string response = "HTTP/1.1 403 Forbidden\r\n\r\n";
        send(client_socket, response.c_str(), response.length(), 0);
        closesocket(client_socket);
        return;
    }
    addToHostRunning(hostname);
    struct sockaddr_in target_addr = {0};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    resolve_hostname(hostname.c_str(), target_addr);

    SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (target_socket == INVALID_SOCKET)
    {
        std::cerr << "Failed to create target socket\n";
        send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
        closesocket(client_socket);
        removeFromHostRunning(hostname);
        return;
    }
    if (connect(target_socket, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
    {
        std::cerr << "Failed to connect to HTTP target: " << hostname << std::endl;
        send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
        closesocket(client_socket);
        if (target_socket != INVALID_SOCKET)
        {
            closesocket(target_socket);
        }
        removeFromHostRunning(hostname);
        return;
    }

    send(target_socket, request.c_str(), request.length(), 0);

    // Relay response back to client
    logMessage("Connecting to " + hostname + "\n");
    std::array<char, BUFFER_SIZE> buffer;
    int recv_size;

    while (running && (recv_size = recv(target_socket, buffer.data(), buffer.size(), 0)) > 0)
    {
        if (is_blacklisted(hostname))
            break;
        send(client_socket, buffer.data(), recv_size, 0);
    }

    logMessage("Disconnect to " + hostname + "\n");
    if (target_socket != INVALID_SOCKET)
    {
        closesocket(target_socket);
    }
    closesocket(client_socket);
    removeFromHostRunning(hostname);
}

// Function to handle client connections
void handleClient(SOCKET clientSocket)
{
    char buffer[BUFFER_SIZE];
    struct sockaddr_in clientAddr = {};
    int clientAddrLen = sizeof(clientAddr);
    getpeername(clientSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
    int recvSize = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
    std::string s(clientIP);

    if (recvSize == SOCKET_ERROR || !running)
    {
        if (clientSocket != INVALID_SOCKET)
        {
            closesocket(clientSocket);
        }
        return;
    }
    buffer[recvSize] = '\0';
    std::string request(buffer);

    // Log the request
    logMessage(std::string("Request from client: " + s + "\r\n" + request + "\r\n"));

    if (request.find("CONNECT") == 0)
    {
        handleHttpsRequest(clientSocket, request);
    }
    else if (request.find("GET") == 0 || request.find("POST") == 0)
    {
        handleHttpRequest(clientSocket, request);
    }

    if (clientSocket != INVALID_SOCKET)
    {
        closesocket(clientSocket);
    }
    if (!Clients.empty())
        Clients.erase(clientIP);
}

// Thread function to listen for client connections
void listenForClients()
{
    SOCKET clientSocket;
    struct sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);

    while (running)
    {
        clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);

        if (!running)
        {
            if (clientSocket != INVALID_SOCKET)
            {
                closesocket(clientSocket);
            }
            break;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::string s(clientIP);
        Clients.insert(s);
        bool exist = 0;
        for (auto client : CurrentClients)
        {
            if (s == client)
            {
                exist = 1;
                break;
            }
        }
        std::cout << Clients.size() << " " << CurrentClients.size() << "\n";
        if (!exist)
            CurrentClients.push_back(s);
        if (clientSocket == INVALID_SOCKET)
        {
            if (running)
            {
                std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
            }

            continue;
        }
        // logMessage("Client connected: "+ s + "\r\n");
        // // std::cout << "Client connected: " + s + "\n";
        // // Create a new thread to handle the client
        std::thread clientThread(handleClient, clientSocket);
        clientThread.detach();
        ClientBoxMessage();
    }
}