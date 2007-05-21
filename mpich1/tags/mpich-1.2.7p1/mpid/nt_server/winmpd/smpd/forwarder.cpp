#include "GetStringOpt.h"
#include "mpdimpl.h"
#include <stdio.h>

HANDLE g_hForwarderMutex = NULL;

/*#define dbg_printf err_printf*/

struct ForwarderEntry
{
    ForwarderEntry();
    ~ForwarderEntry();
    char pszFwdHost[MAX_HOST_LENGTH];
    int nFwdPort;
    int nPort;
    SOCKET sockStop;
    ForwarderEntry *pNext;
};

ForwarderEntry::ForwarderEntry()
{
    sockStop = INVALID_SOCKET;
}

ForwarderEntry::~ForwarderEntry()
{
    if (sockStop != INVALID_SOCKET)
	easy_closesocket(sockStop);
    sockStop = INVALID_SOCKET;
}

struct ForwardIOThreadArg
{
    ForwardIOThreadArg();
    ~ForwardIOThreadArg();
    SOCKET sockStop;
    SOCKET sockListen;
    SOCKET sockForward;
    int nPort;
};

ForwardIOThreadArg::ForwardIOThreadArg()
{
    sockStop = INVALID_SOCKET;
    sockListen = INVALID_SOCKET;
    sockForward = INVALID_SOCKET;
    nPort = 0;
}

ForwardIOThreadArg::~ForwardIOThreadArg()
{
    if (sockStop != INVALID_SOCKET)
	easy_closesocket(sockStop);
    sockStop = INVALID_SOCKET;
    if (sockListen != INVALID_SOCKET)
	easy_closesocket(sockListen);
    sockListen = INVALID_SOCKET;
    if (sockForward != INVALID_SOCKET)
	easy_closesocket(sockForward);
    sockForward = INVALID_SOCKET;
}

ForwarderEntry *g_pForwarderList = NULL;

static void ForwarderToString(ForwarderEntry *p, char *pszStr, int length)
{
    if (!snprintf_update(pszStr, length, "FORWARDER:\n"))
	return;
    if (!snprintf_update(pszStr, length, " inport: %d\n outhost: %s:%d\n stop socket: %d\n",
	p->nPort, p->pszFwdHost, p->nFwdPort, p->sockStop))
	return;
}

void statForwarders(char *pszOutput, int length)
{
    ForwarderEntry *p;

    *pszOutput = '\0';
    length--; // leave room for the null character

    if (g_pForwarderList == NULL)
	return;

    WaitForSingleObject(g_hForwarderMutex, INFINITE);
    p = g_pForwarderList;
    while (p)
    {
	ForwarderToString(p, pszOutput, length);
	length = length - strlen(pszOutput);
	pszOutput = &pszOutput[strlen(pszOutput)];
	p = p->pNext;
    }
    ReleaseMutex(g_hForwarderMutex);
}

void ConcatenateForwardersToString(char *pszStr)
{
    char pszLine[100];
    
    WaitForSingleObject(g_hForwarderMutex, INFINITE);
    ForwarderEntry *p = g_pForwarderList;
    while (p)
    {
	_snprintf(pszLine, 100, "%s:%d -> %s:%d\n", g_pszHost, p->nPort, p->pszFwdHost, p->nFwdPort);
	strncat(pszStr, pszLine, MAX_CMD_LENGTH - 1 - strlen(pszStr));
	p = p->pNext;
    }
    ReleaseMutex(g_hForwarderMutex);
}

static void RemoveForwarder(int nPort)
{
    WaitForSingleObject(g_hForwarderMutex, INFINITE);

    ForwarderEntry *pEntry = g_pForwarderList;
    if (pEntry != NULL)
    {
	if (pEntry->nPort == nPort)
	{
	    g_pForwarderList = g_pForwarderList->pNext;
	    delete pEntry;
	    ReleaseMutex(g_hForwarderMutex);
	    return;
	}
	while (pEntry->pNext)
	{
	    if (pEntry->pNext->nPort == nPort)
	    {
		ForwarderEntry *pTemp = pEntry->pNext;
		pEntry->pNext = pEntry->pNext->pNext;
		delete pTemp;
		ReleaseMutex(g_hForwarderMutex);
		return;
	    }
	    pEntry = pEntry->pNext;
	}
    }
    ReleaseMutex(g_hForwarderMutex);
}

static void MakeLoop(SOCKET *psockRead, SOCKET *psockWrite)
{
    SOCKET sock;
    char host[100];
    int port;

    // Create a listener
    if (easy_create(&sock, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	*psockRead = INVALID_SOCKET;
	*psockWrite = INVALID_SOCKET;
	return;
    }
    listen(sock, 5);
    easy_get_sock_info(sock, host, &port);
    
    // Connect to myself
    if (easy_create(psockWrite, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	easy_closesocket(sock);
	*psockRead = INVALID_SOCKET;
	*psockWrite = INVALID_SOCKET;
	return;
    }
    if (easy_connect(*psockWrite, host, port) == SOCKET_ERROR)
    {
	easy_closesocket(*psockWrite);
	easy_closesocket(sock);
	*psockRead = INVALID_SOCKET;
	*psockWrite = INVALID_SOCKET;
	return;
    }

    // Accept the connection from myself
    *psockRead = easy_accept(sock);

    easy_closesocket(sock);
}

static int ReadWriteAlloc(SOCKET sock, SOCKET sockForward, int n)
{
    int num_to_receive, num_received;
    char *pBuffer;

    pBuffer = new char[n + sizeof(int) + sizeof(char) + sizeof(int)];
    *(int*)pBuffer = n;
    num_to_receive = n + sizeof(int) + sizeof(char);
    
    num_received = easy_receive(sock, &pBuffer[sizeof(int)], num_to_receive);
    if (num_received == SOCKET_ERROR || num_received == 0)
    {
	delete pBuffer;
	return SOCKET_ERROR;
    }
    if (easy_send(sockForward, pBuffer, num_received + sizeof(int)) == SOCKET_ERROR)
    {
	delete pBuffer;
	return SOCKET_ERROR;
    }

    delete pBuffer;
    return 0;
}

static int ReadWrite(SOCKET sock, SOCKET sockForward, int n)
{
    int num_to_receive, num_received;
    char pBuffer[1024+sizeof(int)+sizeof(char)+sizeof(int)];

    if (n > 1024)
	return ReadWriteAlloc(sock, sockForward, n);

    *(int*)pBuffer = n;
    num_to_receive = n + sizeof(int) + sizeof(char);
    
    num_received = easy_receive(sock, &pBuffer[sizeof(int)], num_to_receive);
    if (num_received == SOCKET_ERROR || num_received == 0)
    {
	return SOCKET_ERROR;
    }

    if (easy_send(sockForward, pBuffer, num_received + sizeof(int)) == SOCKET_ERROR)
    {
	return SOCKET_ERROR;
    }

    return 0;
}

void ForwardIOThread(ForwardIOThreadArg *pArg)
{
    SOCKET client_sock, stop_sock, listen_sock, forward_sock;
    int n, i;
    DWORD num_read;
    fd_set total_set, readset;
    SOCKET sockActive[FD_SETSIZE];
    int nActive = 0;
    int nDatalen;
    bool bDeleteOnEmpty = false;
    int nPort;
    
    listen_sock = pArg->sockListen;
    pArg->sockListen = INVALID_SOCKET;
    stop_sock = pArg->sockStop;
    pArg->sockStop = INVALID_SOCKET;
    forward_sock = pArg->sockForward;
    pArg->sockForward = INVALID_SOCKET;
    nPort = pArg->nPort;

    delete pArg;
    pArg = NULL;

    FD_ZERO(&total_set);
    
    FD_SET(listen_sock, &total_set);
    FD_SET(stop_sock, &total_set);
    FD_SET(forward_sock, &total_set);

    while (true)
    {
	readset = total_set;
	dbg_printf("ForwardIOThread: select, nActive %d\n", nActive);
	n = select(0, &readset, NULL, NULL, NULL);
	if (n == SOCKET_ERROR)
	{
	    err_printf("ForwardIOThread: select failed, error %d\n", WSAGetLastError());
	    break;
	}
	if (n == 0)
	{
	    err_printf("ForwardIOThread: select returned zero sockets available\n");
	    break;
	}
	else
	{
	    if (FD_ISSET(stop_sock, &readset))
	    {
		char c;
		num_read = easy_receive(stop_sock, &c, 1);
		if (num_read == SOCKET_ERROR || num_read == 0)
		    break;
		if (c == 0)
		{
		    if (nActive == 0)
		    {
			dbg_printf("ForwardIOThread: %d breaking\n", nPort);
			break;
		    }
		    dbg_printf("ForwardIOThread: ------ %d signalled to exit on empty, %d sockets remaining\n", nPort, nActive);
		    //if (total_set.fd_count == 3)
			//err_printf("ForwardIOThread: ERROR: total_set is empty\n");
		    bDeleteOnEmpty = true;
		}
		else
		{
		    dbg_printf("ForwardIOThread: aborting forwarder %d\n", nPort);
		    break;
		}
		n--;
	    }
	    if (FD_ISSET(listen_sock, &readset))
	    {
		if ((nActive + 3) >= FD_SETSIZE)
		{
		    client_sock = easy_accept(listen_sock);
		    easy_closesocket(client_sock);
		    dbg_printf("ForwardIOThread: too many clients connecting to the forwarder, connect rejected: nActive = %d\n", nActive);
		}
		else
		{
		    client_sock = easy_accept(listen_sock);
		    if (client_sock == INVALID_SOCKET)
		    {
			int error = WSAGetLastError();
			err_printf("ForwardIOThread: easy_accept failed: %d\n", error);
			break;
		    }
		    
		    char cType;
		    if (easy_receive(client_sock, &cType, sizeof(char)) == SOCKET_ERROR)
		    {
			int error = WSAGetLastError();
			err_printf("ForwardIOThread: easy_receive failed, error %d\n", error);
			break;
		    }
		    
		    if (cType == 0)
		    {
			easy_closesocket(client_sock);
			err_printf("ForwardIOThread: stdin redirection not handled by forwarder thread, socket closed.\n");
		    }
		    else
		    {
			sockActive[nActive] = client_sock;
			FD_SET(client_sock, &total_set);
			nActive++;
			dbg_printf("ForwardIOThread: %d adding socket %d (+%d)\n", nPort, client_sock, nActive);
		    }
		}
		n--;
	    }
	    if (FD_ISSET(forward_sock, &readset))
	    {
		easy_closesocket(forward_sock);
		err_printf("ForwardIOThread: forward socket unexpectedly closed\n");
		break;
	    }
	    if (n > 0)
	    {
		if (nActive < 1)
		{
		    err_printf("ForwardIOThread: Error, n=%d while nActive=%d\n", n, nActive);
		    break;
		}
		else
		{
		    for (i=0; n > 0; i++)
		    {
			if (FD_ISSET(sockActive[i], &readset))
			{
			    num_read = easy_receive(sockActive[i], (char*)&nDatalen, sizeof(int));
			    if (num_read == SOCKET_ERROR || num_read == 0)
			    {
				dbg_printf("ForwardIOThread: port %d, removing socket[%d]=%d (%d active)\n", nPort, i, sockActive[i], nActive);
				FD_CLR(sockActive[i], &total_set);
				easy_closesocket(sockActive[i]);
				nActive--;
				sockActive[i] = sockActive[nActive];
				i--;
			    }
			    else
			    {
				if (ReadWrite(sockActive[i], forward_sock, nDatalen) == SOCKET_ERROR)
				{
				    dbg_printf("ForwardIOThread: port %d, abandoning socket[%d]=%d (%d active)\n", nPort, i, sockActive[i], nActive);
				    FD_CLR(sockActive[i], &total_set);
				    easy_closesocket(sockActive[i]);
				    nActive--;
				    sockActive[i] = sockActive[nActive];
				    i--;
				}
			    }
			    n--;
			}
		    }
		}
	    }
	    if (nActive == 0 && bDeleteOnEmpty)
	    {
		dbg_printf("ForwardIOThread: %d breaking on empty\n", nPort);
		break;
	    }
	}
    }
    easy_closesocket(forward_sock);
    easy_closesocket(stop_sock);
    for (i=0; i<nActive; i++)
	easy_closesocket(sockActive[i]);
    easy_closesocket(listen_sock);
    RemoveForwarder(nPort);
    dbg_printf("ForwardIOThread: %d exiting\n", nPort);
    return;
}

int CreateIOForwarder(char *pszFwdHost, int nFwdPort)
{
    int error;
    char pszHost[100];
    HANDLE hThread;
    DWORD dwThreadId;
    ForwarderEntry *pEntry;
    ForwardIOThreadArg *pArg;
    int nPort;
    char ch = 1;
    int iter;

    pArg = new ForwardIOThreadArg;

    // Connect to the forwardee
    if (easy_create(&pArg->sockForward, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	err_printf("CreateIOForwarder: easy_create failed: error %d\n", error);
	delete pArg;
	return INVALID_SOCKET;
    }
    if (easy_connect(pArg->sockForward, pszFwdHost, nFwdPort) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	err_printf("CreateIOForwarder: easy_connect(%s:%d) failed: error %d\n", pszFwdHost, nFwdPort, error);
	delete pArg;
	return INVALID_SOCKET;
    }
    if (easy_send(pArg->sockForward, &ch, sizeof(char)) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	err_printf("CreateIOForwarder: easy_send failed: error %d\n", error);
	delete pArg;
	return INVALID_SOCKET;
    }

    pEntry = new ForwarderEntry;

    // Save the forwardee stuff.  Used only by the forwarders command.
    strncpy(pEntry->pszFwdHost, pszFwdHost, MAX_HOST_LENGTH);
    pEntry->nFwdPort = nFwdPort;

    // Create a listener
    if (easy_create(&pArg->sockListen, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	err_printf("CreateIOForwarder: easy_create listen socket failed: error %d\n", error);
	delete pEntry;
	delete pArg;
	return INVALID_SOCKET;
    }
    listen(pArg->sockListen, 10);
    easy_get_sock_info(pArg->sockListen, pszHost, &pEntry->nPort);
    nPort = pEntry->nPort;

    dbg_printf("create forwarder %s:%d -> %s:%d\n", pszHost, pEntry->nPort, pszFwdHost, nFwdPort);

    // Create a stop signal socket
    MakeLoop(&pArg->sockStop, &pEntry->sockStop);
    if (pArg->sockStop == INVALID_SOCKET || pEntry->sockStop == INVALID_SOCKET)
    {
	delete pEntry;
	delete pArg;
	return INVALID_SOCKET;
    }

    // Let the forward thread know what port it is connected to
    pArg->nPort = nPort;

    // Create the forwarder thread
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ForwardIOThread, pArg, 0, &dwThreadId);
	if (hThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hThread == NULL)
    {
	error = GetLastError();
	err_printf("CreateIOForwarder: CreateThread failed, error %d\n", error);
	delete pEntry;
	delete pArg;
	return INVALID_SOCKET;
    }
    CloseHandle(hThread);
    
    // Add the new entry to the list
    WaitForSingleObject(g_hForwarderMutex, INFINITE);
    pEntry->pNext = g_pForwarderList;
    g_pForwarderList = pEntry;
    ReleaseMutex(g_hForwarderMutex);

    return nPort;
}

void StopIOForwarder(int nPort, bool bWaitForEmpty)
{
    WaitForSingleObject(g_hForwarderMutex, INFINITE);
    ForwarderEntry *pEntry = g_pForwarderList;

    while (pEntry)
    {
	if (pEntry->nPort == nPort)
	{
	    if (bWaitForEmpty)
	    {
		char ch = 0;
		easy_send(pEntry->sockStop, &ch, 1);
		ReleaseMutex(g_hForwarderMutex);
	    }
	    else
	    {
		int nPort = pEntry->nPort;
		easy_send(pEntry->sockStop, "x", 1);
		easy_closesocket(pEntry->sockStop);
		pEntry->sockStop = INVALID_SOCKET;
		ReleaseMutex(g_hForwarderMutex);
		RemoveForwarder(nPort);
	    }
	    return;
	}
	pEntry = pEntry->pNext;
    }
    ReleaseMutex(g_hForwarderMutex);
    err_printf("StopIOForwarder: forwarder port %d not found\n", nPort);
}

void AbortAllForwarders()
{
    int nPort;
    
    while (g_pForwarderList)
    {
	WaitForSingleObject(g_hForwarderMutex, INFINITE);
	if (g_pForwarderList)
	{
	    nPort = g_pForwarderList->nPort;
	    ReleaseMutex(g_hForwarderMutex);
	    StopIOForwarder(g_pForwarderList->nPort, false);
	}
	else
	    ReleaseMutex(g_hForwarderMutex);
    }
}
