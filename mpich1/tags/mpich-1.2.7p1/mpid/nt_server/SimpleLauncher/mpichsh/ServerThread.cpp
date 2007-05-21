#include "stdafx.h"
#include "ServerThread.h"
#include "ClientThread.h"
#include "sockets.h"

HANDLE g_hStopSocketLoopEvent;

void SocketServerThread(int port)
{
	WSADATA wsaData;
	SOCKET sock;
	WSAEVENT sock_event, aEvents[2];
	int error;

	// Start up the Winsock2 dll
	if (WSAStartup( MAKEWORD( 2, 0 ), &wsaData ))
		return;

	error = Simple_create_bind_socket(&sock, &sock_event, port);
	if (error)
	{
		printf("Unable to create a socket on port %d. Error %d\n", port, error);
		return;
	}

	// associate sock_event with sock
	WSAEventSelect(sock, sock_event, FD_ACCEPT);

	if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		printf("listen failed. Error: %d\n", error);
		Simple_closesocket(sock, sock_event);
		return;
	}

	DWORD result;

	aEvents[0] = sock_event;
	aEvents[1] = g_hStopSocketLoopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	// Loop indefinitely, waiting for remote connections or a stop signal
	while (true)
	{
		SOCKET temp_socket;
		WSAEVENT temp_event;
		result = WSAWaitForMultipleEvents(2, aEvents, FALSE, INFINITE, FALSE);
		if ((result != WSA_WAIT_EVENT_0) && (result != WSA_WAIT_EVENT_0+1))
		{
			printf("Wait for a connect event failed. Error: %d", result);
			Simple_closesocket(sock, sock_event);
			return;
		}
		
		if (result == WSA_WAIT_EVENT_0+1)
		{
			Simple_closesocket(sock, sock_event);
			CloseHandle(g_hStopSocketLoopEvent);
			return;
		}

		temp_socket = accept(sock, NULL, NULL);
		if (temp_socket != INVALID_SOCKET)
		{
			SocketClientThreadArg *cArg = new SocketClientThreadArg;
			HANDLE hThread;
			temp_event = WSACreateEvent();
			if (WSAEventSelect(temp_socket, temp_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
			{
				printf("WSAEventSelect failed. Error: %d\n", WSAGetLastError());
				Simple_closesocket(sock, sock_event);
				CloseHandle(g_hStopSocketLoopEvent);
				return;
			}
			cArg->sock = temp_socket;
			cArg->sock_event = temp_event;
			DWORD dwThreadID;
			hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SocketClientThread, cArg, 0, &dwThreadID);
			if (hThread == NULL)
			{
				delete cArg;
				Simple_closesocket(temp_socket, temp_event);
				printf("CreateThread failed in SocketServerThread. Error: %d", GetLastError());
				Simple_closesocket(sock, sock_event);
				CloseHandle(g_hStopSocketLoopEvent);
				return;
			}
			CloseHandle(hThread);
			continue;
		}
		result = GetLastError();
		if (result == WSAEWOULDBLOCK)
		{
			WSAResetEvent(sock_event);
			WSAEventSelect(sock, sock_event, FD_ACCEPT);
		}
		else
		{
			printf("SocketServerThread: accept failed. Error: %d\n", result);
			Simple_closesocket(sock, sock_event);
			CloseHandle(g_hStopSocketLoopEvent);
			return;
		}
	}

	WSACleanup();
}
