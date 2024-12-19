#include "../include/config.h"
#include "../include/utils.h"
#include "../include/proxy.h"
#include "../include/ui.h"

#include <iostream>
#include <windows.h>
#include <thread>

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