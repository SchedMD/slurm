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
    int i;
    nType = MPD_SOCKET; 
    sock = INVALID_SOCKET;
    for (i=0; i<CREATE_OBJECT_RETRIES; i++)
    {
	ovl.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (ovl.hEvent != NULL)
	    break;
	Sleep(CREATE_OBJECT_SLEEP_TIME);
    }
    if (ovl.hEvent == NULL)
    {
	hMutex = NULL;
	this->~MPD_Context();
	return;
    }
    for (i=0; i<CREATE_OBJECT_RETRIES; i++)
    {
	hMutex = CreateMutex(NULL, FALSE, NULL);
	if (hMutex != NULL)
	    break;
	Sleep(CREATE_OBJECT_SLEEP_TIME);
    }
    if (hMutex == NULL)
    {
	this->~MPD_Context();
	return;
    }
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
    if (ovl.hEvent != NULL)
	CloseHandle(ovl.hEvent);
    if (hMutex != NULL)
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
	    pTrailer->pNext = pIter->pNext;
	    LeaveCriticalSection(&g_ContextCriticalSection);
	    dbg_printf("delete MPD_Context: 0x%p %s(%d)\n", p, ContextTypeToString(p), p->sock);
	    delete p;
	    return;
	}
	pIter = pIter->pNext;
	pTrailer = pTrailer->pNext;
    }

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
    case MPD_WRITING_DONE_EXIT:
	fprintf(fout, "MPD_WRITING_DONE_EXIT");
	break;
    case MPD_WRITING_DONE:
	fprintf(fout, "MPD_WRITING_DONE");
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

int printLLState(char *str, int n, MPD_LowLevelState nLLState)
{
    int ret_val;
    switch (nLLState)
    {
    case MPD_WRITING_CMD:
	ret_val = _snprintf(str, n, "MPD_WRITING_CMD");
	break;
    case MPD_WRITING_LAUNCH_CMD:
	ret_val = _snprintf(str, n, "MPD_WRITING_LAUNCH_CMD");
	break;
    case MPD_WRITING_LAUNCH_RESULT:
	ret_val = _snprintf(str, n, "MPD_WRITING_LAUNCH_RESULT");
	break;
    case MPD_WRITING_EXITCODE:
	ret_val = _snprintf(str, n, "MPD_WRITING_EXITCODE");
	break;
    case MPD_WRITING_FIRST_EXITALL_CMD:
	ret_val = _snprintf(str, n, "MPD_WRITING_FIRST_EXITALL_CMD");
	break;
    case MPD_WRITING_EXITALL_CMD:
	ret_val = _snprintf(str, n, "MPD_WRITING_EXITALL_CMD");
	break;
    case MPD_WRITING_KILL_CMD:
	ret_val = _snprintf(str, n, "MPD_WRITING_KILL_CMD");
	break;
    case MPD_WRITING_HOSTS_CMD:
	ret_val = _snprintf(str, n, "MPD_WRITING_HOSTS_CMD");
	break;
    case MPD_WRITING_HOSTS_RESULT:
	ret_val = _snprintf(str, n, "MPD_WRITING_HOSTS_RESULT");
	break;
    case MPD_WRITING_RESULT:
	ret_val = _snprintf(str, n, "MPD_WRITING_RESULT");
	break;
    case MPD_READING_CMD:
	ret_val = _snprintf(str, n, "MPD_READING_CMD");
	break;
    case MPD_WRITING_DONE_EXIT:
	ret_val = _snprintf(str, n, "MPD_WRITING_DONE_EXIT");
	break;
    case MPD_WRITING_DONE:
	ret_val = _snprintf(str, n, "MPD_WRITING_DONE");
	break;
    case MPD_AUTHENTICATE_READING_APPEND:
	ret_val = _snprintf(str, n, "MPD_AUTHENTICATE_READING_APPEND");
	break;
    case MPD_AUTHENTICATE_WRITING_APPEND:
	ret_val = _snprintf(str, n, "MPD_AUTHENTICATE_WRITING_APPEND");
	break;
    case MPD_AUTHENTICATE_READING_CRYPTED:
	ret_val = _snprintf(str, n, "MPD_AUTHENTICATE_READING_CRYPTED");
	break;
    case MPD_AUTHENTICATE_WRITING_CRYPTED:
	ret_val = _snprintf(str, n, "MPD_AUTHENTICATE_WRITING_CRYPTED");
	break;
    case MPD_AUTHENTICATE_READING_RESULT:
	ret_val = _snprintf(str, n, "MPD_AUTHENTICATE_READING_RESULT");
	break;
    case MPD_AUTHENTICATE_WRITING_RESULT:
	ret_val = _snprintf(str, n, "MPD_AUTHENTICATE_WRITING_RESULT");
	break;
    case MPD_AUTHENTICATED:
	ret_val = _snprintf(str, n, "MPD_AUTHENTICATED");
	break;
    case MPD_INVALID_LOWLEVEL:
	ret_val = _snprintf(str, n, "MPD_INVALID_LOWLEVEL");
	break;
    default:
	ret_val = _snprintf(str, n, "%d - invalid state", nLLState);
	break;
    }
    return ret_val;
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
    fprintf(fout, "}\n");
}

int MPD_Context::Print(char *str, int length)
{
    int n;
    int len_orig = length;
    n = _snprintf(str, length, "{\n");
    str += n; length -= n;
    n = _snprintf(str, length, " nType: ");
    str += n; length -= n;
    switch (nType)
    {
    case MPD_SOCKET:
	n = _snprintf(str, length, "MPD_SOCKET\n");
	str += n; length -= n;
	break;
    case MPD_CONSOLE_SOCKET:
	n = _snprintf(str, length, "MPD_CONSOLE_SOCKET\n");
	str += n; length -= n;
	break;
    default:
	n = _snprintf(str, length, "%d - invalid type\n", nType);
	str += n; length -= n;
	break;
    }
    if (sock == INVALID_SOCKET)
    {
	n = _snprintf(str, length, " sock: INVALID_SOCKET, ");
	str += n; length -= n;
    }
    else
    {
	n = _snprintf(str, length, " sock: %d, ", sock);
	str += n; length -= n;
    }
    n = _snprintf(str, length, "pszHost: '%s', ", pszHost);
    str += n; length -= n;
    n = _snprintf(str, length, "nCurPos: %d, ", nCurPos);
    str += n; length -= n;
    if (bDeleteMe)
    {
	n = _snprintf(str, length, "bDeleteMe: true\n");
	str += n; length -= n;
    }
    else
    {
	n = _snprintf(str, length, "bDeleteMe: false\n");
	str += n; length -= n;
    }
    n = _snprintf(str, length, " pszIn: '%s'\n", pszIn);
    str += n; length -= n;
    n = _snprintf(str, length, " pszOut: '%s'\n", pszOut);
    str += n; length -= n;
    n = _snprintf(str, length, " states: ");
    str += n; length -= n;
    switch (nState)
    {
    case MPD_IDLE:
	n = _snprintf(str, length, "MPD_IDLE, ");
	str += n; length -= n;
	break;
    case MPD_READING:
	n = _snprintf(str, length, "MPD_READING, ");
	str += n; length -= n;
	break;
    case MPD_WRITING:
	n = _snprintf(str, length, "MPD_WRITING, ");
	str += n; length -= n;
	break;
    case MPD_INVALID:
	n = _snprintf(str, length, "MPD_INVALID, ");
	str += n; length -= n;
	break;
    default:
	n = _snprintf(str, length, "%d - invalid state, ", nState);
	str += n; length -= n;
	break;
    }
    n = printLLState(str, length, nLLState);
    str += n; length -= n;
    n = _snprintf(str, length, "\n");
    str += n; length -= n;
    if (pWriteList == NULL)
    {
	n = _snprintf(str, length, " pWriteList: NULL\n");
	str += n; length -= n;
    }
    else
    {
	WriteNode *pNode;
	n = _snprintf(str, length, " pWriteList:\n");
	str += n; length -= n;
	pNode = pWriteList;
	while (pNode)
	{
	    n = _snprintf(str, length, "  (");
	    str += n; length -= n;
	    n = printLLState(str, length, pNode->nState);
	    str += n; length -= n;
	    n = _snprintf(str, length, ", '%s')\n", pNode->pString);
	    str += n; length -= n;
	    pNode = pNode->pNext;
	}
    }
    n = _snprintf(str, length, "}\n");
    str += n; length -= n;
    return len_orig - length;
}

void statContext(char *pszOutput, int length)
{
    int n;
    MPD_Context *p;
    p = g_pList;
    while (p)
    {
	n = p->Print(pszOutput, length);
	pszOutput += n; length -= n;
	p = p->pNext;
    }
}

char *ContextTypeToString(MPD_Context *p)
{
    switch (p->nType)
    {
    case MPD_SOCKET:
	return "MPD_SOCKET";
    case MPD_CONSOLE_SOCKET:
	return "MPD_CONSOLE_SOCKET";
	break;
    }
    return "UNKNOWN_SOCKET";
}
