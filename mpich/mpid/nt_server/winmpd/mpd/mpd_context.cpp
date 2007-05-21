#include "mpdimpl.h"

CRITICAL_SECTION g_ContextCriticalSection;

WriteNode::WriteNode()
{ 
    pString = NULL; 
    nState = MPD_INVALID_LOWLEVEL; 
    pNext = NULL; 
}

WriteNode::WriteNode(char *p, MPD_LowLevelState n)
{
    pString = new char[strlen(p)+1];
    if (pString != NULL)
	strcpy(pString, p); 
    nState = n; 
    pNext = NULL; 
}

WriteNode::~WriteNode() 
{ 
    if (pString) 
	delete pString; 
    pString = NULL; 
}

MPD_Context::MPD_Context()
{
    nType = MPD_SOCKET; 
    sock = INVALID_SOCKET;
    ovl.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    hMutex = CreateMutex(NULL, FALSE, NULL);
    bReadPosted = false;
    bDeleted = false;
    pszHost[0] = '\0'; 
    pszIn[0] = '\0';
    pszOut[0] = '\0';
    nCurPos = 0; 
    nState = MPD_INVALID; 
    nLLState = MPD_INVALID_LOWLEVEL;
    bDeleteMe = false;
    pWriteList = NULL;
    bPassChecked = false;
    nConnectingState = MPD_INVALID_CONNECTING_STATE;
    bFileInitCalled = false;
    pszFileAccount[0] = '\0';
    pszFilePassword[0] = '\0';
    pNext = NULL; 
}

MPD_Context::~MPD_Context()
{
    nType = MPD_SOCKET; 
    if (sock != INVALID_SOCKET)
	easy_closesocket(sock);
    sock = INVALID_SOCKET; 
    CloseHandle(ovl.hEvent);
    CloseHandle(hMutex);
    bReadPosted = false;
    bDeleted = true;
    pszHost[0] = '\0'; 
    pszIn[0] = '\0';
    pszOut[0] = '\0';
    nCurPos = 0; 
    nState = MPD_INVALID; 
    nLLState = MPD_INVALID_LOWLEVEL;
    bDeleteMe = false;
    pWriteList = NULL;
    bPassChecked = false;
    nConnectingState = MPD_INVALID_CONNECTING_STATE;
    bFileInitCalled = false;
    pszFileAccount[0] = '\0';
    pszFilePassword[0] = '\0';
    pNext = NULL; 
}

MPD_Context* GetContext(SOCKET sock)
{
    MPD_Context *p = g_pList;
    while (p)
    {
	if (p->sock == sock)
	    return p;
	p = p->pNext;
    }
    return NULL;
}

void RemoveContext(MPD_Context *p)
{
    MPD_Context *pTrailer, *pIter;

    if (p == NULL)
	return;

    EnterCriticalSection(&g_ContextCriticalSection);

    if (p->bReadPosted)
	dbg_printf("RemoveContext: %s(%d): Error, removing context with a read posted.\n", ContextTypeToString(p), p->sock);

    if (p == g_pList)
    {
	if (p == g_pRightContext)
	    g_pRightContext = NULL;
	if (p == g_pLeftContext)
	    g_pLeftContext = NULL;
	g_pList = g_pList->pNext;
	LeaveCriticalSection(&g_ContextCriticalSection);
	dbg_printf("delete MPD_Context: 0x%p %s(%d)\n", p, ContextTypeToString(p), p->sock);
	delete p;
	return;
    }

    pTrailer = g_pList;
    pIter = g_pList->pNext;
    while (pIter)
    {
	if (pIter == p)
	{
	    if (p == g_pRightContext)
		g_pRightContext = NULL;
	    if (p == g_pLeftContext)
		g_pLeftContext = NULL;
	    pTrailer->pNext = pIter->pNext;
	    LeaveCriticalSection(&g_ContextCriticalSection);
	    dbg_printf("delete MPD_Context: 0x%p %s(%d)\n", p, ContextTypeToString(p), p->sock);
	    delete p;
	    return;
	}
	pIter = pIter->pNext;
	pTrailer = pTrailer->pNext;
    }

    if (p == g_pRightContext)
	g_pRightContext = NULL;
    if (p == g_pLeftContext)
	g_pLeftContext = NULL;

    LeaveCriticalSection(&g_ContextCriticalSection);

    dbg_printf("delete MPD_Context: 0x%p %s(%d) *** not in list ***\n", p, ContextTypeToString(p), p->sock);
    delete p;
}

void RemoveAllContexts()
{
    while (g_pList)
	RemoveContext(g_pList);
}

MPD_Context *CreateContext()
{
    MPD_Context *p;
    p = new MPD_Context();

    EnterCriticalSection(&g_ContextCriticalSection);
    p->pNext = g_pList;
    g_pList = p;
    LeaveCriticalSection(&g_ContextCriticalSection);

    dbg_printf("new    MPD_Context: 0x%p\n", p);
    return p;
}

void ContextInit()
{
    InitializeCriticalSection(&g_ContextCriticalSection);
}

void ContextFinalize()
{
    DeleteCriticalSection(&g_ContextCriticalSection);
}

void printLLState(FILE *fout, MPD_LowLevelState nLLState)
{
    switch (nLLState)
    {
    case MPD_WRITING_CMD:
	fprintf(fout, "MPD_WRITING_CMD");
	break;
    case MPD_WRITING_LAUNCH_CMD:
	fprintf(fout, "MPD_WRITING_LAUNCH_CMD");
	break;
    case MPD_WRITING_LAUNCH_RESULT:
	fprintf(fout, "MPD_WRITING_LAUNCH_RESULT");
	break;
    case MPD_WRITING_EXITCODE:
	fprintf(fout, "MPD_WRITING_EXITCODE");
	break;
    case MPD_WRITING_FIRST_EXITALL_CMD:
	fprintf(fout, "MPD_WRITING_FIRST_EXITALL_CMD");
	break;
    case MPD_WRITING_EXITALL_CMD:
	fprintf(fout, "MPD_WRITING_EXITALL_CMD");
	break;
    case MPD_WRITING_KILL_CMD:
	fprintf(fout, "MPD_WRITING_KILL_CMD");
	break;
    case MPD_WRITING_HOSTS_CMD:
	fprintf(fout, "MPD_WRITING_HOSTS_CMD");
	break;
    case MPD_WRITING_HOSTS_RESULT:
	fprintf(fout, "MPD_WRITING_HOSTS_RESULT");
	break;
    case MPD_WRITING_RESULT:
	fprintf(fout, "MPD_WRITING_RESULT");
	break;
    case MPD_READING_CMD:
	fprintf(fout, "MPD_READING_CMD");
	break;
    case MPD_READING_NEW_LEFT:
	fprintf(fout, "MPD_READING_NEW_LEFT");
	break;
    case MPD_WRITING_OLD_LEFT_HOST:
	fprintf(fout, "MPD_WRITING_OLD_LEFT_HOST");
	break;
    case MPD_WRITING_DONE_EXIT:
	fprintf(fout, "MPD_WRITING_DONE_EXIT");
	break;
    case MPD_WRITING_DONE:
	fprintf(fout, "MPD_WRITING_DONE");
	break;
    case MPD_WRITING_NEW_LEFT:
	fprintf(fout, "MPD_WRITING_NEW_LEFT");
	break;
    case MPD_READING_LEFT_HOST:
	fprintf(fout, "MPD_READING_LEFT_HOST");
	break;
    case MPD_WRITING_CONNECT_LEFT:
	fprintf(fout, "MPD_WRITING_CONNECT_LEFT");
	break;
    case MPD_WRITING_NEW_LEFT_HOST_EXIT:
	fprintf(fout, "MPD_WRITING_NEW_LEFT_HOST_EXIT");
	break;
    case MPD_WRITING_NEW_LEFT_HOST:
	fprintf(fout, "MPD_WRITING_NEW_LEFT_HOST");
	break;
    case MPD_READING_CONNECT_LEFT:
	fprintf(fout, "MPD_READING_CONNECT_LEFT");
	break;
    case MPD_READING_NEW_LEFT_HOST:
	fprintf(fout, "MPD_READING_NEW_LEFT_HOST");
	break;
    case MPD_WRITING_NEW_RIGHT:
	fprintf(fout, "MPD_WRITING_NEW_RIGHT");
	break;
    case MPD_READING_NEW_RIGHT:
	fprintf(fout, "MPD_READING_NEW_RIGHT");
	break;
    case MPD_AUTHENTICATE_READING_APPEND:
	fprintf(fout, "MPD_AUTHENTICATE_READING_APPEND");
	break;
    case MPD_AUTHENTICATE_WRITING_APPEND:
	fprintf(fout, "MPD_AUTHENTICATE_WRITING_APPEND");
	break;
    case MPD_AUTHENTICATE_READING_CRYPTED:
	fprintf(fout, "MPD_AUTHENTICATE_READING_CRYPTED");
	break;
    case MPD_AUTHENTICATE_WRITING_CRYPTED:
	fprintf(fout, "MPD_AUTHENTICATE_WRITING_CRYPTED");
	break;
    case MPD_AUTHENTICATE_READING_RESULT:
	fprintf(fout, "MPD_AUTHENTICATE_READING_RESULT");
	break;
    case MPD_AUTHENTICATE_WRITING_RESULT:
	fprintf(fout, "MPD_AUTHENTICATE_WRITING_RESULT");
	break;
    case MPD_AUTHENTICATED:
	fprintf(fout, "MPD_AUTHENTICATED");
	break;
    case MPD_INVALID_LOWLEVEL:
	fprintf(fout, "MPD_INVALID_LOWLEVEL");
	break;
    default:
	fprintf(fout, "%d - invalid state", nLLState);
	break;
    }
}

void MPD_Context::Print(FILE *fout)
{
    fprintf(fout, "{\n");
    fprintf(fout, " nType: ");
    switch (nType)
    {
    case MPD_SOCKET:
	fprintf(fout, "MPD_SOCKET\n");
	break;
    case MPD_LEFT_SOCKET:
	fprintf(fout, "MPD_LEFT_SOCKET\n");
	break;
    case MPD_RIGHT_SOCKET:
	fprintf(fout, "MPD_RIGHT_SOCKET\n");
	break;
    case MPD_CONSOLE_SOCKET:
	fprintf(fout, "MPD_CONSOLE_SOCKET\n");
	break;
    default:
	fprintf(fout, "%d - invalid type\n", nType);
	break;
    }
    if (sock == INVALID_SOCKET)
	fprintf(fout, " sock: INVALID_SOCKET, ");
    else
	fprintf(fout, " sock: %d, ", sock);
    fprintf(fout, "pszHost: '%s', ", pszHost);
    fprintf(fout, "nCurPos: %d, ", nCurPos);
    if (bDeleteMe)
	fprintf(fout, "bDeleteMe: true\n");
    else
	fprintf(fout, "bDeleteMe: false\n");
    fprintf(fout, " pszIn: '%s'\n", pszIn);
    fprintf(fout, " pszOut: '%s'\n", pszOut);
    fprintf(fout, " states: ");
    switch (nState)
    {
    case MPD_IDLE:
	fprintf(fout, "MPD_IDLE, ");
	break;
    case MPD_READING:
	fprintf(fout, "MPD_READING, ");
	break;
    case MPD_WRITING:
	fprintf(fout, "MPD_WRITING, ");
	break;
    case MPD_INVALID:
	fprintf(fout, "MPD_INVALID, ");
	break;
    default:
	fprintf(fout, "%d - invalid state, ", nState);
	break;
    }
    printLLState(fout, nLLState);
    fprintf(fout, "\n");
    if (pWriteList == NULL)
	fprintf(fout, " pWriteList: NULL\n");
    else
    {
	WriteNode *pNode;
	fprintf(fout, " pWriteList:\n");
	pNode = pWriteList;
	while (pNode)
	{
	    fprintf(fout, "  (");
	    printLLState(fout, pNode->nState);
	    fprintf(fout, ", '%s')\n", pNode->pString);
	    pNode = pNode->pNext;
	}
    }
    if (nConnectingState != MPD_INVALID_CONNECTING_STATE)
    {
	switch (nConnectingState)
	{
	case MPD_INSERTING:
	    fprintf(fout, " nConnectingState: MPD_INSERTING\n");
	    break;
	case MPD_CONNECTING_LEFT:
	    fprintf(fout, " nConnectingState: MPD_CONNECTING_LEFT\n");
	    break;
	default:
	    fprintf(fout, " nConnectingState: invalid - %d\n", nConnectingState);
	    break;
	}
    }
    fprintf(fout, "}\n");
}

void statContext(char *pszOutput, int length)
{
    FILE *fout;
    MPD_Context *p;

    fout = tmpfile();
    if (fout == NULL)
	return;

    fprintf(fout, "Contexts:\n");
    p = g_pList;
    while (p)
    {
	p->Print(fout);
	p = p->pNext;
    }
    if (g_pRightContext == NULL)
	fprintf(fout, " right context = NULL\n");
    if (g_pLeftContext == NULL)
	fprintf(fout, " left context = NULL\n");

    rewind(fout);
    fread(pszOutput, length, 1, fout);

    fclose(fout);

    rmtmp();
}

char *ContextTypeToString(MPD_Context *p)
{
    switch (p->nType)
    {
    case MPD_SOCKET:
	return "MPD_SOCKET";
    case MPD_LEFT_SOCKET:
	return "MPD_LEFT_SOCKET";
    case MPD_RIGHT_SOCKET:
	return "MPD_RIGHT_SOCKET";
    case MPD_CONSOLE_SOCKET:
	return "MPD_CONSOLE_SOCKET";
	break;
    }
    return "UNKNOWN_SOCKET";
}

void CheckContext(MPD_Context *p)
{
    if (p == g_pLeftContext)
	dbg_printf("MPD_Context ptr = g_pLeftContext\n");
    if (p == g_pRightContext)
	dbg_printf("MPD_Context ptr = g_pRightContext\n");
}
