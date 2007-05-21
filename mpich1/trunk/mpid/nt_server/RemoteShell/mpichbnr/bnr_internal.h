#ifndef BNRINTERNAL_H
#define BNRINTERNAL_H

#include "bnr.h"

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

extern HANDLE g_hMPDPipe;
extern HANDLE g_hMPDOutputPipe;
extern HANDLE g_hMPDEndOutputPipe;
extern BNR_Group g_bnrGroup;
extern BNR_Group g_bnrParent;

int GetString(HANDLE hInput, char *pBuffer);
int GetZString(HANDLE hInput, char *pBuffer);

struct SpawnedProcess
{
	char pszSpawnID[10];
	char pszLaunchID[10];
	char pszHost[100];
};

struct SpawnedProcessNode
{
	int nProc;
	SpawnedProcess *pProcesses;
	SpawnedProcessNode *pNext;
};

struct BNR_Group_node
{
	BNR_Group_node();
	~BNR_Group_node();
	BNR_Group_node& operator=(BNR_Group_node &n2);

	int nRefCount;
	char pszName[256];
	int nID, nRank, nSize;
	BNR_Group_node *pMerged1, *pMerged2, *pParent;
	SpawnedProcessNode *pProcessList;
	BNR_Group_node *pNext;
};

extern BNR_Group_node *g_pGroupList;
BNR_Group_node* AddBNRGroupToList(int nID, int nRank, int nSize, BNR_Group_node *pParent = NULL);
BNR_Group_node* MergeBNRGroupToList(BNR_Group_node *pMerged1 = NULL, BNR_Group_node *pMerged2 = NULL);
BNR_Group_node* FindBNRGroupFromInt(int nGroup);

#endif
