#include "mpdimpl.h"

bool ConnectToSelf()
{
    char host[MAX_HOST_LENGTH];
    DWORD length = MAX_HOST_LENGTH;
    MPD_Context *pRightContext, *pLeftContext;

    host[0] = '\0';
    GetComputerName(host, &length);

    // Initialize the new contexts
    pRightContext = CreateContext();
    if (pRightContext == NULL)
	return false;
    strncpy(pRightContext->pszHost, host, MAX_HOST_LENGTH);
    pRightContext->nCurPos = 0;
    pRightContext->nState = MPD_IDLE;
    pRightContext->nLLState = MPD_READING_CMD;
    pRightContext->nType = MPD_RIGHT_SOCKET;

    pLeftContext = CreateContext();
    if (pLeftContext == NULL)
    {
	RemoveContext(pRightContext);
	return false;
    }
    strncpy(pLeftContext->pszHost, host, MAX_HOST_LENGTH);
    pLeftContext->nCurPos = 0;
    pLeftContext->nState = MPD_IDLE;
    pLeftContext->nLLState = MPD_READING_CMD;
    pLeftContext->nType = MPD_LEFT_SOCKET;

    MakeLoopAsync(&pLeftContext->sock, &pRightContext->sock);
    if (pLeftContext->sock == INVALID_SOCKET || pRightContext->sock == INVALID_SOCKET)
    {
	RemoveContext(pLeftContext);
	RemoveContext(pRightContext);
	return false;
    }

    // overwrite the old left and right contexts
    g_pRightContext = pRightContext;
    g_pLeftContext = pLeftContext;
    strncpy(g_pszRightHost, host, MAX_HOST_LENGTH);
    strncpy(g_pszLeftHost, host, MAX_HOST_LENGTH);

    //dbg_printf("ConnectToSelf succeeded\n");
    return true;
}
