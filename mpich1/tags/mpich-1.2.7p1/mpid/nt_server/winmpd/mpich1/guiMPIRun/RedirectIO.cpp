#include "stdafx.h"
#include "guiMPIRun.h"
#include "guiMPIRunDoc.h"
#include "guiMPIRunView.h"
#include "RedirectIO.h"
#include "global.h"
#include <stdio.h>
#include "mpdutil.h"

static CGuiMPIRunView *g_pDlg;
static HANDLE g_hConsoleOutputMutex;
static SOCKET g_sockListen;
static HANDLE g_hListenReleasedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

static void WriteOutputToRichEdit(char *pszStr, COLORREF color, CRichEditCtrl &edit)
{
    CHARFORMAT cf;
    int last;
    int nBefore;

    nBefore = edit.GetLineCount();

    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;
    cf.dwEffects = 0;
    last = -1;
    edit.SetSel(last, last);
    edit.ReplaceSel(pszStr);
    last = edit.GetTextLength();
    edit.SetSel(last - strlen(pszStr), last);
    edit.SetSelectionCharFormat(cf);
    edit.SetSel(-1, -1);
    edit.LineScroll(edit.GetLineCount() - nBefore);

    if (g_pDlg->m_redirect && g_pDlg->m_fout)
    {
	fprintf(g_pDlg->m_fout, "%s", pszStr);
	fflush(g_pDlg->m_fout);
    }
}

void RedirectRichEdit(SOCKET sock)
{
    HANDLE hEvent[3];
    DWORD dwResult;

    hEvent[0] = g_pDlg->m_hAbortEvent;
    hEvent[1] = g_pDlg->m_hJobFinished;
    hEvent[2] = g_pDlg->m_hRedirectStdinEvent;

    while (true)
    {
	dwResult = WaitForMultipleObjects(3, hEvent, FALSE, INFINITE);
	switch (dwResult)
	{
	case WAIT_OBJECT_0:
	case WAIT_OBJECT_0+1:
	    easy_closesocket(sock);
	    CloseHandle(g_pDlg->m_hRedirectRicheditThread);
	    g_pDlg->m_hRedirectRicheditThread = NULL;
	    return;
	    break;
	case WAIT_OBJECT_0+2:
	    if (WaitForSingleObject(g_pDlg->m_hRedirectStdinMutex, 10000) == WAIT_OBJECT_0)
	    {
		if (g_pDlg->m_pRedirectStdinList == NULL)
		{
		    easy_closesocket(sock);
		    ReleaseMutex(g_pDlg->m_hRedirectStdinMutex);
		    CloseHandle(g_pDlg->m_hRedirectRicheditThread);
		    g_pDlg->m_hRedirectRicheditThread = NULL;
		    return;
		}
		if (easy_send(sock, g_pDlg->m_pRedirectStdinList->str.GetBuffer(0), g_pDlg->m_pRedirectStdinList->str.GetLength()) == SOCKET_ERROR)
		{
		    easy_closesocket(sock);
		    ReleaseMutex(g_pDlg->m_hRedirectStdinMutex);
		    CloseHandle(g_pDlg->m_hRedirectRicheditThread);
		    g_pDlg->m_hRedirectRicheditThread = NULL;
		    return;
		}
		CGuiMPIRunView::RedirectStdinStruct *pNode = g_pDlg->m_pRedirectStdinList;
		g_pDlg->m_pRedirectStdinList = g_pDlg->m_pRedirectStdinList->pNext;
		delete pNode;
		if (g_pDlg->m_pRedirectStdinList == NULL)
		    ResetEvent(g_pDlg->m_hRedirectStdinEvent);
		ReleaseMutex(g_pDlg->m_hRedirectStdinMutex);
	    }
	    break;
	default:
	    easy_closesocket(sock);
	    CloseHandle(g_pDlg->m_hRedirectRicheditThread);
	    g_pDlg->m_hRedirectRicheditThread = NULL;
	    return;
	    break;
	}
    }
}

void RedirectIOThread2(SOCKET abort_sock)
{
    SOCKET client_sock, child_abort_sock = INVALID_SOCKET;
    int n, i;
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif
    BOOL b;
    char pBuffer[1024];
    DWORD num_read;
    fd_set total_set, readset;
    SOCKET sockActive[FD_SETSIZE];
    int nActive = 0;
    int nRank;
    char cType;
    int nDatalen;
    bool bDeleteOnEmpty = false;
    HANDLE hChildThread = NULL;
    int iter;
    
    FD_ZERO(&total_set);
    FD_SET(abort_sock, &total_set);
    FD_SET(g_sockListen, &total_set);

    while (true)
    {
	readset = total_set;
	n = select(0, &readset, NULL, NULL, NULL);
	if (n == SOCKET_ERROR)
	{
	    printf("RedirectIOControlThread2: bselect failed, error %d\n", WSAGetLastError());fflush(stdout);
	    easy_closesocket(abort_sock);
	    for (i=0; i<nActive; i++)
		easy_closesocket(sockActive[i]);
	    return;
	}
	if (n == 0)
	{
	    printf("RedirectIOControlThread2: bselect returned zero sockets available\n");fflush(stdout);
	    easy_closesocket(abort_sock);
	    for (i=0; i<nActive; i++)
		easy_closesocket(sockActive[i]);
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
			easy_closesocket(sockActive[i]);
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
	    if (FD_ISSET(g_sockListen, &readset))
	    {
		if ((nActive + 3) >= FD_SETSIZE)
		{
		    SOCKET temp_sock;
		    MakeLoop(&temp_sock, &child_abort_sock);
		    if (temp_sock == INVALID_SOCKET || child_abort_sock == INVALID_SOCKET)
		    {
			MessageBox(NULL, "Unable to create a socket", "Critical error", MB_OK);
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
			MessageBox(NULL, "Unable to create an io thread", "Critical error", MB_OK);
			break;
		    }
		    FD_CLR(g_sockListen, &total_set);
		}
		else
		{
		    client_sock = easy_accept(g_sockListen);
		    if (client_sock == INVALID_SOCKET)
		    {
			char str[256];
			int error = WSAGetLastError();
			sprintf(str, "RedirectIOControlThread: baccept failed: %d\n", error);
			MessageBox(NULL, str, "Error", MB_OK);
			break;
		    }
#ifdef USE_LINGER_SOCKOPT
		    linger.l_onoff = 1;
		    linger.l_linger = 60;
		    if (setsockopt(client_sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)) == SOCKET_ERROR)
		    {
			char str[256];
			int error = WSAGetLastError();
			sprintf(str, "RedirectIOControlThread: bsetsockopt failed: %d\n", error);
			MessageBox(NULL, str, "Error", MB_OK);
			easy_closesocket(client_sock);
			break;
		    }
#endif
		    b = TRUE;
		    setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));
		    
		    if (easy_receive(client_sock, &cType, sizeof(char)) == SOCKET_ERROR)
			break;
		    
		    if (cType == 0)
		    {
			DWORD dwThreadID;
			if (g_pDlg->m_hRedirectRicheditThread != NULL)
			    TerminateThread(g_pDlg->m_hRedirectRicheditThread, 0);
			for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
			{
			    g_pDlg->m_hRedirectRicheditThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectRichEdit, (void*)client_sock, 0, &dwThreadID);
			    if (g_pDlg->m_hRedirectRicheditThread != NULL)
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
				WaitForSingleObject(g_hConsoleOutputMutex, INFINITE);
				pBuffer[num_read] = '\0';
				if (g_pDlg->m_bNoColor)
				    WriteOutputToRichEdit(pBuffer, (COLORREF)0, g_pDlg->m_output);
				else
				    WriteOutputToRichEdit(pBuffer, aGlobalColor[nRank%NUM_GLOBAL_COLORS], g_pDlg->m_output);
				ReleaseMutex(g_hConsoleOutputMutex);
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
	easy_closesocket(sockActive[i]);
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

void RedirectIOThread(RedirectIOArg *pArg)
{
    SOCKET listen_sock, client_sock, signal_sock, /*abort_sock,*/ child_abort_sock = INVALID_SOCKET;
    int n, i;
    SOCKET sockStopIOSignalSocket;
    char pBuffer[1024];
    DWORD num_read;
    int nRank;
    char cType;
    int nDatalen;
    bool bDeleteOnEmpty = false;
    HANDLE hChildThread = NULL;
    int iter;

    try{
    // This is easier than passing these two arguments to all the io threads
    g_pDlg = pArg->pDlg;
    g_hConsoleOutputMutex = pArg->pDlg->m_hConsoleOutputMutex;

    // Create a listener
    if (easy_create(&listen_sock, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	char str[256];
	int error = WSAGetLastError();
	sprintf(str, "RedirectIOControlThread: easy_create listen socket failed: error %d\n", error);
	MessageBox(NULL, str, "Critical Error", MB_OK);
	easy_socket_finalize();
	ExitProcess(error);
    }
    listen(listen_sock, 5);
    easy_get_sock_info(listen_sock, pArg->pDlg->m_pszIOHost, &pArg->pDlg->m_nIOPort);
    easy_get_ip_string(pArg->pDlg->m_pszIOHost, pArg->pDlg->m_pszIOHost);
    g_sockListen = listen_sock;

    // Connect a stop socket to myself
    if (easy_create(&pArg->pDlg->m_sockStopIOSignalSocket, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	char str[256];
	int error = WSAGetLastError();
	sprintf(str, "easy_create(m_sockStopIOSignalSocket) failed, error %d\n", error);fflush(stdout);
	MessageBox(NULL, str, "Critical Error", MB_OK);
	ExitProcess(error);
    }
    if (easy_connect(pArg->pDlg->m_sockStopIOSignalSocket, pArg->pDlg->m_pszIOHost, pArg->pDlg->m_nIOPort) == SOCKET_ERROR)
    {
	char str[256];
	int error = WSAGetLastError();
	sprintf(str, "easy_connect(m_sockStopIOSignalSocket, %s, %d) failed, error %d\n", pArg->pDlg->m_pszIOHost, pArg->pDlg->m_nIOPort, error);
	MessageBox(NULL, str, "Critical Error", MB_OK);
	ExitProcess(error);
    }
    sockStopIOSignalSocket = pArg->pDlg->m_sockStopIOSignalSocket;

    // Accept the connection from myself
    signal_sock = easy_accept(listen_sock);

    SetEvent(pArg->hReadyEvent); // The waiting thread will delete pArg, so don't use it again after this statement
    pArg = NULL;
    
    fd_set total_set, readset;
    SOCKET sockActive[FD_SETSIZE];
    int nActive = 0;
    
    FD_ZERO(&total_set);
    
    FD_SET(listen_sock, &total_set);
    FD_SET(signal_sock, &total_set);

    while (true)
    {
	readset = total_set;
	n = select(0, &readset, NULL, NULL, NULL);
	if (n == SOCKET_ERROR)
	{
	    printf("RedirectIOControlThread: bselect failed, error %d\n", WSAGetLastError());fflush(stdout);
	    break;
	}
	if (n == 0)
	{
	    printf("RedirectIOControlThread: bselect returned zero sockets available\n");fflush(stdout);
	    break;
	}
	else
	{
	    if (FD_ISSET(signal_sock, &readset))
	    {
		char c;
		bool bAbortNow = true;
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
		if (bAbortNow)
		{
		    break;
		}
		n--;
	    }
	    if (FD_ISSET(listen_sock, &readset))
	    {
		if ((nActive + 3) >= FD_SETSIZE)
		{
		    SOCKET temp_sock;
		    MakeLoop(&temp_sock, &child_abort_sock);
		    if (temp_sock == INVALID_SOCKET || child_abort_sock == INVALID_SOCKET)
		    {
			MessageBox(NULL, "Unable to create a socket", "Critical error", MB_OK);
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
			MessageBox(NULL, "Unable to create an io thread", "Critical error", MB_OK);
			break;
		    }
		    FD_CLR(g_sockListen, &total_set);
		    listen_sock = INVALID_SOCKET;
		}
		else
		{
		    client_sock = easy_accept(listen_sock);
		    if (client_sock == INVALID_SOCKET)
		    {
			char str[256];
			int error = WSAGetLastError();
			sprintf(str, "RedirectIOControlThread: baccept failed: %d\n", error);
			MessageBox(NULL, str, "Error", MB_OK);
			break;
		    }
		    
		    if (easy_receive(client_sock, &cType, sizeof(char)) == SOCKET_ERROR)
			return;
		    
		    if (cType == 0)
		    {
			DWORD dwThreadID;
			if (g_pDlg->m_hRedirectRicheditThread != NULL)
			    TerminateThread(g_pDlg->m_hRedirectRicheditThread, 0);
			for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
			{
			    g_pDlg->m_hRedirectRicheditThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectRichEdit, (void*)client_sock, 0, &dwThreadID);
			    if (g_pDlg->m_hRedirectRicheditThread != NULL)
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
				WaitForSingleObject(g_hConsoleOutputMutex, INFINITE);
				pBuffer[num_read] = '\0';
				if (g_pDlg->m_bNoColor)
				    WriteOutputToRichEdit(pBuffer, (COLORREF)0, g_pDlg->m_output);
				else
				    WriteOutputToRichEdit(pBuffer, aGlobalColor[nRank%NUM_GLOBAL_COLORS], g_pDlg->m_output);
				ReleaseMutex(g_hConsoleOutputMutex);
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
    for (i=0; i<nActive; i++)
	easy_closesocket(sockActive[i]);
    easy_closesocket(signal_sock);
    if (listen_sock != INVALID_SOCKET)
	easy_closesocket(listen_sock);
    if (hChildThread != NULL)
	CloseHandle(hChildThread);
    }catch(...)
    {
	MessageBox(NULL, "Unhandled exception caught in RedirectIOThread", "Error", MB_OK);
    }
}
