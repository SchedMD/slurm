#ifndef SOCKETS_H
#define SOCKETS_H

#include <winsock2.h>
#include <windows.h>

#define USE_LINGER_SOCKOPT

int NT_create_bind_socket(SOCKET *sock, WSAEVENT *event, int port = 0, unsigned long addr = INADDR_ANY);
int NT_connect(SOCKET socket, char *host, int port);
int NT_closesocket(SOCKET sock, WSAEVENT event);
int NT_get_sock_info(SOCKET sock, char *name, int *port);
int NT_get_ip(char *host, unsigned long *pIP);
int NT_get_host(unsigned long nIP, char *host);

int ReceiveBlocking(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags);
int ReceiveBlockingTimeout(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags, int timeout);
int ReceiveSomeBlocking(SOCKET sock, WSAEVENT event, char *buffer, int *len, int flags);
int SendBlocking(SOCKET sock, char *buffer, int length, int flags);

#endif
