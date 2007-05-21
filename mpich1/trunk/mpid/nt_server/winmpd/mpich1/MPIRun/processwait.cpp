#include <stdio.h>
#include <stdlib.h>
#include "LaunchProcess.h"
#include "global.h"
#include "mpirun.h"
#include "mpdutil.h"

struct ProcessWaitThreadArg
{
    int n;
    SOCKET *pSocket;
    int *pId;
    int *pRank;
    SOCKET sockAbort;
};

struct ProcessWaitAbortThreadArg
{
    SOCKET sockAbort;
    SOCKET sockStop;
    int n;
    SOCKET *pSocket;
};

// Function name	: ProcessWaitAbort
// Description	    : 
// Return type		: void 
// Argument         : ProcessWaitAbortThreadArg *pArg
void ProcessWaitAbort(ProcessWaitAbortThreadArg *pArg)
{
    int n, i;
    fd_set readset;

    FD_ZERO(&readset);
    FD_SET(pArg->sockAbort, &readset);
    FD_SET(pArg->sockStop, &readset);

    n = select(0, &readset, NULL, NULL, NULL);

    if (n == SOCKET_ERROR)
    {
	PrintError(WSAGetLastError(), "bselect failed\n");fflush(stdout);
	for (i=0; i<pArg->n; i++)
	{
	    easy_closesocket(pArg->pSocket[i]);
	}
	easy_closesocket(pArg->sockAbort);
	easy_closesocket(pArg->sockStop);
	return;
    }
    if (n == 0)
    {
	printf("ProcessWaitAbort: bselect returned zero sockets available\n");fflush(stdout);
	for (i=0; i<pArg->n; i++)
	{
	    easy_closesocket(pArg->pSocket[i]);
	}
	easy_closesocket(pArg->sockAbort);
	easy_closesocket(pArg->sockStop);
	return;
    }
    if (FD_ISSET(pArg->sockAbort, &readset))
    {
	for (i=0; i<pArg->n; i++)
	{
	    easy_send(pArg->pSocket[i], "x", 1);
	}
    }
    for (i=0; i<pArg->n; i++)
    {
	easy_closesocket(pArg->pSocket[i]);
    }
    easy_closesocket(pArg->sockAbort);
    easy_closesocket(pArg->sockStop);
}

// Function name	: ProcessWait
// Description	    : 
// Return type		: void 
// Argument         : ProcessWaitThreadArg *pArg
void ProcessWait(ProcessWaitThreadArg *pArg)
{
    int i, j, n;
    fd_set totalset, readset;
    char str[256];
    
    FD_ZERO(&totalset);
    
    FD_SET(pArg->sockAbort, &totalset);
    for (i=0; i<pArg->n; i++)
    {
	FD_SET(pArg->pSocket[i], &totalset);
    }
    
    while (pArg->n)
    {
	readset = totalset;
	n = select(0, &readset, NULL, NULL, NULL);
	if (n == SOCKET_ERROR)
	{
	    PrintError(WSAGetLastError(), "bselect failed\n");fflush(stdout);
	    for (i=0, j=0; i<pArg->n; i++, j++)
	    {
		while (pArg->pSocket[j] == INVALID_SOCKET)
		    j++;
		easy_closesocket(pArg->pSocket[j]);
		pArg->pSocket[j] = INVALID_SOCKET;
	    }
	    return;
	}
	if (n == 0)
	{
	    printf("ProcessWait: bselect returned zero sockets available");fflush(stdout);
	    for (i=0, j=0; i<pArg->n; i++, j++)
	    {
		while (pArg->pSocket[j] == INVALID_SOCKET)
		    j++;
		easy_closesocket(pArg->pSocket[j]);
		pArg->pSocket[j] = INVALID_SOCKET;
	    }
	    return;
	}

	if (FD_ISSET(pArg->sockAbort, &readset))
	{
	    for (i=0; pArg->n > 0; i++)
	    {
		while (pArg->pSocket[i] == INVALID_SOCKET)
		    i++;
		sprintf(str, "kill %d", pArg->pId[i]);
		//printf("%d:%s\n", __LINE__, str);fflush(stdout);
		WriteString(pArg->pSocket[i], str);

		int nRank = pArg->pRank[i];
		if (g_nNproc > FORWARD_NPROC_THRESHOLD)
		{
		    if (nRank > 0 && (g_nNproc/2) > nRank)
		    {
			//printf("rank %d(%d) stopping forwarder\n", nRank, g_pProcessLaunchId[i]);fflush(stdout);
			sprintf(str, "stopforwarder port=%d abort=yes", g_pForwardHost[nRank].nPort);
			WriteString(pArg->pSocket[i], str);
		    }
		}

		sprintf(str, "freeprocess %d", pArg->pId[i]);
		pArg->pId[i] = -1; // nobody should use the id after we free it
		WriteString(pArg->pSocket[i], str);
		ReadStringTimeout(pArg->pSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
		WriteString(pArg->pSocket[i], "done");
		easy_closesocket(pArg->pSocket[i]);
		pArg->pSocket[i] = INVALID_SOCKET;
		pArg->n--;
	    }
	    return;
	}
	for (i=0; n>0; i++)
	{
	    while (pArg->pSocket[i] == INVALID_SOCKET)
		i++;
	    if (FD_ISSET(pArg->pSocket[i], &readset))
	    {
		if (ReadStringTimeout(pArg->pSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT))
		{
		    int nRank = pArg->pRank[i];
		    
		    if (strnicmp(str, "FAIL", 4) == 0)
		    {
			// get the error
			sprintf(str, "geterror %d", pArg->pId[i]);
			WriteString(pArg->pSocket[i], str);
			ReadStringTimeout(pArg->pSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
			printf("getexitcode(rank %d) failed: %s\n", nRank, str);fflush(stdout);
			if (g_bUseJobHost)
			{
			    UpdateJobKeyValue(nRank, "error", str);
			    
			    // get the time the process exited
			    sprintf(str, "getexittime %d", pArg->pId[i]);
			    WriteString(pArg->pSocket[i], str);
			    ReadStringTimeout(pArg->pSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
			    UpdateJobKeyValue(nRank, "exittime", str);
			}
			
			if (easy_send(g_sockBreak, "x", 1) == SOCKET_ERROR)
			{
			    printf("Hard abort.\n");fflush(stdout);
			    ExitProcess(-1);
			}
		    }
		    else
		    {
			if (g_bUseJobHost)
			{
			    strtok(str, ":"); // strip the extra data from the string
			    UpdateJobKeyValue(nRank, "exitcode", str);
			    
			    char *temp;
			    if (g_bOutputExitCodes)
				temp = strdup(str);
			    // get the time the process exited
			    sprintf(str, "getexittime %d", pArg->pId[i]);
			    WriteString(pArg->pSocket[i], str);
			    ReadStringTimeout(pArg->pSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
			    UpdateJobKeyValue(nRank, "exittime", str);
			    if (g_bOutputExitCodes)
			    {
				printf("[rank %d exit code: %s, time: %s]\n", nRank, temp, str);fflush(stdout);
				free(temp);
			    }
			}
			else
			{
			    if (g_bOutputExitCodes)
			    {
				strtok(str, ":"); // strip the extra data from the string
				printf("[rank %d exit code: %s]\n", nRank, str);fflush(stdout);
			    }
			}

			if (!g_bNoMPI)
			{
			    if (g_bMPICH2)
			    {
				/*
				sprintf(str, "dbget name='%s' key='P-%d.finalized'", pmi_kvsname, nRank);
				WriteString(pArg->pSocket[i], str);
				ReadStringTimeout(pArg->pSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
				if (stricmp(str, "true") != 0)
				{
				    if (stricmp(str, "false") != 0)
					printf("dbget(%s:P-%d.finalized) returned: %s\n", pmi_kvsname, nRank, str);
				    else
				    {
					if (!g_bSuppressErrorOutput)
					    printf("process %d on %s exited without calling MPIFinalize\n", nRank, g_pProcessHost[nRank].host);
				    }
				    fflush(stdout);
				    easy_send(g_sockBreak, "x", 1);
				}
				*/
			    }
			    else
			    {
				if (g_bMPICH2)
				{
				    /*
				    sprintf(str, "dbget name='%s' key='P-%d.finalized'", pmi_kvsname, nRank);
				    WriteString(pArg->pSocket[i], str);
				    ReadStringTimeout(pArg->pSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
				    if (stricmp(str, "true") != 0)
				    {
					if (stricmp(str, "false") != 0)
					    printf("dbget(%s:P-%d.finalized) returned: %s\n", pmi_kvsname, nRank, str);
					else
					{
					    if (!g_bSuppressErrorOutput)
						printf("process %d on %s exited without calling MPIFinalize\n", nRank, g_pProcessHost[nRank].host);
					}
					fflush(stdout);
					easy_send(g_sockBreak, "x", 1);
				    }
				    */
				}
				else
				{
				    sprintf(str, "getmpifinalized %d", pArg->pId[i]);
				    WriteString(pArg->pSocket[i], str);
				    ReadStringTimeout(pArg->pSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
				    if (stricmp(str, "yes") != 0)
				    {
					if (stricmp(str, "no") != 0)
					    printf("getmpifinalized returned: %s\n", str);
					else
					{
					    if (!g_bSuppressErrorOutput)
						printf("process %d on %s exited without calling MPIFinalize\n", nRank, g_pProcessHost[nRank].host);
					}
					fflush(stdout);
					easy_send(g_sockBreak, "x", 1);
				    }
				}
			    }
			}
		    }
		    
		    if (g_nNproc > FORWARD_NPROC_THRESHOLD)
		    {
			if (nRank > 0 && (g_nNproc/2) > nRank)
			{
			    sprintf(str, "stopforwarder port=%d abort=no", g_pForwardHost[nRank].nPort);
			    WriteString(pArg->pSocket[i], str);
			}
		    }
		    
		    sprintf(str, "freeprocess %d", pArg->pId[i]);
		    pArg->pId[i] = -1; // nobody should use the id after we free it
		    WriteString(pArg->pSocket[i], str);
		    ReadStringTimeout(pArg->pSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
		    
		    WriteString(pArg->pSocket[i], "done");
		    easy_closesocket(pArg->pSocket[i]);
		    FD_CLR(pArg->pSocket[i], &totalset);
		    pArg->pSocket[i] = INVALID_SOCKET;
		    n--;
		    pArg->n--;
		}
		else
		{
		    PrintError(WSAGetLastError(), "ProcessWait:Reading the exit code for process %d failed\n", i);fflush(stdout);
		    easy_closesocket(pArg->pSocket[i]);
		    FD_CLR(pArg->pSocket[i], &totalset);
		    pArg->pSocket[i] = INVALID_SOCKET;
		    n--;
		    pArg->n--;
		    if (easy_send(g_sockBreak, "x", 1) == SOCKET_ERROR)
		    {
			printf("Unable to abort processes.\n");fflush(stdout);
			ExitProcess(-1);
		    }
		    //return;
		}
	    }
	}
    }
}

// Function name	: WaitForExitCommands
// Description	    : 
// Return type		: void 
void WaitForExitCommands()
{
    bool bKillSent = false;
    int iter;
    if (g_nNumProcessSockets < FD_SETSIZE)
    {
	int i, n;
	fd_set totalset, readset;
	char str[256];
	SOCKET break_sock;

	//printf("waiting for %d processes\n", g_nNumProcessSockets);fflush(stdout);
	/*
	for (i=0; i<g_nNumProcessSockets; i++)
	{
	    printf("socket[%d] = id %d\n", g_pProcessSocket[i], g_pProcessLaunchId[i]);
	}
	fflush(stdout);
	*/

	MakeLoop(&break_sock, &g_sockBreak);
	SetEvent(g_hBreakReadyEvent); // allow a break to happen if the user has already hit Ctrl-C

	FD_ZERO(&totalset);

	FD_SET(break_sock, &totalset);
	for (i=0; i<g_nNumProcessSockets; i++)
	{
	    FD_SET(g_pProcessSocket[i], &totalset);
	}

	while (g_nNumProcessSockets)
	{
	    readset = totalset;
	    n = select(0, &readset, NULL, NULL, NULL);
	    if (n == SOCKET_ERROR)
	    {
		PrintError(WSAGetLastError(), "WaitForExitCommands: bselect failed\n");fflush(stdout);
		for (i=0; g_nNumProcessSockets > 0; i++)
		{
		    while (g_pProcessSocket[i] == INVALID_SOCKET)
			i++;
		    //printf("closing socket [%d]: %s\n", i, bto_string(g_pProcessSocket[i]));fflush(stdout);
		    easy_closesocket(g_pProcessSocket[i]);
		    g_pProcessSocket[i] = INVALID_SOCKET;
		    g_nNumProcessSockets--;
		}
		return;
	    }
	    if (n == 0)
	    {
		printf("WaitForExitCommands: bselect returned zero sockets available\n");fflush(stdout);
		for (i=0; g_nNumProcessSockets > 0; i++)
		{
		    while (g_pProcessSocket[i] == INVALID_SOCKET)
			i++;
		    //printf("closing socket [%d]: %s\n", i, bto_string(g_pProcessSocket[i]));fflush(stdout);
		    easy_closesocket(g_pProcessSocket[i]);
		    g_pProcessSocket[i] = INVALID_SOCKET;
		    g_nNumProcessSockets--;
		}
		return;
	    }
	    else
	    {
		if (FD_ISSET(break_sock, &readset))
		{
		    int num_read = easy_receive(break_sock, str, 1);
		    if (num_read == 0 || num_read == SOCKET_ERROR)
		    {
			FD_CLR(break_sock, &totalset);
		    }
		    else
		    {
			if (!bKillSent)
			{
			    printf("Sending kill commands to launched processes\n");fflush(stdout);
			    for (int j=0, i=0; i<g_nNumProcessSockets; i++, j++)
			    {
				while (g_pProcessSocket[j] == INVALID_SOCKET)
				    j++;
				sprintf(str, "kill %d", g_pProcessLaunchId[j]);
				//printf("%d:%s\n", __LINE__, str);fflush(stdout);
				//printf("kill %d (id[%d])\n", g_pProcessLaunchId[j], j);fflush(stdout);
				if (WriteString(g_pProcessSocket[j], str) == SOCKET_ERROR)
				{
				    printf("writing kill command failed\n");fflush(stdout);
				}
			    }
			    bKillSent = true;
			}
		    }
		    n--;
		}
		for (i=0; n>0; i++)
		{
		    while (g_pProcessSocket[i] == INVALID_SOCKET)
			i++;
		    if (FD_ISSET(g_pProcessSocket[i], &readset))
		    {
			if (ReadStringTimeout(g_pProcessSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT))
			{
			    int nRank = g_pLaunchIdToRank[i];
			    
			    if (strnicmp(str, "FAIL", 4) == 0)
			    {
				sprintf(str, "geterror %d", g_pProcessLaunchId[i]);
				WriteString(g_pProcessSocket[i], str);
				ReadStringTimeout(g_pProcessSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
				printf("getexitcode(rank %d) failed: %s\n", nRank, str);fflush(stdout);
				
				if (g_bUseJobHost)
				{
				    UpdateJobKeyValue(nRank, "error", str);

				    // get the time the process exited
				    sprintf(str, "getexittime %d", g_pProcessLaunchId[i]);
				    WriteString(g_pProcessSocket[i], str);
				    ReadStringTimeout(g_pProcessSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
				    UpdateJobKeyValue(nRank, "exittime", str);
				}
				
				if (easy_send(g_sockBreak, "x", 1) == SOCKET_ERROR)
				{
				    printf("Aborting.\n");fflush(stdout);
				    ExitProcess(-1);
				}
			    }
			    else
			    {
				//printf("[%d] ExitProcess: %s\n", nRank, str);fflush(stdout);
				if (g_bUseJobHost)
				{
				    strtok(str, ":"); // strip the extra data from the string
				    UpdateJobKeyValue(nRank, "exitcode", str);
				
				    char *temp;
				    if (g_bOutputExitCodes)
					temp = strdup(str);
				    // get the time the process exited
				    sprintf(str, "getexittime %d", g_pProcessLaunchId[i]);
				    WriteString(g_pProcessSocket[i], str);
				    ReadStringTimeout(g_pProcessSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
				    UpdateJobKeyValue(nRank, "exittime", str);
				    if (g_bOutputExitCodes)
				    {
					printf("[rank %d exit code: %s, time: %s]\n", nRank, temp, str);fflush(stdout);
					free(temp);
				    }
				}
				else
				{
				    if (g_bOutputExitCodes)
				    {
					strtok(str, ":"); // strip the extra data from the string
					printf("[rank %d exit code: %s]\n", nRank, str);fflush(stdout);
				    }
				}

				if (!g_bNoMPI)
				{
				    if (g_bMPICH2)
				    {
					/*
					sprintf(str, "dbget name='%s' key='P-%d.finalized'", pmi_kvsname, nRank);
					WriteString(g_pProcessSocket[i], str);
					ReadStringTimeout(g_pProcessSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
					if (stricmp(str, "true") != 0)
					{
					    if (stricmp(str, "false") != 0)
						printf("dbget(%s:P-%d.finalized) returned: %s\n", pmi_kvsname, nRank, str);
					    else
					    {
						if (!g_bSuppressErrorOutput)
						    printf("process %d on %s exited without calling MPIFinalize\n", nRank, g_pProcessHost[nRank].host);
					    }
					    fflush(stdout);
					    easy_send(g_sockBreak, "x", 1);
					}
					*/
				    }
				    else
				    {
					sprintf(str, "getmpifinalized %d", g_pProcessLaunchId[i]);
					WriteString(g_pProcessSocket[i], str);
					ReadStringTimeout(g_pProcessSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
					if (stricmp(str, "yes") != 0)
					{
					    if (stricmp(str, "no") != 0)
						printf("getmpifinalized returned: %s\n", str);
					    else
					    {
						if (!g_bSuppressErrorOutput)
						    printf("process %d on %s exited without calling MPIFinalize\n", nRank, g_pProcessHost[nRank].host);
					    }
					    fflush(stdout);
					    easy_send(g_sockBreak, "x", 1);
					}
				    }
				}
			    }
			    
			    if (g_nNproc > FORWARD_NPROC_THRESHOLD)
			    {
				if (nRank > 0 && (g_nNproc/2) > nRank)
				{
				    //printf("rank %d(%d) stopping forwarder\n", nRank, g_pProcessLaunchId[i]);fflush(stdout);
				    sprintf(str, "stopforwarder port=%d abort=no", g_pForwardHost[nRank].nPort);
				    WriteString(g_pProcessSocket[i], str);
				}
			    }

			    /////////////////////////////////////////////////////////////////////////////
			    // Maybe we need the process structure around to get the exit codes or errors
			    sprintf(str, "freeprocess %d", g_pProcessLaunchId[i]);
			    g_pProcessLaunchId[i] = -1; // nobody should use the id after we free it
			    WriteString(g_pProcessSocket[i], str);
			    ReadStringTimeout(g_pProcessSocket[i], str, g_nMPIRUN_SHORT_TIMEOUT);
			    /////////////////////////////////////////////////////////////////////////////
			    
			    WriteString(g_pProcessSocket[i], "done");
			    //printf("closing socket [%d]: %s\n", i, bto_string(g_pProcessSocket[i]));fflush(stdout);
			    FD_CLR(g_pProcessSocket[i], &totalset);
			    easy_closesocket(g_pProcessSocket[i]);
			    g_pProcessSocket[i] = INVALID_SOCKET;
			    //printf("closing socket [%d]\n", i);fflush(stdout);
			    n--;
			    g_nNumProcessSockets--;
			    //printf("(E:%d)", g_pProcessLaunchId[i]);fflush(stdout);
			}
			else
			{
			    if (WSAGetLastError() != 0)
				PrintError(WSAGetLastError(), "WaitForExitCommands:Reading the exit code for process %d failed.\n", i);
			    else
			    {
				printf("WaitForExitCommands:Reading the exit code for process %d failed.\n", i);
				fflush(stdout);
			    }
			    FD_CLR(g_pProcessSocket[i], &totalset);
			    sprintf(str, "kill %d", g_pProcessLaunchId[i]);
			    WriteString(g_pProcessSocket[i], str);
			    WriteString(g_pProcessSocket[i], "done");
			    //printf("closing socket [%d]: %s\n", i, bto_string(g_pProcessSocket[i]));fflush(stdout);
			    easy_closesocket(g_pProcessSocket[i]);
			    g_pProcessSocket[i] = INVALID_SOCKET;
			    //printf("closing socket [%d]\n", i);fflush(stdout);
			    n--;
			    g_nNumProcessSockets--;
			    if (easy_send(g_sockBreak, "x", 1) == SOCKET_ERROR)
			    {
				printf("Unable to abort processes.\n");fflush(stdout);
				ExitProcess(-1);
			    }
			}
		    }
		}
	    }
	}
	
	easy_closesocket(g_sockBreak);
	g_sockBreak = INVALID_SOCKET;
	delete [] g_pProcessSocket;
	delete [] g_pProcessLaunchId;
	delete [] g_pLaunchIdToRank;
	g_pProcessSocket = NULL;
	g_pProcessLaunchId = NULL;
	g_pLaunchIdToRank = NULL;
    }
    else
    {
	DWORD dwThreadID;
	int num = (g_nNumProcessSockets / (FD_SETSIZE-1)) + 1;
	HANDLE *hThread = new HANDLE[num];
	SOCKET *pAbortsock = new SOCKET[num];
	SOCKET sockStop;
	ProcessWaitThreadArg *arg = new ProcessWaitThreadArg[num];
	ProcessWaitAbortThreadArg *arg2 = new ProcessWaitAbortThreadArg;
        int i;
	for (i=0; i<num; i++)
	{
	    if (i == num-1)
		arg[i].n = g_nNumProcessSockets % (FD_SETSIZE-1);
	    else
		arg[i].n = (FD_SETSIZE-1);
	    arg[i].pSocket = &g_pProcessSocket[i*(FD_SETSIZE-1)];
	    arg[i].pId = &g_pProcessLaunchId[i*(FD_SETSIZE-1)];
	    arg[i].pRank = &g_pLaunchIdToRank[i*(FD_SETSIZE-1)];
	    MakeLoop(&arg[i].sockAbort, &pAbortsock[i]);
	}
	for (i=0; i<num; i++)
	{
	    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
	    {
		hThread[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ProcessWait, &arg[i], 0, &dwThreadID);
		if (hThread[i] != NULL)
		    break;
		Sleep(CREATE_THREAD_SLEEP_TIME);
	    }
	}
	MakeLoop(&arg2->sockAbort, &g_sockBreak);
	MakeLoop(&arg2->sockStop, &sockStop);

	HANDLE hWaitAbortThread;
	for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
	{
	    hWaitAbortThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ProcessWaitAbort, arg2, 0, &dwThreadID);
	    if (hWaitAbortThread != NULL)
		break;
	    Sleep(CREATE_THREAD_SLEEP_TIME);
	}

	SetEvent(g_hBreakReadyEvent);

	WaitForMultipleObjects(num, hThread, TRUE, INFINITE);
	for (i=0; i<num; i++)
	    CloseHandle(hThread[i]);
	delete hThread;
	delete arg;

	easy_send(sockStop, "x", 1);
	easy_closesocket(sockStop);
	WaitForSingleObject(hWaitAbortThread, 10000);
	delete pAbortsock;
	delete arg2;
	CloseHandle(hWaitAbortThread);

	easy_closesocket(g_sockBreak);
	g_sockBreak = INVALID_SOCKET;
	delete g_pProcessSocket;
	delete g_pProcessLaunchId;
	delete g_pLaunchIdToRank;
	g_pProcessSocket = NULL;
	g_pProcessLaunchId = NULL;
	g_pLaunchIdToRank = NULL;
    }
    //printf("WaitForExitCommands returning\n");fflush(stdout);
}
