#include <iostream>
#include <winsock2.h>
#include <thread>
#include <ws2tcpip.h>
#include <unordered_set>
#include <mutex>
#include <string>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 4096

// Global Variables
HWND hLogWindow;
HWND hAddBlacklistEdit;
HWND hRemoveBlacklistEdit;
HWND hAddBlacklistBtn;
HWND hRemoveBlacklistBtn;
HWND hStartStopBtn;

bool serverRunning = false;
std::thread serverThread;

std::unordered_set<std::string> blacklist;
std::mutex blacklist_mutex;

// Helper Functions
void appendLog(const char* text) {
    int textLen = GetWindowTextLength(hLogWindow);
    SendMessage(hLogWindow, EM_SETSEL, textLen, textLen);
    SendMessage(hLogWindow, EM_REPLACESEL, 0, (LPARAM)text);
    // Ensure scroll to bottom
    SendMessage(hLogWindow, EM_SCROLLCARET, 0, 0);
}

void appendLog(const std::string& text) {
    appendLog(text.c_str());
}

void addToBlacklist(const std::string& hostname) {
    std::lock_guard<std::mutex> lock(blacklist_mutex);
    blacklist.insert(hostname);
    appendLog(("Added to blacklist: " + hostname + "\r\n").c_str());
}

void removeFromBlacklist(const std::string& hostname) {
    std::lock_guard<std::mutex> lock(blacklist_mutex);
    blacklist.erase(hostname);
    appendLog(("Removed from blacklist: " + hostname + "\r\n").c_str());
}

bool isBlacklisted(const std::string& hostname) {
    std::lock_guard<std::mutex> lock(blacklist_mutex);
    return blacklist.count(hostname) > 0;
}

void resolve_hostname(const char* hostname, struct sockaddr_in& server) {
    struct addrinfo hints = { 0 }, * res = NULL;
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
        struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
        server.sin_addr = addr->sin_addr;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &server.sin_addr, ip_str, INET_ADDRSTRLEN);
        appendLog(("Resolved " + std::string(hostname) + " to " + ip_str + "\r\n").c_str());
        freeaddrinfo(res);
    }
    else {
        appendLog(("Failed to resolve hostname: " + std::string(hostname) + "\r\n").c_str());
    }
}

void handle_client(SOCKET client_socket) {
    char buffer[BUFFER_SIZE];
    int recv_size;

    // Receive client request
    recv_size = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (recv_size == SOCKET_ERROR) {
        appendLog(("Recv failed. Error Code: " + std::to_string(WSAGetLastError()) + "\r\n").c_str());
        closesocket(client_socket);
        return;
    }

    buffer[recv_size] = '\0'; // Null-terminate the request
    appendLog(("Request received:\r\n" + std::string(buffer) + "\r\n").c_str());

    // If CONNECT method, forward the request and establish a tunnel
    if (strncmp(buffer, "CONNECT", 7) == 0) {
        appendLog("Handling CONNECT request...\r\n");

        // Extract hostname and port from CONNECT request
        char hostname[256];
        int port;

        sscanf(buffer, "CONNECT %255[^:]:%d", hostname, &port);
        appendLog(("CONNECT request to " + std::string(hostname) + ":" + std::to_string(port) + "\r\n").c_str());

        if (isBlacklisted(hostname)) {
            appendLog(("Request blocked for blacklisted host: " + std::string(hostname) + "\r\n").c_str());
            send(client_socket, "HTTP/1.1 403 Forbidden\r\n\r\n", 26, 0);
            closesocket(client_socket);
            return;
        }

        // Establish connection to the target server
        struct sockaddr_in target_addr{ 0 };
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(port);
        resolve_hostname(hostname, target_addr);

        SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(target_socket, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
            appendLog(("Failed to connect to target: " + std::string(hostname) + ":" + std::to_string(port) + " " + std::to_string(WSAGetLastError()) + "\r\n").c_str());
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

        appendLog(("HTTP " + std::string(method) + " request for " + std::string(url) + "\r\n").c_str());

        char hostname[256] = { 0 };
        sscanf(url, "http://%255[^/]", hostname);

        if (isBlacklisted(hostname)) {
            appendLog(("Request blocked for blacklisted host: " + std::string(hostname) + "\r\n").c_str());
            send(client_socket, "HTTP/1.1 403 Forbidden\r\n\r\n", 26, 0);
            closesocket(client_socket);
            return;
        }

        struct sockaddr_in target_addr = { 0 };
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(80);
        resolve_hostname(hostname, target_addr);

        SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(target_socket, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
            appendLog(("Failed to connect to HTTP target: " + std::string(hostname) + "\r\n").c_str());
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


void startServer()
{
    if (serverRunning) return;

    WSADATA wsa;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server, client;
    int c;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        appendLog(("Failed to initialize Winsock. Error Code: " + std::to_string(WSAGetLastError()) + "\r\n").c_str());
        return;
    }
    appendLog("Winsock initialized.\r\n");

    // Create a socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        appendLog(("Could not create socket. Error Code: " + std::to_string(WSAGetLastError()) + "\r\n").c_str());
        WSACleanup();
        return;
    }
    appendLog("Socket created.\r\n");

    // Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    // Bind the socket
    if (bind(server_socket, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        appendLog(("Bind failed. Error Code: " + std::to_string(WSAGetLastError()) + "\r\n").c_str());
        closesocket(server_socket);
        WSACleanup();
        return;
    }
    appendLog("Bind done.\r\n");

    // Listen for incoming connections
    listen(server_socket, 3);
    appendLog("Waiting for incoming connections...\r\n");
    serverRunning = true;
    SetWindowTextW(hStartStopBtn, L"Stop Server");
    c = sizeof(struct sockaddr_in);
    while (serverRunning) {
        client_socket = accept(server_socket, (struct sockaddr*)&client, &c);
        if (client_socket != INVALID_SOCKET) {
          appendLog("Connection accepted.\r\n");
          std::thread client_thread(handle_client, client_socket);
          client_thread.detach(); // Handle the client in a separate thread
      }
      else {
          if (serverRunning) {
              appendLog(("Accept failed. Error Code: " + std::to_string(WSAGetLastError()) + "\r\n").c_str());
          }
           break;
      }

    }
    closesocket(server_socket);
    WSACleanup();
    serverRunning = false;
    SetWindowTextW(hStartStopBtn, L"Start Server");
    appendLog("Server Stopped.\r\n");
    return;
}

void stopServer() {
    if (!serverRunning) return;
    serverRunning = false;
}

// Window Procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // Create log window (EDIT control with multiline style)
        hLogWindow = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 10, 780, 400, hwnd, NULL, GetModuleHandle(NULL), NULL);
        if (hLogWindow == NULL) {
            MessageBoxW(hwnd, L"Could not create log window", L"Error", MB_ICONERROR | MB_OK);
            return -1;
        }

        // Create add blacklist label, text box, button
        CreateWindowW(L"STATIC", L"Add Blacklist:", WS_CHILD | WS_VISIBLE, 10, 420, 100, 20, hwnd, NULL, NULL, NULL);
        hAddBlacklistEdit = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER,
            120, 420, 200, 20, hwnd, NULL, NULL, NULL);
        hAddBlacklistBtn = CreateWindowW(L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE,
            330, 420, 100, 25, hwnd, (HMENU)1, NULL, NULL);

        // Create remove blacklist label, text box, button
        CreateWindowW(L"STATIC", L"Remove Blacklist:", WS_CHILD | WS_VISIBLE, 10, 450, 100, 20, hwnd, NULL, NULL, NULL);
        hRemoveBlacklistEdit = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER,
            120, 450, 200, 20, hwnd, NULL, NULL, NULL);
        hRemoveBlacklistBtn = CreateWindowW(L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE,
            330, 450, 100, 25, hwnd, (HMENU)2, NULL, NULL);


        // Create start / stop button
        hStartStopBtn = CreateWindowW(L"BUTTON", L"Start Server", WS_CHILD | WS_VISIBLE,
            10, 480, 120, 25, hwnd, (HMENU)3, NULL, NULL);

        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 1) { // Add Button Clicked
            int len = GetWindowTextLength(hAddBlacklistEdit);
            if (len > 0) {
                char* buffer = new char[len + 1];
                GetWindowTextA(hAddBlacklistEdit, buffer, len + 1);

                // Convert char* to wchar_t*
                 int wlen = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, NULL, 0);
                wchar_t* wbuffer = new wchar_t[wlen];
                MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wbuffer, wlen);

                // Convert wchar_t* to char* for std::string
                int requiredSize = WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, nullptr, 0, nullptr, nullptr);
                char* charBuffer = new char[requiredSize];
                WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, charBuffer, requiredSize, nullptr, nullptr);

                addToBlacklist(std::string(charBuffer));

                SetWindowTextW(hAddBlacklistEdit, L"");
                delete[] buffer;
                delete[] wbuffer;
                delete[] charBuffer;
            }
        }
        else if (LOWORD(wParam) == 2) { // Remove Button Clicked
            int len = GetWindowTextLength(hRemoveBlacklistEdit);
            if (len > 0) {
                char* buffer = new char[len + 1];
                GetWindowTextA(hRemoveBlacklistEdit, buffer, len + 1);

                // Convert char* to wchar_t*
                int wlen = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, NULL, 0);
                wchar_t* wbuffer = new wchar_t[wlen];
                MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wbuffer, wlen);

                 // Convert wchar_t* to char* for std::string
                int requiredSize = WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, nullptr, 0, nullptr, nullptr);
                char* charBuffer = new char[requiredSize];
                WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, charBuffer, requiredSize, nullptr, nullptr);
                
                removeFromBlacklist(std::string(charBuffer));
                
                SetWindowTextW(hRemoveBlacklistEdit, L"");
                delete[] buffer;
                delete[] wbuffer;
                delete[] charBuffer;
            }
        }
        else if (LOWORD(wParam) == 3) // Start/Stop button clicked
        {
            if (!serverRunning)
            {
                if (serverThread.joinable()) {
                    serverThread.join();
                }
                serverThread = std::thread(startServer);
                serverThread.detach();
            }
            else {
                stopServer();
                //serverThread.join();
            }
        }
        break;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register the window class
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ProxyWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    // Create the window
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Proxy Server GUI", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxW(NULL, L"Window creation failed!", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (serverThread.joinable()) {
        serverThread.join();
    }
    return (int)msg.wParam;
}