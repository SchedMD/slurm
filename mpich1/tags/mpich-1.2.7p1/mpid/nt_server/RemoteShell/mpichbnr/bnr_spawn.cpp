#include "bnr_internal.h"

int (*g_notify_fn)(BNR_Group group, int rank, int exit_code) = NULL;
LONG g_nProcessesRemaining = 0;
HANDLE g_hProcessFinishedThread = NULL;

void ProcessFinishedThread()
{
	char pBuffer[100];
	int nGroup, nRank;
	int nExitCode;
	int nRetVal;
	char *token;
	BNR_Group_node *pGroup;

	//printf("ProcessFinishedThread started, nProc: %d\n", g_nProcessesRemaining);fflush(stdout);
	while (true)
	{
		nRetVal = GetZString(g_hMPDEndOutputPipe, pBuffer);
		if (nRetVal)
		{
			printf("GetZString failed, error: %d\n", nRetVal);
			ExitThread(nRetVal);
		}
		
		//printf("%s\n", pBuffer);fflush(stdout);
		
		
		token = strtok(pBuffer, " ");
		if (token != NULL)
		{
			nGroup = atoi(token);
			pGroup = FindBNRGroupFromInt(nGroup);
			token = strtok(NULL, " ");
			if (token != NULL)
			{
				nRank = atoi(token);
				token = strtok(NULL, " ");
				if (token != NULL)
				{
					nExitCode = atoi(token);
					if (g_notify_fn != NULL)
						g_notify_fn(pGroup, nRank, nExitCode);
					nRetVal = InterlockedDecrement(&g_nProcessesRemaining);
					if (nRetVal == 0)
					{
						CloseHandle(g_hProcessFinishedThread);
						g_hProcessFinishedThread = NULL;
						return;
					}
				}
			}
		}
	}
}

/* not collective.
 * remote_group is an open BNR_Group and may be passed to Spawn 
 * multiple times. It is not valid until it is closed.  
 * BNR_Spawn will fail if remote_group is closed or uninitialized.
 * notify_fn is called if a process exits, and gets the
 * group, rank, and return code. argv and env
 * arrays are null terminated.  The caller's group is the
 * parent of the spawned processes.
 */

MPICH_BNR_API int BNR_Spawn( BNR_Group remote_group, 
						    int count, char *command, char *args, char *env, 
						    BNR_Info info, int (*notify_fn)(BNR_Group group, 
						    int rank, int exit_code) )
{
	int i;
	int flag;
	char pBuffer[4096], pszEnv[3072];
	char pszStdinHost[101], pszStdoutHost[101], pszStderrHost[101];
	int nStdinPort, nStdoutPort, nStderrPort;
	int group = ((BNR_Group_node*)remote_group)->nID;
	DWORD dwNumWritten;

	BNR_Info_get(info, "stdoutHost", 100, pszStdoutHost, &flag);
	BNR_Info_get(info, "stdoutPort", 100, pBuffer, &flag);
	nStdoutPort = atoi(pBuffer);
	BNR_Info_get(info, "stdinHost", 100, pszStdinHost, &flag);
	BNR_Info_get(info, "stdinPort", 100, pBuffer, &flag);
	nStdinPort = atoi(pBuffer);
	BNR_Info_get(info, "stderrHost", 100, pszStderrHost, &flag);
	BNR_Info_get(info, "stderrPort", 100, pBuffer, &flag);
	nStderrPort = atoi(pBuffer);

	SpawnedProcess *pProcesses = new SpawnedProcess[count];

	// get the next 'count' hosts available
	sprintf(pBuffer, "next %d\n", count);
	WriteFile(g_hMPDPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
	for (i=0; i<count; i++)
		GetString(g_hMPDOutputPipe, pProcesses[i].pszHost);

	// Make sure the exit output thread is ready before any processes are launched
	g_notify_fn = notify_fn;
	if (g_nProcessesRemaining == 0 && g_hProcessFinishedThread == NULL)
	{
		g_nProcessesRemaining = count;
		DWORD dwThreadID;
		g_hProcessFinishedThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ProcessFinishedThread, NULL, 0, &dwThreadID);
	}
	else
	{
		for (i=0; i<count; i++)
			InterlockedIncrement(&g_nProcessesRemaining);
	}

	// launch processes
	for (i=0; i<count; i++)
	{
		sprintf(pszEnv, "MPICH_BNR_LIB=mpichbnr.dll|BNR_RANK=%d|BNR_SIZE=%d|BNR_GROUP=%d|BNR_PARENT=%d|BNR_PARENT_SIZE=%d",
			i, count, group, ((BNR_Group_node*)g_bnrGroup)->nID, ((BNR_Group_node*)g_bnrGroup)->nSize);
		if (strlen(env))
		{
			strcat(pszEnv, "|");
			strcat(pszEnv, env);
		}
		if (i == 0)
			sprintf(pBuffer, "launch h'%s'c'%s'a'%s'g'%d'r'%d'e'%s'0'%s:%d'1'%s:%d'2'%s:%d'\n", 
			pProcesses[i].pszHost, command, args, group, i, pszEnv,
			pszStdinHost, nStdinPort, 
			pszStdoutHost, nStdoutPort, 
			pszStderrHost, nStderrPort);
		else
			sprintf(pBuffer, "launch h'%s'c'%s'a'%s'g'%d'r'%d'e'%s'1'%s:%d'2'%s:%d'\n", 
			pProcesses[i].pszHost, command, args, group, i, pszEnv,
			pszStdoutHost, nStdoutPort, 
			pszStderrHost, nStderrPort);
		WriteFile(g_hMPDPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
		GetString(g_hMPDOutputPipe, pProcesses[i].pszSpawnID);
	}

	// get the launch ids
	for (i=0; i<count; i++)
	{
		sprintf(pBuffer, "launchid %d\n", pProcesses[i].pszSpawnID);
		WriteFile(g_hMPDPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
		GetString(g_hMPDOutputPipe, pProcesses[i].pszLaunchID);
	}

	// store the process information in the group structure
	BNR_Group_node *pRemoteGroup = (BNR_Group_node*)remote_group;
	SpawnedProcessNode *pProcessNode = new SpawnedProcessNode;
	pProcessNode->nProc = count;
	pProcessNode->pProcesses = pProcesses;
	pProcessNode->pNext = pRemoteGroup->pProcessList;
	pRemoteGroup->pProcessList = pProcessNode;

	// Increase the size of the remote group 
	((BNR_Group_node*)remote_group)->nSize += count;

	return BNR_SUCCESS;
}
