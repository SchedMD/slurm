#include "mpdimpl.h"

HANDLE g_hEnqueueMutex = CreateMutex(NULL, FALSE, NULL);

#if 0
void EnqueueWrite(MPD_Context *p, char *pszStr, MPD_LowLevelState nState)
{
    // Note: You cannot have a concurrent reading and writing sessions in a
    // single context.  You can have multiple writes enqueued, but not multiple
    // reads.  There is a potential bug here if a command is being read when
    // EnqueueWrite is called.  This function will switch the state to WRITING
    // and the remaining data will not be read.  Then when the write is finished
    // the rest of the data will be read and it will be misunderstood.

    if (p == NULL)
    {
	err_printf("attempting to enqueue '%s' into a NULL context\n", pszStr);
	return;
    }

    WaitForSingleObject(g_hEnqueueMutex, INFINITE);
    dbg_printf("EnqueueWrite[%s]: '%s'\n", bto_string(p->sock), pszStr);

    p->nState = MPD_WRITING;
    p->nLLState = nState;
    if (pszStr != NULL)
    {
	strncpy(p->pszOut, pszStr, MAX_CMD_LENGTH);
	p->pszOut[MAX_CMD_LENGTH-1] = '\0';
    }
    WriteString(p->sock, p->pszOut);
    ReleaseMutex(g_hEnqueueMutex);
    p->bDeleteMe = false;
    p->nState = MPD_IDLE;
    StringWritten(p);
    if (p->bDeleteMe)
	RemoveContext(p);
}

void DequeueWrite(MPD_Context *p)
{
}
#endif

void EnqueueWrite(MPD_Context *p, char *pszStr, MPD_LowLevelState nState)
{
    // Note: You cannot have a concurrent reading and writing sessions in a
    // single context.  You can have multiple writes enqueued, but not multiple
    // reads.  There is a potential bug here if a command is being read when
    // EnqueueWrite is called.  This function will switch the state to WRITING
    // and the remaining data will not be read.  Then when the write is finished
    // the rest of the data will be read and it will be misunderstood.

    if (p == NULL)
    {
	err_printf("attempting to enqueue '%s' into a NULL context\n", pszStr);
	return;
    }

    WaitForSingleObject(g_hEnqueueMutex, INFINITE);
    dbg_printf("EnqueueWrite[%d]: '%s'\n", p->sock, pszStr);
    if (p->nState == MPD_READING)
    {
	dbg_printf(":::DANGER WILL ROGERS::: switching from MPD_READING to MPD_WRITING on sock[%d]\n", p->sock);
    }
    if (p->nState != MPD_WRITING)
    {
	p->nCurPos = 0;
	p->nLLState = nState;
	if (pszStr != NULL)
	{
	    strncpy(p->pszOut, pszStr, MAX_CMD_LENGTH);
	    p->pszOut[MAX_CMD_LENGTH-1] = '\0';
	}
	DoWriteSet(p->sock);
	dbg_printf("write enqueued directly into context\n");
    }
    else
    {
	if (pszStr == NULL)
	{
	    err_printf("EnqueueWrite called with pszStr == NULL and nState == MPD_WRITING\n");
	    return;
	}

	if (p->pWriteList == NULL)
	{
	    p->pWriteList = new WriteNode(pszStr, nState);
	}
	else
	{
	    WriteNode *e = p->pWriteList;
	    while (e->pNext)
		e = e->pNext;
	    e->pNext = new WriteNode(pszStr, nState);
	}
	DoWriteSet(p->sock);
	dbg_printf("write enqueued into pWriteList\n");
    }
    p->nState = MPD_WRITING;
    ReleaseMutex(g_hEnqueueMutex);
}

void DequeueWrite(MPD_Context *p)
{
    //dbg_printf("DequeueWrite[%s]: %s\n", bto_string(p->sock), p->pszOut);
    if (p->pWriteList == NULL)
    {
	//dbg_printf("sock[%s] state: MPD_IDLE, llstate: MPD_READING_CMD\n", bto_string(p->sock));
	p->nLLState = MPD_READING_CMD;
	p->nState = MPD_IDLE;
	g_nActiveW--;
	//dbg_printf("sock %d removed from write set\n", p->sock);
	return;
    }

    WriteNode *e = p->pWriteList;
    p->pWriteList = p->pWriteList->pNext;

    p->nCurPos = 0;
    p->nState = MPD_WRITING;
    p->nLLState = e->nState;
    //strncpy(p->pszOut, e->pString, MAX_CMD_LENGTH);
    //p->pszOut[MAX_CMD_LENGTH-1] = '\0';
    strcpy(p->pszOut, e->pString); // This strcpy doesn't need to be length checked because it already was when e was created
    dbg_printf("sock[%d] currently set to write '%s'\n", p->sock, p->pszOut);

    delete e;
}
