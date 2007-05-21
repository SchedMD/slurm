#include "LaunchProcess.h"
#include <stdio.h>
#include "global.h"
#include "..\Common\MPIJobDefs.h"
#include "Translate_Error.h"
#include "mpdutil.h"
#include "mpd.h"
#include "RedirectIO.h"
#include <stdlib.h>

static char *GenerateMapString(MapDriveNode *pNode)
{
    char *str, *ret_val;
    if (pNode == NULL)
	return NULL;
    ret_val = str = new char[8192];
    str += sprintf(str, " m='%c:%s", pNode->cDrive, pNode->pszShare);
    pNode = pNode->pNext;
    while (pNode)
    {
	str += sprintf(str, ";%c:%s", pNode->cDrive, pNode->pszShare);
	pNode = pNode->pNext;
    }
    strcpy(str, "'");
    return ret_val;
}

bool HostIsLocal(char *pszHost)
{
    char temp[100], localhost[100];
    DWORD len = 100;

    //return false;

    strcpy(temp, pszHost);
    // get rid of the domain extension
    strtok(temp, ".");
    // get the local computer name
    GetComputerName(localhost, &len);
    // compare the computer name to the provided name
    if (stricmp(temp, localhost) == 0)
	return true;

    if (gethostname(localhost, 100) != SOCKET_ERROR)
    {
	// compare to the result of gethostname
	if (stricmp(pszHost, localhost) == 0)
	    return true;
	// compare to an ip string
	easy_get_ip_string(localhost, localhost);
	if (stricmp(pszHost, localhost) == 0)
	    return true;
	// convert to an ip string and then compare
	strcpy(temp, pszHost);
	easy_get_ip_string(temp, temp);
	if (stricmp(pszHost, temp) == 0)
	    return true;
    }
    return false;
}

// Function name	: LaunchProcess
// Description	    : 
// Return type		: void 
// Argument         : LaunchProcessArg *arg
void MPIRunLaunchProcess(MPIRunLaunchProcessArg *arg)
{
    DWORD length = 100;
    HANDLE hRIThread = NULL;
    long error;
    int nPid;
    int nPort = MPD_DEFAULT_PORT;
    SOCKET sock, root_sock;
    int launchid;
    char pszStartupDB[100];
    char pszStr[MAX_CMD_LENGTH+1];
    char pszIOE[10];
    char *dbg_str = "no";
    char *pszMap = NULL;
    bool bLocalStartup = false;

    if (arg->bUseDebugFlag)
	dbg_str = "yes";

    if (arg->i == 0 && g_bLocalRoot && HostIsLocal(arg->pszHost))
	bLocalStartup = true;

    //printf("MPIRunLaunchProcess:connecting to %s:%d rank %d\n", arg->pszHost, nPort, arg->i);fflush(stdout);
    error = ConnectToMPD(arg->pszHost, nPort, arg->pszPassPhrase, &sock);

    if (error == 0)
    {
	if (!g_bMPICH2)
	{
	    if (arg->i == 0 && !g_bNoMPI)
	    {
		sprintf(pszStr, "dbcreate");
		if (WriteString(sock, pszStr) == SOCKET_ERROR)
		{
		    printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
		    //ExitProcess(0);
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    return;
		}
		// read result
		if (!ReadStringTimeout(sock, pszStartupDB, g_nMPIRUN_SHORT_TIMEOUT))
		{
		    printf("ERROR: ReadString failed to read the database name: error %d\n", WSAGetLastError());
		    //ExitProcess(0);
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    return;
		}
		if (strnicmp(pszStartupDB, "FAIL ", 5) == 0)
		{
		    printf("Unable to create a database on '%s'\n%s", arg->pszHost, pszStartupDB);fflush(stdout);
		    //ExitProcess(0);
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    return;
		}
		// The ordering of the arguments may seem funny because pszHost is last.
		// I think this needs to be this way so old mpd's can still launch new mpich processes
		sprintf(pszStr, "|MPICH_EXTRA=mpd:%s:%d:%s:%s", pszStartupDB, nPort, arg->pszPassPhrase, arg->pszHost);
		strncat(arg->pszEnv, pszStr, MAX_CMD_LENGTH - 1 - strlen(arg->pszEnv));

		if (g_bUseJobHost)
		{
		    PutJobInDatabase(arg);
		}
	    }
	    else
	    {
		sprintf(pszStr, "|MPICH_EXTRA=mpd:%s:%d:%s", arg->pszHost, nPort, arg->pszPassPhrase);
		strncat(arg->pszEnv, pszStr, MAX_CMD_LENGTH - 1 - strlen(arg->pszEnv));
	    }
	}

	if (arg->i == 0)
	    strcpy(pszIOE, "012"); // only redirect stdin to the root process
	else
	    strcpy(pszIOE, "12");

	if (g_nNproc > FORWARD_NPROC_THRESHOLD)
	{
	    if (arg->i > 0)
	    {
		while (g_pForwardHost[(arg->i - 1)/2].nPort == 0)
		    Sleep(100);
		sprintf(arg->pszIOHostPort, "%s:%d", g_pForwardHost[(arg->i - 1)/2].pszHost, g_pForwardHost[(arg->i - 1)/2].nPort);
		if (g_nNproc/2 > arg->i)
		{
		    strncpy(g_pForwardHost[arg->i].pszHost, arg->pszHost, MAX_HOST_LENGTH);
		    g_pForwardHost[arg->i].pszHost[MAX_HOST_LENGTH-1] = '\0';
		    sprintf(pszStr, "createforwarder host=%s forward=%s", arg->pszHost, arg->pszIOHostPort);
		    WriteString(sock, pszStr);
		    ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
		    int nTempPort = atoi(pszStr);
		    if (nTempPort == -1)
		    {
			// If creating the forwarder fails, redirect output to the root instead
			g_pForwardHost[arg->i] = g_pForwardHost[0];
		    }
		    else
			g_pForwardHost[arg->i].nPort = nTempPort;
		    //printf("forwarder %s:%d\n", g_pForwardHost[arg->i].pszHost, g_pForwardHost[arg->i].nPort);fflush(stdout);
		}
	    }
	}

	if (g_pDriveMapList)
	    pszMap = GenerateMapString(g_pDriveMapList);

	// LaunchProcess
	//printf("MPIRunLaunchProcess:launching on %s, %s\n", arg->pszHost, arg->pszCmdLine);fflush(stdout);
	if (arg->bLogon)
	{
	    char *pszEncoded;
	    pszEncoded = EncodePassword(arg->pszPassword);
	    if (strlen(arg->pszDir) > 0)
	    {
		if (_snprintf(pszStr, MAX_CMD_LENGTH, "launch h=%s c='%s' e='%s' a=%s p=%s %s=%s k=%d d='%s' g=%s", 
		    arg->pszHost, arg->pszCmdLine, arg->pszEnv, arg->pszAccount, pszEncoded, 
		    pszIOE, arg->pszIOHostPort, arg->i, arg->pszDir, dbg_str) < 0)
		{
		    printf("ERROR: command exceeds internal buffer size\n");
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    if (pszEncoded != NULL) free(pszEncoded);
		    return;
		}
	    }
	    else
	    {
		if (_snprintf(pszStr, MAX_CMD_LENGTH, "launch h=%s c='%s' e='%s' a=%s p=%s %s=%s k=%d g=%s", 
		    arg->pszHost, arg->pszCmdLine, arg->pszEnv, arg->pszAccount, pszEncoded, 
		    pszIOE, arg->pszIOHostPort, arg->i, dbg_str) < 0)
		{
		    printf("ERROR: command exceeds internal buffer size\n");
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    if (pszEncoded != NULL) free(pszEncoded);
		    return;
		}
	    }
	    if (pszEncoded != NULL) free(pszEncoded);
	}
	else
	{
	    if (strlen(arg->pszDir) > 0)
	    {
		if (_snprintf(pszStr, MAX_CMD_LENGTH, "launch h=%s c='%s' e='%s' %s=%s k=%d d='%s' g=%s",
		    arg->pszHost, arg->pszCmdLine, arg->pszEnv, 
		    pszIOE, arg->pszIOHostPort, arg->i, arg->pszDir, dbg_str) < 0)
		{
		    printf("ERROR: command exceeds internal buffer size\n");
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    return;
		}
	    }
	    else
	    {
		if (_snprintf(pszStr, MAX_CMD_LENGTH, "launch h=%s c='%s' e='%s' %s=%s k=%d g=%s",
		    arg->pszHost, arg->pszCmdLine, arg->pszEnv, 
		    pszIOE, arg->pszIOHostPort, arg->i, dbg_str) < 0)
		{
		    printf("ERROR: command exceeds internal buffer size\n");
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    return;
		}
	    }
	}
	if (arg->bUsePriorities)
	{
	    char str[100];
	    sprintf(str, " r='%d:%d'", arg->nPriorityClass, arg->nPriority);
	    strcat(pszStr, str);
	}
	if (pszMap)
	{
	    strcat(pszStr, pszMap);
	    delete [] pszMap;
	}
	//printf("MPIRunLaunchProcess:launch command = %s\n", pszStr);fflush(stdout);
	if (bLocalStartup)
	{
	    // launch process
	    LaunchRootProcess(pszStr, &root_sock, &nPid);
	    launchid = 1010101;
	}
	else
	{
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("ERROR: Unable to send launch command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    if (!ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT))
	    {
		printf("ERROR: Unable to read the result of the launch command for process %d sent to '%s'\r\nError %d", arg->i, arg->pszHost, WSAGetLastError());
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    launchid = atoi(pszStr);
	    // save the launch id, get the pid
	    sprintf(pszStr, "getpid %d", launchid);
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("ERROR: Unable to send getpid command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    // the following timeout needs to be longer than MPIRUN_SHORT_TIMEOUT because the CreateProcess command may take
	    // a while to start the process.  For example: it may live on a shared directory
	    /*printf("launching process with timeout: %d seconds\n", g_nMPIRUN_CREATE_PROCESS_TIMEOUT);fflush(stdout);*/
	    if (!ReadStringTimeout(sock, pszStr, g_nMPIRUN_CREATE_PROCESS_TIMEOUT))
	    {
		error = WSAGetLastError();
		if (error == ERROR_TIMEOUT || error == 0)
		{
		    printf("Launch process error: Timed out waiting for the result of the process launch command sent to host '%s' for process %d\r\n", arg->pszHost, arg->i);
		}
		else
		{
		    printf("Launch process error: Unable to read the result of the getpid command sent to '%s' for process %d\r\nError %d", arg->pszHost, arg->i, error);
		    fflush(stdout);
		}
		printf("Attempt to launch process %d (%s) on '%s' failed.\n", arg->i, arg->pszCmdLine, arg->pszHost);
		fflush(stdout);
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    nPid = atoi(pszStr);
	    if (nPid == -1)
	    {
		sprintf(pszStr, "geterror %d", launchid);
		if (WriteString(sock, pszStr) == SOCKET_ERROR)
		{
		    printf("ERROR: Unable to send geterror command after an unsuccessful launch on '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    return;
		}
		if (!ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT))
		{
		    printf("ERROR: Unable to read the result of the geterror command on '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    return;
		}
		if (strcmp(pszStr, "ERROR_SUCCESS"))
		{
		    if (arg->i == 0 && !g_bNoMPI)
		    {
			printf("Failed to launch the root process:\n%s\n%s\n", arg->pszCmdLine, pszStr);fflush(stdout);
		    }
		    else
		    {
			printf("Failed to launch process %d:\n'%s'\n%s\n", arg->i, arg->pszCmdLine, pszStr);fflush(stdout);
		    }
		    
		    sprintf(pszStr, "freeprocess %d", launchid);
		    WriteString(sock, pszStr);
		    ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
		    WriteString(sock, "done");
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    return;
		}
	    }
	}

	// Get the port number and redirect input to the first process
	if (arg->i == 0 && !g_bNoMPI && !g_bMPICH2)
	{
	    /*
	    // Check if the root process is alive
	    sprintf(pszStr, "getexitcode %d", launchid);
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("Error: Unable to send a getexitcode command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());fflush(stdout);
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    if (!ReadStringTimeout(sock, pszStr, g_nLaunchTimeout))
	    {
		printf("ERROR: Unable to read the result of the root getexitcode command on '%s': error %d", arg->pszHost, WSAGetLastError());
		WriteString(sock, "done");
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    if (stricmp(pszStr, "ACTIVE") != 0)
	    {
		printf("ERROR: The root process on %s has unexpectedly exited.\n", arg->pszHost);
		WriteString(sock, "done");
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    */

	    // barrier to let the root process do the put
	    sprintf(pszStr, "barrier name=%s count=2", arg->pszJobID);
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("ERROR: Unable to write the barrier command: error %d", WSAGetLastError());
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    bool bBarrierLoopContinue = true;
	    while (bBarrierLoopContinue)
	    {
		if (!ReadStringTimeout(sock, pszStr, g_nLaunchTimeout))
		{
		    error = WSAGetLastError();
		    if (error != 0)
		    {
			printf("ERROR: Unable to read the result of the barrier command on '%s': error %d", 
			    arg->pszHost, error);
		    }
		    else
		    {
			if (bLocalStartup)
			{
			    // check to see if the process is still running
			}
			else
			{
			    sprintf(pszStr, "getexitcode %d", launchid);
			    if (WriteString(sock, pszStr) == SOCKET_ERROR)
			    {
				printf("Error: Unable to send a getexitcode command to '%s'\r\nError %d", 
				    arg->pszHost, WSAGetLastError());fflush(stdout);
				easy_closesocket(sock);
				SetEvent(g_hAbortEvent);
				delete arg;
				return;
			    }
			    if (!ReadStringTimeout(sock, pszStr, g_nLaunchTimeout))
			    {
				printf("ERROR: Unable to read the result of the root getexitcode command on '%s': error %d", 
				    arg->pszHost, WSAGetLastError());
				sprintf(pszStr, "freeprocess %d", launchid);
				WriteString(sock, pszStr);
				ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
				WriteString(sock, "done");
				easy_closesocket(sock);
				SetEvent(g_hAbortEvent);
				delete arg;
				return;
			    }
			    if (stricmp(pszStr, "ACTIVE") == 0)
			    {
				printf("ERROR: timed-out waiting for the root process to call MPI_Init\n");
				if (g_bUseJobHost)
				{
				    // Save this process's information to the job database
				    PutJobProcessInDatabase(arg, nPid);
				}
			    }
			    else
			    {
				printf("ERROR: The root process on %s has unexpectedly exited.\n", arg->pszHost);
				if (g_bUseJobHost)
				{
				    sprintf(pszStr, "geterror %d", launchid);
				    WriteString(sock, pszStr);
				    pszStr[0] = '\0';
				    ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
				    // Save this process's information to the job database
				    PutJobProcessInDatabase(arg, nPid);
				    UpdateJobKeyValue(0, "error", pszStr);
				}
				sprintf(pszStr, "freeprocess %d", launchid);
				WriteString(sock, pszStr);
				ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
				WriteString(sock, "done");
				easy_closesocket(sock);
				SetEvent(g_hAbortEvent);
				delete arg;
				return;
			    }

			    sprintf(pszStr, "freeprocess %d", launchid);
			    WriteString(sock, pszStr);
			    ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
			}
		    }
		    WriteString(sock, "done");
		    easy_closesocket(sock);
		    SetEvent(g_hAbortEvent);
		    delete arg;
		    return;
		}
		if (strncmp(pszStr, "SUCCESS", 8))
		{
		    if (strncmp(pszStr, "INFO", 4) == 0)
		    {
			char *s;
			int id, x=0;
			s = strstr(pszStr, "id=");
			if (s)
			{
			    s++;s++;s++; // move over 'id='
			    id = atoi(s);
			    s = strstr(pszStr, "exitcode=");
			    if (s)
			    {
				s += strlen("exitcode="); // move over 'exitcode='
				x = atoi(s);
			    }
			    if (id == launchid)
			    {
				printf("ERROR: The root process on %s has unexpectedly exited. Exit code = %d\n", arg->pszHost, x);
				if (bLocalStartup)
				{
				    sprintf(pszStr, "freeprocess %d", launchid);
				    WriteString(sock, pszStr);
				    ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
				}
				WriteString(sock, "done");
				easy_closesocket(sock);
				SetEvent(g_hAbortEvent);
				delete arg;
				return;
			    }
			}
		    }
		    else
		    {
			printf("ERROR: barrier failed on '%s':\n%s", arg->pszHost, pszStr);
			if (bLocalStartup)
			{
			    sprintf(pszStr, "freeprocess %d", launchid);
			    WriteString(sock, pszStr);
			    ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
			}
			WriteString(sock, "done");
			easy_closesocket(sock);
			SetEvent(g_hAbortEvent);
			delete arg;
			return;
		    }
		}
		else
		{
		    bBarrierLoopContinue = false;
		}
	    }

	    // after the barrier, the data is available so do the get
	    sprintf(pszStr, "dbget name=%s key=port", pszStartupDB);
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("ERROR: Unable to write '%s': error %d", pszStr, WSAGetLastError());
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    
	    if (!ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT))
	    {
		printf("ERROR: Unable to get the root port: error %d", WSAGetLastError());
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    if (strncmp(pszStr, DBS_FAIL_STR, strlen(DBS_FAIL_STR)+1) == 0)
	    {
		printf("ERROR: Unable to get the root port:\n%s", pszStr);
		sprintf(pszStr, "freeprocess %d", launchid);
		WriteString(sock, pszStr);
		ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
		WriteString(sock, "done");
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }

	    // save the retrieved data
	    g_nRootPort = atoi(pszStr);

	    // destroy the database since it is no longer necessary
	    sprintf(pszStr, "dbdestroy name=%s", pszStartupDB);
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
		//ExitProcess(0);
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    // read result
	    if (!ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT))
	    {
		printf("ERROR: ReadString failed to read the result of dbdestroy: error %d\n", WSAGetLastError());
		//ExitProcess(0);
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    if (strnicmp(pszStr, DBS_FAIL_STR, strlen(DBS_FAIL_STR)+1) == 0)
	    {
		printf("Unable to destroy the database '%s' on '%s'\n%s", pszStartupDB, arg->pszHost, pszStr);fflush(stdout);
		sprintf(pszStr, "freeprocess %d", launchid);
		WriteString(sock, pszStr);
		ReadStringTimeout(sock, pszStr, g_nMPIRUN_SHORT_TIMEOUT);
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	}

	if (g_bUseJobHost)
	{
	    //printf("MPIRunLaunchProcess:putting job in database\n");fflush(stdout);
	    // Save this process's information to the job database
	    PutJobProcessInDatabase(arg, nPid);
	}

	// Wait for the process to exit
	if (bLocalStartup)
	{
	    // send a simulated getexitcodewait command to the local process
	    sprintf(pszStr, "getexitcodewait %d", launchid);
	    if (WriteString(root_sock, pszStr) == SOCKET_ERROR)
	    {
		printf("Error: Unable to send a getexitcodewait command to local host\r\nError %d", WSAGetLastError());fflush(stdout);
		easy_closesocket(root_sock);
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	}
	else
	{
	    sprintf(pszStr, "getexitcodewait %d", launchid);
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("Error: Unable to send a getexitcodewait command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());fflush(stdout);
		easy_closesocket(sock);
		SetEvent(g_hAbortEvent);
		delete arg;
		return;
	    }
	    //printf("getexitcodewait %d socket: 0x%p:%d\n", arg->i, sock, sock);fflush(stdout);
	}

	int i = InterlockedIncrement(&g_nNumProcessSockets) - 1;
	if (bLocalStartup)
	    g_pProcessSocket[i] = root_sock;
	else
	    g_pProcessSocket[i] = sock;
	g_pProcessLaunchId[i] = launchid;
	g_pLaunchIdToRank[i] = arg->i;

	//printf("[[[[P:%d]]]]\n", launchid);fflush(stdout);
    }
    else
    {
	printf("MPIRunLaunchProcess: Connect to %s failed, error %d\n", arg->pszHost, error);fflush(stdout);
	//ExitProcess(0);
	SetEvent(g_hAbortEvent);
	delete arg;
	return;
    }
    
    memset(arg->pszPassword, 0, 100);
    delete arg;
}
