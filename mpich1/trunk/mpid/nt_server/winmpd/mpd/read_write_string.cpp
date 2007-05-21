#include "mpdimpl.h"
#include <stdio.h>

int ContextWriteString(MPD_Context *p, char *str)
{
    int ret_val;

    if (str == NULL)
    {
	if (p != NULL)
	    err_printf("ContextWriteString: Error, %s(%d) cannot write a NULL string.\n", ContextTypeToString(p), p->sock);
	else
	    err_printf("ContextWriteString: Error, NULL context and NULL string.\n");
	WSASetLastError(E_INVALIDARG);
	return SOCKET_ERROR;
    }
    if (p == NULL)
    {
	err_printf("ContextWriteString: Error, unable to write '%s' to NULL context", str);
	WSASetLastError(E_INVALIDARG);
	return SOCKET_ERROR;
    }

    WaitForSingleObject(p->hMutex, INFINITE);
    if (str == NULL)
    {
	dbg_printf("%s(%d) Wrote: '%s'\n", ContextTypeToString(p), p->sock, p->pszOut);
	ret_val = WriteString(p->sock, p->pszOut);
    }
    else
    {
	dbg_printf("%s(%d) Wrote: '%s'\n", ContextTypeToString(p), p->sock, str);
	ret_val = WriteString(p->sock, str);
    }
    ReleaseMutex(p->hMutex);
    return ret_val;
}

int PostContextRead(MPD_Context *p)
{
    int ret_val = 0;
    MPD_Context *pIter;
    bool bFound = false;

    if (p == NULL)
	return -1;
    EnterCriticalSection(&g_ContextCriticalSection);
    pIter = g_pList;
    while (pIter)
    {
	if (p == pIter)
	{
	    bFound = true;
	    break;
	}
	pIter = pIter->pNext;
    }
    //LeaveCriticalSection(&g_ContextCriticalSection);
    if (!bFound)
    {
	err_printf("PostContextRead: %s(%d): Error, PostContextRead called on a context not in the list.\n", ContextTypeToString(p), p->sock);
	LeaveCriticalSection(&g_ContextCriticalSection);
	ExitProcess(12345);
    }

    WaitForSingleObject(p->hMutex, INFINITE);
    if (p->bReadPosted)
    {
	err_printf("PostContextRead: %s(%d): Error, posting a read twice.\n", ContextTypeToString(p), p->sock);
	ReleaseMutex(p->hMutex);
	LeaveCriticalSection(&g_ContextCriticalSection);
	ExitProcess(54321);
    }
    if (p->bDeleted)
    {
	err_printf("PostContextRead: %s(%d): Error, posting a read on a deleted context.\n", ContextTypeToString(p), p->sock);
	ReleaseMutex(p->hMutex);
	LeaveCriticalSection(&g_ContextCriticalSection);
	ExitProcess(4444);
    }
    p->bReadPosted = true;
    p->ovl.Offset = 0;
    p->ovl.OffsetHigh = 0;
    if (!ReadFile((HANDLE)p->sock, p->pszIn, 1, &p->dwNumRead, &p->ovl))
    {
	ret_val = GetLastError();
	if (ret_val == ERROR_IO_PENDING)
	    ret_val = 0;
    }
    ReleaseMutex(p->hMutex);
    LeaveCriticalSection(&g_ContextCriticalSection);
    return ret_val;
}
