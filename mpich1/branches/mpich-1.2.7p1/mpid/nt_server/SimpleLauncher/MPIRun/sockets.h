#ifndef SOCKETS_H
#define SOCKETS_H

#include <winsock2.h>
#include <windows.h>

#define USE_LINGER_SOCKOPT

int Simple_create_bind_socket(SOCKET *sock, WSAEVENT *event, int port=0, unsigned long addr=INADDR_ANY);
int Simple_connect(SOCKET sock, char *host, int port);
int Simple_closesocket(SOCKET sock, WSAEVENT event);
int Simple_get_sock_info(SOCKET sock, char *name, int *port);
int SendBlocking(SOCKET sock, char *buffer, int length, int flags);
int ReceiveBlocking(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags);
int ReceiveBlockingTimeout(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags, int timeout);
int ReceiveSome(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags);

#endif
