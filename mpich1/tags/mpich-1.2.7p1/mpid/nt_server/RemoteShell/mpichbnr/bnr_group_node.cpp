#include "bnr_internal.h"

BNR_Group_node *g_pGroupList = NULL;

BNR_Group_node::BNR_Group_node()
{
	nRefCount = 0;
	pszName[0] = '\0';
	nID = -1;
	nRank = -1;
	nSize = -1;
	pMerged1 = NULL;
	pMerged2 = NULL;
	pParent = NULL;
	pNext = NULL;
	pProcessList = NULL;
}

BNR_Group_node& BNR_Group_node::operator=(BNR_Group_node &n2)
{
	if (this != &n2)
	{
		nRefCount = n2.nRefCount;
		nID = n2.nID;
		nRank = n2.nRank;
		nSize = n2.nSize;
		pMerged1 = n2.pMerged1;
		pMerged2 = n2.pMerged2;
		pNext = n2.pNext;
		pParent = n2.pParent;
		strcpy(pszName, n2.pszName);

		pProcessList = NULL; // for now don't copy the process list
	}
	return *this;
}

BNR_Group_node::~BNR_Group_node()
{
	SpawnedProcessNode *p = pProcessList;
	while (p)
	{
		pProcessList = pProcessList->pNext;
		if (p->pProcesses)
			delete p->pProcesses;
		delete p;
		p = pProcessList;
	}
}

BNR_Group_node* AddBNRGroupToList(int nID, int nRank, int nSize, BNR_Group_node *pParent)
{
	BNR_Group_node *pNode;

	// Check parameters
	if (nID == -1)
		return (BNR_Group_node*)BNR_GROUP_NULL;
		
	// Allocate a new node
	pNode = new BNR_Group_node;
	if (pNode == NULL)
		return (BNR_Group_node*)BNR_GROUP_NULL;


	// Fill in the values for the new node
	pNode->nID = nID;
	pNode->nRank = nRank;
	pNode->nSize = nSize;
	pNode->pParent = pParent;
	sprintf(pNode->pszName, "%d", nID);

	// Add the new node to the beginning of the list
	pNode->pNext = g_pGroupList;
	g_pGroupList = pNode;

	return pNode;
}

BNR_Group_node *g_pTempNode = NULL;

static void InsertNode(BNR_Group_node *pNode)
{
	// Add the node to the sorted list
	BNR_Group_node *p = g_pTempNode;
	if (pNode->nID == -1)
		return;

	if (g_pTempNode == NULL)
	{
		g_pTempNode = new BNR_Group_node;
		*g_pTempNode = *pNode;
		g_pTempNode->pNext = NULL;
	}
	else
	{
		if (p->nID > pNode->nID)
		{
			BNR_Group_node *pTemp = new BNR_Group_node;
			*pTemp = *pNode;
			pTemp->pNext = g_pTempNode;
			g_pTempNode = pTemp;
		}
		else
		{
			while (p->pNext != NULL)
			{
				if (p->pNext->nID > pNode->nID)
				{
					BNR_Group_node *pTemp = new BNR_Group_node;
					*pTemp = *pNode;
					pTemp->pNext = p->pNext;
					p->pNext = pTemp;
					break;
				}
				p = p->pNext;
			}
			if (p->pNext == NULL)
			{
				BNR_Group_node *pTemp = new BNR_Group_node;
				*pTemp = *pNode;
				pTemp->pNext = NULL;
				p->pNext = pTemp;
			}
		}
	}
}

static bool FindNodeInList(BNR_Group_node *pNode)
{
	BNR_Group_node *p = g_pTempNode;
	while (p)
	{
		if (p == pNode)
			return true;
		p = p->pNext;
	}

	return false;
}

static void RecurseInsertNodes(BNR_Group_node *pNode)
{
	if (pNode == NULL || pNode == BNR_INVALID_GROUP)
		return;

	if (FindNodeInList(pNode))
		return;

	InsertNode(pNode);
	RecurseInsertNodes(pNode->pMerged1);
	RecurseInsertNodes(pNode->pMerged2);
}

static bool FigureOutRankSizeAndName(BNR_Group_node *pOne, BNR_Group_node *pTwo, int *nRank, int *nSize, char *pszName)
{
	BNR_Group_node *p, *pTemp;
	pszName[0] = '\0';
	g_pTempNode = NULL;
	RecurseInsertNodes(pOne);
	RecurseInsertNodes(pTwo);
	p = g_pTempNode;
	*nSize = 0;
	*nRank = -1;
	while (p)
	{
		if (p->nID != -1)
		{
			if (pszName[0] != '\0')
				strcat(pszName, ".");
			strcat(pszName, p->pszName);

			if (p->nRank != -1)
				*nRank = *nSize + p->nRank;
			
			if (p->nID != -1 && p->nSize != -1)
				*nSize = *nSize + p->nSize;
		}
		pTemp = p;
		p = p->pNext;
		delete pTemp;
	}
	g_pTempNode = NULL;
	return true;
}

HANDLE g_hMergeMutex = CreateMutex(NULL, FALSE, NULL);

BNR_Group_node* MergeBNRGroupToList(BNR_Group_node *pMerged1, BNR_Group_node *pMerged2)
{
	if (pMerged1 == (BNR_Group_node*)BNR_INVALID_GROUP || pMerged2 == (BNR_Group_node*)BNR_INVALID_GROUP)
		return (BNR_Group_node*)BNR_INVALID_GROUP;
	BNR_Group_node *pNode = new BNR_Group_node;
	if (pNode == NULL)
		return (BNR_Group_node*)BNR_INVALID_GROUP;

	// Fill in the values for the new node
	pNode->nID = -1;
	pNode->pszName[0] = '\0';
	WaitForSingleObject(g_hMergeMutex, INFINITE);
	FigureOutRankSizeAndName(pMerged1, pMerged2, &pNode->nRank, &pNode->nSize, pNode->pszName);
	ReleaseMutex(g_hMergeMutex);
	pNode->pMerged1 = pMerged1;
	pNode->pMerged2 = pMerged2;
	pNode->pParent = (BNR_Group_node*)BNR_GROUP_NULL;
	pNode->pNext = NULL;

	// Add the new node to the beginning of the list
	pNode->pNext = g_pGroupList;
	g_pGroupList = pNode;

	return pNode;
}

BNR_Group_node* FindBNRGroupFromInt(int nGroup)
{
	BNR_Group_node *p = g_pGroupList;
	while (p)
	{
		if (p->nID == nGroup)
			return p;
		p = p->pNext;
	}

	return (BNR_Group_node*)BNR_GROUP_NULL;
}
