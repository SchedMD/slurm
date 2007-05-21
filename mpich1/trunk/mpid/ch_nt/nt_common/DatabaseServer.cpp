#include "Database.h"
#include <stdio.h>

HANDLE g_hStopDBSLoopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

// Function name	: DatabaseServer::DatabaseServer
// Description	    : 
// Return type		: 
DatabaseServer::DatabaseServer()
{
	// Start the Winsock dll
	WSADATA wsaData;
	int err = WSAStartup( MAKEWORD( 2, 0 ), &wsaData );
	if (err != 0)
		printf("Winsock2 dll not initialized\n");

	m_hServerThread = NULL;
	m_nPort = 0;
	strcpy(m_pszHost, "127.0.0.1");
	gethostname(m_pszHost, 100);
	m_pList = NULL;
	m_hMutex = CreateMutex(NULL, FALSE, NULL);
}

// Function name	: DeleteValueList
// Description	    : 
// Return type		: void 
// Argument         : DatabaseServer::ValueNode *pList
void DeleteValueList(DatabaseServer::ValueNode *pList)
{
	if (pList == NULL)
		return;
	DeleteValueList(pList->pNext);
	if (pList->pData != NULL)
		delete pList->pData;
	delete pList;
}

// Function name	: DeleteKeyList
// Description	    : 
// Return type		: void 
// Argument         : DatabaseServer::KeyNode *pList
void DeleteKeyList(DatabaseServer::KeyNode *pList)
{
	if (pList == NULL)
		return;
	DeleteKeyList(pList->pNext);
	DeleteValueList(pList->pValueList);
	if (pList->pszKey != NULL)
		delete pList->pszKey;
	delete pList;
}

// Function name	: DeleteIDList
// Description	    : 
// Return type		: void 
// Argument         : DatabaseServer::IDNode *pList
void DeleteIDList(DatabaseServer::IDNode *pList)
{
	if (pList == NULL)
		return;
	DeleteIDList(pList->pNext);
	DeleteKeyList(pList->pKeyList);
	delete pList;
}

// Function name	: DatabaseServer::~DatabaseServer
// Description	    : 
// Return type		: 
DatabaseServer::~DatabaseServer()
{
	if (m_hServerThread != NULL)
	{
		//TerminateThread(m_hServerThread, 0);
		//m_hServerThread = NULL;
		SetEvent(g_hStopDBSLoopEvent);
		while (m_hServerThread)
			Sleep(200);
	}
	CloseHandle(g_hStopDBSLoopEvent);
	g_hStopDBSLoopEvent = NULL;
	WaitForSingleObject(m_hMutex, 5000);
	DeleteIDList(m_pList);
	m_pList = NULL;
	ReleaseMutex(m_hMutex);
	CloseHandle(m_hMutex);
	m_hMutex = NULL;
}

// Function name	: DatabaseServer::Start
// Description	    : 
// Return type		: bool 
bool DatabaseServer::Start()
{
	if (m_hServerThread == NULL)
	{
		DWORD dwThreadID;
		for (int i=0; i<DBS_CREATE_THREAD_RETRIES; i++)
		{
			m_hServerThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)DatabaseServerThread, this, 0, &dwThreadID);
			if (m_hServerThread != NULL)
			{
				return true;
			}
			Sleep(DBS_CREATE_THREAD_SLEEP_TIME);
		}
		return false;
	}
	return true;
}

// Function name	: DatabaseServer::GetHost
// Description	    : 
// Return type		: bool 
// Argument         : char *pszHost
// Argument         : int length
bool DatabaseServer::GetHost(char *pszHost, int length)
{
	if (strlen(m_pszHost) >= (unsigned int)length)
		return false;
	strcpy(pszHost, m_pszHost);
	return true;
}

// Function name	: DatabaseServer::SetPort
// Description	    : 
// Return type		: bool 
// Argument         : int nPort
bool DatabaseServer::SetPort(int nPort)
{
	if (m_hServerThread != NULL)
		return false;
	m_nPort = nPort;
	return true;
}

// Function name	: DatabaseServer::GetPort
// Description	    : 
// Return type		: int 
int DatabaseServer::GetPort()
{
	if (m_hServerThread != NULL)
	{
		while (m_nPort == 0)
			Sleep(200);
		return m_nPort;
	}
	return -1;
}

// Function name	: DatabaseServer::Stop
// Description	    : 
// Return type		: bool 
bool DatabaseServer::Stop()
{
	if (m_hServerThread != NULL)
	{
		//TerminateThread(m_hServerThread, 0);
		//m_hServerThread = NULL;
		SetEvent(g_hStopDBSLoopEvent);
		while (m_hServerThread)
			Sleep(200);
	}
	return true;
}

// Function name	: DatabaseServer::Delete
// Description	    : 
// Return type		: int 
// Argument         : char *pszID
int DatabaseServer::Delete(char *pszID)
{
	IDNode *pNode, *pTrailer;

	if (WaitForSingleObject(m_hMutex, DATABASE_TIMEOUT) != WAIT_OBJECT_0)
		return MPI_DBS_FAIL;

	pNode = pTrailer = m_pList;

	while (pNode != NULL)
	{
		if (strcmp(pNode->pszID, pszID) == 0)
		{
			if (m_pList == pNode)
				m_pList = pNode->pNext;
			else
				pTrailer->pNext = pNode->pNext;
			pNode->pNext = NULL;
			DeleteIDList(pNode);
			ReleaseMutex(m_hMutex);
			return MPI_DBS_SUCCESS;
		}
		if (pTrailer != pNode)
			pTrailer = pTrailer->pNext;
		pNode = pNode->pNext;
	}

	ReleaseMutex(m_hMutex);

	return MPI_DBS_SUCCESS;
}

// Function name	: DatabaseServer::Get
// Description	    : 
// Return type		: int 
// Argument         : char *pszID
// Argument         : char *pszKey
// Argument         : void *&pValueData
// Argument         : int *length
int DatabaseServer::Get(char *pszID, char *pszKey, void *&pValueData, int *length)
{
	IDNode *pNode;
	KeyNode *pKey;
	ValueNode *pValue;

	while (true)
	{
		if (WaitForSingleObject(m_hMutex, DATABASE_TIMEOUT) != WAIT_OBJECT_0)
			return MPI_DBS_FAIL;
		
		pNode = m_pList;
		while (pNode != NULL)
		{
			if (strcmp(pNode->pszID, pszID) == 0)
			{
				pKey = pNode->pKeyList;
				while (pKey != NULL)
				{
					if (strcmp(pKey->pszKey, pszKey) == 0)
					{
						if (pKey->pValueList != NULL)
						{
							pValue = pKey->pValueList;
							if (pValue->length > *length)
							{
								*length = pValue->length;
								ReleaseMutex(m_hMutex);
								return MPI_DBS_FAIL;
							}
							if (pKey->bPersistent)
							{
								// make a copy of the data
								pValueData = new char[pValue->length];
								memcpy(pValueData, pValue->pData, pValue->length);
								*length = pValue->length;
							}
							else
							{
								// consume the data
								pValueData = pValue->pData;
								*length = pValue->length;
								// put the next entry as the new head of the list
								pKey->pValueList = pValue->pNext;
								delete pValue;
							}
							ReleaseMutex(m_hMutex);
							return MPI_DBS_SUCCESS;
						}
					}
					pKey = pKey->pNext;
				}
			}
			pNode = pNode->pNext;
		}

		ReleaseMutex(m_hMutex);
		Sleep(100);
	}
		
	return MPI_DBS_SUCCESS;
}

// Function name	: DatabaseServer::Put
// Description	    : 
// Return type		: int 
// Argument         : char *pszID
// Argument         : char *pszKey
// Argument         : void *pValueData
// Argument         : int length
// Argument         : bool bPersistent
int DatabaseServer::Put(char *pszID, char *pszKey, void *pValueData, int length, bool bPersistent)
{
	IDNode *pIter, *pLast;

	if (WaitForSingleObject(m_hMutex, DATABASE_TIMEOUT) != WAIT_OBJECT_0)
		return MPI_DBS_FAIL;

	if (m_pList == NULL)
	{
		// The list is empty
		KeyNode *pKey = new KeyNode;
		ValueNode *pValue = new ValueNode;
		IDNode *pNode = new IDNode;

		pNode->pKeyList = NULL;
		pNode->pNext = NULL;
		pNode->pKeyList = pKey;
		strcpy(pNode->pszID, pszID);

		pKey->bPersistent = bPersistent;
		pKey->pNext = NULL;
		pKey->pValueList = pValue;
		pKey->pszKey = pszKey;

		pValue->pNext = NULL;
		pValue->pData = pValueData;
		pValue->length = length;

		m_pList = pNode;
		ReleaseMutex(m_hMutex);
		return MPI_DBS_SUCCESS;
	}
	
	pIter = m_pList;
	while (pIter != NULL)
	{
		if (strcmp(pIter->pszID, pszID) == 0)
		{
			// We've matched the id string
			// Now search the key nodes
			KeyNode *pKeyIter, *pKeyLast = NULL;
			pKeyIter = pIter->pKeyList;
			while (pKeyIter != NULL)
			{
				if (strcmp(pKeyIter->pszKey, pszKey) == 0)
				{
					delete pszKey; // David Ashton 4.6.0000 memory leak fixed ?
					// We've matched the key string
					ValueNode *pValue = new ValueNode;
					pValue->length = length;
					pValue->pData = pValueData;
					pValue->pNext = NULL;
					//if (pKeyIter->bPersistent) // <----- This method retains the option specified on the first put call
					pKeyIter->bPersistent = bPersistent; // <---- This method sets the state to match the current call
					if (bPersistent)
					{
						DeleteValueList( pKeyIter->pValueList );
						pKeyIter->pValueList = pValue;
					}
					else
					{
						if (pKeyIter->pValueList == NULL)
							pKeyIter->pValueList = pValue;
						else
						{
							ValueNode *p = pKeyIter->pValueList;
							while (p->pNext != NULL)
								p = p->pNext;
							p->pNext = pValue;
						}
					}
					ReleaseMutex(m_hMutex);
					return MPI_DBS_SUCCESS;
				}

				if (pKeyIter->pNext == NULL)
					pKeyLast = pKeyIter;
				pKeyIter = pKeyIter->pNext;
			}
			// We've reached the end of the list without matching the key strings
			KeyNode *pKey = new KeyNode;
			pKey->pNext = NULL;
			pKey->bPersistent = bPersistent;
			pKey->pszKey = pszKey;
			pKey->pValueList = new ValueNode;
			pKey->pValueList->length = length;
			pKey->pValueList->pData = pValueData;
			pKey->pValueList->pNext = NULL;

			if (pIter->pKeyList == NULL)
				pIter->pKeyList = pKey;
			else
				pKeyLast->pNext = pKey;

			ReleaseMutex(m_hMutex);
			return MPI_DBS_SUCCESS;
		}

		if (pIter->pNext == NULL)
			pLast = pIter;
		pIter = pIter->pNext;
	}

	// We've reached the end of the list without matching the id string
	KeyNode *pKey = new KeyNode;
	ValueNode *pValue = new ValueNode;
	IDNode *pNode = new IDNode;
	
	pNode->pKeyList = NULL;
	pNode->pNext = NULL;
	pNode->pKeyList = pKey;
	strcpy(pNode->pszID, pszID);
	
	pKey->bPersistent = bPersistent;
	pKey->pNext = NULL;
	pKey->pValueList = pValue;
	pKey->pszKey = pszKey;
	
	pValue->pNext = NULL;
	pValue->pData = pValueData;
	pValue->length = length;
	
	pLast->pNext = pNode;
	
	ReleaseMutex(m_hMutex);

	return MPI_DBS_SUCCESS;
}

// Function name	: DatabaseServer::PrintState
// Description	    : 
// Return type		: void 
void DatabaseServer::PrintState()
{
	IDNode *pNode = m_pList;
	KeyNode *pKey;
	ValueNode *pValue;

	if (WaitForSingleObject(m_hMutex, DATABASE_TIMEOUT) != WAIT_OBJECT_0)
		return;

	printf("DATABASE:\n{\n");
	while (pNode)
	{
		printf(" NODE \"%s\"\n", pNode->pszID);
		pKey = pNode->pKeyList;
		while (pKey)
		{
			if (pKey->bPersistent)
				printf("  KEY \"%s\" persistent\n", pKey->pszKey);
			else
				printf("  KEY \"%s\" consumable\n", pKey->pszKey);
			pValue = pKey->pValueList;
			while (pValue)
			{
				printf("   VALUE: ");
				fwrite(pValue->pData, pValue->length, 1, stdout);
				printf("\n");
				pValue = pValue->pNext;
			}
			pKey = pKey->pNext;
		}
		pNode = pNode->pNext;
	}
	printf("}\n");

	ReleaseMutex(m_hMutex);
}

// Function name	: DatabaseServer::PrintStateToBuffer
// Description	    : This function will incorrectly handle binary data.  The values in the database must be strings
//                    in order for this function to work properly.
// Return type		: void 
// Argument         : char *pszBuffer
// Argument         : int *pnLength
void DatabaseServer::PrintStateToBuffer(char *pszBuffer, int *pnLength)
{
	IDNode *pNode = m_pList;
	KeyNode *pKey;
	ValueNode *pValue;
	char pLocalBuffer[4096];
	int nLength = 0;

	if (WaitForSingleObject(m_hMutex, DATABASE_TIMEOUT) != WAIT_OBJECT_0)
		return;

	sprintf(pLocalBuffer, "DATABASE:\n{\n");
	if ((int)strlen(pLocalBuffer) < (*pnLength - 1))
	{
		strcpy(pszBuffer, pLocalBuffer);
		nLength = strlen(pszBuffer);
	}
	else
	{
		*pnLength = 0;
		ReleaseMutex(m_hMutex);
		return;
	}
	while (pNode)
	{
		sprintf(pLocalBuffer, " NODE \"%s\"\n", pNode->pszID);
		if ((int)strlen(pLocalBuffer) < (*pnLength - nLength))
		{
			strcat(pszBuffer, pLocalBuffer);
			nLength = strlen(pszBuffer);
		}
		else
		{
			*pnLength = nLength;
			ReleaseMutex(m_hMutex);
			return;
		}
		pKey = pNode->pKeyList;
		while (pKey)
		{
			if (pKey->bPersistent)
				sprintf(pLocalBuffer, "  KEY \"%s\" persistent\n", pKey->pszKey);
			else
				sprintf(pLocalBuffer, "  KEY \"%s\" consumable\n", pKey->pszKey);
			if ((int)strlen(pLocalBuffer) < (*pnLength - nLength))
			{
				strcat(pszBuffer, pLocalBuffer);
				nLength = strlen(pszBuffer);
			}
			else
			{
				*pnLength = nLength;
				ReleaseMutex(m_hMutex);
				return;
			}
			pValue = pKey->pValueList;
			while (pValue)
			{
				sprintf(pLocalBuffer, "   VALUE: ");
				//fwrite(pValue->pData, pValue->length, 1, stdout);
				strcat(pLocalBuffer, (const char *)pValue->pData);
				//printf("\n");
				strcat(pLocalBuffer, "\n");
				if ((int)strlen(pLocalBuffer) < (*pnLength - nLength))
				{
					strcat(pszBuffer, pLocalBuffer);
					nLength = strlen(pszBuffer);
				}
				else
				{
					*pnLength = nLength;
					ReleaseMutex(m_hMutex);
					return;
				}
				pValue = pValue->pNext;
			}
			pKey = pKey->pNext;
		}
		pNode = pNode->pNext;
	}
	sprintf(pLocalBuffer, "}\n");
	if ((int)strlen(pLocalBuffer) < (*pnLength - nLength))
	{
		strcat(pszBuffer, pLocalBuffer);
		nLength = strlen(pszBuffer);
	}
	*pnLength = nLength;

	ReleaseMutex(m_hMutex);
}

// Function name	: StrCatGrow
// Description	    : 
// Return type		: void 
// Argument         : char *&pBase
// Argument         : char *&pCur
// Argument         : int &length
// Argument         : char *pCat
// Argument         : int catlen
void StrCatGrow(char *&pBase, char *&pCur, int &length, char *pCat, int catlen)
{
	if (pCur - pBase + catlen > length)
	{
		char *pBuf = new char[2*length+catlen];
		memcpy(pBuf, pBase, length);
		length = 2*length + catlen;
		pCur = pBuf + (pCur - pBase);
		delete pBase;
		pBase = pBuf;
	}

	memcpy(pCur, pCat, catlen);
	pCur = pCur + catlen;
}

// Function name	: StrCatGrow
// Description	    : 
// Return type		: void 
// Argument         : char *&pBase
// Argument         : char *&pCur
// Argument         : int &length
// Argument         : char *pCat
void StrCatGrow(char *&pBase, char *&pCur, int &length, char *pCat)
{
	int catlen = strlen(pCat)+1;
	StrCatGrow(pBase, pCur, length, pCat, catlen);
	pCur--; // Backup to the '\0' character
}

// Function name	: Database::GetState
// Description	    : 
// Return type		: void 
// Argument         : char *pszOutput
// Argument         : int *length
int DatabaseServer::GetState(char *pszOutput, int *length)
{
	IDNode *pNode = m_pList;
	KeyNode *pKey;
	ValueNode *pValue;
	char pBuffer[1024];
	int len = 1024;
	char *pBase = new char[1024];
	char *pCur = pBase;
	pBase[0] = '\0';

	if (WaitForSingleObject(m_hMutex, DATABASE_TIMEOUT) != WAIT_OBJECT_0)
		return MPI_DBS_FAIL;

	StrCatGrow(pBase, pCur, len, "DATABASE:\n{\n");
	while (pNode)
	{
		sprintf(pBuffer, " NODE \"%s\"\n", pNode->pszID);
		StrCatGrow(pBase, pCur, len, pBuffer);
		pKey = pNode->pKeyList;
		while (pKey)
		{
			if (pKey->bPersistent)
			{
				sprintf(pBuffer, "  KEY \"%s\" persistent\n", pKey->pszKey);
				StrCatGrow(pBase, pCur, len, pBuffer);
			}
			else
			{
				sprintf(pBuffer, "  KEY \"%s\" consumable\n", pKey->pszKey);
				StrCatGrow(pBase, pCur, len, pBuffer);
			}
			pValue = pKey->pValueList;
			while (pValue)
			{
				StrCatGrow(pBase, pCur, len, "   VALUE: ");
				StrCatGrow(pBase, pCur, len, (char*)pValue->pData, pValue->length);
				StrCatGrow(pBase, pCur, len, "\n");
				pValue = pValue->pNext;
			}
			pKey = pKey->pNext;
		}
		pNode = pNode->pNext;
	}
	StrCatGrow(pBase, pCur, len, "}\n");

	int difference = pCur - pBase + 1;
	if (difference <= *length)
	{
		memcpy(pszOutput, pBase, difference);
		*length = difference;
		ReleaseMutex(m_hMutex);
		return MPI_DBS_SUCCESS;
	}

	*length = difference;
	ReleaseMutex(m_hMutex);
	return MPI_DBS_FAIL;
}
