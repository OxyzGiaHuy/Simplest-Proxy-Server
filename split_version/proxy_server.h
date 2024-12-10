#ifndef PROXY_SERVER_H
#define PROXY_SERVER_H

#include <winsock2.h>
#include <iostream>

#define BUFFER_SIZE 4096

void handle_client(SOCKET client_socket);

#endif // PROXY_SERVER_H