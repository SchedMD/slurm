#ifndef NT_TCP_SOCKETS_H
#define NT_TCP_SOCKETS_H

#include "nt_common.h"

#define USE_LINGER_SOCKOPT

int NT_Tcp_create_bind_socket(SOCKET *sock, WSAEVENT *event, int port = 0, unsigned long addr = INADDR_ANY);
int NT_Tcp_connect(SOCKET socket, char *host, int port);
int NT_Tcp_closesocket(SOCKET sock, WSAEVENT event);
int NT_Tcp_get_sock_info(SOCKET sock, char *name, int *port);
int NT_Tcp_get_ip_string(char *host, char *ipstr);

int ReceiveBlocking(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags);
int ReceiveBlockingTimeout(SOCKET sock, WSAEVENT event, char *buffer, int len, int flags, int timeout);
int SendBlocking(SOCKET sock, char *buffer, int length, int flags);
int SendStreamBlocking(SOCKET sock, char *buffer, int length, int type);

#endif
