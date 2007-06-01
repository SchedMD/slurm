#include "Redirection.h"
#include <stdio.h>

int g_nIOListenPort = 0;
char g_pszIOListenHost[100] = "";
LONG g_nConnectionsLeft;
HANDLE g_hNoMoreConnectionsEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

struct RedirectIOArg
{
	SOCKET sock;
	WSAEVENT sock_event;
	bool bStdout;
};

// Function name	: RedirectInput
// Description	    : 
// Return type		: void 
// Argument         : RedirectIOArg *pArg
void RedirectInput(RedirectIOArg *pArg)
{
	DWORD dwNumRead;
	char buffer[1024];
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	while (ReadFile(hStdin, buffer, 1024, &dwNumRead, NULL))
	{
		SendBlocking(pArg->sock, buffer, dwNumRead, 0);
	}
	NT_closesocket(pArg->sock, pArg->sock_event);
	delete pArg;
}

// Function name	: RedirectOutput
// Description	    : 
// Return type		: void 
// Argument         : RedirectIOArg *pArg
void RedirectOutput(RedirectIOArg *pArg)
{
	DWORD dwNumWritten;
	char buffer[1024];
	int length = 1024;
	//bool bCounted = pArg->bStdout;
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
	while (ReceiveSomeBlocking(pArg->sock, pArg->sock_event, buffer, &length, 0) == 0)
	{
		if (length == 0)
			break;
		if (pArg->bStdout)
			WriteFile(hStdout, buffer, length, &dwNumWritten, NULL);
		else
			WriteFile(hStderr, buffer, length, &dwNumWritten, NULL);
		length = 1024;
	}
	NT_closesocket(pArg->sock, pArg->sock_event);
	delete pArg;
	//if (bCounted)
	{
		if (InterlockedDecrement(&g_nConnectionsLeft) == 0)
		{
			//printf("count to zero, setting event\n");
			SetEvent(g_hNoMoreConnectionsEvent);
		}
		//else printf("decrementing count\n");
	}
}

// Function name	: RedirectIOLoopThread
// Description	    : 
// Return type		: void 
// Argument         : HANDLE hReadyEvent
void RedirectIOLoopThread(HANDLE hReadyEvent)
{
	SOCKET sock;
	WSAEVENT sock_event;
	int error = 0;
	DWORD result;
	SOCKET temp_socket;
	WSAEVENT temp_event;
	HANDLE hThread;
	DWORD dwThreadID;

	WSADATA wsaData;
	int err;

	// Start the Winsock dll.
	if ((err = WSAStartup( MAKEWORD( 2, 0 ), &wsaData )) != 0)
	{
		printf("Winsock2 dll not initialized, error: %d\n", err);
		return;
	}

	// create a listening socket
	error = NT_create_bind_socket(&sock, &sock_event, g_nIOListenPort);
	if (error)
	{
		printf("RedirectIOLoopThread: NT_Tcp_create_bind_socket failed, error %d\n", error);
		return;
	}

	// associate sock_event with sock
	if (WSAEventSelect(sock, sock_event, FD_ACCEPT) == SOCKET_ERROR)
	{
		printf("RedirectIOLoopThread: WSAEventSelect(FD_ACCEPT) failed for the control socket, error %d\n", WSAGetLastError());
		return;
	}

	if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
	{
		printf("RedirectIOLoopThread: listen failed, error %d\n", WSAGetLastError());
		return;
	}

	// get the port and local hostname for the listening socket
	error = NT_get_sock_info(sock, g_pszIOListenHost, &g_nIOListenPort);
	if (error)
	{
		printf("RedirectIOLoopThread: Unable to get host and port of listening socket, error %d\n", error);
		return;
	}

	// Signal that the control port is valid
	if (!SetEvent(hReadyEvent))
	{
		printf("RedirectIOLoopThread: SetEvent(hReadyEvent) failed, error %d\n", GetLastError());
		return;
	}

	// Loop indefinitely, waiting for remote connections or a stop signal
	char c;
	while (true)
	{
		result = WSAWaitForMultipleEvents(1, &sock_event, TRUE, INFINITE, FALSE);
		if (result != WSA_WAIT_EVENT_0)
		{
			printf("RedirectIOLoopThread: Wait for a connect event failed, error %d\n", result);
			return;
		}
		
		temp_socket = accept(sock, NULL, NULL);
		if (temp_socket != INVALID_SOCKET)
		{
			if ((temp_event = WSACreateEvent()) == WSA_INVALID_EVENT)
			{
				printf("RedirectIOLoopThread: WSACreateEvent failed, error %d\n", WSAGetLastError());
				return;
			}
			if (WSAEventSelect(temp_socket, temp_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
			{
				printf("RedirectIOLoopThread: WSAEventSelect failed, error %d\n", WSAGetLastError());
				return;
			}
			ReceiveBlocking(temp_socket, temp_event, &c, 1, 0);

			RedirectIOArg *cArg = new RedirectIOArg;
			cArg->sock = temp_socket;
			cArg->sock_event = temp_event;
			if (c == 1)
				cArg->bStdout = true;
			if (c == 2)
				cArg->bStdout = false;
			if (c == 0)
				hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectInput, cArg, 0, &dwThreadID);
			else
				hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectOutput, cArg, 0, &dwThreadID);
			if (hThread == NULL)
			{
				delete cArg;
				NT_closesocket(temp_socket, temp_event);
				printf("CreateThread failed in RedirectIOLoopThread, error %d\n", GetLastError());
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
			printf("RedirectIOLoopThread: accept failed, error %d\n", result);
			return;
		}
	}
}
