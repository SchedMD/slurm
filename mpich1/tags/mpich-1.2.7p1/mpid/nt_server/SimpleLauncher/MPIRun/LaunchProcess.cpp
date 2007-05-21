#include "sockets.h"
#include "LaunchProcess.h"
#include <stdio.h>
#include "RedirectInput.h"

void LaunchProcessSocket(LaunchProcessArg *arg)
{
	DWORD length = 100;
	DWORD num_written, num_read;
	char msg[1024];
	WSAEVENT sock_event;
	SOCKET sock;
	int ret_val;
	RedirectInputThreadArg *rarg = NULL;
	HANDLE hRIThread = NULL;

	if (ret_val = Simple_create_bind_socket(&sock, &sock_event))
	{
		printf("Unable to create a socket. Error: %d\n", ret_val);
		delete arg;
		return;
	}

	if (Simple_connect(sock, arg->pszHost, arg->nPort))
	{
		printf("Unable to connect to %s on %d\n", arg->pszHost, arg->nPort);
		delete arg;
		return;
	}

	sprintf(msg, "-dir\"%s\"-env\"%s\"%s", arg->pszDir, arg->pszEnv, arg->pszCmdLine);
	length = strlen(msg)+1;
	if (SendBlocking(sock, (char*)&length, sizeof(int), 0) == SOCKET_ERROR)
	{
		printf("SendBlocking length failed. Error: %d\n", WSAGetLastError());
		Simple_closesocket(sock, sock_event);
		delete arg;
		return;
	}
	if (SendBlocking(sock, msg, length, 0) == SOCKET_ERROR)
	{
		printf("SendBlocking msg failed. Error: %d\n", WSAGetLastError());
		Simple_closesocket(sock, sock_event);
		delete arg;
		return;
	}
	
	if (arg->i == 0)
	{
		rarg = new RedirectInputThreadArg;
		rarg->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		rarg->hSock = sock;
		DWORD dwThreadID;
		hRIThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectInputSocketThread, rarg, 0, &dwThreadID);
	}

	// Redirect output
	char pBuffer[1024];
	HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	while (num_read = ReceiveSome(sock, sock_event, pBuffer, 1024, 0))
	{
		if ((num_read == SOCKET_ERROR) || (num_read == 0))
		{
			Simple_closesocket(sock, sock_event);
			delete arg;
			return;
		}
		WriteFile(hStdOut, pBuffer, num_read, &num_written, NULL);
	}
	if (arg->i == 0)
	{
		SetEvent(rarg->hEvent);
		WaitForSingleObject(hRIThread, 5000);
	}
	Simple_closesocket(sock, sock_event);
	delete arg;
}

