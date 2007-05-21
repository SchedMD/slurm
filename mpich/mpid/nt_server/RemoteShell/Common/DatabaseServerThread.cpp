#include "Database.h"
#include "sockets.h"

// Function name	: DatabaseServerThread
// Description	    : 
// Return type		: void 
// Argument         : DatabaseServer *pServer
void DatabaseServerThread(DatabaseServer *pServer)
{
	SOCKET sock;
	WSAEVENT sock_event, aEvents[2];
	int error = 0;
	DWORD result;
	SOCKET temp_socket;
	WSAEVENT temp_event;
	HANDLE hThread;
	DWORD dwThreadID;

	// create a listening socket
	error = NT_create_bind_socket(&sock, &sock_event, pServer->m_nPort);
	if (error)
	{
		dbs_error("DatabaseServerThread: NT_create_bind_socket failed", 1);
		CloseHandle(pServer->m_hServerThread);
		pServer->m_hServerThread = NULL;
		return;
	}

	// associate sock_event with sock
	if (WSAEventSelect(sock, sock_event, FD_ACCEPT) == SOCKET_ERROR)
	{
		dbs_error("DatabaseServerThread: WSAEventSelect(FD_ACCEPT) failed for the control socket", WSAGetLastError());
		CloseHandle(pServer->m_hServerThread);
		pServer->m_hServerThread = NULL;
		return;
	}

	if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
	{
		dbs_error("DatabaseServerThread: listen failed", WSAGetLastError());
		CloseHandle(pServer->m_hServerThread);
		pServer->m_hServerThread = NULL;
		return;
	}

	// get the port and local hostname for the listening socket
	error = NT_get_sock_info(sock, pServer->m_pszHost, &pServer->m_nPort);
	if (error)
	{
		dbs_error("DatabaseServerThread: Unable to get host and port of listening socket", error, sock, sock_event);
		CloseHandle(pServer->m_hServerThread);
		pServer->m_hServerThread = NULL;
		return;
	}

	aEvents[0] = sock_event;
	aEvents[1] = g_hStopDBSLoopEvent;
	// Loop indefinitely, waiting for remote connections or a stop signal
	while (true)
	{
		result = WSAWaitForMultipleEvents(2, aEvents, FALSE, INFINITE, FALSE);
		if ((result != WSA_WAIT_EVENT_0) && (result != WSA_WAIT_EVENT_0+1))
		{
			dbs_error("DatabaseServerThread: Wait for a connect event failed", result, sock, sock_event);
			CloseHandle(pServer->m_hServerThread);
			pServer->m_hServerThread = NULL;
			return;
		}
		
		if (result == WSA_WAIT_EVENT_0+1)
		{
			closesocket(sock);
			pServer->m_hServerThread = NULL;
			return;
		}

		temp_socket = accept(sock, NULL, NULL);
		if (temp_socket != INVALID_SOCKET)
		{
			DBSClientArg *cArg = new DBSClientArg;
			if ((temp_event = WSACreateEvent()) == WSA_INVALID_EVENT)
			{
				dbs_error("DatabaseServerThread: WSACreateEvent failed", WSAGetLastError(), sock, sock_event);
				CloseHandle(pServer->m_hServerThread);
				pServer->m_hServerThread = NULL;
				return;
			}
			if (WSAEventSelect(temp_socket, temp_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
			{
				dbs_error("DatabaseServerThread: WSAEventSelect failed", WSAGetLastError(), sock, sock_event);
				CloseHandle(pServer->m_hServerThread);
				pServer->m_hServerThread = NULL;
				return;
			}
			cArg->sock = temp_socket;
			cArg->sock_event = temp_event;
			cArg->pServer = pServer;
			hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)DatabaseClientThread, cArg, 0, &dwThreadID);
			if (hThread == NULL)
			{
				delete cArg;
				NT_closesocket(temp_socket, temp_event);
				dbs_error("CreateThread failed in DatabaseServerThread.", GetLastError(), sock, sock_event);
				CloseHandle(pServer->m_hServerThread);
				pServer->m_hServerThread = NULL;
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
			dbs_error("DatabaseServerThread: accept failed", result, sock, sock_event);
			CloseHandle(pServer->m_hServerThread);
			pServer->m_hServerThread = NULL;
			return;
		}
	}
}
