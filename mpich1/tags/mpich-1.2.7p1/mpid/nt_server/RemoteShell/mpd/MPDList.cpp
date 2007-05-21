#include "MPDList.h"
#include <winsock2.h>
#include <windows.h>
#include "sockets.h"

// Function name	: MPDList::MPDList
// Description	    : 
// Return type		: 
MPDList::MPDList()
{
	m_pList = NULL;
	m_nPort = 0;
	m_nIP = 0;
	m_nSpawns = 1;
	m_bLookupIP = false;
}

// Function name	: MPDList::~MPDList
// Description	    : 
// Return type		: 
MPDList::~MPDList()
{
	Node *p;
	while (m_pList)
	{
		p = m_pList;
		m_pList = m_pList->pNext;
		delete p;
	}
}

// Function name	: AddHostThread
// Description	    : 
// Return type		: void 
// Argument         : MPDList::Node *pNode
void AddHostThread(MPDList::Node *pNode)
{
	// If this call is still pending when the program exits, there could be a memory access violation
	// when the node is deleted.  Catch this or any error, ignore it, and exit the thread.
	try{
	NT_get_host(pNode->nIP, pNode->pszhost);
	}catch(...){}
}

// Function name	: MPDList::Add
// Description	    : 
// Return type		: int 
// Argument         : unsigned long nIP
// Argument         : int nPort
// Argument         : int nSpawns
int MPDList::Add(unsigned long nIP, int nPort, int nSpawns)
{
	Node *pCurrent;
	if (m_pList)
	{
		pCurrent = m_pList;
		while (pCurrent)
		{
			if (pCurrent->nIP == nIP && pCurrent->nPort == nPort)
			{
				//if (NT_get_host(pCurrent->nIP, pCurrent->pszhost))
					pCurrent->pszhost[0] = '\0';
				DWORD dwThreadID;
				CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AddHostThread, pCurrent, 0, &dwThreadID));
				pCurrent->nSpawns = nSpawns;
				pCurrent->bEnabled = true;
				return MPDLIST_SUCCESS;
			}
			pCurrent = pCurrent->pNext;
		}
	}
	pCurrent = new Node;
	pCurrent->nIP = nIP;
	pCurrent->nPort = nPort;
	pCurrent->nSpawned = 0;
	pCurrent->nSpawns = nSpawns;
	pCurrent->bEnabled = true;
	pCurrent->pNext = m_pList;
	//if (NT_get_host(pCurrent->nIP, pCurrent->pszhost))
		pCurrent->pszhost[0] = '\0';
	DWORD dwThreadID;
	CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AddHostThread, pCurrent, 0, &dwThreadID));
	m_pList = pCurrent;

	return MPDLIST_SUCCESS;
}

// Function name	: MPDList::Enable
// Description	    : 
// Return type		: int 
// Argument         : unsigned long nIP
// Argument         : int nPort
int MPDList::Enable(unsigned long nIP, int nPort)
{
	Node *pCurrent;
	pCurrent = m_pList;
	while (pCurrent)
	{
		if (pCurrent->nIP == nIP && pCurrent->nPort == nPort)
		{
			pCurrent->bEnabled = true;
			return MPDLIST_SUCCESS;
		}
		pCurrent = pCurrent->pNext;
	}
	return MPDLIST_FAIL;
}

// Function name	: MPDList::Disable
// Description	    : 
// Return type		: int 
// Argument         : unsigned long nIP
// Argument         : int nPort
int MPDList::Disable(unsigned long nIP, int nPort)
{
	Node *pCurrent;
	pCurrent = m_pList;
	while (pCurrent)
	{
		if (pCurrent->nIP == nIP && pCurrent->nPort == nPort)
		{
			pCurrent->bEnabled = false;
			return MPDLIST_SUCCESS;
		}
		pCurrent = pCurrent->pNext;
	}
	return MPDLIST_FAIL;
}

// Function name	: MPDList::Remove
// Description	    : 
// Return type		: int 
// Argument         : unsigned long nIP
// Argument         : int nPort
int MPDList::Remove(unsigned long nIP, int nPort)
{
	Node *pTrailer, *pCurrent;
	pTrailer = pCurrent = m_pList;

	if (m_pList == NULL)
		return MPDLIST_SUCCESS;

	if (pCurrent->nIP == nIP && pCurrent->nPort == nPort)
	{
		m_pList = m_pList->pNext;
		delete pCurrent;
		return MPDLIST_SUCCESS;
	}

	pCurrent = pCurrent->pNext;
	while (pCurrent)
	{
		if (pCurrent->nIP == nIP && pCurrent->nPort == nPort)
		{
			pTrailer->pNext = pCurrent->pNext;
			delete pCurrent;
			return MPDLIST_SUCCESS;
		}
		pCurrent = pCurrent->pNext;
		pTrailer = pTrailer->pNext;
	}

	return MPDLIST_SUCCESS;
}

// Function name	: MPDList::SetNumSpawns
// Description	    : 
// Return type		: int 
// Argument         : unsigned long nIP
// Argument         : int nPort
// Argument         : int nSpawns
int MPDList::SetNumSpawns(unsigned long nIP, int nPort, int nSpawns)
{
	Node *pCurrent = m_pList;
	while (pCurrent)
	{
		if (pCurrent->nIP == nIP && pCurrent->nPort == nPort)
		{
			pCurrent->nSpawns = nSpawns;
			return MPDLIST_SUCCESS;
		}
		pCurrent = pCurrent->pNext;
	}
	return MPDLIST_FAIL;
}

// Function name	: MPDList::Increment
// Description	    : 
// Return type		: int 
// Argument         : unsigned long nIP
// Argument         : int nPort
int MPDList::Increment(unsigned long nIP, int nPort)
{
	Node *pCurrent = m_pList;
	while (pCurrent)
	{
		if (pCurrent->nIP == nIP && pCurrent->nPort == nPort)
		{
			pCurrent->nSpawned++;
			return MPDLIST_SUCCESS;
		}
		pCurrent = pCurrent->pNext;
	}
	return MPDLIST_FAIL;
}

// Function name	: MPDList::Decrement
// Description	    : 
// Return type		: int 
// Argument         : unsigned long nIP
// Argument         : int nPort
int MPDList::Decrement(unsigned long nIP, int nPort)
{
	Node *pCurrent = m_pList;
	while (pCurrent)
	{
		if (pCurrent->nIP == nIP && pCurrent->nPort == nPort)
		{
			pCurrent->nSpawned--;
			if (pCurrent->nSpawned >= 0)
				return MPDLIST_SUCCESS;
			else
			{
				pCurrent->nSpawned = 0;
				return MPDLIST_FAIL;
			}
		}
		pCurrent = pCurrent->pNext;
	}
	return MPDLIST_FAIL;
}

// Function name	: MPDList::GetNextAvailable
// Description	    : 
// Return type		: int 
// Argument         : unsigned long *pnIP
// Argument         : int *pnPort
int MPDList::GetNextAvailable(unsigned long *pnIP, int *pnPort)
{
	if (m_pList == NULL)
	{
		*pnIP = 0;
		*pnPort = 0;
		return MPDLIST_FAIL;
	}

	int level = 1;
	while (true)
	{
		Node *pCurrent = m_pList;
		while (pCurrent)
		{
			if (pCurrent->bEnabled && (pCurrent->nSpawned < (pCurrent->nSpawns * level)))
			{
				*pnIP = pCurrent->nIP;
				*pnPort = pCurrent->nPort;
				return MPDLIST_SUCCESS;
			}
			pCurrent = pCurrent->pNext;
		}
		level++;
	}

	return MPDLIST_FAIL;
}

// Function name	: MPDList::GetNextAvailable
// Description	    : 
// Return type		: MPDAvailableNode 
// Argument         : int n
MPDAvailableNode* MPDList::GetNextAvailable(int n)
{
	MPDAvailableNode *pList = NULL, *pTemp;

	// Pretend to launch on n nodes, getting and incrementing n slots.
	for (int i=0; i<n; i++)
	{
		pTemp = new MPDAvailableNode;
		pTemp->pNext = pList;
		pList = pTemp;
		GetNextAvailable(&pTemp->nIP, &pTemp->nPort);
		Increment(pTemp->nIP, pTemp->nPort);
	}
	
	// Then decrement all the nodes acquired because no processes have really been launched.
	pTemp = pList;
	while (pTemp != NULL)
	{
		Decrement(pTemp->nIP, pTemp->nPort);
		pTemp = pTemp->pNext;
	}
	return pList;
}

// Function name	: MPDList::GetMyID
// Description	    : 
// Return type		: int 
// Argument         : unsigned long *pnIP
// Argument         : int *pnPort
// Argument         : int *pnSpawns
int MPDList::GetMyID(unsigned long *pnIP, int *pnPort, int *pnSpawns)
{
	if (m_nPort == 0)
		return MPDLIST_GET_BEFORE_SET;

	*pnIP = m_nIP;
	*pnPort = m_nPort;
	if (pnSpawns != NULL)
		*pnSpawns = m_nSpawns;

	return MPDLIST_SUCCESS;
}

// Function name	: MPDList::GetID
// Description	    : 
// Return type		: int 
// Argument         : char *pszHost
// Argument         : unsigned long *pnIP
// Argument         : int *pnPort
// Argument         : int *pnSpawns
int MPDList::GetID(char *pszHost, unsigned long *pnIP, int *pnPort, int *pnSpawns)
{
	*pnIP = inet_addr(pszHost);

	if (*pnIP == INADDR_NONE)
	{
		LPHOSTENT lphost;
		lphost = gethostbyname(pszHost);
		if (lphost != NULL)
			*pnIP = ((LPIN_ADDR)lphost->h_addr)->s_addr;
		else
			return WSAEINVAL;
	}

	Node *pNode = m_pList;
	while (pNode != NULL)
	{
		if (pNode->nIP == *pnIP)
		{
			*pnPort = pNode->nPort;
			if (pnSpawns != NULL)
				*pnSpawns = pNode->nSpawns;
			return MPDLIST_SUCCESS;
		}
		pNode = pNode->pNext;
	}

	*pnIP = 0;
	*pnPort = 0;
	if (pnSpawns != NULL)
		*pnSpawns = 0;

	return MPDLIST_FAIL;
}

// Function name	: MPDList::SetMyID
// Description	    : 
// Return type		: int 
// Argument         : unsigned long nIP
// Argument         : int nPort
int MPDList::SetMyID(unsigned long nIP, int nPort)
{
	m_nIP = nIP;
	m_nPort = nPort;

	if (NT_get_host(nIP, m_pszHost))
		m_pszHost[0] = '\0';

	return MPDLIST_SUCCESS;
}

// Function name	: MPDList::SetMyID
// Description	    : 
// Return type		: int 
// Argument         : char *pszHost
// Argument         : int nPort
int MPDList::SetMyID(char *pszHost, int nPort)
{
	m_nIP = inet_addr(pszHost);

	if (m_nIP == INADDR_NONE)
	{
		LPHOSTENT lphost;
		lphost = gethostbyname(pszHost);
		if (lphost != NULL)
			m_nIP = ((LPIN_ADDR)lphost->h_addr)->s_addr;
		else
			return WSAEINVAL;
	}

	if (NT_get_host(m_nIP, m_pszHost))
		m_pszHost[0] = '\0';

	m_nPort = nPort;

	return MPDLIST_SUCCESS;
}

// Function name	: MPDList::SetMySpawns
// Description	    : 
// Return type		: int 
// Argument         : int nSpawns
int MPDList::SetMySpawns(int nSpawns)
{
	m_nSpawns = nSpawns;
	return MPDLIST_SUCCESS;
}

// Function name	: MPDList::Print
// Description	    : 
// Return type		: void 
void MPDList::Print()
{
	//char host[100];
	Node *n = m_pList;
	printf("MPDList:\n");
	while (n != NULL)
	{
		printf("IP %d.%d.%d.%d:%d(%02d), running:%2d, ",
			(int)(n->nIP & 0xff),
			(int)((n->nIP >> 8) & 0xff),
			(int)((n->nIP >> 16) & 0xff),
			(int)((n->nIP >> 24) & 0xff),
			n->nPort, n->nSpawns, n->nSpawned);
		if (n->bEnabled)
			printf("enabled  ");
		else
			printf("disabled ");
		if (m_bLookupIP)
		{
			printf("(%s)\n", n->pszhost);
			/*
			if (NT_get_host(n->nIP, host))
				printf("\n");
			else
				printf("(%s)\n", host);
			//*/
		}
		else
			printf("\n");
		n = n->pNext;
	}
	fflush(stdout);
}

// Function name	: MPDList::PrintToString
// Description	    : 
// Return type		: void 
// Argument         : char *pBuffer
void MPDList::PrintToString(char *pBuffer)
{
	//char host[100];
	char buf[256];
	Node *n = m_pList;
	strcpy(pBuffer, "MPDList:\n");
	while (n != NULL)
	{
		sprintf(buf, "IP %d.%d.%d.%d:%d(%02d), running:%2d, ",
			(int)(n->nIP & 0xff),
			(int)((n->nIP >> 8) & 0xff),
			(int)((n->nIP >> 16) & 0xff),
			(int)((n->nIP >> 24) & 0xff),
			n->nPort, n->nSpawns, n->nSpawned);
		strcat(pBuffer, buf);
		if (n->bEnabled)
			strcat(pBuffer, "enabled  ");
		else
			strcat(pBuffer, "disabled ");
		if (m_bLookupIP)
		{
			sprintf(buf, "(%s)\n", n->pszhost);
			strcat(pBuffer, buf);
			/*
			if (NT_get_host(n->nIP, host))
				strcat(pBuffer, "\n");
			else
			{
				sprintf(buf, "(%s)\n", host);
				strcat(pBuffer, buf);
			}
			//*/
		}
		else
			strcat(pBuffer, "\n");
		n = n->pNext;
	}
}
