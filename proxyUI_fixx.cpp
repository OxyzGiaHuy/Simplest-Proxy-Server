#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <array>
#include <deque>
#include <queue>
#include <set>
#include <algorithm>
#include <sstream>
#include <regex>
#include <winuser.h>
#include <ctime>
#include <fstream>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

#define WM_SOCKET WM_USER + 1
#define DEFAULT_PORT "8888"
#define BUFFER_SIZE 4096


// Global variables
std::vector<std::string> blacklist;
std::mutex blacklist_mutex;
SOCKET serverSocket = INVALID_SOCKET;
HWND hWndEdit, hWndList, hWndStart, hWndStop, hWndUrl, hwndHostRunning, hWndClient;
HWND hWndUserGuide;
std::queue<std::string> List;
std::set<std::string> Clients;
std::vector<std::string> CurrentClients;
bool running = false;

const int MAX_LOG_ENTRIES = 30;
std::deque<std::string> log_entries;
std::mutex log_mutex;

std::set<std::string> active_hosts;
std::mutex active_hosts_mutex;

static std::atomic<int> activeConnections(0);



std::string getLogFileName() {
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);
    char buffer[20];+
    std::strftime(buffer, sizeof(buffer), "log-%d-%m-%Y.txt", localTime);

    return std::string(buffer);
}

void logMessageToFile(const std::string& message) {
    std::string logFileName = getLogFileName();

    std::fstream logFile(logFileName, std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open the log f  ile: " << logFileName << "\n";
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    char timeBuffer[20];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", localTime);
    {
    std::lock_guard<std::mutex> lock(log_mutex);
    logFile << "[" << timeBuffer << "] " << message << "\n";
    }

    logFile.close();
}

void logMessage(const std::string& message) {
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    char timeBuffer[20]; 
    std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S %d/%m/%Y", localTime);
    std::string currentTime(timeBuffer);
    std::string tmp;
    tmp =  "[" + currentTime + "] " + message + "\r\n";

    List.push(tmp);

    while (List.size() > MAX_LOG_ENTRIES)
    {
        List.pop(); 
    }

    std::string logContent;
    std::queue<std::string> tempQueue = List; 
    while (!tempQueue.empty())
    {
        logContent += tempQueue.front();
        tempQueue.pop();
    }

    SetWindowTextA(hWndEdit, logContent.c_str());

    int textLength = GetWindowTextLengthA(hWndEdit);
    SendMessageA(hWndEdit, EM_SETSEL, textLength, textLength);
    SendMessageA(hWndEdit, EM_SCROLLCARET, 0, 0);
}

void ClientBoxMessage()
{
    std::string content;
    for(auto client:Clients)
    {
        content += client + "\r\n";
    }
    SetWindowTextA(hWndClient, content.c_str());
    int textLength = GetWindowTextLengthA(hWndEdit);
    SendMessageA(hWndClient, EM_SETSEL, textLength, textLength);
    SendMessageA(hWndClient, EM_SCROLLCARET, 0, 0);
}

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

// Helper function to extract hostname and port from the "Host" header.
bool parseHostHeader(const std::string &request, std::string &hostname, int &port)
{
    std::string hostHeaderLower;
    std::string::size_type pos = request.find("host:");
    if (pos == std::string::npos)
    {
        pos = request.find("Host:");
        if (pos == std::string::npos)
        {
            return false;
        }
    }

    pos += 5; // Move past "Host:" or "host:"
    std::string::size_type endline = request.find("\r\n", pos);
    if (endline == std::string::npos)
    {
        return false;
    }

    hostHeaderLower = request.substr(pos, endline - pos);
    size_t start = hostHeaderLower.find_first_not_of(' ');
    if (start != std::string::npos)
    {
        hostHeaderLower = hostHeaderLower.substr(start);
    }

    size_t colonPos = hostHeaderLower.find(":");
    if (colonPos == std::string::npos)
    {
        hostname = hostHeaderLower;
        port = 80;
    }
    else
    {
        hostname = hostHeaderLower.substr(0, colonPos);
        try
        {
            port = std::stoi(hostHeaderLower.substr(colonPos + 1));
        }
        catch (const std::invalid_argument &e)
        {
            // Invalid port, use default
            port = 80;
        }
    }
    return true;
}
// Function to resolve hostname to IP address
bool resolve_hostname(const char *hostname, struct sockaddr_in &server)
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
        std:: string t(ip_str);
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

// Function to check if a URL is in the blacklist
bool is_blacklisted(const std::string &hostname)
{
    std::lock_guard<std::mutex> lock(blacklist_mutex); // Đảm bảo thread-safe
    for (const auto &blocked : blacklist)
    {
        if (hostname.find(blocked) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

void add_to_blacklist(const std::string &url)
{
    std::lock_guard<std::mutex> lock(blacklist_mutex);

    std::string hostname;
    int port = 443; // Default port if no port is specified

    // Regular expression to match protocol, hostname, and optional port.
    std::regex url_regex(R"(^(?:(http|https):\/\/)?([^:\/]+)(?::(\d+))?(\/.*)?$)");
    std::smatch url_match;

    
    if (std::regex_match(url, url_match, url_regex))
    {
        hostname = url_match[2].str();
    }
    else
    {
        hostname = url;
    }

    bool isDuplicate = false;
    for (const auto &blocked : blacklist)
    {
        if (hostname == blocked)
        {
            isDuplicate = true;
            break;
        }
    }

    // Avoid duplicates
    if (!isDuplicate)
    {
        blacklist.push_back(hostname);
        std::string url_string = hostname;
        SendMessageA(hWndList, LB_ADDSTRING, 0, (LPARAM)url_string.c_str());
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
    if (connect(target_socket, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
    {
        std::cerr << "Failed to connect to target: " << hostname << ":" << port << " Error: " << WSAGetLastError() << std::endl;
        send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
        closesocket(client_socket);
        removeFromHostRunning(hostname);
        return;
    }

    // Send connection established response to the client
    const char *connection_established = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_socket, connection_established, strlen(connection_established), 0);

    // Relay data between client and server (this will forward encrypted data for HTTPS)
    fd_set fdset;
    std::array<char, BUFFER_SIZE> relay_buffer;
    logMessage("Connecting to "+ s + "\n");
    while (true)
    {
        
        FD_ZERO(&fdset);
        FD_SET(client_socket, &fdset);
        FD_SET(target_socket, &fdset);

        int activity = select(0, &fdset, NULL, NULL, NULL);
        if (activity <= 0)
            break;

        if(is_blacklisted(hostname)) break;
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
    closesocket(target_socket);
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
    if (connect(target_socket, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
    {
        std::cerr << "Failed to connect to HTTP target: " << hostname << std::endl;
        send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
        closesocket(client_socket);
        removeFromHostRunning(hostname);
        return;
    }

    send(target_socket, request.c_str(), request.length(), 0);

    // Relay response back to client
    logMessage("Connecting to "+ hostname + "\n");
    std::array<char, BUFFER_SIZE> buffer;
    int recv_size;

    while ((recv_size = recv(target_socket, buffer.data(), buffer.size(), 0)) > 0)
    {
        if(is_blacklisted(hostname)) break;
        send(client_socket, buffer.data(), recv_size, 0);
    }

    logMessage("Disconnect to "+ hostname + "\n");
    closesocket(target_socket);
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
    if(!Clients.empty()) Clients.erase(clientIP);
    closesocket(clientSocket);
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
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::string s(clientIP);
        Clients.insert(s);
        bool exist = 0;
        for(auto client : CurrentClients)
        {
            if( s == client)
            {
                exist = 1;
                break;
            }
        }
        std::cout<< Clients.size() << " " << CurrentClients.size() << "\n";
        if(!exist) CurrentClients.push_back(s);
        if (clientSocket == INVALID_SOCKET)
        {
            if (running)
            {
                std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
            }
            break;
        }
        // logMessage("Client connected: "+ s + "\r\n");
        // // std::cout << "Client connected: " + s + "\n";
        // // Create a new thread to handle the client
        std::thread clientThread(handleClient, clientSocket);
        clientThread.detach();
        ClientBoxMessage();
    }
}

// Function to remove a URL from the blacklist

void removeBlacklistUrl(int index)
{
    if (index >= 0 && index < blacklist.size())
    {
        std::lock_guard<std::mutex> lock(blacklist_mutex);
        blacklist.erase(blacklist.begin() + index);
        SendMessage(hWndList, LB_DELETESTRING, index, 0);
    }
}

// Windows event handling function
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
        {
            // Create controls
            // Log window (hWndEdit)
            CreateWindowA("STATIC", "Proxy Log:", WS_VISIBLE | WS_CHILD, 10, 10, 570, 20, hWnd, NULL, NULL, NULL);
            hWndEdit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | WS_BORDER, 10, 40, 570, 175, hWnd, NULL, NULL, NULL);

            // Client window
            CreateWindowA("STATIC", "Client connecting:", WS_VISIBLE | WS_CHILD, 590, 10, 200, 20, hWnd, NULL, NULL, NULL);
            hWndClient = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | WS_BORDER, 590, 40, 200, 175, hWnd, NULL, NULL, NULL);

            // Blacklist window (hWndList)
            hWndList = CreateWindowA("LISTBOX", "Blacklist", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_BORDER | WS_CAPTION | WS_VSCROLL, 10, 220, 360, 200, hWnd, (HMENU)3, NULL, NULL);

            // Host running window (hwndHostRunning)
            hwndHostRunning = CreateWindowA("LISTBOX", "Host Running", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | WS_BORDER | WS_CAPTION, 380, 220, 410, 200, hWnd, (HMENU)6, NULL, NULL);

            // Start button
            hWndStart = CreateWindowA("BUTTON", "Start",
                                    WS_CHILD | WS_VISIBLE,
                                    10, 430, 100, 30, hWnd, (HMENU)1, NULL, NULL);

            // Stop button
            hWndStop = CreateWindowA("BUTTON", "Stop",
                                    WS_CHILD | WS_VISIBLE | WS_DISABLED,
                                    120, 430, 100, 30, hWnd, (HMENU)2, NULL, NULL);

            // Textbox for URL input
            hWndUrl = CreateWindowExA(0, "EDIT", "",
                                    WS_CHILD | WS_VISIBLE | WS_BORDER,
                                    240, 430, 360, 30, hWnd, (HMENU)5, NULL, NULL);

            // Add URL button
            CreateWindowExA(0, "BUTTON", "Add URL",
                            WS_CHILD | WS_VISIBLE,
                            610, 430, 80, 30, hWnd, (HMENU)3, NULL, NULL);

            // Remove button
            CreateWindowA("BUTTON", "Remove",
                        WS_CHILD | WS_VISIBLE,
                        700, 430, 80, 30, hWnd, (HMENU)4, NULL, NULL);
            
            // Guide for users
            hWndUserGuide = CreateWindowA("EDIT", 
            "User Guide:\r\n"
            "- To add a URL to the blacklist, enter it in the input box and click 'Add URL'.\r\n"
            "- To remove a URL, select it from the blacklist and click 'Remove'.\r\n"
            "- Use 'Start' to activate the proxy and 'Stop' to deactivate it.\r\n",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_BORDER, 
            10, 470, 780, 100, hWnd, NULL, NULL, NULL);
        break;
        }
        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case 1: // Start
            {
                // Initialize Winsock
                WSADATA wsaData;
                if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
                {
                    MessageBoxA(hWnd, "Failed to initialize Winsock", "Error", MB_OK | MB_ICONERROR);
                    return 1;
                }

                // Create socket
                serverSocket = socket(AF_INET, SOCK_STREAM, 0);
                if (serverSocket == INVALID_SOCKET)
                {
                    MessageBoxA(hWnd, "Failed to create socket", "Error", MB_OK | MB_ICONERROR);
                    WSACleanup();
                    return 1;
                }

                // Set up address and port
                struct addrinfo *result = NULL, *ptr = NULL, hints;
                ZeroMemory(&hints, sizeof(hints));
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;
                hints.ai_flags = AI_PASSIVE;

                if (getaddrinfo(NULL, DEFAULT_PORT, &hints, &result) != 0)
                {
                    MessageBoxA(hWnd, "getaddrinfo failed", "Error", MB_OK | MB_ICONERROR);
                    WSACleanup();
                    return 1;
                }

                // Bind socket
                if (bind(serverSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR)
                {
                    MessageBoxA(hWnd, "bind failed", "Error", MB_OK | MB_ICONERROR);
                    freeaddrinfo(result);
                    closesocket(serverSocket);
                    WSACleanup();
                    return 1;
                }

                freeaddrinfo(result);

                // Listen for connections
                if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
                {
                    MessageBoxA(hWnd, "listen failed", "Error", MB_OK | MB_ICONERROR);
                    closesocket(serverSocket);
                    WSACleanup();
                    return 1;
                }

                // Enable/disable Start/Stop buttons
                EnableWindow(hWndStart, FALSE);
                EnableWindow(hWndStop, TRUE);

                // Log the start event
                std::cout << "Proxy server started.\n";
                std::cout << "Proxy is running on port 8888\n";
                logMessage("Proxy server started.\r\n");

                // Create a thread to listen for clients
                running = true;
                std::thread listenThread(listenForClients);
                listenThread.detach();
            }
            break;
            case 2: // Stop
            {
                // Stop the proxy server
                running = false;
                closesocket(serverSocket);
                WSACleanup();

                // Enable/disable Start/Stop buttons
                EnableWindow(hWndStart, TRUE);
                EnableWindow(hWndStop, FALSE);
                while (!List.empty())
                {
                    List.pop();
                }
                
                // Log the stop event
                std::cout << "Proxy server stopped.\n";
                logMessage("Proxy server stopped.\r\n");
            }
            break;
            case 3: // Add URL
            {
                char url[256];
                GetWindowTextA(hWndUrl, url, 256);
                // Thêm URL vào blacklist
                if (strlen(url) > 0)
                {
                    add_to_blacklist(url);
                    SetWindowTextA(hWndUrl, "");
                }
            }
            break;
            case 4: // Remove
            {
                int index = SendMessage(hWndList, LB_GETCURSEL, 0, 0);
                removeBlacklistUrl(index);
            }
            break;
            }
            break;
        }
        case WM_CHAR:
        {
            if (LOWORD(wParam) == VK_RETURN)
            { // Check if Enter key is pressed
                char url[256];
                GetWindowTextA(hWndUrl, url, 256);
                // Thêm URL vào blacklist
                if (strlen(url) > 0)
                {
                    add_to_blacklist(url);
                    SetWindowTextA(hWndUrl, ""); // Clear the textbox after adding URL
                }
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Register window class
    const char *CLASS_NAME = "ProxyApp";
    WNDCLASSEXA wcex;
    wcex.cbSize = sizeof(WNDCLASSEXA);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassExA(&wcex))
    {
        MessageBoxA(NULL, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create window
    HWND hWnd = CreateWindowA(CLASS_NAME, "Proxy App", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 820, 630, NULL, NULL, hInstance, NULL);
    if (!hWnd)
    {
        MessageBoxA(NULL, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
