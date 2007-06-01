#include "GetHosts.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

// Function name	: GetHostsFromRegistry
// Description	    : 
// Return type		: HostNode * 
// Argument         : int nMPDsToLaunch
HostNode * GetHostsFromRegistry(int nMPDsToLaunch)
{
	// For now just use the local host over and over again
	HostNode *pNode = new HostNode;
	pNode->pNext = NULL;
	pNode->bPrimaryMPD = false;
	pNode->nSpawns = nMPDsToLaunch;
	gethostname(pNode->pszHost, HOSTNAMELEN);
	return pNode;
}

// Function name	: ParseLineIntoHostNode
// Description	    : 
// Return type		: HostNode* 
// Argument         : char *line
HostNode* ParseLineIntoHostNode(char *line)
{
	char *token;
	HostNode *node = NULL;

	token = strtok(line, " \t\n");
	if (token == NULL || *token == '#')
		return NULL;
	node = new HostNode;
	node->pNext = NULL;
	strcpy(node->pszHost, token);
	token = strtok(NULL, " \t\n");
	node->nSpawns = (token == NULL) ? 1 : atoi(token);
	node->bPrimaryMPD = false;

	return node;
}

// Function name	: MarkHostList
// Description	    : 
// Return type		: void 
// Argument         : HostNode *pList
void MarkHostList(HostNode *pList)
{
	HostNode *pCurrent = pList;
	HostNode *pTemp;

	if (pList == NULL)
		return;

	// Mark everyone as a primary node
	while (pCurrent)
	{
		pCurrent->bPrimaryMPD = true;
		pCurrent = pCurrent->pNext;
	}
	
	// Unmark all the repeats
	pCurrent = pList;
	while (pCurrent)
	{
		if (pCurrent->bPrimaryMPD)
		{
			pTemp = pCurrent->pNext;
			while (pTemp)
			{
				if (stricmp(pCurrent->pszHost, pTemp->pszHost) == 0)
					pTemp->bPrimaryMPD = false;
				pTemp = pTemp->pNext;
			}
		}
		pCurrent = pCurrent->pNext;
	}
}

/*
// Function name	: CompressHostList
// Description	    : 
// Return type		: void 
// Argument         : HostNode *pList
void CompressHostList(HostNode *pList)
{
	HostNode *pCurrent = pList;
	HostNode *pTemp, *pTemp2;

	if (pList == NULL)
		return;

	while (pCurrent)
	{
		pTemp = pCurrent->pNext;
		while (pTemp)
		{
			if (stricmp(pCurrent->pszHost, pTemp->pszHost) == 0)
			{
				pCurrent->nSpawns += pTemp->nSpawns;
				pTemp->nSpawns = 0;
			}
			pTemp = pTemp->pNext;
		}
		if (pCurrent->pNext)
		{
			pTemp = pCurrent->pNext;
			while (pTemp && pTemp->nSpawns == 0)
			{
				pTemp2 = pTemp;
				pTemp = pTemp->pNext;
				delete pTemp2;
			}
			pCurrent->pNext = pTemp;
		}
		pCurrent = pCurrent->pNext;
	}
}
//*/

// Function name	: GetHostsFromFile
// Description	    : 
// Return type		: HostNode * 
// Argument         : int nMPDsToLaunch
// Argument         : char *pszHostFile
HostNode * GetHostsFromFile(int nMPDsToLaunch, char *pszHostFile)
{
	FILE *fin;
	char buffer[1024] = "";
	HostNode *node, dummy, *trailer;

	fin = fopen(pszHostFile, "r");
	if (fin == NULL)
	{
		printf("Unable to open file: %s\n", pszHostFile);
		return NULL;
	}

	dummy.pNext = NULL;
	node = &dummy;

	if (nMPDsToLaunch == 0)
	{
		while (fgets(buffer, 1024, fin))
		{
			node->pNext = ParseLineIntoHostNode(buffer);
			if (node->pNext != NULL)
				node = node->pNext;
		}
	}
	else
	{
		while (fgets(buffer, 1024, fin))
		{
			node->pNext = ParseLineIntoHostNode(buffer);
			if (node->pNext != NULL)
			{
				nMPDsToLaunch = nMPDsToLaunch - node->nSpawns;
				if (nMPDsToLaunch < 0)
				{
					// We reached the number to launch before reaching the end of the file
					node->nSpawns = node->nSpawns + nMPDsToLaunch;
					fclose(fin);
					//CompressHostList(dummy.pNext);
					MarkHostList(dummy.pNext);
					return dummy.pNext;
				}
				node = node->pNext;
			}
		}
		if (nMPDsToLaunch)
		{
			// We added all the hosts in the file without reaching the desired number.
			// Add the nodes again from the beginning until the desired number is reached.
			trailer = dummy.pNext;
			while (nMPDsToLaunch)
			{
				node->pNext = new HostNode;
				node = node->pNext;
				node->nSpawns = trailer->nSpawns;
				strcpy(node->pszHost, trailer->pszHost);
				node->pNext = NULL;

				nMPDsToLaunch = nMPDsToLaunch - node->nSpawns;
				if (nMPDsToLaunch < 0)
				{
					// We passed the desired number to launch so back off and quit
					node->nSpawns = node->nSpawns + nMPDsToLaunch;
					nMPDsToLaunch = 0;
				}

				trailer = trailer->pNext;
			}
		}
	}
	fclose(fin);

	//CompressHostList(dummy.pNext);
	MarkHostList(dummy.pNext);
	return dummy.pNext;
}

// Function name	: GetHostsFromCmdLine
// Description	    : 
// Return type		: HostNode * 
// Argument         : int argc
// Argument         : char **argv
HostNode * GetHostsFromCmdLine(int argc, char **argv)
{
	HostNode *pList = NULL, *pCurrent = NULL;
	for (int i=1; i<argc; i+=2)
	{
		if (pList == NULL)
		{
			pList = new HostNode;
			pCurrent = pList;
		}
		else
		{
			pCurrent->pNext = new HostNode;
			pCurrent = pCurrent->pNext;
		}
		pCurrent->pNext = NULL;
		pCurrent->bPrimaryMPD = false;
		strcpy(pCurrent->pszHost, argv[i]);
		if (argv[i+1])
			pCurrent->nSpawns = atoi(argv[i+1]);
		else
			pCurrent->nSpawns = 1;
	}
	MarkHostList(pList);
	return pList;
}
