#include "proxy_server.h"
#include "blacklist.h"
#include "logging.h"
#include "utility.h"
#include <iostream>
#include <thread>
#include <array>
#include <set>
#include <mutex>

SOCKET serverSocket = INVALID_SOCKET;
bool running = false;
extern HWND hwndHostRunning;
std::set<std::string> active_hosts;
std::mutex active_hosts_mutex;

void addToActiveHosts(const std::string &hostname)
{
    std::lock_guard<std::mutex> lock(active_hosts_mutex);
    if (active_hosts.find(hostname) == active_hosts.end())
    {
        active_hosts.insert(hostname);
        SendMessageA(hwndHostRunning, LB_ADDSTRING, 0, (LPARAM)hostname.c_str());
    }
}

void removeFromActiveHosts(const std::string &hostname)
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
    // logMessage("Handling CONNECT request...");

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
    std::cout << "CONNECT request to " << hostname << ":" << port << std::endl;

    // Check blacklist
    if (isBlacklisted(hostname))
    {
        std::string response = "HTTP/1.1 403 Forbidden\r\n\r\n";
        send(client_socket, response.c_str(), response.length(), 0);
        closesocket(client_socket);
        return;
    }
    addToActiveHosts(hostname);

    // Establish connection to the target server
    struct sockaddr_in target_addr{0};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    resolveHostname(hostname, target_addr);

    SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(target_socket, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
    {
        std::cerr << "Failed to connect to target: " << hostname << ":" << port << " Error: " << WSAGetLastError() << std::endl;
        send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
        closesocket(client_socket);
        removeFromActiveHosts(hostname);
        return;
    }

    // Send connection established response to the client
    const char *connection_established = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_socket, connection_established, strlen(connection_established), 0);

    // Relay data between client and server (this will forward encrypted data for HTTPS)
    fd_set fdset;
    std::array<char, BUFFER_SIZE> relay_buffer;

    while (true)
    {
        FD_ZERO(&fdset);
        FD_SET(client_socket, &fdset);
        FD_SET(target_socket, &fdset);

        int activity = select(0, &fdset, NULL, NULL, NULL);
        if (activity <= 0)
            break;

        if (isBlacklisted(hostname))
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
    closesocket(target_socket);
    closesocket(client_socket);
    removeFromActiveHosts(hostname);
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
    if (isBlacklisted(hostname))
    {
        std::string response = "HTTP/1.1 403 Forbidden\r\n\r\n";
        send(client_socket, response.c_str(), response.length(), 0);
        closesocket(client_socket);
        return;
    }
    addToActiveHosts(hostname);
    struct sockaddr_in target_addr = {0};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    resolveHostname(hostname.c_str(), target_addr);

    SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(target_socket, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
    {
        std::cerr << "Failed to connect to HTTP target: " << hostname << std::endl;
        send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
        closesocket(client_socket);
        removeFromActiveHosts(hostname);
        return;
    }

    send(target_socket, request.c_str(), request.length(), 0);

    // Relay response back to client
    std::array<char, BUFFER_SIZE> buffer;
    int recv_size;
    while ((recv_size = recv(target_socket, buffer.data(), buffer.size(), 0)) > 0)
    {
        if (isBlacklisted(hostname))
            break;
        send(client_socket, buffer.data(), recv_size, 0);
    }

    closesocket(target_socket);
    closesocket(client_socket);
    removeFromActiveHosts(hostname);
}

// Function to handle client connections
void handleClientConnection(SOCKET clientSocket)
{
    char buffer[BUFFER_SIZE];
    struct sockaddr_in clientAddr = {};
    int clientAddrLen = sizeof(clientAddr);
    getpeername(clientSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
    int recvSize = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
    std::string s(clientIP);

    if (recvSize == SOCKET_ERROR)
    {
        closesocket(clientSocket);
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

    closesocket(clientSocket);
}

// Thread function to listen for client connections
void listenForClientConnections()
{
    SOCKET clientSocket;
    struct sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);

    while (running)
    {
        clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::string s(clientIP);
        if (clientSocket == INVALID_SOCKET)
        {
            if (running)
            {
                std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
            }
            break;
        }
        logMessage("Client connected: " + s + "\r\n");
        // Create a new thread to handle the client
        std::thread clientThread(handleClientConnection, clientSocket);
        clientThread.detach();
    }
}

// Function to resolve hostname to IP address
bool resolveHostname(const char *hostname, struct sockaddr_in &server)
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, NULL, &hints, &res) == 0)
    {
        struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
        server.sin_addr = addr->sin_addr;
        char *ip_str = inet_ntoa(server.sin_addr);
        std::string s(hostname);
        std::string t(ip_str);
        // logMessage(s + " --> " + t + "\n");
        logMessageToFile(s + " --> " + t + "\n");
        freeaddrinfo(res);
        return true;
    }
    else
    {
        std::string s(hostname);
        logMessage("Failed to resolve hostname: " + s + "\n");
        logMessageToFile("Failed to resolve hostname: " + s + "\n");
        return false;
    }
}