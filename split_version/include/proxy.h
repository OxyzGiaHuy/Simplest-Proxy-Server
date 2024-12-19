#ifndef PROXY_H
#define PROXY_H

#include <winsock2.h>
#include <string>

void handleHttpsRequest(SOCKET client_socket, const std::string &request);
void handleHttpRequest(SOCKET client_socket, const std::string &request);
void handleClient(SOCKET clientSocket);
void listenForClients();
void addToHostRunning(const std::string &hostname);
void removeFromHostRunning(const std::string &hostname);

#endif