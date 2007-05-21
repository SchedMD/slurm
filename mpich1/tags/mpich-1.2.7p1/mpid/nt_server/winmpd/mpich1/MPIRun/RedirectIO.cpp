#include "global.h"
#include <stdio.h>
#include "mpdutil.h"
#include "RedirectIO.h"

static SOCKET g_sockListen;
static HANDLE g_hListenReleasedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

static void RedirectStdin(SOCKET sock)
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
	if (easy_send(sock, pBuffer, num_read) == SOCKET_ERROR)
	    break;
    }
    easy_closesocket(sock);
}

void RedirectIOThread2(SOCKET abort_sock)
{
    SOCKET client_sock, child_abort_sock = INVALID_SOCKET;
    SOCKET sockListen;
    int n, i;
    char pBuffer[1024];
    DWORD num_read, num_written;
    fd_set total_set, readset;
    SOCKET sockActive[FD_SETSIZE];
    int nActive = 0;
    HANDLE hStdout, hStderr, hOut;
    int nRank;
    char cType;
    int nDatalen;
    bool bDeleteOnEmpty = false;
    HANDLE hChildThread = NULL;
    int iter;

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    hStderr = GetStdHandle(STD_ERROR_HANDLE);
    
    FD_ZERO(&total_set);
    FD_SET(abort_sock, &total_set);
    FD_SET(g_sockListen, &total_set);
    sockListen = g_sockListen;

    while (true)
    {
	readset = total_set;
	n = select(0, &readset, NULL, NULL, NULL);
	if (n == SOCKET_ERROR)
	{
	    printf("RedirectIOThread2: bselect failed, error %d\n", WSAGetLastError());fflush(stdout);
	    easy_closesocket(abort_sock);
	    for (i=0; i<nActive; i++)
		easy_closesocket(sockActive[i]);
	    if (sockListen != INVALID_SOCKET)
	    {
		SetEvent(g_hListenReleasedEvent);
		//easy_closesocket(sockListen);
	    }
	    if (hChildThread != NULL)
		CloseHandle(hChildThread);
	    return;
	}
	if (n == 0)
	{
	    printf("RedirectIOThread2: bselect returned zero sockets available\n");fflush(stdout);
	    easy_closesocket(abort_sock);
	    for (i=0; i<nActive; i++)
		easy_closesocket(sockActive[i]);
	    if (sockListen != INVALID_SOCKET)
	    {
		SetEvent(g_hListenReleasedEvent);
		//easy_closesocket(sockListen);
	    }
	    if (hChildThread != NULL)
		CloseHandle(hChildThread);
	    return;
	}
	else
	{
	    if (FD_ISSET(abort_sock, &readset))
	    {
		char c;
		bool bCloseNow = true;
		num_read = easy_receive(abort_sock, &c, 1);
		if (num_read == 1)
		{
		    if (c == 0)
		    {
			if (child_abort_sock != INVALID_SOCKET)
			    easy_send(child_abort_sock, &c, 1);

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
			easy_closesocket(sockActive[i]);
		    }
		    nActive = 0;
		    if (child_abort_sock == INVALID_SOCKET)
			SetEvent(g_hListenReleasedEvent);
		    else
		    {
			easy_send(child_abort_sock, "x", 1);
			easy_closesocket(child_abort_sock);
		    }
		    easy_closesocket(abort_sock);
		    if (hChildThread != NULL)
			CloseHandle(hChildThread);
		    return;
		}
	    }
	    if (sockListen != INVALID_SOCKET && FD_ISSET(sockListen, &readset))
	    {
		if ((nActive + 3) >= FD_SETSIZE)
		{
		    SOCKET temp_sock;
		    MakeLoop(&temp_sock, &child_abort_sock);
		    if (temp_sock == INVALID_SOCKET || child_abort_sock == INVALID_SOCKET)
		    {
			printf("Critical error: Unable to create a socket.\n");fflush(stdout);
			break;
		    }
		    DWORD dwThreadId;
		    HANDLE hThread;
		    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
		    {
			hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOThread2, (LPVOID)temp_sock, 0, &dwThreadId);
			if (hThread != NULL)
			    break;
			Sleep(CREATE_THREAD_SLEEP_TIME);
		    }
		    if (hThread == NULL)
		    {
			printf("Critical error: Unable to create an io thread\n");fflush(stdout);
			break;
		    }
		    CloseHandle(hThread);
		    FD_CLR(sockListen, &total_set);
		    sockListen = INVALID_SOCKET;
		}
		else
		{
		    client_sock = easy_accept(sockListen);
		    if (client_sock == INVALID_SOCKET)
		    {
			int error = WSAGetLastError();
			printf("RedirectIOThread2: baccept failed: %d\n", error);fflush(stdout);
			break;
		    }
		    
		    if (easy_receive(client_sock, &cType, sizeof(char)) == SOCKET_ERROR)
		    {
			break;
		    }
		    
		    if (cType == 0)
		    {
			DWORD dwThreadID;
			for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
			{
			    hChildThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectStdin, (void*)client_sock, 0, &dwThreadID);
			    if (hChildThread != NULL)
				break;
			    Sleep(CREATE_THREAD_SLEEP_TIME);
			}
		    }
		    else
		    {
			sockActive[nActive] = client_sock;
			FD_SET(client_sock, &total_set);
			nActive++;
		    }
		}
		n--;
	    }
	    if (n > 0)
	    {
		for (i=0; n > 0; i++)
		{
		    if (FD_ISSET(sockActive[i], &readset))
		    {
			char pTemp[sizeof(int)+sizeof(char)+sizeof(int)];
			num_read = easy_receive(sockActive[i], pTemp, sizeof(int)+sizeof(char)+sizeof(int));
			if (num_read == SOCKET_ERROR || num_read == 0)
			{
			    FD_CLR(sockActive[i], &total_set);
			    easy_closesocket(sockActive[i]);
			    nActive--;
			    sockActive[i] = sockActive[nActive];
			    i--;
			    //printf("(-%d)", nActive);fflush(stdout);
			}
			else
			{
			    nDatalen = *(int*)pTemp;
			    cType = pTemp[sizeof(int)];
			    nRank = *(int*)&pTemp[sizeof(int)+sizeof(char)];
			    num_read = easy_receive(sockActive[i], pBuffer, nDatalen);
			    if (num_read == SOCKET_ERROR || num_read == 0)
			    {
				FD_CLR(sockActive[i], &total_set);
				easy_closesocket(sockActive[i]);
				nActive--;
				sockActive[i] = sockActive[nActive];
				i--;
				//printf("(-%d)", nActive);fflush(stdout);
			    }
			    else
			    {
				hOut = (cType == 1) ? hStdout : hStderr;
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
				    if (!WriteFile(hOut, pBuffer, num_read, &num_written, NULL))
				    {
					printf("*** output lost ***\n");fflush(stdout);
				    }
				}
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
	easy_closesocket(sockActive[i]);
    }
    if (child_abort_sock == INVALID_SOCKET)
	SetEvent(g_hListenReleasedEvent);
    else
    {
	easy_send(child_abort_sock, "x", 1);
	easy_closesocket(child_abort_sock);
    }
    easy_closesocket(abort_sock);
    if (hChildThread != NULL)
	CloseHandle(hChildThread);
}

void RedirectIOThread(HANDLE hReadyEvent)
{
    SOCKET client_sock, signal_sock, child_abort_sock = INVALID_SOCKET;
    SOCKET sockListen;
    int n, i;
    SOCKET sockStopIOSignalSocket;
    char pBuffer[1024];
    DWORD num_read, num_written;
    HANDLE hStdout, hStderr, hOut;
    int nRank;
    char cType;
    int nDatalen;
    bool bDeleteOnEmpty = false;
    HANDLE hChildThread = NULL;
    int iter;

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    hStderr = GetStdHandle(STD_ERROR_HANDLE);

    // Create a listener
    if (easy_create(&g_sockListen, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	int error = WSAGetLastError();
	printf("RedirectIOThread: easy_create listen socket failed: error %d\n", error);fflush(stdout);
	easy_socket_finalize();
	ExitProcess(error);
    }
    listen(g_sockListen, 5);
    if (g_bIPRoot)
    {
	// I've seen problems where the job nodes cannot contact back to the mpirun process
	// using the host name.  But they can connect back if they know the ip address.
	// This will not work on systems with multiple nics, for those systems a list
	// of ip's need to be transfered.
	/*easy_get_sock_info_ip(g_sockListen, g_pszIOHost, &g_nIOPort);*/
	easy_get_sock_info(g_sockListen, g_pszIOHost, &g_nIOPort);
	easy_get_ip_string(g_pszIOHost, g_pszIOHost);
    }
    else
    {
	easy_get_sock_info(g_sockListen, g_pszIOHost, &g_nIOPort);
    }

    // Connect a stop socket to myself
    if (easy_create(&g_sockStopIOSignalSocket, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	int error = WSAGetLastError();
	printf("easy_create(g_sockStopIOSignalSocket) failed, error %d\n", error);fflush(stdout);
	ExitProcess(error);
    }
    if (easy_connect(g_sockStopIOSignalSocket, g_pszIOHost, g_nIOPort) == SOCKET_ERROR)
    {
	int error = WSAGetLastError();
	printf("easy_connect(g_sockStopIOSignalSocket, %s, %d) failed, error %d\n", g_pszIOHost, g_nIOPort, error);fflush(stdout);
	ExitProcess(error);
    }
    sockStopIOSignalSocket = g_sockStopIOSignalSocket;

    // Accept the connection from myself
    signal_sock = easy_accept(g_sockListen);
    if (signal_sock == INVALID_SOCKET)
    {
	int error = WSAGetLastError();
	printf("easy_accept failed, error %d\n", error);
	ExitProcess(error);
    }

    if (!SetEvent(hReadyEvent))
    {
	int error = GetLastError();
	printf("RedirectIOThread failed to set the ready event, error %d\n", error);
	ExitProcess(error);
    }

    fd_set total_set, readset;
    SOCKET sockActive[FD_SETSIZE];
    int nActive = 0;

    FD_ZERO(&total_set);
    
    FD_SET(g_sockListen, &total_set);
    FD_SET(signal_sock, &total_set);
    sockListen = g_sockListen;

    while (true)
    {
	readset = total_set;
	n = select(0, &readset, NULL, NULL, NULL);
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
	    if (FD_ISSET(signal_sock, &readset))
	    {
		char c;
		num_read = easy_receive(signal_sock, &c, 1);
		if (num_read == 1)
		{
		    if (c == 0)
		    {
			if (child_abort_sock != INVALID_SOCKET)
			    easy_send(child_abort_sock, &c, 1);

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
	    if (sockListen != INVALID_SOCKET && FD_ISSET(sockListen, &readset))
	    {
		if ((nActive + 3) >= FD_SETSIZE)
		{
		    SOCKET temp_sock;
		    MakeLoop(&temp_sock, &child_abort_sock);
		    if (temp_sock == INVALID_SOCKET || child_abort_sock == INVALID_SOCKET)
		    {
			printf("Critical error: Unable to create a socket\n");fflush(stdout);
			break;
		    }
		    DWORD dwThreadId;
		    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
		    {
			hChildThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOThread2, (LPVOID)temp_sock, 0, &dwThreadId);
			if (hChildThread != NULL)
			    break;
			Sleep(CREATE_THREAD_SLEEP_TIME);
		    }
		    if (hChildThread == NULL)
		    {
			printf("Critical error: Unable to create an io thread\n");fflush(stdout);
			break;
		    }
		    FD_CLR(sockListen, &total_set);
		    sockListen = INVALID_SOCKET;
		    //printf("started new IO redirection thread\n");fflush(stdout);
		}
		else
		{
		    client_sock = easy_accept(sockListen);
		    if (client_sock == INVALID_SOCKET)
		    {
			int error = WSAGetLastError();
			printf("RedirectIOThread: baccept failed: %d\n", error);fflush(stdout);
			break;
		    }
		    
		    if (easy_receive(client_sock, &cType, sizeof(char)) == SOCKET_ERROR)
			return;
		    
		    if (cType == 0)
		    {
			HANDLE hThread;
			DWORD dwThreadID;
			for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
			{
			    hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectStdin, (void*)client_sock, 0, &dwThreadID);
			    if (hThread != NULL)
				break;
			    Sleep(CREATE_THREAD_SLEEP_TIME);
			}
			if (hThread == NULL)
			{
			    printf("Critical error: Standard input redirection thread creation failed. error %d\n", GetLastError());fflush(stdout);
			}
			else
			    CloseHandle(hThread);
		    }
		    else
		    {
			sockActive[nActive] = client_sock;
			FD_SET(client_sock, &total_set);
			nActive++;
			//printf("(+%d:%d)", nActive, bget_fd(client_sock));fflush(stdout);
		    }
		}
		n--;
	    }
	    if (n > 0)
	    {
		for (i=0; n > 0; i++)
		{
		    if (FD_ISSET(sockActive[i], &readset))
		    {
			char pTemp[sizeof(int)+sizeof(char)+sizeof(int)];
			num_read = easy_receive(sockActive[i], pTemp, sizeof(int)+sizeof(char)+sizeof(int));
			if (num_read == SOCKET_ERROR || num_read == 0)
			{
			    //printf("(-%d:%d)", nActive, bget_fd(sockActive[i]));fflush(stdout);
			    FD_CLR(sockActive[i], &total_set);
			    easy_closesocket(sockActive[i]);
			    nActive--;
			    sockActive[i] = sockActive[nActive];
			    i--;
			}
			else
			{
			    nDatalen = *(int*)pTemp;
			    cType = pTemp[sizeof(int)];
			    nRank = *(int*)&pTemp[sizeof(int)+sizeof(char)];
			    //printf("\nreceiving %d bytes from %d of type %d\n", nDatalen, nRank, (int)cType);fflush(stdout);
			    num_read = easy_receive(sockActive[i], pBuffer, nDatalen);
			    if (num_read == SOCKET_ERROR || num_read == 0)
			    {
				FD_CLR(sockActive[i], &total_set);
				easy_closesocket(sockActive[i]);
				nActive--;
				sockActive[i] = sockActive[nActive];
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
				if (g_bDoMultiColorOutput)
				{
				    WaitForSingleObject(g_hConsoleOutputMutex, INFINITE);
				    SetConsoleTextAttribute(hOut, aConsoleColorAttribute[nRank%NUM_OUTPUT_COLORS]);
				    //printf("(%d)", bget_fd(sockActive[i]));fflush(stdout);
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
				    if (!WriteFile(hOut, pBuffer, num_read, &num_written, NULL))
				    {
					printf("*** output lost ***\n");fflush(stdout);
				    }
				}
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

    if (child_abort_sock != INVALID_SOCKET)
    {
	//printf("signalling child threads to shut down\n");fflush(stdout);
	easy_send(child_abort_sock, "x", 1);
	WaitForSingleObject(g_hListenReleasedEvent, 10000);
	easy_closesocket(g_sockListen);
    }
    else if (sockListen != INVALID_SOCKET)
    {
	//printf("closing listen socket\n");fflush(stdout);
	easy_closesocket(sockListen);
    }
    for (i=0; i<nActive; i++)
    {
	//printf("closing io socket %d\n", i);fflush(stdout);
	easy_closesocket(sockActive[i]);
    }
    //printf("closing signal socket\n");fflush(stdout);
    easy_closesocket(signal_sock);
    if (hChildThread != NULL)
	CloseHandle(hChildThread);
    //printf("RedirectIOThread exiting\n");fflush(stdout);
}
