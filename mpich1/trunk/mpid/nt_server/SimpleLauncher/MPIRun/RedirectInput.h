#ifndef REDIRECT_INPUT_H
#define REDIRECT_INPUT_H

#include <winsock2.h>
#include <windows.h>

struct RedirectInputThreadArg
{
	HANDLE hEvent;
	SOCKET hSock;
};

void RedirectInputSocketThread(RedirectInputThreadArg *arg);

#endif
