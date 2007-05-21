#include "mpdimpl.h"

bool InsertIntoRing(char *pszHost, bool bPostRead /*= true*/)
{
    char temp_host[MAX_HOST_LENGTH];
    char str[14 + MAX_HOST_LENGTH];

    if (pszHost == NULL || pszHost[0] == '\0')
    {
	//err_printf("InsertIntoRing called with no host name\n");
	return false;
    }

    dbg_printf("InsertIntoRing: inserting at '%s'\n", pszHost);

    MPD_Context *pContext = CreateContext();
    pContext->nType = MPD_RIGHT_SOCKET;

    easy_create(&pContext->sock);
    if (easy_connect(pContext->sock, pszHost, g_nPort) == SOCKET_ERROR)
    {
	err_printf("InsertIntoRing: easy_connect(%d, %s:%d) failed, error %d\n", pContext->sock, pszHost, g_nPort, WSAGetLastError());
	RemoveContext(pContext);
	return false;
    }
    strncpy(pContext->pszHost, pszHost, MAX_HOST_LENGTH);
    pContext->pszHost[MAX_HOST_LENGTH-1] = '\0';

    dbg_printf("InsertIntoRing::authenticating connection.\n");
    if (!AuthenticateConnectedConnection(&pContext))
    {
	err_printf("InsertIntoRing: Authentication with '%s:%d' failed\n", pszHost, g_nPort);
	return false;
    }

    // indicate that this is a "left" connection from the remote end's point of view
    _snprintf(pContext->pszOut, 256, "left %s", g_pszHost);
    dbg_printf("InsertIntoRing:: writing '%s' to %s\n", pContext->pszOut, pszHost);
    if (ContextWriteString(pContext, pContext->pszOut) == SOCKET_ERROR)
    {
	err_printf("InsertIntoRing: sending '%s' command failed, error %d\n", pContext->pszOut, WSAGetLastError());
	RemoveContext(pContext);
	return false;
    }

    // send the "new left" command
    dbg_printf("InsertIntoRing:: writing 'new left' to %s\n", pszHost);
    if (ContextWriteString(pContext, "new left") == SOCKET_ERROR)
    {
	err_printf("InsertIntoRing: sending 'new left' command failed, error %d\n", pContext->pszOut, WSAGetLastError());
	RemoveContext(pContext);
	return false;
    }

    // read the old left host
    dbg_printf("InsertIntoRing:: reading the old left host from %s\n", pszHost);
    if (!ReadString(pContext->sock, temp_host))
    {
	err_printf("InsertIntoRing: reading left host failed, error %d\n", WSAGetLastError());
	RemoveContext(pContext);
	return false;
    }

    // send the "connect left" command
    _snprintf(str, MAX_HOST_LENGTH, "connect left %s", temp_host);
    dbg_printf("InsertIntoRing:: writing '%s' to %s\n", str, g_pRightContext->pszHost);
    if (ContextWriteString(g_pRightContext, str) == SOCKET_ERROR)
    {
	err_printf("InsertIntoRing: sending '%s' command failed, error %d\n", str, WSAGetLastError());
	RemoveContext(pContext);
	return false;
    }

    // save the context as this mpd's right context
    g_pRightContext = pContext;
    strncpy(g_pszRightHost, pszHost, MAX_HOST_LENGTH);

    pContext->nState = MPD_IDLE;
    pContext->nLLState = MPD_READING_CMD;

    // Only post a read if there is a completion port available when InsertIntoRing is called.
    if (bPostRead)
    {
	if (CreateIoCompletionPort((HANDLE)pContext->sock, g_hCommPort, (DWORD)pContext, g_NumCommPortThreads) == NULL)
	{
	    err_printf("InsertIntoRing: Unable to associate completion port with socket, error %d\n", GetLastError());
	    RemoveContext(pContext);
	    return false;
	}
	dbg_printf("InsertIntoRing:: posting read on the new right socket.\n");
	PostContextRead(pContext);
    }

    return true;
}
