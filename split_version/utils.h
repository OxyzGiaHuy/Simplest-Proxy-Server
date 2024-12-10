#ifndef UTILS_H
#define UTILS_H

#include <winsock2.h>

void resolve_hostname(const char* hostname, struct sockaddr_in& server);

#endif // UTILS_H