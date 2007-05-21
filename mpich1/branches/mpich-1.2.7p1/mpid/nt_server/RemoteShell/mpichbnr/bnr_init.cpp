#include "bnr_internal.h"
#include <stdio.h>

// Function name	: GetNameForPipe
// Description	    : 
// Return type		: void 
// Argument         : char *pszName
void GetNameForPipe(char *pszName)
{
	char pszUserName[100];
	DWORD length;

	length = 100;
	if (GetEnvironmentVariable("MPD_RING_USER_NAME", pszUserName, 100))
	{
		sprintf(pszName, "\\\\.\\pipe\\mpd%s", pszUserName);
	}
	else
	{
		if (GetUserName(pszUserName, &length))
			sprintf(pszName, "\\\\.\\pipe\\mpd%s", pszUserName);
		else
			strcpy(pszName, "\\\\.\\pipe\\mpdpipe");
	}
}

// Function name	: NoRingInit
// Description	    : 
// Return type		: int 
int NoRingInit()
{
	char pBuffer[100];
	int nGroup, nRank = -1, nSize = -1, nParentGroup = -1, nParentSize = -1;
	if (GetEnvironmentVariable("BNR_GROUP", pBuffer, 100))
	{
		nGroup = atoi(pBuffer);
			
		if (GetEnvironmentVariable("BNR_RANK", pBuffer, 100))
			nRank = atoi(pBuffer);
		if (GetEnvironmentVariable("BNR_SIZE", pBuffer, 100))
			nSize = atoi(pBuffer);
	}
	else
	{
		nGroup = 123;
		nRank = 0;
		nSize = 1;
	}

	if (GetEnvironmentVariable("BNR_PARENT", pBuffer, 100))
	{
		nParentGroup = atoi(pBuffer);
		if (GetEnvironmentVariable("BNR_PARENT_SIZE", pBuffer, 100))
			nParentSize = atoi(pBuffer);
		g_bnrParent = AddBNRGroupToList(nParentGroup, -1, nParentSize);
	}
	else
		g_bnrParent = BNR_GROUP_NULL;

	g_bnrGroup = AddBNRGroupToList(nGroup, nRank, nSize, (BNR_Group_node*)g_bnrParent);

	if (nRank == -1 || nSize == -1)
		return BNR_FAIL;
	return BNR_SUCCESS;
}

// Function name	: BNR_Init
// Description	    : 
// Return type		: MPICH_BNR_API int 
MPICH_BNR_API int BNR_Init()
{
	int error;
	DWORD dwNumWritten;
	char pszPipeName[MAX_PATH] = "";
	char pBuffer[4096];

	if (g_hMPDPipe != NULL)
	{
		return BNR_SUCCESS;
	}

	GetNameForPipe(pszPipeName);
	//printf("connecting to pipe '%s'\n", pszPipeName);fflush(stdout);
	g_hMPDPipe = CreateFile(
		pszPipeName,
		GENERIC_WRITE,
		0, NULL,
		OPEN_EXISTING,
		0, NULL);
	
	if (g_hMPDPipe == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		if (NoRingInit() == BNR_SUCCESS)
			return BNR_SUCCESS;
		printf("BNR_Init: Unable to create pipe '%s', error %d\n", pszPipeName, error);fflush(stdout);
		return BNR_FAIL;
	}

	strcat(pszPipeName, "out");
	g_hMPDOutputPipe = CreateNamedPipe(
		pszPipeName,
		PIPE_ACCESS_INBOUND | FILE_FLAG_WRITE_THROUGH,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		PIPE_UNLIMITED_INSTANCES,
		0,0,0, 
		NULL
		);
	
	if (g_hMPDOutputPipe == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		CloseHandle(g_hMPDPipe);
		g_hMPDPipe = NULL;
		if (NoRingInit() == BNR_SUCCESS)
			return BNR_SUCCESS;
		printf("BNR_Init: Unable to create pipe: error %d on pipe '%s'\n", error, pszPipeName);
		return BNR_FAIL;
	}
	
	WriteFile(g_hMPDPipe, pszPipeName, strlen(pszPipeName)+1, &dwNumWritten, NULL);
	//printf("waiting for connection back on pipe '%s'\n", pszPipeName);fflush(stdout);
	if (!ConnectNamedPipe(g_hMPDOutputPipe, NULL))
	{
		error = GetLastError();
		if (error != ERROR_PIPE_CONNECTED)
		{
			CloseHandle(g_hMPDPipe);
			g_hMPDPipe = NULL;
			CloseHandle(g_hMPDOutputPipe);
			g_hMPDOutputPipe = NULL;
			if (NoRingInit() == BNR_SUCCESS)
				return BNR_SUCCESS;
			printf("BNR_Init: Unable to connect to client pipe '%s': error %d\n", pszPipeName, error);fflush(stdout);
			return BNR_FAIL;
		}
	}

	strcat(pszPipeName, "2");
	g_hMPDEndOutputPipe = CreateNamedPipe(
		pszPipeName,
		PIPE_ACCESS_INBOUND | FILE_FLAG_WRITE_THROUGH,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		PIPE_UNLIMITED_INSTANCES,
		0,0,0, 
		NULL
		);
	
	if (g_hMPDEndOutputPipe == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		CloseHandle(g_hMPDPipe);
		g_hMPDPipe = NULL;
		CloseHandle(g_hMPDOutputPipe);
		g_hMPDOutputPipe = NULL;
		if (NoRingInit() == BNR_SUCCESS)
			return BNR_SUCCESS;
		printf("BNR_Init: Unable to create pipe: error %d on pipe '%s'\n", error, pszPipeName);
		return BNR_FAIL;
	}
	
	WriteFile(g_hMPDPipe, pszPipeName, strlen(pszPipeName)+1, &dwNumWritten, NULL);
	//printf("waiting for connection back on pipe '%s'\n", pszPipeName);fflush(stdout);
	if (!ConnectNamedPipe(g_hMPDEndOutputPipe, NULL))
	{
		error = GetLastError();
		if (error != ERROR_PIPE_CONNECTED)
		{
			CloseHandle(g_hMPDPipe);
			g_hMPDPipe = NULL;
			CloseHandle(g_hMPDOutputPipe);
			g_hMPDOutputPipe = NULL;
			CloseHandle(g_hMPDEndOutputPipe);
			g_hMPDEndOutputPipe = NULL;
			if (NoRingInit() == BNR_SUCCESS)
				return BNR_SUCCESS;
			printf("BNR_Init: Unable to connect to client pipe '%s': error %d\n", pszPipeName, error);fflush(stdout);
			return BNR_FAIL;
		}
	}

	///////////////////////////////////////////////////////
	// Get or create the two groups: GROUP, PARENT
	//

	// If this process was spawned then the group id is placed in the environment.
	int nGroup, nRank = -1, nSize = -1, nParentGroup = -1, nParentSize = -1;
	if (GetEnvironmentVariable("BNR_GROUP", pBuffer, 4096))
	{
		nGroup = atoi(pBuffer);
			
		if (GetEnvironmentVariable("BNR_RANK", pBuffer, 4096))
			nRank = atoi(pBuffer);
		if (GetEnvironmentVariable("BNR_SIZE", pBuffer, 4096))
			nSize = atoi(pBuffer);
		else
		{
			char pBuffer[100];
			sprintf(pBuffer, "id %d\nget size\n", nGroup);
			WriteFile(g_hMPDPipe, "id %d\nget size\n", strlen(pBuffer), &dwNumWritten, NULL);
			if (GetString(g_hMPDOutputPipe, pBuffer))
			{
				printf("BNR_Init: GetString(group size) failed\n");fflush(stdout);
				return BNR_FAIL;
			}
			nSize = atoi(pBuffer);
		}
	}
	else
	{
		// If the process was not spawned, then a new group needs to be allocated.
		WriteFile(g_hMPDPipe, "create group\n", strlen("create group\n"), &dwNumWritten, NULL);
		if (GetString(g_hMPDOutputPipe, pBuffer))
		{
			printf("BNR_Init: GetString(group id) failed\n");fflush(stdout);
			return BNR_FAIL;
		}
		nGroup = atoi(pBuffer);
		nRank = 0;
		nSize = 1;
	}

	if (GetEnvironmentVariable("BNR_PARENT", pBuffer, 4096))
	{
		nParentGroup = atoi(pBuffer);
		if (GetEnvironmentVariable("BNR_PARENT_SIZE", pBuffer, 4096))
			nParentSize = atoi(pBuffer);
		g_bnrParent = AddBNRGroupToList(nParentGroup, -1, nParentSize);
	}
	else
		g_bnrParent = BNR_GROUP_NULL;

	g_bnrGroup = AddBNRGroupToList(nGroup, nRank, nSize, (BNR_Group_node*)g_bnrParent);

	//printf("id: %d, rank: %d, size: %d\nparent id: %d, parent size: %d\n", nGroup, nRank, nSize, nParentGroup, nParentSize);
	return BNR_SUCCESS;
}
