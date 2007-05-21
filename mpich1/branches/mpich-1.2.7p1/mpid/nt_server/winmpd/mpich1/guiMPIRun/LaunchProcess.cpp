#include "stdafx.h"
#include "guiMPIRun.h"
#include "guiMPIRunDoc.h"
#include "guiMPIRunView.h"
#include "LaunchProcess.h"
#include <stdio.h>
#include "global.h"
#include "RedirectIO.h"
#include "..\Common\MPIJobDefs.h"
#include "Translate_Error.h"
#include "mpdutil.h"
#include "mpd.h"

/*
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
*/

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
    SOCKET sock;
    int launchid;
    char pszStartupDB[100];
    char pszStr[MAX_CMD_LENGTH+1];
    char pszIOE[10];
    char pszError[512];
    LONG i;
    char *dbg_str = "no";
    char *pszMap = NULL;

    if (arg->bUseDebugFlag)
	dbg_str = "yes";

    //printf("connecting to %s:%d rank %d\n", arg->pszHost, nPort, arg->i);fflush(stdout);
    if ((error = ConnectToMPDReport(arg->pszHost, nPort, arg->pszPassPhrase, &sock, pszStr)) == 0)
    {
	if (arg->i == 0 && !arg->pDlg->m_bNoMPI)
	{
	    sprintf(pszStr, "dbcreate");
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
		//ExitProcess(0);
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    // read result
	    if (!ReadString(sock, pszStartupDB))
	    {
		printf("ERROR: ReadString failed to read the database name: error %d\n", WSAGetLastError());
		//ExitProcess(0);
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    if (strnicmp(pszStartupDB, "FAIL ", 5) == 0)
	    {
		printf("Unable to create a database on '%s'\n%s", arg->pszHost, pszStartupDB);fflush(stdout);
		//ExitProcess(0);
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
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
	
	if (arg->i == 0)
	    strcpy(pszIOE, "012"); // only redirect stdin to the root process
	else
	    strcpy(pszIOE, "12");

	if (arg->pDlg->m_nproc > FORWARD_NPROC_THRESHOLD)
	{
	    if (arg->i > 0)
	    {
		while (arg->pDlg->m_pForwardHost[(arg->i - 1)/2].nPort == 0)
		    Sleep(100);
		sprintf(arg->pszIOHostPort, "%s:%d", arg->pDlg->m_pForwardHost[(arg->i - 1)/2].pszHost, arg->pDlg->m_pForwardHost[(arg->i - 1)/2].nPort);
		if (arg->pDlg->m_nproc/2 > arg->i)
		{
		    strncpy(arg->pDlg->m_pForwardHost[arg->i].pszHost, arg->pszHost, MAX_HOST_LENGTH);
		    arg->pDlg->m_pForwardHost[arg->i].pszHost[MAX_HOST_LENGTH-1] = '\0';
		    sprintf(pszStr, "createforwarder host=%s forward=%s", arg->pszHost, arg->pszIOHostPort);
		    WriteString(sock, pszStr);
		    ReadString(sock, pszStr);
		    int nTempPort = atoi(pszStr);
		    if (nTempPort == -1)
		    {
			// If creating the forwarder fails, redirect output to the root instead
			// This assignment isn't thread safe.  Who knows if the host part of the structure will be written before the port.
			arg->pDlg->m_pForwardHost[arg->i] = arg->pDlg->m_pForwardHost[0];
		    }
		    else
			arg->pDlg->m_pForwardHost[arg->i].nPort = nTempPort;
		    //printf("forwarder %s:%d\n", g_pForwardHost[arg->i].pszHost, g_pForwardHost[arg->i].nPort);fflush(stdout);
		}
	    }
	}

	/*
	if (arg->pDlg->m_pDriveMapList)
	    pszMap = GenerateMapString(arg->pDlg->m_pDriveMapList);
	*/
	/*
	if (arg->pDlg->m_pDriveMapList)
	{
	    MapDriveNode *pNode = arg->pDlg->m_pDriveMapList;
	    char *pszEncoded;
	    while (pNode)
	    {
		pszEncoded = EncodePassword(arg->pszPassword);
		sprintf(pszStr, "map drive=%c share=%s account=%s password=%s", 
		    pNode->cDrive, pNode->pszShare, arg->pszAccount, pszEncoded);
		if (pszEncoded != NULL) free(pszEncoded);
		if (WriteString(sock, pszStr) == SOCKET_ERROR)
		{
		    sprintf(pszError, "Unable to send map command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
		    MessageBox(NULL, pszError, "Critical Error", MB_OK);
		    easy_closesocket(sock);
		    SetEvent(arg->pDlg->m_hAbortEvent);
		    delete arg;
		    return;
		}
		if (!ReadString(sock, pszStr))
		{
		    sprintf(pszError, "Unable to read the result of a map command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
		    MessageBox(NULL, pszError, "Critical Error", MB_OK);
		    easy_closesocket(sock);
		    SetEvent(arg->pDlg->m_hAbortEvent);
		    delete arg;
		    return;
		}
		if (stricmp(pszStr, "SUCCESS"))
		{
		    sprintf(pszError, "Unable to map %c: to %s on %s\r\n%s", pNode->cDrive, pNode->pszShare, arg->pszHost, pszStr);
		    MessageBox(NULL, pszError, "Error", MB_OK);
		    easy_closesocket(sock);
		    SetEvent(arg->pDlg->m_hAbortEvent);
		    delete arg;
		    return;
		}
		pNode = pNode->pNext;
	    }
	}
	*/

	// LaunchProcess
	if (arg->bLogon)
	{
	    char *pszEncoded;
	    pszEncoded = EncodePassword(arg->pszPassword);
	    if (strlen(arg->pszDir) > 0)
	    {
		sprintf(pszStr, "launch h=%s c='%s' e='%s' a=%s p=%s %s=%s k=%d d='%s' g=%s", 
		    arg->pszHost, arg->pszCmdLine, arg->pszEnv, arg->pszAccount, pszEncoded, pszIOE, arg->pszIOHostPort, arg->i, arg->pszDir, dbg_str);
	    }
	    else
	    {
		sprintf(pszStr, "launch h=%s c='%s' e='%s' a=%s p=%s %s=%s k=%d g=%s", 
		    arg->pszHost, arg->pszCmdLine, arg->pszEnv, arg->pszAccount, pszEncoded, pszIOE, arg->pszIOHostPort, arg->i, dbg_str);
	    }
	    if (pszEncoded != NULL) free(pszEncoded);
	}
	else
	{
	    if (strlen(arg->pszDir) > 0)
	    {
		sprintf(pszStr, "launch h=%s c='%s' e='%s' %s=%s k=%d d='%s' g=%s",
		    arg->pszHost, arg->pszCmdLine, arg->pszEnv, pszIOE, arg->pszIOHostPort, arg->i, arg->pszDir, dbg_str);
	    }
	    else
	    {
		sprintf(pszStr, "launch h=%s c='%s' e='%s' %s=%s k=%d g=%s",
		    arg->pszHost, arg->pszCmdLine, arg->pszEnv, pszIOE, arg->pszIOHostPort, arg->i, dbg_str);
	    }
	}

	if (arg->pszMap[0] != '\0')
	    strcat(pszStr, arg->pszMap);
	/*
	if (pszMap)
	{
	    strcat(pszStr, pszMap);
	    delete [] pszMap;
	}
	*/
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    sprintf(pszError, "Unable to send launch command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
	    MessageBox(NULL, pszError, "Critical Error", MB_OK);
	    easy_closesocket(sock);
	    SetEvent(arg->pDlg->m_hAbortEvent);
	    delete arg;
	    return;
	}
	if (!ReadString(sock, pszStr))
	{
	    sprintf(pszError, "Unable to read the result of the launch command on '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
	    MessageBox(NULL, pszError, "Critical Error", MB_OK);
	    easy_closesocket(sock);
	    SetEvent(arg->pDlg->m_hAbortEvent);
	    delete arg;
	    return;
	}
	launchid = atoi(pszStr);
	// save the launch id, get the pid
	sprintf(pszStr, "getpid %d", launchid);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    sprintf(pszError, "Unable to send getpid command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
	    MessageBox(NULL, pszError, "Critical Error", MB_OK);
	    easy_closesocket(sock);
	    SetEvent(arg->pDlg->m_hAbortEvent);
	    delete arg;
	    return;
	}
	if (!ReadString(sock, pszStr))
	{
	    sprintf(pszError, "Unable to read the result of the getpid command on '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
	    MessageBox(NULL, pszError, "Critical Error", MB_OK);
	    easy_closesocket(sock);
	    SetEvent(arg->pDlg->m_hAbortEvent);
	    delete arg;
	    return;
	}
	nPid = atoi(pszStr);
	if (nPid == -1)
	{
	    sprintf(pszStr, "geterror %d", launchid);
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		sprintf(pszError, "Unable to send geterror command after an unsuccessful launch on '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
		MessageBox(NULL, pszError, "Critical Error", MB_OK);
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    if (!ReadString(sock, pszStr))
	    {
		sprintf(pszError, "Unable to read the result of the geterror command on '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
		MessageBox(NULL, pszError, "Critical Error", MB_OK);
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    if (strcmp(pszStr, "ERROR_SUCCESS"))
	    {
		if (arg->i == 0 && !arg->pDlg->m_bNoMPI)
		{
		    sprintf(pszError, "Failed to launch the root process:\n%s\n%s\n", arg->pszCmdLine, pszStr);
		}
		else
		{
		    sprintf(pszError, "Failed to launch process %d:\n'%s'\n%s\n", arg->i, arg->pszCmdLine, pszStr);
		}
		//UnmapDrives(sock, arg->pDlg->m_pDriveMapList);
		sprintf(pszStr, "freeprocess %d", launchid);
		WriteString(sock, pszStr);
		ReadString(sock, pszStr);
		WriteString(sock, "done");
		easy_closesocket(sock);
		MessageBox(NULL, pszError, "Critical Error", MB_OK);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	}
	
	// Get the port number and redirect input to the first process
	if (arg->i == 0 && !arg->pDlg->m_bNoMPI)
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
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    /*
	    if (!ReadString(sock, pszStr))
	    {
		printf("ERROR: Unable to read the result of the barrier command on '%s': error %d", arg->pszHost, WSAGetLastError());
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    */
	    if (!ReadStringTimeout(sock, pszStr, g_nLaunchTimeout))
	    {
		error = WSAGetLastError();
		if (error != 0)
		{
		    printf("ERROR: Unable to read the result of the barrier command on '%s': error %d", arg->pszHost, error);
		}
		else
		{
		    sprintf(pszStr, "getexitcode %d", launchid);
		    if (WriteString(sock, pszStr) == SOCKET_ERROR)
		    {
			printf("Error: Unable to send a getexitcode command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());fflush(stdout);
			easy_closesocket(sock);
			SetEvent(arg->pDlg->m_hAbortEvent);
			delete arg;
			return;
		    }
		    if (!ReadStringTimeout(sock, pszStr, g_nLaunchTimeout))
		    {
			printf("ERROR: Unable to read the result of the root getexitcode command on '%s': error %d", arg->pszHost, WSAGetLastError());
			//UnmapDrives(sock, arg->pDlg->m_pDriveMapList);
			sprintf(pszStr, "freeprocess %d", launchid);
			WriteString(sock, pszStr);
			ReadString(sock, pszStr);
			WriteString(sock, "done");
			easy_closesocket(sock);
			SetEvent(arg->pDlg->m_hAbortEvent);
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
			    ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT);
			    // Save this process's information to the job database
			    PutJobProcessInDatabase(arg, nPid);
			    UpdateJobKeyValue(0, "error", pszStr);
			}
			//UnmapDrives(sock, arg->pDlg->m_pDriveMapList);
			sprintf(pszStr, "freeprocess %d", launchid);
			WriteString(sock, pszStr);
			ReadString(sock, pszStr);
			WriteString(sock, "done");
			easy_closesocket(sock);
			SetEvent(arg->pDlg->m_hAbortEvent);
			delete arg;
			return;
		    }
		    //UnmapDrives(sock, arg->pDlg->m_pDriveMapList);
		    sprintf(pszStr, "freeprocess %d", launchid);
		    WriteString(sock, pszStr);
		    ReadString(sock, pszStr);
		}
		WriteString(sock, "done");
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    if (strncmp(pszStr, "SUCCESS", 8))
	    {
		printf("ERROR: barrier failed on '%s':\n%s", arg->pszHost, pszStr);
		//UnmapDrives(sock, arg->pDlg->m_pDriveMapList);
		sprintf(pszStr, "freeprocess %d", launchid);
		WriteString(sock, pszStr);
		ReadString(sock, pszStr);
		WriteString(sock, "done");
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }

	    // after the barrier, the data is available so do the get
	    sprintf(pszStr, "dbget name=%s key=port", pszStartupDB);
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("ERROR: Unable to write '%s': error %d", pszStr, WSAGetLastError());
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    
	    if (!ReadString(sock, pszStr))
	    {
		printf("ERROR: Unable to get the root port: error %d", WSAGetLastError());
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    if (strncmp(pszStr, DBS_FAIL_STR, strlen(DBS_FAIL_STR)+1) == 0)
	    {
		printf("ERROR: Unable to get the root port:\n%s", pszStr);
		//UnmapDrives(sock, arg->pDlg->m_pDriveMapList);
		sprintf(pszStr, "freeprocess %d", launchid);
		WriteString(sock, pszStr);
		ReadString(sock, pszStr);
		WriteString(sock, "done");
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }

	    // save the gotten data
	    arg->pDlg->m_nRootPort = atoi(pszStr);

	    // destroy the database since it is no longer necessary
	    sprintf(pszStr, "dbdestroy name=%s", pszStartupDB);
	    if (WriteString(sock, pszStr) == SOCKET_ERROR)
	    {
		printf("ERROR: Unable to write '%s' to socket[%d]\n", pszStr, sock);
		//ExitProcess(0);
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    // read result
	    if (!ReadString(sock, pszStr))
	    {
		printf("ERROR: ReadString failed to read the result of dbdestroy: error %d\n", WSAGetLastError());
		//ExitProcess(0);
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	    if (strnicmp(pszStr, DBS_FAIL_STR, strlen(DBS_FAIL_STR)+1) == 0)
	    {
		printf("Unable to destroy the database '%s' on '%s'\n%s", pszStartupDB, arg->pszHost, pszStr);fflush(stdout);
		//UnmapDrives(sock, arg->pDlg->m_pDriveMapList);
		sprintf(pszStr, "freeprocess %d", launchid);
		WriteString(sock, pszStr);
		ReadString(sock, pszStr);
		WriteString(sock, "done");
		easy_closesocket(sock);
		SetEvent(arg->pDlg->m_hAbortEvent);
		delete arg;
		return;
	    }
	}
	
	if (g_bUseJobHost)
	{
	    // Save this process's information to the job database
	    PutJobProcessInDatabase(arg, nPid);
	}

	// Start to wait for the process to exit
	sprintf(pszStr, "getexitcodewait %d", launchid);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
	    sprintf(pszError, "Unable to send a getexitcodewait command to '%s'\r\nError %d", arg->pszHost, WSAGetLastError());
	    MessageBox(NULL, pszError, "Critical Error", MB_OK);
	    easy_closesocket(sock);
	    SetEvent(arg->pDlg->m_hAbortEvent);
	    delete arg;
	    return;
	}

	i = InterlockedIncrement(&arg->pDlg->m_nNumProcessSockets) - 1;
	arg->pDlg->m_pProcessSocket[i] = sock;
	arg->pDlg->m_pProcessLaunchId[i] = launchid;
	arg->pDlg->m_pLaunchIdToRank[i] = arg->i;
    }
    else
    {
	sprintf(pszError, "MPIRunLaunchProcess: Connect to %s failed, error: %s\n", arg->pszHost, pszStr);
	MessageBox(NULL, pszError, "Critical Error", MB_OK);
	SetEvent(arg->pDlg->m_hAbortEvent);
    }

    memset(arg->pszPassword, 0, 100);
    delete arg;
}
