#include "mpdimpl.h"
#include "database.h"

#define EXIT_WORKER_KEY	-1

int g_NumCommPortThreads = 4;
HANDLE g_hCommPort;
HANDLE g_hCommPortEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

BOOL RunRead(MPD_Context *p, int *pRunReturn);
BOOL RunWrite(MPD_Context *p, int *pRunReturn);

void ErrorExit(char *str, int exitcode = -1)
{
    err_printf("****%s", str);
    ContextFinalize();
    dbs_finalize();
    easy_socket_finalize();
    err_printf("****EXITING\n");
    ExitProcess(exitcode);
}

BOOL RunRead(MPD_Context *p, int *pRunReturn)
{
    int error;
    
    if (p->pszIn[0] == '\0')
    {
	err_printf("RunRead: %s(%d): Error, empty string read.\n", ContextTypeToString(p), p->sock);
	p->bDeleteMe = true;
	p->nState = MPD_INVALID;
	return TRUE;
    }

    p->nState = MPD_READING;
    if (ReadString(p->sock, &p->pszIn[1]))
    {
	//dbg_printf("RunRead: '%s'\n", p->pszIn);
	p->nState = MPD_IDLE;
	p->nCurPos = 0;
	switch(p->nType)
	{
	case MPD_SOCKET:
	    err_printf("RunRead: Error, MPD_SOCKET read a string '%s'.\n", p->pszIn);
	    break;
	case MPD_LEFT_SOCKET:
	    //dbg_printf("__left socket read '%s'\n", p->pszIn);
	    HandleLeftRead(p);
	    break;
	case MPD_RIGHT_SOCKET:
	    //err_printf("RunRead: Error, MPD_RIGHT_SOCKET read '%s'\n", p->pszIn);
	    HandleRightRead(p);
	    break;
	case MPD_CONSOLE_SOCKET:
	    //dbg_printf("__console socket read '%s'\n", p->pszIn);
	    HandleConsoleRead(p);
	    break;
	default:
	    err_printf("string '%s' read on socket %d of unknown type %d\n", p->pszIn, p->sock, p->nType);
	    break;
	}
    }
    else
    {
	error = WSAGetLastError();
	err_printf("RunRead: ReadString failed for %s(%d), error %d\n", ContextTypeToString(p), p->sock, error);
	p->bDeleteMe = true;
	p->nState = MPD_INVALID;
    }

    return TRUE;
}

void RunWorkerThread()
{
    DWORD dwKey, nBytes;
    OVERLAPPED *p_Ovl;
    int error;
    MPD_Context *pContext;
    int ret_val;
    
    while (true)
    {
	if (GetQueuedCompletionStatus(g_hCommPort, &nBytes, &dwKey, &p_Ovl, INFINITE))
	{
	    //dbg_printf("RunWorkerThread::%d bytes\n", nBytes);
	    if (dwKey == EXIT_WORKER_KEY)
		ExitThread(0);
	    pContext = (MPD_Context*)dwKey;
	    if (nBytes)
	    {
		if (nBytes == 1)
		{
		    pContext->bReadPosted = false;
		    if (!RunRead(pContext, &ret_val))
			ErrorExit("RunRead returned FALSE", ret_val);

		    if (pContext->bDeleteMe)
		    {
			CheckContext(pContext);
			RemoveContext(pContext);
			pContext = NULL;
		    }
		    else
		    {
			// post the next read
			error = PostContextRead(pContext);
			if (error)
			{
			    if (error == ERROR_NETNAME_DELETED || error == ERROR_IO_PENDING || error == WSAECONNABORTED)
				dbg_printf("RunWorkerThread:Post read for %s(%d) failed, error %d\n", ContextTypeToString(pContext), pContext->sock, error);
			    else
				err_printf("RunWorkerThread:Post read for %s(%d) failed, error %d\n", ContextTypeToString(pContext), pContext->sock, error);
			    CheckContext(pContext);
			    RemoveContext(pContext);
			    pContext = NULL;
			}
		    }
		}
		else
		{
		    dbg_printf("RunWorkerThread: nBytes = %d, *** unexpected ***\n", nBytes);
		    error = PostContextRead(pContext);
		    if (error)
		    {
			err_printf("RunWorkerThread:Post read for %s(%d) failed, error %d\n", ContextTypeToString(pContext), pContext->sock, error);
			CheckContext(pContext);
			RemoveContext(pContext);
			pContext = NULL;
		    }
		}
	    }
	    else
	    {
		dbg_printf("RunWorkerThread::closing context %s(%d)\n", ContextTypeToString(pContext), pContext->sock);
		CheckContext(pContext);
		RemoveContext(pContext);
		pContext = NULL;
	    }
	}
	else
	{
	    error = GetLastError();
	    if (error == ERROR_NETNAME_DELETED || error == ERROR_IO_PENDING || error == WSAECONNABORTED)
	    {
		dbg_printf("RunWorkerThread: GetQueuedCompletionStatus failed, error %d\n", error);
	    }
	    else
	    {
		err_printf("RunWorkerThread: GetQueuedCompletionStatus failed, error %d\n", error);
	    }
	    //return;
	}
    }
}

// Run ///////////////////////////////////////////////////////////////////////
//
//
int Run()
{
    SOCKET listen_socket;
    HANDLE ahEvent[2];
    int error = 0, num_handles=2;
    SOCKET temp_socket;
    DWORD ret_val;
    int i;
    BOOL opt;
    DWORD dwThreadID;
    char host[100];
    int listen_port;
    HANDLE *hWorkers;
    int iter;

    easy_get_ip(&g_nIP);
    easy_get_ip_string(g_pszIP);

    if (ConnectToSelf() == false)
	ErrorExit("Run: ConnectToSelf failed\n");

    if (!g_bStartAlone)
    {
	if (stricmp(g_pszHost, g_pszInsertHost))
	{
	    if (InsertIntoRing(g_pszInsertHost, false) == false)
	    {
		if (stricmp(g_pszHost, g_pszInsertHost2))
		    InsertIntoRing(g_pszInsertHost2, false);
	    }
	}
    }

    ahEvent[0] = g_hCommPortEvent;

    // create a listening socket
    if (error = easy_create(&listen_socket, g_nPort))
	ErrorExit("Run: easy_create(listen socket) failed", error);

    ahEvent[1] = WSACreateEvent();

    // associate listen_socket_event with listen_socket
    if (WSAEventSelect(listen_socket, ahEvent[1], FD_ACCEPT) == SOCKET_ERROR)
	ErrorExit("Run: WSAEventSelect failed for listen_socket", 1);

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR)
	ErrorExit("Run: listen failed", WSAGetLastError());

    // get the port and local hostname for the listening socket
    if (error = easy_get_sock_info(listen_socket, host, &listen_port))
	ErrorExit("Run: Unable to get host and port of listening socket", error);
    dbg_printf("%s:%d\n", host, listen_port);

    // Create the completion port
    g_hCommPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, g_NumCommPortThreads);
    if (g_hCommPort == NULL)
	ErrorExit("Run: CreateIoCompletionPort failed", GetLastError());

    // Start the completion port threads
    hWorkers = new HANDLE[g_NumCommPortThreads];
    for (i=0; i<g_NumCommPortThreads; i++)
    {
	for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
	{
	    hWorkers[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RunWorkerThread, NULL, 0, &dwThreadID);
	    if (hWorkers[i] != NULL)
		break;
	    Sleep(CREATE_THREAD_SLEEP_TIME);
	}
	if (hWorkers[i] == NULL)
	    ErrorExit("Run: CreateThread(RunWorkerThread) failed", GetLastError());
    }

    // associate the left and right contexts with the completion port
    if (CreateIoCompletionPort((HANDLE)g_pRightContext->sock, g_hCommPort, (DWORD)g_pRightContext, g_NumCommPortThreads) == NULL)
	ErrorExit("Run: Unable to associate completion port with socket", GetLastError());
    if (CreateIoCompletionPort((HANDLE)g_pLeftContext->sock, g_hCommPort, (DWORD)g_pLeftContext, g_NumCommPortThreads) == NULL)
	ErrorExit("Run: Unable to associate completion port with socket", GetLastError());
    // post the first reads on the left and right contexts
    error = PostContextRead(g_pRightContext);
    if (error)
	ErrorExit("Run:First posted read for g_pRightContext failed, error %d", error);
    error = PostContextRead(g_pLeftContext);
    if (error)
	ErrorExit("Run:First posted read for g_pLeftContext failed, error %d", error);

    // loop, accepting new connections, until g_hCommPortEvent is signalled
    while (true)
    {
	ret_val = WaitForMultipleObjects(num_handles, ahEvent, FALSE, INFINITE);
	if (ret_val != WAIT_OBJECT_0 && ret_val != WAIT_OBJECT_0+1)
	{
	    ErrorExit("Run: Wait failed, restarting mpd...", GetLastError());
	    return RUN_RESTART;
	}

	// Event[0] is the event used by the ServiceStop thread to communicate with this thread
	if (WaitForSingleObject(ahEvent[0], 0) == WAIT_OBJECT_0)
	{
	    dbg_printf("Run exiting\n");
	    for (i=0; i<g_NumCommPortThreads; i++)
		PostQueuedCompletionStatus(g_hCommPort, 0, EXIT_WORKER_KEY, NULL);
	    for (i=0; i<g_NumCommPortThreads; i++)
	    {
		WaitForSingleObject(hWorkers[i], INFINITE);
		CloseHandle(hWorkers[i]);
	    }
	    delete hWorkers;
	    CloseHandle(g_hCommPortEvent); 
	    CloseHandle(g_hCommPort);
	    closesocket(listen_socket);
	    WSACloseEvent(ahEvent[1]);

	    // cleanup everything
	    ShutdownAllProcesses();
	    AbortAllForwarders();
	    RemoveAllTmpFiles();
	    RemoveAllCachedUsers();

	    return RUN_EXIT;
	}

	// Event[1] is the listen socket event, which is signalled when other processes whish to establish a socket connection with this mpd
	if (WaitForSingleObject(ahEvent[1], 0) == WAIT_OBJECT_0)
	{
	    i=0;
	    opt = TRUE;
	    // Something in my code is causing the listen_socket event to fail to be reset by the accept call.
	    // For now I manually reset it here.
	    WSAResetEvent(ahEvent[1]);

	    temp_socket = accept(listen_socket, NULL, NULL);
	    if (temp_socket == INVALID_SOCKET)
		ErrorExit("Run: accept failed", WSAGetLastError());
	    dbg_printf("socket accepted: %d\n", temp_socket);
	    if (setsockopt(temp_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(BOOL)) == SOCKET_ERROR)
		ErrorExit("Run: setsockopt failed", WSAGetLastError());

	    MPD_Context *pContext = CreateContext();
	    pContext->nLLState = MPD_AUTHENTICATE_WRITING_APPEND;
	    pContext->sock = temp_socket;

	    if (AuthenticateAcceptedConnection(&pContext))
	    {
		// Associate the socket with the completion port
		if (CreateIoCompletionPort((HANDLE)temp_socket, g_hCommPort, (DWORD)pContext, g_NumCommPortThreads) == NULL)
		    ErrorExit("Run: Unable to associate completion port with socket", GetLastError());
		
		// Post the first read from the socket
		error = PostContextRead(pContext);
		if (error)
		{
		    ErrorExit("Run: First posted read failed, error %d", error);
		}
	    }
	    else
	    {
		dbg_printf("Run: AuthenticateConnection failed.\n");
	    }
	}
    }
}
