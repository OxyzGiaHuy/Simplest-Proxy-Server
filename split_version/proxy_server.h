#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>

// Forward declarations
struct BlacklistEntry;

// Constants
#define WM_SOCKET WM_USER + 1
#define DEFAULT_PORT "8888"
#define BUFFER_SIZE 4096

// Function prototypes
void handleClientConnection(SOCKET clientSocket);
void handleHttpsRequest(SOCKET client_socket, const std::string &request);
void handleHttpRequest(SOCKET client_socket, const std::string &request);
bool resolveHostname(const char *hostname, struct sockaddr_in &server);
void listenForClientConnections();
void addToActiveHosts(const std::string &hostname);
void removeFromActiveHosts(const std::string &hostname);

// Global variables (note: avoid overuse of globals)
extern SOCKET serverSocket;
extern bool running;
extern HWND hwndHostRunning;