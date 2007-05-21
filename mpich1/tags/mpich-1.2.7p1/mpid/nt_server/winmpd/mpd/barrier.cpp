#include "mpdimpl.h"

struct sockNode
{
    SOCKET sock;
    sockNode *pNext;
};

struct BarrierStruct
{
    char pszName[100];
    int nCount, nCurIn;
    sockNode *pBfdList;
    BarrierStruct *pNext;
};

HANDLE g_hBarrierStructMutex = NULL;
BarrierStruct *g_pBarrierList = NULL;

static void BarrierToString(BarrierStruct *p, char *pszStr, int length)
{
    struct sockNode *pBfd;
    if (!snprintf_update(pszStr, length, "BARRIER:\n"))
	return;
    if (!snprintf_update(pszStr, length, " name: %s\n count: %d\n in: %d\n", p->pszName, p->nCount, p->nCurIn))
	return;
    pBfd = p->pBfdList;
    if (pBfd)
    {
	if (!snprintf_update(pszStr, length, " socks: "))
	    return;
	while (pBfd)
	{
	    if (!snprintf_update(pszStr, length, "%d, ", pBfd->sock))
		return;
	    pBfd = pBfd->pNext;
	}
	if (!snprintf_update(pszStr, length, "\n"))
	    return;
    }
}

void statBarrier(char *pszOutput, int length)
{
    BarrierStruct *p;

    *pszOutput = '\0';
    length--; // leave room for the null character

    if (g_pBarrierList == NULL)
	return;

    p = g_pBarrierList;
    while (p)
    {
	BarrierToString(p, pszOutput, length);
	length = length - strlen(pszOutput);
	pszOutput = &pszOutput[strlen(pszOutput)];
	p = p->pNext;
    }
}

static void DeleteBfdList(sockNode *p)
{
    if (p == NULL)
	return;
    DeleteBfdList(p->pNext);
    delete p;
}

static void RemoveBarrierStruct(char *pszName)
{
    BarrierStruct *p, *pTrailer;
    WaitForSingleObject(g_hBarrierStructMutex, INFINITE);

    pTrailer = p = g_pBarrierList;

    while (p)
    {
	if (strcmp(p->pszName, pszName) == 0)
	{
	    if (p == g_pBarrierList)
		g_pBarrierList = NULL;
	    else
		pTrailer->pNext = p->pNext;
	    DeleteBfdList(p->pBfdList);
	    dbg_printf("barrier structure '%s' removed\n", pszName);
	    delete p;
	    ReleaseMutex(g_hBarrierStructMutex);
	    return;
	}
	if (pTrailer != p)
	    pTrailer = pTrailer->pNext;
	p = p->pNext;
    }

    err_printf("Error: RemoveBarrierStruct: barrier structure '%s' not found\n", pszName);
    ReleaseMutex(g_hBarrierStructMutex);
}

void SetBarrier(char *pszName, int nCount, SOCKET sock)
{
    BarrierStruct *p;
    WaitForSingleObject(g_hBarrierStructMutex, INFINITE);

    p = g_pBarrierList;

    while (p)
    {
	if (strcmp(p->pszName, pszName) == 0)
	{
	    p->nCurIn++;
	    if (p->nCount != nCount)
		err_printf("Error: count's don't match, %d != %d", p->nCount, nCount);
	    if (sock != INVALID_SOCKET)
	    {
		sockNode *pNode = new sockNode;
		pNode->sock = sock;
		pNode->pNext = p->pBfdList;
		p->pBfdList = pNode;
	    }
	    dbg_printf("SetBarrier: name=%s count=%d curcount=%d\n", p->pszName, p->nCount, p->nCurIn);
	    break;
	}
	p = p->pNext;
    }
    if (p == NULL)
    {
	p = new BarrierStruct;
	strncpy(p->pszName, pszName, 100);
	p->pszName[99] = '\0';
	p->nCount = nCount;
	p->nCurIn = 1;
	if (sock != INVALID_SOCKET)
	{
	    p->pBfdList = new sockNode;
	    p->pBfdList->sock = sock;
	    p->pBfdList->pNext = NULL;
	}
	else
	    p->pBfdList = NULL;
	p->pNext = g_pBarrierList;
	g_pBarrierList = p;
	dbg_printf("SetBarrier: name=%s count=%d curcount=%d\n", p->pszName, p->nCount, p->nCurIn);
    }

    ReleaseMutex(g_hBarrierStructMutex);

    if (p->nCurIn >= p->nCount)
    {
	dbg_printf("SetBarrier: count reached for name=%s, %d:%d\n", p->pszName, p->nCount, p->nCurIn);
	sockNode *pNode = p->pBfdList;
	while (pNode)
	{
	    dbg_printf("SetBarrier: writing success for name=%s\n", p->pszName);
	    WriteString(pNode->sock, "SUCCESS");
	    pNode = pNode->pNext;
	}
	RemoveBarrierStruct(p->pszName);
    }
}

void InformBarriers(int nId, int nExitCode)
{
    BarrierStruct *p;
    char pszStr[1024];

    _snprintf(pszStr, 1024, "INFO - id=%d exitcode=%d", nId, nExitCode);

    WaitForSingleObject(g_hBarrierStructMutex, INFINITE);

    p = g_pBarrierList;

    while (p)
    {
	sockNode *pNode = p->pBfdList;
	while (pNode)
	{
	    WriteString(pNode->sock, pszStr);
	    pNode = pNode->pNext;
	}
	p = p->pNext;
    }

    ReleaseMutex(g_hBarrierStructMutex);
}
