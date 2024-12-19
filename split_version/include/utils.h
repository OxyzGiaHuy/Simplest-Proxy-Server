#ifndef UTILS_H
#define UTILS_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <fstream>
#include <windows.h>

std::string getLogFileName();
void logMessageToFile(const std::string &message);
void logMessage(const std::string &message);
void ClientBoxMessage();
bool resolve_hostname(const char *hostname, struct sockaddr_in &server);
bool is_blacklisted(const std::string &hostname);
void add_to_blacklist(const std::string &url);
void removeBlacklistUrl(int index);
bool parseHostHeader(const std::string &request, std::string &hostname, int &port);

#endif