#include "bnrimpl.h"
#include <stdio.h>
#include "bsocket.h"
#include "mpdutil.h"
#include "RedirectIO.h"

static int g_bfdListen;
static HANDLE g_hListenReleasedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

static void RedirectStdin(int bfd)
{
    char pBuffer[1024];
    DWORD num_read;
    HANDLE hStdin;

    for (int i=0; i<3; i++)
    {
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	if (hStdin != INVALID_HANDLE_VALUE)
	    break;
	Sleep(10);
    }
    if (hStdin == INVALID_HANDLE_VALUE)
    {
	printf("Critical error: Unable to acquire the standard input handle for redirection. error %d\n", GetLastError());
	fflush(stdout);
    }

    while (ReadFile(hStdin, pBuffer, 1024, &num_read, NULL))
    {
	if (beasy_send(bfd, pBuffer, num_read) == SOCKET_ERROR)
	    break;
    }
    beasy_closesocket(bfd);
}

void RedirectIOThread2(int abort_bfd)
{
    int client_bfd, child_abort_bfd = BFD_INVALID_SOCKET;
    int bfdListen;
    int n, i;
    char pBuffer[1024];
    DWORD num_read, num_written;
    bfd_set total_set, readset;
    int bfdActive[FD_SETSIZE];
    int nActive = 0;
    HANDLE hStdout, hStderr, hOut;
    int nRank;
    char cType;
    int nDatalen;
    bool bDeleteOnEmpty = false;
    HANDLE hChildThread = NULL;

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    hStderr = GetStdHandle(STD_ERROR_HANDLE);
    
    BFD_ZERO(&total_set);
    BFD_SET(abort_bfd, &total_set);
    BFD_SET(g_bfdListen, &total_set);
    bfdListen = g_bfdListen;

    while (true)
    {
	readset = total_set;
	n = bselect(0, &readset, NULL, NULL, NULL);
	if (n == SOCKET_ERROR)
	{
	    printf("RedirectIOThread2: bselect failed, error %d\n", WSAGetLastError());fflush(stdout);
	    beasy_closesocket(abort_bfd);
	    for (i=0; i<nActive; i++)
		beasy_closesocket(bfdActive[i]);
	    if (bfdListen != BFD_INVALID_SOCKET)
	    {
		SetEvent(g_hListenReleasedEvent);
		//beasy_closesocket(bfdListen);
	    }
	    if (hChildThread != NULL)
		CloseHandle(hChildThread);
	    return;
	}
	if (n == 0)
	{
	    printf("RedirectIOThread2: bselect returned zero sockets available\n");fflush(stdout);
	    beasy_closesocket(abort_bfd);
	    for (i=0; i<nActive; i++)
		beasy_closesocket(bfdActive[i]);
	    if (bfdListen != BFD_INVALID_SOCKET)
	    {
		SetEvent(g_hListenReleasedEvent);
		//beasy_closesocket(bfdListen);
	    }
	    if (hChildThread != NULL)
		CloseHandle(hChildThread);
	    return;
	}
	else
	{
	    if (BFD_ISSET(abort_bfd, &readset))
	    {
		char c;
		bool bCloseNow = true;
		num_read = beasy_receive(abort_bfd, &c, 1);
		if (num_read == 1)
		{
		    if (c == 0)
		    {
			if (child_abort_bfd != BFD_INVALID_SOCKET)
			    beasy_send(child_abort_bfd, &c, 1);

			if (nActive == 0)
			    WaitForSingleObject(hChildThread, 10000);
			else
			    bCloseNow = false;
			bDeleteOnEmpty = true;
		    }
		}
		if (bCloseNow)
		{
		    for (i=0; i<nActive; i++)
		    {
			beasy_closesocket(bfdActive[i]);
		    }
		    nActive = 0;
		    if (child_abort_bfd == BFD_INVALID_SOCKET)
			SetEvent(g_hListenReleasedEvent);
		    else
		    {
			beasy_send(child_abort_bfd, "x", 1);
			beasy_closesocket(child_abort_bfd);
		    }
		    beasy_closesocket(abort_bfd);
		    if (hChildThread != NULL)
			CloseHandle(hChildThread);
		    return;
		}
	    }
	    if (bfdListen != BFD_INVALID_SOCKET && BFD_ISSET(bfdListen, &readset))
	    {
		if ((nActive + 3) >= FD_SETSIZE)
		{
		    int temp_bfd;
		    MakeLoop(&temp_bfd, &child_abort_bfd);
		    if (temp_bfd == BFD_INVALID_SOCKET || child_abort_bfd == BFD_INVALID_SOCKET)
		    {
			printf("Critical error: Unable to create a socket.\n");fflush(stdout);
			break;
		    }
		    DWORD dwThreadId;
		    HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOThread2, (LPVOID)temp_bfd, 0, &dwThreadId);
		    if (hThread == NULL)
		    {
			printf("Critical error: Unable to create an io thread\n");fflush(stdout);
			break;
		    }
		    CloseHandle(hThread);
		    BFD_CLR(bfdListen, &total_set);
		    bfdListen = BFD_INVALID_SOCKET;
		}
		else
		{
		    client_bfd = beasy_accept(bfdListen);
		    if (client_bfd == BFD_INVALID_SOCKET)
		    {
			int error = WSAGetLastError();
			printf("RedirectIOThread2: baccept failed: %d\n", error);fflush(stdout);
			break;
		    }
		    
		    if (beasy_receive(client_bfd, &cType, sizeof(char)) == SOCKET_ERROR)
		    {
			break;
		    }
		    
		    if (cType == 0)
		    {
			DWORD dwThreadID;
			hChildThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectStdin, (void*)client_bfd, 0, &dwThreadID);
		    }
		    else
		    {
			bfdActive[nActive] = client_bfd;
			BFD_SET(client_bfd, &total_set);
			nActive++;
		    }
		}
		n--;
	    }
	    if (n > 0)
	    {
		for (i=0; n > 0; i++)
		{
		    if (BFD_ISSET(bfdActive[i], &readset))
		    {
			char pTemp[sizeof(int)+sizeof(char)+sizeof(int)];
			num_read = beasy_receive(bfdActive[i], pTemp, sizeof(int)+sizeof(char)+sizeof(int));
			if (num_read == SOCKET_ERROR || num_read == 0)
			{
			    BFD_CLR(bfdActive[i], &total_set);
			    beasy_closesocket(bfdActive[i]);
			    nActive--;
			    bfdActive[i] = bfdActive[nActive];
			    i--;
			    //printf("(-%d)", nActive);fflush(stdout);
			}
			else
			{
			    nDatalen = *(int*)pTemp;
			    cType = pTemp[sizeof(int)];
			    nRank = *(int*)&pTemp[sizeof(int)+sizeof(char)];
			    num_read = beasy_receive(bfdActive[i], pBuffer, nDatalen);
			    if (num_read == SOCKET_ERROR || num_read == 0)
			    {
				BFD_CLR(bfdActive[i], &total_set);
				beasy_closesocket(bfdActive[i]);
				nActive--;
				bfdActive[i] = bfdActive[nActive];
				i--;
				//printf("(-%d)", nActive);fflush(stdout);
			    }
			    else
			    {
				hOut = (cType == 1) ? hStdout : hStderr;
				/*
				if (g_bDoMultiColorOutput)
				{
				    WaitForSingleObject(g_hConsoleOutputMutex, INFINITE);
				    SetConsoleTextAttribute(hOut, aConsoleColorAttribute[nRank%NUM_OUTPUT_COLORS]);
				    if (WriteFile(hOut, pBuffer, num_read, &num_written, NULL))
					FlushFileBuffers(hOut);
				    else
				    {
					printf("*** output lost ***\n");fflush(stdout);
				    }
				    SetConsoleTextAttribute(hOut, g_ConsoleAttribute);
				    ReleaseMutex(g_hConsoleOutputMutex);
				}
				else
				{
				*/
				    if (!WriteFile(hOut, pBuffer, num_read, &num_written, NULL))
				    {
					printf("*** output lost ***\n");fflush(stdout);
				    }
				//}
			    }
			}
			n--;
		    }
		}
	    }
	    if (bDeleteOnEmpty && nActive == 0)
	    {
		if (hChildThread != NULL)
		{
		    WaitForSingleObject(hChildThread, 10000);
		    CloseHandle(hChildThread);
		    hChildThread = NULL;
		}
		break;
	    }
	}
    }

    for (i=0; i<nActive; i++)
    {
	beasy_closesocket(bfdActive[i]);
    }
    if (child_abort_bfd == BFD_INVALID_SOCKET)
	SetEvent(g_hListenReleasedEvent);
    else
    {
	beasy_send(child_abort_bfd, "x", 1);
	beasy_closesocket(child_abort_bfd);
    }
    beasy_closesocket(abort_bfd);
    if (hChildThread != NULL)
	CloseHandle(hChildThread);
}

void RedirectIOThread(RedirectIOArg *pArg)
{
    int client_bfd, signal_bfd, child_abort_bfd = BFD_INVALID_SOCKET;
    int bfdListen;
    int n, i;
    int bfdStopIOSignalSocket;
    char pBuffer[1024];
    DWORD num_read, num_written;
    HANDLE hStdout, hStderr, hOut;
    int nRank;
    char cType;
    int nDatalen;
    bool bDeleteOnEmpty = false;
    HANDLE hChildThread = NULL;

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    hStderr = GetStdHandle(STD_ERROR_HANDLE);

    // Create a listener
    if (beasy_create(&g_bfdListen, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	int error = WSAGetLastError();
	printf("RedirectIOThread: beasy_create listen socket failed: error %d\n", error);fflush(stdout);
	bsocket_finalize();
	ExitProcess(error);
    }
    blisten(g_bfdListen, 5);
    beasy_get_sock_info(g_bfdListen, g_pszIOHost, &g_nIOPort);

    // Connect a stop socket to myself
    if (beasy_create(pArg->m_pbfdStopIOSignalSocket, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	int error = WSAGetLastError();
	printf("beasy_create(m_bfdStopIOSignalSocket) failed, error %d\n", error);fflush(stdout);
	ExitProcess(error);
    }
    if (beasy_connect(*pArg->m_pbfdStopIOSignalSocket, g_pszIOHost, g_nIOPort) == SOCKET_ERROR)
    {
	int error = WSAGetLastError();
	printf("beasy_connect(m_bfdStopIOSignalSocket, %s, %d) failed, error %d\n", g_pszIOHost, g_nIOPort, error);fflush(stdout);
	ExitProcess(error);
    }
    bfdStopIOSignalSocket = *pArg->m_pbfdStopIOSignalSocket;

    // Accept the connection from myself
    signal_bfd = beasy_accept(g_bfdListen);
    if (signal_bfd == BFD_INVALID_SOCKET)
    {
	int error = WSAGetLastError();
	printf("beasy_accept failed, error %d\n", error);
	ExitProcess(error);
    }

    SetEvent(pArg->hReadyEvent);
    delete pArg;
    pArg = NULL;
    
    bfd_set total_set, readset;
    int bfdActive[FD_SETSIZE];
    int nActive = 0;
    
    BFD_ZERO(&total_set);
    
    BFD_SET(g_bfdListen, &total_set);
    BFD_SET(signal_bfd, &total_set);
    bfdListen = g_bfdListen;

    while (true)
    {
	readset = total_set;
	n = bselect(0, &readset, NULL, NULL, NULL);
	if (n == SOCKET_ERROR)
	{
	    printf("RedirectIOThread: bselect failed, error %d\n", WSAGetLastError());fflush(stdout);
	    break;
	}
	if (n == 0)
	{
	    printf("RedirectIOThread: bselect returned zero sockets available\n");fflush(stdout);
	    break;
	}
	else
	{
	    if (BFD_ISSET(signal_bfd, &readset))
	    {
		char c;
		num_read = beasy_receive(signal_bfd, &c, 1);
		if (num_read == 1)
		{
		    if (c == 0)
		    {
			if (child_abort_bfd != BFD_INVALID_SOCKET)
			    beasy_send(child_abort_bfd, &c, 1);

			if (nActive == 0)
			{
			    if (hChildThread != NULL)
				WaitForSingleObject(hChildThread, 10000);
			    break;
			}
			bDeleteOnEmpty = true;
		    }
		}
		else
		{
		    if (num_read == SOCKET_ERROR)
			printf("Error: redirect IO signal socket closed, exiting\n");
		    else
			printf("Error: error reading redirect IO signal socket, error %d\n", WSAGetLastError());
		    fflush(stdout);
		    break;
		}
		n--;
	    }
	    if (bfdListen != BFD_INVALID_SOCKET && BFD_ISSET(bfdListen, &readset))
	    {
		if ((nActive + 3) >= FD_SETSIZE)
		{
		    int temp_bfd;
		    MakeLoop(&temp_bfd, &child_abort_bfd);
		    if (temp_bfd == BFD_INVALID_SOCKET || child_abort_bfd == BFD_INVALID_SOCKET)
		    {
			printf("Critical error: Unable to create a socket\n");fflush(stdout);
			break;
		    }
		    DWORD dwThreadId;
		    hChildThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOThread2, (LPVOID)temp_bfd, 0, &dwThreadId);
		    if (hChildThread == NULL)
		    {
			printf("Critical error: Unable to create an io thread\n");fflush(stdout);
			break;
		    }
		    BFD_CLR(bfdListen, &total_set);
		    bfdListen = BFD_INVALID_SOCKET;
		    //printf("started new IO redirection thread\n");fflush(stdout);
		}
		else
		{
		    client_bfd = beasy_accept(bfdListen);
		    if (client_bfd == BFD_INVALID_SOCKET)
		    {
			int error = WSAGetLastError();
			printf("RedirectIOThread: baccept failed: %d\n", error);fflush(stdout);
			break;
		    }
		    
		    if (beasy_receive(client_bfd, &cType, sizeof(char)) == SOCKET_ERROR)
			return;
		    
		    if (cType == 0)
		    {
			HANDLE hThread;
			DWORD dwThreadID;
			hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectStdin, (void*)client_bfd, 0, &dwThreadID);
			if (hThread == NULL)
			{
			    printf("Critical error: Standard input redirection thread creation failed. error %d\n", GetLastError());fflush(stdout);
			}
			else
			    CloseHandle(hThread);
		    }
		    else
		    {
			bfdActive[nActive] = client_bfd;
			BFD_SET(client_bfd, &total_set);
			nActive++;
			//printf("(+%d:%d)", nActive, bget_fd(client_bfd));fflush(stdout);
		    }
		}
		n--;
	    }
	    if (n > 0)
	    {
		for (i=0; n > 0; i++)
		{
		    if (BFD_ISSET(bfdActive[i], &readset))
		    {
			char pTemp[sizeof(int)+sizeof(char)+sizeof(int)];
			num_read = beasy_receive(bfdActive[i], pTemp, sizeof(int)+sizeof(char)+sizeof(int));
			if (num_read == SOCKET_ERROR || num_read == 0)
			{
			    //printf("(-%d:%d)", nActive, bget_fd(bfdActive[i]));fflush(stdout);
			    BFD_CLR(bfdActive[i], &total_set);
			    beasy_closesocket(bfdActive[i]);
			    nActive--;
			    bfdActive[i] = bfdActive[nActive];
			    i--;
			}
			else
			{
			    nDatalen = *(int*)pTemp;
			    cType = pTemp[sizeof(int)];
			    nRank = *(int*)&pTemp[sizeof(int)+sizeof(char)];
			    //printf("\nreceiving %d bytes from %d of type %d\n", nDatalen, nRank, (int)cType);fflush(stdout);
			    num_read = beasy_receive(bfdActive[i], pBuffer, nDatalen);
			    if (num_read == SOCKET_ERROR || num_read == 0)
			    {
				BFD_CLR(bfdActive[i], &total_set);
				beasy_closesocket(bfdActive[i]);
				nActive--;
				bfdActive[i] = bfdActive[nActive];
				i--;
				/*
				if (num_read == SOCKET_ERROR) 
				    printf("(err-%d)", nActive);
				else
				    printf("(-%d)", nActive);
				fflush(stdout);
				*/
			    }
			    else
			    {
				hOut = (cType == 1) ? hStdout : hStderr;
				hOut = hStdout;
				/*
				if (g_bDoMultiColorOutput)
				{
				    WaitForSingleObject(g_hConsoleOutputMutex, INFINITE);
				    SetConsoleTextAttribute(hOut, aConsoleColorAttribute[nRank%NUM_OUTPUT_COLORS]);
				    //printf("(%d)", bget_fd(bfdActive[i]));fflush(stdout);
				    if (WriteFile(hOut, pBuffer, num_read, &num_written, NULL))
					FlushFileBuffers(hOut);
				    else
				    {
					printf("*** output lost ***\n");fflush(stdout);
				    }
				    SetConsoleTextAttribute(hOut, g_ConsoleAttribute);
				    ReleaseMutex(g_hConsoleOutputMutex);
				}
				else
				{
				*/
				    if (!WriteFile(hOut, pBuffer, num_read, &num_written, NULL))
				    {
					printf("*** output lost ***\n");fflush(stdout);
				    }
				//}
			    }
			}
			n--;
		    }
		}
	    }
	    if (bDeleteOnEmpty && nActive == 0)
	    {
		if (hChildThread != NULL)
		{
		    WaitForSingleObject(hChildThread, 10000);
		    CloseHandle(hChildThread);
		    hChildThread = NULL;
		}
		break;
	    }
	}
    }

    if (child_abort_bfd != BFD_INVALID_SOCKET)
    {
	//printf("signalling child threads to shut down\n");fflush(stdout);
	beasy_send(child_abort_bfd, "x", 1);
	WaitForSingleObject(g_hListenReleasedEvent, 10000);
	beasy_closesocket(g_bfdListen);
    }
    else if (bfdListen != BFD_INVALID_SOCKET)
    {
	//printf("closing listen socket\n");fflush(stdout);
	beasy_closesocket(bfdListen);
    }
    for (i=0; i<nActive; i++)
    {
	//printf("closing io socket %d\n", i);fflush(stdout);
	beasy_closesocket(bfdActive[i]);
    }
    //printf("closing signal socket\n");fflush(stdout);
    beasy_closesocket(signal_bfd);
    if (hChildThread != NULL)
	CloseHandle(hChildThread);
    //printf("RedirectIOThread exiting\n");fflush(stdout);
}
