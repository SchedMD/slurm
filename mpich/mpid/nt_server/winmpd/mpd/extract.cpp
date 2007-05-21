#include "mpdimpl.h"

bool Extract(bool bReConnect)
{
    char pszStr[256];

    // Note: Calling Extract with bReConnect = false will cause the mpd to exit
    // after the extract operation finishes.

    // check if left and right contexts are invalid
    if (g_pLeftContext == NULL || g_pRightContext == NULL)
    {
	if (bReConnect)
	{
	    if (!ConnectToSelf())
	    {
		err_printf("Extract: ConnectToSelf failed\n");
		return false;
	    }
	    if (CreateIoCompletionPort((HANDLE)g_pLeftContext->sock, g_hCommPort, (DWORD)g_pLeftContext, g_NumCommPortThreads) == NULL)
	    {
		err_printf("Extract: Unable to associate completion port with new left socket, error %d\n", GetLastError());
		RemoveContext(g_pLeftContext);
		return false;
	    }
	    if (CreateIoCompletionPort((HANDLE)g_pRightContext->sock, g_hCommPort, (DWORD)g_pRightContext, g_NumCommPortThreads) == NULL)
	    {
		err_printf("Extract: Unable to associate completion port with new right socket, error %d\n", GetLastError());
		RemoveContext(g_pRightContext);
		return false;
	    }
	    PostContextRead(g_pLeftContext);
	    PostContextRead(g_pRightContext);
	    return true;
	}
	return false;
    }

    // check to see if this mpd is connected to itself
    if (strcmp(g_pLeftContext->pszHost, g_pszHost) == 0)
    {
	if (strcmp(g_pRightContext->pszHost, g_pszHost))
	{
	    // left connected to myself but right is not.  This is illegal.
	    err_printf("Extract: invalid state: g_pszHost = %s, pszLeftHost = %s, pszRightHost = %s\n", g_pszHost, 
		g_pLeftContext->pszHost, g_pRightContext->pszHost);
	    return false;
	}
	if (!bReConnect)
	{
	    RemoveContext(g_pLeftContext);
	    g_pLeftContext = NULL;
	    RemoveContext(g_pRightContext);
	    g_pRightContext = NULL;
	    SignalExit();
	    SignalExit();
	}
	return true;
    }
    if (strcmp(g_pRightContext->pszHost, g_pszHost) == 0)
    {
	// right connected to myself but left is not.  This is illegal.
	err_printf("Extract: invalid state: g_pszHost = %s, pszLeftHost = %s, pszRightHost = %s\n", g_pszHost, 
	    g_pLeftContext->pszHost, g_pRightContext->pszHost);
	return false;
    }

    // send "done bounce" before "connect left ..." to guarantee that g_pLeftContext is the right one to send to
    dbg_printf("Extract: sending 'done bounce'\n");
    ContextWriteString(g_pLeftContext, "done bounce");
    _snprintf(pszStr, 256, "connect left %s", g_pLeftContext->pszHost);
    dbg_printf("Extract: sending '%s'\n", pszStr);
    ContextWriteString(g_pRightContext, pszStr);

    if (bReConnect)
    {
	dbg_printf("Extract: calling ConnectToSelf\n");
	if (!ConnectToSelf())
	{
	    err_printf("Extract: ConnectToSelf failed\n");
	    return false;
	}
	if (CreateIoCompletionPort((HANDLE)g_pLeftContext->sock, g_hCommPort, (DWORD)g_pLeftContext, g_NumCommPortThreads) == NULL)
	{
	    err_printf("Extract: Unable to associate completion port with new left socket, error %d\n", GetLastError());
	    RemoveContext(g_pLeftContext);
	    return false;
	}
	if (CreateIoCompletionPort((HANDLE)g_pRightContext->sock, g_hCommPort, (DWORD)g_pRightContext, g_NumCommPortThreads) == NULL)
	{
	    err_printf("Extract: Unable to associate completion port with new right socket, error %d\n", GetLastError());
	    RemoveContext(g_pRightContext);
	    return false;
	}
	PostContextRead(g_pLeftContext);
	PostContextRead(g_pRightContext);
    }
    else
    {
	SignalExit();
	SignalExit();
    }

    return true;
}
