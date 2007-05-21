#include "LaunchNode.h"

LaunchNode *LaunchNode::g_pList = NULL;
HANDLE LaunchNode::g_hMutex = CreateMutex(NULL, FALSE, NULL);
int LaunchNode::g_nCurID = 0;

// Function name	: LaunchNode::LaunchNode
// Description	    : 
// Return type		: 
LaunchNode::LaunchNode()
{
	m_hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hEndEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_nID = 0;
	m_dwData = 0;
	m_hEndOutputPipe = NULL;
	m_dwExitCode = 0;
	m_pNext = NULL;
}

#include <stdio.h>
// Function name	: LaunchNode::~LaunchNode
// Description	    : 
// Return type		: 
LaunchNode::~LaunchNode()
{
	CloseHandle(m_hEvent);
	m_hEvent = NULL;
	CloseHandle(m_hEndEvent);
	m_hEndEvent = NULL;
	m_nID = 0;
	m_dwData = 0;
	//*
	//printf("freeing launch node, closing m_hEndOutputPipe\n");fflush(stdout);
	if (m_hEndOutputPipe != NULL)
		CloseHandle(m_hEndOutputPipe);
	m_hEndOutputPipe = NULL;
	//*/
	m_dwExitCode = 0;
	RemoveNode(this);
}

// Function name	: LaunchNode::RemoveNode
// Description	    : 
// Return type		: void 
// Argument         : LaunchNode *pNode
void LaunchNode::RemoveNode(LaunchNode *pNode)
{
	LaunchNode *p, *pTrailer;
	WaitForSingleObject(g_hMutex, INFINITE);
	p = pTrailer = g_pList;
	while (p != NULL)
	{
		if (p == pNode)
		{
			if (p == g_pList)
				g_pList = g_pList->m_pNext;
			else
				pTrailer->m_pNext = p->m_pNext;
			p->m_pNext = NULL;
		}
		if (pTrailer != pNode)
			pTrailer = pTrailer->m_pNext;
		p = p->m_pNext;
	}
	ReleaseMutex(g_hMutex);
}

// Function name	: *LaunchNode::AllocLaunchNode
// Description	    : 
// Return type		: LaunchNode 
LaunchNode *LaunchNode::AllocLaunchNode()
{
	LaunchNode *pNode = new LaunchNode;

	WaitForSingleObject(g_hMutex, INFINITE);
	pNode->m_nID = g_nCurID++;
	pNode->m_pNext = g_pList;
	g_pList = pNode;
	ReleaseMutex(g_hMutex);

	return pNode;
}

// Function name	: LaunchNode::GetLaunchNodeData
// Description	    : 
// Return type		: DWORD 
// Argument         : int nID
// Argument         : int nTimeout
DWORD LaunchNode::GetLaunchNodeData(int nID, int nTimeout)
{
	LaunchNode *pNode = g_pList;
	while (pNode != NULL)
	{
		if (pNode->m_nID == nID)
		{
			if (WaitForSingleObject(pNode->m_hEvent, nTimeout) == WAIT_OBJECT_0)
				return pNode->m_dwData;
			else
				return -1;
		}
		pNode = pNode->m_pNext;
	}
	return -1;
}

// Function name	: LaunchNode::FreeLaunchNode
// Description	    : 
// Return type		: void 
// Argument         : LaunchNode *pNode
void LaunchNode::FreeLaunchNode(LaunchNode *pNode)
{
	RemoveNode(pNode);
	delete pNode;
}

#include <stdio.h>
// Function name	: LaunchNode::Set
// Description	    : 
// Return type		: void 
// Argument         : DWORD dwData
void LaunchNode::Set(DWORD dwData)
{
	m_dwData = dwData;
	SetEvent(m_hEvent);
	//printf("launchid %d:%d\n", m_nID, m_dwData);fflush(stdout);
}

void LaunchNode::InitData(HANDLE hEndOutputPipe)
{
	m_hEndOutputPipe = hEndOutputPipe;
}

// Function name	: LaunchNode::SetExit
// Description	    : 
// Return type		: void 
// Argument         : DWORD dwExitCode
void LaunchNode::SetExit(int nGroup, int nRank, DWORD dwExitCode)
{
	m_dwExitCode = dwExitCode;
	SetEvent(m_hEndEvent);
	if (m_hEndOutputPipe)
	{
		DWORD dwNumWritten;
		char pBuffer[100];
		sprintf(pBuffer, "%d %d %d", nGroup, nRank, dwExitCode);
		WriteFile(m_hEndOutputPipe, pBuffer, strlen(pBuffer)+1, &dwNumWritten, NULL);
	}
	else
	{
		printf("m_hEndOutputPipe == NULL\n");fflush(stdout);
	}
}

// Function name	: LaunchNode::GetID
// Description	    : 
// Return type		: int 
int LaunchNode::GetID()
{
	return m_nID;
}

// Function name	: LaunchNode::GetData
// Description	    : 
// Return type		: DWORD 
// Argument         : int nTimeout
DWORD LaunchNode::GetData(int nTimeout)
{
	if (WaitForSingleObject(m_hEvent, nTimeout) == WAIT_OBJECT_0)
		return m_dwData;
	else
		return -1;
}
