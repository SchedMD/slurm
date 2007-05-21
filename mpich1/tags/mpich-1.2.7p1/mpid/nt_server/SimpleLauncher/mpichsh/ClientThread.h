#ifndef CLIENT_THREAD_H
#define CLIENT_THREAD_H

#include <winsock2.h>

struct SocketClientThreadArg
{
	SOCKET sock;
	WSAEVENT sock_event;
};

void SocketClientThread(SocketClientThreadArg *arg);

#endif
