#include <iostream>
#include <winsock2.h>
#include <thread>
#include <ws2tcpip.h>
#include <unordered_set>
#include <mutex>
#include <string>
#include <windows.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 4096

#define WM_UPDATE_BLACKLIST_ADD (WM_USER + 1)
#define WM_UPDATE_BLACKLIST_REMOVE (WM_USER + 2)
#define WM_UPDATE_RUNNINGHOST_ADD (WM_USER + 3)
#define WM_UPDATE_RUNNINGHOST_REMOVE (WM_USER + 4)

// Global Variables
HWND hLogWindow;
HWND hAddBlacklistEdit;
HWND hRemoveBlacklistEdit;
HWND hAddBlacklistBtn;
HWND hRemoveBlacklistBtn;
HWND hStartStopBtn;
HWND hBlacklistListBox;
HWND hRunningHostsListBox;

bool serverRunning = false;
std::thread serverThread;

std::unordered_set<std::string> blacklist;
std::mutex blacklist_mutex;

std::vector<std::string> runningHosts;
std::mutex runningHosts_mutex;

// Helper Functions
std::string getTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
    localtime_s(&now_tm, &now_c); // Use localtime_s for thread-safety

    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(2) << now_tm.tm_hour << ":"
       << std::setfill('0') << std::setw(2) << now_tm.tm_min << ":"
       << std::setfill('0') << std::setw(2) << now_tm.tm_sec << "] ";
    return ss.str();
}

void appendLog(const char *text)
{
    int textLen = GetWindowTextLength(hLogWindow);
    SendMessage(hLogWindow, EM_SETSEL, textLen, textLen);
    SendMessage(hLogWindow, EM_REPLACESEL, 0, (LPARAM)text);
    // Ensure scroll to bottom
    SendMessage(hLogWindow, EM_SCROLLCARET, 0, 0);
}

void appendLog(const std::string &text)
{
    appendLog(text.c_str());
}

void logMessage(const std::string &message)
{
    std::string log_message = getTimestamp() + message;
    appendLog(log_message.c_str());
}

void logResolved(const std::string &hostname, const std::string &ip_address)
{
    logMessage("Resolved " + hostname + " -> " + ip_address + "\r\n");
}

void logRequest(const std::string &type, const std::string &target)
{
    logMessage(type + " request for " + target + "\r\n");
}

void logBlocked(const std::string &hostname)
{
    logMessage("Blocked request to " + hostname + "\r\n");
}

void logError(const std::string &message)
{
    logMessage("Error: " + message + "\r\n");
}

void logConnect(const std::string &hostname, int port)
{
    logMessage("CONNECT request to " + hostname + ":" + std::to_string(port) + "\r\n");
}

// Blacklist Box
void updateBlacklistListBox()
{
    SendMessage(hBlacklistListBox, LB_RESETCONTENT, 0, 0);
    {
        std::lock_guard<std::mutex> lock(blacklist_mutex);
        for (const auto &host : blacklist)
        {
            SendMessage(hBlacklistListBox, LB_ADDSTRING, 0, (LPARAM)host.c_str());
        }
    }
}

void addToBlacklist(const std::string &hostname)
{
    {
        std::lock_guard<std::mutex> lock(blacklist_mutex);
        blacklist.insert(hostname);
        logMessage("Added to blacklist: " + hostname + "\r\n");
    }
    PostMessage(hLogWindow, WM_UPDATE_BLACKLIST_ADD, 0, (LPARAM) new std::string(hostname));
}

void removeFromBlacklist(const std::string &hostname)
{
    {
        std::lock_guard<std::mutex> lock(blacklist_mutex);
        blacklist.erase(hostname);
        logMessage("Removed from blacklist: " + hostname + "\r\n");
    }
    PostMessage(hLogWindow, WM_UPDATE_BLACKLIST_REMOVE, 0, (LPARAM) new std::string(hostname));
}

bool isBlacklisted(const std::string &hostname)
{
    std::lock_guard<std::mutex> lock(blacklist_mutex);
    return blacklist.count(hostname) > 0;
}

// Running Host Box
void updateRunningHostsListBox()
{
    SendMessage(hRunningHostsListBox, LB_RESETCONTENT, 0, 0);
    {
        std::lock_guard<std::mutex> lock(runningHosts_mutex);
        for (const auto &host : runningHosts)
        {
            SendMessage(hRunningHostsListBox, LB_ADDSTRING, 0, (LPARAM)host.c_str());
        }
    }
}

void addRunningHost(const std::string &hostname)
{
    {
        std::lock_guard<std::mutex> lock(runningHosts_mutex);
        runningHosts.push_back(hostname);
    }
    PostMessage(hLogWindow, WM_UPDATE_RUNNINGHOST_ADD, 0, (LPARAM) new std::string(hostname));
}

void removeRunningHost(const std::string &hostname)
{
    {
        std::lock_guard<std::mutex> lock(runningHosts_mutex);
        for (size_t i = 0; i < runningHosts.size(); ++i)
        {
            if (runningHosts[i] == hostname)
            {
                runningHosts.erase(runningHosts.begin() + i);
                break;
            }
        }
    }
    PostMessage(hLogWindow, WM_UPDATE_RUNNINGHOST_REMOVE, 0, (LPARAM) new std::string(hostname));
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
    // Remove leading space
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

void resolve_hostname(const char *hostname, struct sockaddr_in &server, std::string &ip_str)
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    ip_str = "N/A";
    if (getaddrinfo(hostname, NULL, &hints, &res) == 0)
    {
        struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
        server.sin_addr = addr->sin_addr;
        char temp_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &server.sin_addr, temp_ip_str, INET_ADDRSTRLEN);
        ip_str = temp_ip_str;
        freeaddrinfo(res);
    }
    else
        logError("Failed to resolve hostname: " + std::string(hostname));
}

void handle_client(SOCKET client_socket)
{
    char buffer[BUFFER_SIZE];
    int recv_size;

    // Receive client request
    recv_size = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (recv_size == SOCKET_ERROR)
    {
        logError("Recv failed. Error Code: " + std::to_string(WSAGetLastError()));
        closesocket(client_socket);
        return;
    }

    buffer[recv_size] = '\0'; // Null-terminate the request
    logMessage("Request received:\r\n" + std::string(buffer) + "\r\n");

    // If CONNECT method, forward the request and establish a tunnel
    if (strncmp(buffer, "CONNECT", 7) == 0)
    {

        // Extract hostname and port from CONNECT request
        char hostname[256];
        int port;

        sscanf(buffer, "CONNECT %255[^:]:%d", hostname, &port);
        logConnect(hostname, port);
        addRunningHost(hostname);

        if (isBlacklisted(hostname))
        {
            logBlocked(hostname);
            removeRunningHost(hostname);
            send(client_socket, "HTTP/1.1 403 Forbidden\r\n\r\n", 26, 0);
            closesocket(client_socket);
            return;
        }

        // Establish connection to the target server
        struct sockaddr_in target_addr{0};
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(port);
        std::string ip_str;
        resolve_hostname(hostname, target_addr, ip_str);
        logResolved(hostname, ip_str);

        SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(target_socket, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
        {
            logError("Failed to connect to target: " + std::string(hostname) + ":" + std::to_string(port) + " " + std::to_string(WSAGetLastError()));
            removeRunningHost(hostname);
            send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
            closesocket(client_socket);
            return;
        }

        // Send connection established response to the client
        const char *connection_established = "HTTP/1.1 200 Connection Established\r\n\r\n";
        send(client_socket, connection_established, strlen(connection_established), 0);

        // Relay data between client and server (this will forward encrypted data for HTTPS)
        fd_set fdset;
        char relay_buffer[BUFFER_SIZE];
        int bytes_read;

        while (1)
        {
            FD_ZERO(&fdset);
            FD_SET(client_socket, &fdset);
            FD_SET(target_socket, &fdset);

            int activity = select(0, &fdset, NULL, NULL, NULL);
            if (activity <= 0)
                break;

            // Forward data from client to target
            if (FD_ISSET(client_socket, &fdset))
            {
                bytes_read = recv(client_socket, relay_buffer, sizeof(relay_buffer), 0);
                if (bytes_read <= 0)
                    break;
                send(target_socket, relay_buffer, bytes_read, 0);
            }

            // Forward data from target to client
            if (FD_ISSET(target_socket, &fdset))
            {
                bytes_read = recv(target_socket, relay_buffer, sizeof(relay_buffer), 0);
                if (bytes_read <= 0)
                    break;
                send(client_socket, relay_buffer, bytes_read, 0);
            }
        }
        removeRunningHost(hostname);

        // Close connections
        closesocket(target_socket);
        closesocket(client_socket);
        return;
    }

    // Handle HTTP traffic (e.g., GET requests)
    if (strncmp(buffer, "GET", 3) == 0 || strncmp(buffer, "POST", 4) == 0)
    {
        char method[10], url[256], protocol[10];
        sscanf(buffer, "%s %s %s", method, url, protocol);
        logRequest(method, url);

        std::string hostname;
        int port = 80;
        if (!parseHostHeader(buffer, hostname, port))
        {
            logError("Could not find Host header\r\n");
            send(client_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 26, 0);
            closesocket(client_socket);
            return;
        }

        addRunningHost(hostname);

        if (isBlacklisted(hostname))
        {
            logBlocked(hostname);
            removeRunningHost(hostname);
            const char *forbiddenResponse = "HTTP/1.1 403 Forbidden\r\n\r\n";
            send(client_socket, forbiddenResponse, strlen(forbiddenResponse), 0);
            closesocket(client_socket);
            return;
        }

        struct sockaddr_in target_addr = {0};
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(port);

        std::string ip_str;
        resolve_hostname(hostname.c_str(), target_addr, ip_str);
        logResolved(hostname, ip_str);

        SOCKET target_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(target_socket, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0)
        {
            logError("Failed to connect to HTTP target: " + std::string(hostname));
            removeRunningHost(hostname);
            send(client_socket, "HTTP/1.1 502 Bad Gateway\r\n\r\n", 28, 0);
            closesocket(client_socket);
            return;
        }

        send(target_socket, buffer, recv_size, 0);

        // Relay response back to client
        while ((recv_size = recv(target_socket, buffer, BUFFER_SIZE, 0)) > 0)
        {
            send(client_socket, buffer, recv_size, 0);
        }
        removeRunningHost(hostname);
        closesocket(target_socket);
    }
    closesocket(client_socket);
}

void startServer()
{
    if (serverRunning)
        return;

    WSADATA wsa;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server, client;
    int c;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        logError("Failed to initialize Winsock. Error Code: " + std::to_string(WSAGetLastError()));
        return;
    }
    logMessage("Winsock initialized.\r\n");

    // Create a socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET)
    {
        logError("Could not create socket. Error Code: " + std::to_string(WSAGetLastError()));
        WSACleanup();
        return;
    }
    logMessage("Socket created.\r\n");

    // Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    // Bind the socket
    if (bind(server_socket, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
    {
        logError("Bind failed. Error Code: " + std::to_string(WSAGetLastError()));
        closesocket(server_socket);
        WSACleanup();
        return;
    }
    logMessage("Bind done.\r\n");

    // Listen for incoming connections
    listen(server_socket, 3);
    logMessage("Waiting for incoming connections...\r\n");
    serverRunning = true;
    SetWindowTextW(hStartStopBtn, L"Stop Server");
    c = sizeof(struct sockaddr_in);
    while (serverRunning)
    {
        client_socket = accept(server_socket, (struct sockaddr *)&client, &c);
        if (client_socket != INVALID_SOCKET)
        {
            logMessage("Connection accepted.\r\n");
            std::thread client_thread(handle_client, client_socket);
            client_thread.detach(); // Handle the client in a separate thread
        }
        else
        {
            if (serverRunning)
                logError("Accept failed. Error Code: " + std::to_string(WSAGetLastError()));
            break;
        }
    }
    closesocket(server_socket);
    WSACleanup();
    serverRunning = false;
    SetWindowTextW(hStartStopBtn, L"Start Server");
    logMessage("Server Stopped.\r\n");
    return;
}

void stopServer()
{
    if (!serverRunning)
        return;
    serverRunning = false;
}

// Window Procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        // Create log window (EDIT control with multiline style)
        hLogWindow = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                     10, 10, 800, 200, hwnd, NULL, GetModuleHandle(NULL), NULL);
        if (hLogWindow == NULL)
        {
            MessageBoxW(hwnd, L"Could not create log window", L"Error", MB_ICONERROR | MB_OK);
            return -1;
        }

        // Create blacklist label and listbox
        CreateWindowW(L"STATIC", L"Blacklist", WS_CHILD | WS_VISIBLE, 10, 220, 400, 20, hwnd, NULL, NULL, NULL);
        hBlacklistListBox = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL,
                                            10, 250, 400, 300, hwnd, NULL, NULL, NULL);

        // Create Running Host label and listbox
        CreateWindowW(L"STATIC", L"Host Running", WS_CHILD | WS_VISIBLE, 420, 220, 400, 20, hwnd, NULL, NULL, NULL);
        hRunningHostsListBox = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL,
                                               420, 250, 400, 300, hwnd, NULL, NULL, NULL);

        // Create add blacklist label, text box, button
        CreateWindowW(L"STATIC", L"Add to Blacklist", WS_CHILD | WS_VISIBLE, 10, 560, 200, 20, hwnd, NULL, NULL, NULL);
        hAddBlacklistEdit = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                            220, 560, 200, 20, hwnd, NULL, NULL, NULL);
        hAddBlacklistBtn = CreateWindowW(L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE,
                                         440, 560, 100, 25, hwnd, (HMENU)1, NULL, NULL);

        // Create remove blacklist label, text box, button
        CreateWindowW(L"STATIC", L"Delete from Blacklist", WS_CHILD | WS_VISIBLE, 10, 590, 200, 20, hwnd, NULL, NULL, NULL);
        hRemoveBlacklistEdit = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER,
                                               220, 590, 200, 20, hwnd, NULL, NULL, NULL);
        hRemoveBlacklistBtn = CreateWindowW(L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE,
                                            440, 590, 100, 25, hwnd, (HMENU)2, NULL, NULL);

        // Create start / stop button
        hStartStopBtn = CreateWindowW(L"BUTTON", L"Start Proxy", WS_CHILD | WS_VISIBLE,
                                      10, 640, 120, 25, hwnd, (HMENU)3, NULL, NULL);
        break;
    }
    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 1)
        { // Add Button Clicked
            int len = GetWindowTextLength(hAddBlacklistEdit);
            if (len > 0)
            {
                std::unique_ptr<char[]> buffer(new char[len + 1]); // Use unique_ptr for memory management
                GetWindowTextA(hAddBlacklistEdit, buffer.get(), len + 1);
                addToBlacklist(std::string(buffer.get()));

                SetWindowTextW(hAddBlacklistEdit, L"");
            }
        }
        else if (LOWORD(wParam) == 2)
        { // Remove Button Clicked
            int len = GetWindowTextLength(hRemoveBlacklistEdit);
            if (len > 0)
            {
                std::unique_ptr<char[]> buffer(new char[len + 1]); // Use unique_ptr for memory management
                GetWindowTextA(hRemoveBlacklistEdit, buffer.get(), len + 1);
                removeFromBlacklist(std::string(buffer.get()));
                SetWindowTextW(hRemoveBlacklistEdit, L"");
            }
        }
        else if (LOWORD(wParam) == 3) // Start/Stop button clicked
        {
            if (!serverRunning)
            {
                if (serverThread.joinable())
                    serverThread.join();
                serverThread = std::thread(startServer);
                serverThread.detach();
            }
            else
                stopServer();
        }
        break;
    }

    case WM_UPDATE_BLACKLIST_ADD:
    {
        std::string *hostname = (std::string *)lParam;
        SendMessage(hBlacklistListBox, LB_ADDSTRING, 0, (LPARAM)hostname->c_str());
        delete hostname;
    }
    break;
    case WM_UPDATE_BLACKLIST_REMOVE:
    {
        std::string *hostname = (std::string *)lParam;
        int index = -1;
        for (int i = 0; i < SendMessage(hBlacklistListBox, LB_GETCOUNT, 0, 0); ++i)
        {
            char buffer[256];
            SendMessageA(hBlacklistListBox, LB_GETTEXT, i, (LPARAM)buffer);
            if (std::string(buffer) == *hostname)
            {
                index = i;
                break;
            }
        }
        if (index != -1)
        {
            SendMessage(hBlacklistListBox, LB_DELETESTRING, index, 0);
        }
        delete hostname;
    }
    break;
    case WM_UPDATE_RUNNINGHOST_ADD:
    {
        std::string *hostname = (std::string *)lParam;
        SendMessage(hRunningHostsListBox, LB_ADDSTRING, 0, (LPARAM)hostname->c_str());
        delete hostname;
    }
    break;
    case WM_UPDATE_RUNNINGHOST_REMOVE:
    {
        std::string *hostname = (std::string *)lParam;
        int index = -1;
        for (int i = 0; i < SendMessage(hRunningHostsListBox, LB_GETCOUNT, 0, 0); ++i)
        {
            char buffer[256];
            SendMessageA(hRunningHostsListBox, LB_GETTEXT, i, (LPARAM)buffer);

            if (std::string(buffer) == *hostname)
            {
                index = i;
                break;
            }
        }
        if (index != -1)
        {
            SendMessage(hRunningHostsListBox, LB_DELETESTRING, index, 0);
        }
        delete hostname;
    }
    break;

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Register the window class
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ProxyWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    // Create the window
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Proxy Server GUI", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 900, 800, NULL, NULL, hInstance, NULL);

    if (!hwnd)
    {
        MessageBoxW(NULL, L"Window creation failed!", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (serverThread.joinable())
    {
        serverThread.join();
    }
    return (int)msg.wParam;
}