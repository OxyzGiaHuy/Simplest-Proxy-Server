#include <winsock2.h>
#include <ws2tcpip.h> // Add this header for inet_pton
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <array> // Add this header for std::array

#pragma comment(lib, "ws2_32.lib")

#define WM_SOCKET WM_USER + 1
#define DEFAULT_PORT "8888"
#define BUFFER_SIZE 4096

// Structure to store blacklist information
struct BlacklistEntry {
    std::string hostname;
    int port;
};

// Global variables
std::vector<std::string> blacklist;
std::mutex blacklist_mutex;
SOCKET serverSocket = INVALID_SOCKET;
// std::vector<BlacklistEntry> blacklist;
HWND hWndEdit, hWndList, hWndStart, hWndStop, hWndUrl,hwndHostRunning;
// std::mutex mtx;
std::condition_variable cv;
bool running = false; 


// Function to resolve hostname to IP address
bool resolve_hostname(const char* hostname, struct sockaddr_in& server) {
    struct addrinfo hints = { 0 }, * res = NULL;
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
        struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
        server.sin_addr = addr->sin_addr;
        char* ip_str = inet_ntoa(server.sin_addr);
        std::cout << "Resolved " << hostname << " to " << ip_str << std::endl;
        
        freeaddrinfo(res);
        return true;
    }
    else {
        std::cerr << "Failed to resolve hostname: " << hostname << std::endl;
        return false;
    }
}

// Function to check if a URL is in the blacklist
bool is_blacklisted(const std::string& hostname) {
    std::lock_guard<std::mutex> lock(blacklist_mutex); // Đảm bảo thread-safe
    for (const auto& blocked : blacklist) {
        if (hostname.find(blocked) != std::string::npos) {
            return true;
        }
    }
    return false;
}
void add_to_blacklist(const std::string& url) {
    std::lock_guard<std::mutex> lock(blacklist_mutex);
    blacklist.push_back(url);
    SendMessage(hWndList, LB_ADDSTRING, 0, (LPARAM)(url).c_str());
}

// Function to handle CONNECT requests
void handleConnectRequest(SOCKET client_socket, const std::string& request) {
    SendMessage(hWndEdit,EM_REPLACESEL, TRUE, (LPARAM)(std::string("Handling CONNECT request...").c_str()));

    // Extract hostname and port from CONNECT request
    char hostname[256] = { 0 };
    int port = 0;

    if (sscanf(request.c_str(), "CONNECT %255[^:]:%d", hostname, &port) != 2) {
        std::cerr << "Error parsing CONNECT request\n";
        send(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 26, 0);
        closesocket(client_socket);
        return;
    }
    std::cout << "CONNECT request to " << hostname << ":" << port << std::endl;

    // Check blacklist
    std::string url = std::string(hostname) + ":" + std::to_string(port);
    if (is_blacklisted(hostname)) {
        std::string response = "HTTP/1.1 403 Forbidden\r\n\r\n";
        send(client_socket, response.c_str(), response.length(), 0);
        closesocket(client_socket);
        return;
    }
    SendMessage(hwndHostRunning, LB_ADDSTRING, 0, (LPARAM)(hostname));
    // Establish connection to the target server
    struct sockaddr_in target_addr { 0 };
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    resolve_hostname(hostname, target_addr);

    SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(target_socket, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
        std::cerr << "Failed to connect to target: " << hostname << ":" << port << " Error: " << WSAGetLastError() << std::endl;
        send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
        closesocket(client_socket);
        return;
    }

    // Send connection established response to the client
    const char* connection_established = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_socket, connection_established, strlen(connection_established), 0);

    // Relay data between client and server (this will forward encrypted data for HTTPS)
    fd_set fdset;
    std::array<char, BUFFER_SIZE> relay_buffer;

    while (true) {
        FD_ZERO(&fdset);
        FD_SET(client_socket, &fdset);
        FD_SET(target_socket, &fdset);

        int activity = select(0, &fdset, NULL, NULL, NULL);
        if (activity <= 0) break;

        // Forward data from client to target
        if (FD_ISSET(client_socket, &fdset)) {
            int recv_size = recv(client_socket, relay_buffer.data(), relay_buffer.size(), 0);
            if (recv_size <= 0) break;
            send(target_socket, relay_buffer.data(), recv_size, 0);
        }

        // Forward data from target to client
        if (FD_ISSET(target_socket, &fdset)) {
            int recv_size = recv(target_socket, relay_buffer.data(), relay_buffer.size(), 0);
            if (recv_size <= 0) break;
            send(client_socket, relay_buffer.data(), recv_size, 0);
        }
    }

    // Close connections
    closesocket(target_socket);
    closesocket(client_socket);
    return;
}

// Function to handle HTTP requests
void handleHttpRequest(SOCKET client_socket, const std::string& request) {
    char method[10] = { 0 }, url[256] = { 0 }, protocol[10] = { 0 };
    if (sscanf(request.c_str(), "%9s %255s %9s", method, url, protocol) != 3) {
        std::cerr << "Error parsing HTTP request\n";
        send(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 26, 0);
        closesocket(client_socket);
        return;
    }

    std::cout << "HTTP " << method << " request for " << url << std::endl;
    char hostname[256] = { 0 };
    sscanf(url, "http://%255[^/]", hostname);

    // Check blacklist
    if (is_blacklisted(hostname)) {
        std::string response = "HTTP/1.1 403 Forbidden\r\n\r\n";
        send(client_socket, response.c_str(), response.length(), 0);
        closesocket(client_socket);
        return;
    }
    SendMessage(hwndHostRunning, LB_ADDSTRING, 0, (LPARAM)(hostname));
    struct sockaddr_in target_addr = { 0 };
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

    send(target_socket, request.c_str(), request.length(), 0);

    // Relay response back to client
    std::array<char, BUFFER_SIZE> buffer;
    int recv_size;
    while ((recv_size = recv(target_socket, buffer.data(), buffer.size(), 0)) > 0) {
        send(client_socket, buffer.data(), recv_size, 0);
    }

    closesocket(target_socket);
    closesocket(client_socket);
}

// Function to handle client connections
void handleClient(SOCKET clientSocket) {
    char buffer[BUFFER_SIZE];
    int recvSize = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
    if (recvSize == SOCKET_ERROR) {
        closesocket(clientSocket);
        return;
    }
    buffer[recvSize] = '\0';
    std::string request(buffer);

    // Log the request
    SendMessage(hWndEdit, EM_REPLACESEL, TRUE, (LPARAM)(std::string("Request from client: \r\n" + request + "\r\n").c_str()));

    if (request.find("CONNECT") == 0) {
        handleConnectRequest(clientSocket, request);
    }
    else if (request.find("GET") == 0 || request.find("POST") == 0) {
        handleHttpRequest(clientSocket, request);
    }

    closesocket(clientSocket);
}

// Thread function to listen for client connections
void listenForClients() {
    SOCKET clientSocket;
    struct sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);

    while (running) {
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (running) {
                std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
            }
            break;
        }

        // Create a new thread to handle the client
        std::thread clientThread(handleClient, clientSocket);
        clientThread.detach();
    }
}

// Function to add a URL to the blacklist
// void addBlacklistUrl(const std::string& url) {
//     std::string hostname, portStr;
//     int port;

//     // Extract hostname and port from URL
//     size_t pos = url.find(":");
//     if (pos != std::string::npos) {
//         hostname = url.substr(0, pos);
//         portStr = url.substr(pos + 1);
//         port = std::stoi(portStr);
//     }
//     else {
//         hostname = url;
//         port = 80; // Default port for HTTP
//     }

//     // Add to blacklist
//     blacklist.push_back({ hostname, port });

//     // Update listbox
//     SendMessage(hWndList, LB_ADDSTRING, 0, (LPARAM)(hostname + ":" + portStr).c_str());
// }

// Function to remove a URL from the blacklist
void removeBlacklistUrl(int index) {
    if (index >= 0 && index < blacklist.size()) {
        blacklist.erase(blacklist.begin() + index);
        SendMessage(hWndList, LB_DELETESTRING, index, 0);
    }
}

// Windows event handling function
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
         // Create controls
        // Log window (hWndEdit)
        hWndEdit = CreateWindow("EDIT",NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | WS_BORDER, 10, 10, 780, 200, hWnd, NULL, NULL, NULL);

        // Blacklist window (hWndList)
        hWndList = CreateWindow("LISTBOX", "Blacklist", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_BORDER| WS_CAPTION,10, 220, 360, 200, hWnd, (HMENU)3, NULL, NULL);

        // Host running window (hwndHostRunning)
        hwndHostRunning = CreateWindow("LISTBOX", "Host Running", WS_CHILD | WS_VISIBLE | WS_VSCROLL| LBS_NOTIFY | WS_BORDER| WS_CAPTION ,380, 220, 410, 200, hWnd, (HMENU)6, NULL, NULL);

        // Start button
        hWndStart = CreateWindow("BUTTON", "Start", 
            WS_CHILD | WS_VISIBLE, 
            10, 430, 100, 30, hWnd, (HMENU)1, NULL, NULL);

        // Stop button
        hWndStop = CreateWindow("BUTTON", "Stop", 
            WS_CHILD | WS_VISIBLE | WS_DISABLED,
            120, 430, 100, 30, hWnd, (HMENU)2, NULL, NULL);

        // Textbox for URL input
        hWndUrl = CreateWindowEx(0, "EDIT", "", 
            WS_CHILD | WS_VISIBLE | WS_BORDER, 
            240, 430, 360, 30, hWnd, (HMENU)5, NULL, NULL); 

        // Add URL button
        CreateWindowEx(0, "BUTTON", "Add URL", 
            WS_CHILD | WS_VISIBLE, 
            610, 430, 80, 30, hWnd, (HMENU)3, NULL, NULL);

        // Remove button
        CreateWindow("BUTTON", "Remove", 
            WS_CHILD | WS_VISIBLE, 
            700, 430, 80, 30, hWnd, (HMENU)4, NULL, NULL);
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case 1: // Start
            {
                // Initialize Winsock
                WSADATA wsaData;
                if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                    MessageBox(hWnd, "Failed to initialize Winsock", "Error", MB_OK | MB_ICONERROR);
                    return 1;
                }

                // Create socket
                serverSocket = socket(AF_INET, SOCK_STREAM, 0);
                if (serverSocket == INVALID_SOCKET) {
                    MessageBox(hWnd, "Failed to create socket", "Error", MB_OK | MB_ICONERROR);
                    WSACleanup();
                    return 1;
                }

                // Set up address and port
                struct addrinfo* result = NULL, * ptr = NULL, hints;
                ZeroMemory(&hints, sizeof(hints));
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;
                hints.ai_flags = AI_PASSIVE;

                if (getaddrinfo(NULL, DEFAULT_PORT, &hints, &result) != 0) {
                    MessageBox(hWnd, "getaddrinfo failed", "Error", MB_OK | MB_ICONERROR);
                    WSACleanup();
                    return 1;
                }

                // Bind socket
                if (bind(serverSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
                    MessageBox(hWnd, "bind failed", "Error", MB_OK | MB_ICONERROR);
                    freeaddrinfo(result);
                    closesocket(serverSocket);
                    WSACleanup();
                    return 1;
                }

                freeaddrinfo(result);

                // Listen for connections
                if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
                    MessageBox(hWnd, "listen failed", "Error", MB_OK | MB_ICONERROR);
                    closesocket(serverSocket);
                    WSACleanup();
                    return 1;
                }

                // Enable/disable Start/Stop buttons
                EnableWindow(hWndStart, FALSE);
                EnableWindow(hWndStop, TRUE);

                // Log the start event
                SendMessage(hWndEdit, EM_REPLACESEL, TRUE, (LPARAM)"Proxy server started.\r\n");

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

                // Log the stop event
                SendMessage(hWndEdit, EM_REPLACESEL, TRUE, (LPARAM)"Proxy server stopped.\r\n");
            }
            break;
        case 3: // Add URL
            {
                char url[256]; 
                GetWindowText(hWndUrl, url, 256);
                // Thêm URL vào blacklist
                add_to_blacklist(url);
                SetWindowText(hWndUrl, ""); 
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
    case WM_CHAR: { 
        if (LOWORD(wParam) == VK_RETURN) { // Check if Enter key is pressed
            char url[256]; 
            GetWindowText(hWndUrl, url, 256);
                // Thêm URL vào blacklist
                add_to_blacklist(url);
            SetWindowText(hWndUrl, ""); // Clear the textbox after adding URL
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register window class
    const char* CLASS_NAME = "ProxyApp";
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
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
    if (!RegisterClassEx(&wcex)) {
        MessageBox(NULL, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create window
    HWND hWnd = CreateWindow(CLASS_NAME, "Proxy App", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 830, 600, NULL, NULL, hInstance, NULL);
    if (!hWnd) {
        MessageBox(NULL, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}