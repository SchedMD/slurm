#include "localonly.h"
#include "Translate_Error.h"
#include <stdio.h>
#include <time.h>
#include "WaitThread.h"
#include <stdlib.h>
#include <process.h>
#include <ctype.h>

// Function name	: SetEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : char * pEnv
void SetEnvironmentVariables(char *pEnv)
{
    char name[MAX_PATH]="", value[MAX_PATH]="";
    char *pChar;
    
    pChar = name;
    while (*pEnv != '\0')
    {
	if (*pEnv == '=')
	{
	    *pChar = '\0';
	    pChar = value;
	}
	else
	{
	    if (*pEnv == '|')
	    {
		*pChar = '\0';
		pChar = name;
		SetEnvironmentVariable(name, value);
	    }
	    else
	    {
		*pChar = *pEnv;
		pChar++;
	    }
	}
	pEnv++;
    }
    *pChar = '\0';
    SetEnvironmentVariable(name, value);
}

// Function name	: RemoveEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : char *pEnv
void RemoveEnvironmentVariables(char *pEnv)
{
    char name[MAX_PATH]="", value[MAX_PATH]="";
    char *pChar;
    
    pChar = name;
    while (*pEnv != '\0')
    {
	if (*pEnv == '=')
	{
	    *pChar = '\0';
	    pChar = value;
	}
	else
	{
	    if (*pEnv == '|')
	    {
		*pChar = '\0';
		pChar = name;
		SetEnvironmentVariable(name, NULL);
	    }
	    else
	    {
		*pChar = *pEnv;
		pChar++;
	    }
	}
	pEnv++;
    }
    *pChar = '\0';
    SetEnvironmentVariable(name, NULL);
}

static int ParseArgString(char *pszArgs, char **pFirstArg)
{
    int nArgs = 1;
    
    if (pszArgs == NULL)
    {
	*pFirstArg = NULL;
	return 0;
    }

    // skip over leading whitespace
    while (isspace(*pszArgs) && *pszArgs != '\0')
	pszArgs++;

    // the string is all whitespace
    if (*pszArgs == '\0')
    {
	*pFirstArg = NULL;
	return 0;
    }

    // save the first argument
    *pFirstArg = pszArgs;
    if (*pszArgs == '"')
    {
	pFirstArg++;
	pszArgs++;
	while (*pszArgs != '"' && *pszArgs != '\0')
	    pszArgs++;
	if (*pszArgs == '\0')
	    return 1;
	while (isspace(*pszArgs) && *pszArgs != '\0')
	{
	    *pszArgs = '\0';
	    pszArgs++;
	}
	if (*pszArgs == '\0')
	    return 1;
    }

    // tokenize the rest of the arguments
    while (*pszArgs != '\0')
    {
	if (*pszArgs == '"')
	{
	    *pszArgs = '\0';
	    pszArgs++;
	    while (*pszArgs != '"' && *pszArgs != '\0')
		pszArgs++;
	    if (*pszArgs == '\0')
		return nArgs;
	    while (isspace(*pszArgs) && *pszArgs != '\0')
	    {
		*pszArgs = '\0';
		pszArgs++;
	    }
	    if (*pszArgs == '\0')
		return nArgs;
	}
	else
	{
	    while (!isspace(*pszArgs) && *pszArgs != '\0')
		pszArgs++;
	    if (*pszArgs == '\0')
		return nArgs;
	    while (isspace(*pszArgs) && *pszArgs != '\0')
	    {
		*pszArgs = '\0';
		pszArgs++;
	    }
	}
	nArgs++;
    }

    return nArgs;
}

static void MakeArgs(char *pszCmd, char *pszArgs, char ***pppArgs)
{
    int nArgs;
    int i;
    char **ppArgs;
    char *pArgs = new char[strlen(pszArgs)+1];
    char *pArg;

    strcpy(pArgs, pszArgs);
    nArgs = ParseArgString(pArgs, &pArg);
    *pppArgs = new char *[nArgs+2];
    ppArgs = *pppArgs;
    ppArgs[0] = pszCmd;
    for (i=1; i<=nArgs; i++)
    {
	ppArgs[i] = new char[strlen(pArg)+1];
	strcpy(ppArgs[i], pArg);
	while (*pArg != '\0')
	    pArg++;
	if (i<nArgs)
	{
	    while (*pArg == '\0')
		pArg++;
	}
    }
    ppArgs[nArgs+1] = NULL;
    delete pArgs;
}

// This function was modified to work with cygwin and the bash shell.
// Function name	: RunLocal
// Description	    : 
// Return type		: void 
// Argument         : bool bDoSMP
void RunLocal(bool bDoSMP)
{
    DWORD size = 100;
    char pszHost[MAX_HOST_LENGTH], pszCmdLine[MAX_CMD_LENGTH], error_msg[256], pszEnv[MAX_CMD_LENGTH], pszExtra[MAX_PATH];
    char *pEnv;
    int rootPort=0;
    HANDLE *hProcess = new HANDLE[g_nHosts];
    int i;
    char **ppArgs;
    char **ppArg;
    int launchtimeout = 10;
    char pszTimeout[20];
    DWORD len = 20;
    
    GetComputerName(pszHost, &size);
    
    if (ReadMPDRegistry("timeout", pszTimeout, &len))
    {
	launchtimeout = atoi(pszTimeout);
	if (launchtimeout < 1)
	    launchtimeout = 10;
    }

    // Remove quotations
    if (g_pszExe[0] == '"')
    {
	char pszTemp[MAX_CMD_LENGTH];
	strncpy(pszTemp, &g_pszExe[1], MAX_CMD_LENGTH);
	pszTemp[MAX_CMD_LENGTH-1] = '\0';
	strcpy(g_pszExe, pszTemp);
	if (g_pszExe[strlen(g_pszExe)-1] == '"')
	    g_pszExe[strlen(g_pszExe)-1] = '\0';
    }

    strncpy(pszCmdLine, g_pszExe, MAX_CMD_LENGTH);
    pszCmdLine[MAX_CMD_LENGTH-1] = '\0';
    MakeArgs(pszCmdLine, g_pszArgs, &ppArgs);
    
    if (strlen(g_pszDir))
	SetCurrentDirectory(g_pszDir);

    if (!g_bMPICH2)
    {
	GetTempFileName(".", "mpi", 0, pszExtra);
	// This produces a name in the form: ".\XXXmpi.tmp"
	// \ is illegal in named objects so use &pszExtra[2] instead of pszExtra for the JobID
	if (bDoSMP)
	{
	    sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_IPROC=0|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_EXTRA=%s|MPICH_COMNIC=%s|MPICH_SHM_LOW=0|MPICH_SHM_HIGH=%d",
		&pszExtra[2], g_nHosts, pszHost, -1, pszExtra, pszHost, g_nHosts-1);
	}
	else
	{
	    sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_IPROC=0|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_EXTRA=%s|MPICH_COMNIC=%s",
		&pszExtra[2], g_nHosts, pszHost, -1, pszExtra, pszHost);
	}

	SetEnvironmentVariables(pszEnv);
	if (strlen(g_pszEnv) > 0)
	    SetEnvironmentVariables(g_pszEnv);
	pEnv = GetEnvironmentStrings();

	// launch first process
	if ((hProcess[0] = (HANDLE)spawnv(_P_NOWAIT, pszCmdLine, ppArgs)) == (HANDLE)-1)
	{
	    int error = errno;
	    Translate_Error(error, error_msg, "CreateProcess failed: ");
	    printf("Unable to launch '%s', error %d: %s", pszCmdLine, error, error_msg);
	    return;
	}

	RemoveEnvironmentVariables(pszEnv);
	FreeEnvironmentStrings(pEnv);

	if (g_bNoMPI)
	{
	    rootPort = -1;
	}
	else
	{
	    // Open the file and read the port number written by the first process
	    HANDLE hFile = CreateFile(pszExtra, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	    if (hFile == INVALID_HANDLE_VALUE)
	    {
		Translate_Error(GetLastError(), error_msg, "CreateFile failed ");
		printf(error_msg);
		return;
	    }

	    DWORD num_read = 0;
	    char pBuffer[100];
	    pBuffer[0] = '\0';
	    char *pChar = pBuffer;
	    clock_t cStart = clock();
	    while (true)
	    {
		num_read = 0;
		if (!ReadFile(hFile, pChar, 100, &num_read, NULL))
		{
		    Translate_Error(GetLastError(), error_msg, "ReadFile failed ");
		    printf(error_msg);
		    CloseHandle(hFile);
		    DeleteFile(pszExtra);
		    return;
		}
		if (num_read == 0)
		{
		    if (clock() - cStart > launchtimeout * CLOCKS_PER_SEC)
		    {
			printf("Wait for process 0 to write port to temporary file timed out\n");
			TerminateProcess(hProcess, 0);
			CloseHandle(hFile);
			DeleteFile(pszExtra);
			return;
		    }
		    Sleep(100);
		}
		else
		{
		    for (i=0; i<(int)num_read; i++)
		    {
			if (*pChar == '\n')
			    break;
			pChar ++;
		    }
		    if (*pChar == '\n')
			break;
		}
	    }
	    CloseHandle(hFile);
	    rootPort = atoi(pBuffer);
	}
	DeleteFile(pszExtra);
    }

    // launch all the rest of the processes including the root process for mpich2 apps
    for (i=g_bMPICH2 ? 0 : 1; i<g_nHosts; i++)
    {
	if (g_bMPICH2)
	{
	    if (bDoSMP)
	    {
		sprintf(pszEnv, "PMI_KVS=%s|PMI_RANK=%d|PMI_SIZE=%d|PMI_MPD=%s:%d|PMI_SHM_CLIQUES=(0..%d)",
		    pmi_kvsname, i, g_nHosts, pmi_host, pmi_port, g_nHosts-1);
	    }
	    else
	    {
		sprintf(pszEnv, "PMI_KVS=%s|PMI_RANK=%d|PMI_SIZE=%d|PMI_MPD=%s:%d",
		    pmi_kvsname, i, g_nHosts, pmi_host, pmi_port);
	    }
	}
	else
	{
	    if (bDoSMP)
	    {
		sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_IPROC=%d|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_COMNIC=%s|MPICH_SHM_LOW=0|MPICH_SHM_HIGH=%d",
		    &pszExtra[2], i, g_nHosts, pszHost, rootPort, pszHost, g_nHosts-1);
	    }
	    else
	    {
		sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_IPROC=%d|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_COMNIC=%s",
		    &pszExtra[2], i, g_nHosts, pszHost, rootPort, pszHost);
	    }
	}

	SetEnvironmentVariables(pszEnv);
	pEnv = GetEnvironmentStrings();
	
	if ((hProcess[i] = (HANDLE)spawnv(_P_NOWAIT, pszCmdLine, ppArgs)) == (HANDLE)-1)
	{
	    int error = errno;
	    Translate_Error(error, error_msg, "CreateProcess failed: ");
	    printf("Unable to launch '%s', error %d: %s", pszCmdLine, error, error_msg);
	    return;
	}
	
	RemoveEnvironmentVariables(pszEnv);
	FreeEnvironmentStrings(pEnv);
    }
    
    // free the argument array created by MakeArgs
    ppArg = &ppArgs[1];
    while (*ppArg != NULL)
    {
	delete *ppArg;
	ppArg++;
    }
    delete ppArgs;
    ppArgs = NULL;

    // Wait for all the processes to terminate
    WaitForLotsOfObjects(g_nHosts, hProcess);
    
    for (i=0; i<g_nHosts; i++)
	CloseHandle(hProcess[i]);
    delete hProcess;
}

// This is the original RunLocal that uses CreateProcess.
// It was replaced with the code above which uses spawn to make cygwin happy.
#if 0
// Function name	: RunLocal
// Description	    : 
// Return type		: void 
// Argument         : bool bDoSMP
void RunLocal(bool bDoSMP)
{
    DWORD size = 100;
    char pszHost[100], pszCmdLine[MAX_CMD_LENGTH], error_msg[256], pszEnv[MAX_CMD_LENGTH], pszExtra[MAX_PATH];
    STARTUPINFO saInfo;
    PROCESS_INFORMATION psInfo;
    char *pEnv;
    int rootPort=0;
    HANDLE *hProcess = new HANDLE[g_nHosts];
    int i;
    
    GetComputerName(pszHost, &size);
    
    sprintf(pszCmdLine, "%s %s", g_pszExe, g_pszArgs);
    
    GetTempFileName(".", "mpi", 0, pszExtra);
    // This produces a name in the form: ".\XXXmpi.tmp"
    // \ is illegal in named objects so use &pszExtra[2] instead of pszExtra for the JobID
    if (bDoSMP)
    {
	sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_IPROC=0|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_EXTRA=%s|MPICH_COMNIC=%s|MPICH_SHM_LOW=0|MPICH_SHM_HIGH=%d",
	    &pszExtra[2], g_nHosts, pszHost, -1, pszExtra, pszHost, g_nHosts-1);
    }
    else
    {
	sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_IPROC=0|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_EXTRA=%s|MPICH_COMNIC=%s",
	    &pszExtra[2], g_nHosts, pszHost, -1, pszExtra, pszHost);
    }
    
    SetEnvironmentVariables(pszEnv);
    if (strlen(g_pszEnv) > 0)
	SetEnvironmentVariables(g_pszEnv);
    pEnv = GetEnvironmentStrings();
    
    GetStartupInfo(&saInfo);
    
    // launch first process
    if (CreateProcess(
	NULL,
	pszCmdLine,
	NULL, NULL, FALSE,
	IDLE_PRIORITY_CLASS, 
	pEnv,
	NULL,
	&saInfo, &psInfo))
    {
	hProcess[0] = psInfo.hProcess;
	CloseHandle(psInfo.hThread);
    }
    else
    {
	int error = GetLastError();
	Translate_Error(error, error_msg, "CreateProcess failed: ");
	printf("Unable to launch '%s', error %d: %s", pszCmdLine, error, error_msg);
	return;
    }
    
    RemoveEnvironmentVariables(pszEnv);
    FreeEnvironmentStrings(pEnv);
    
    if (g_bNoMPI)
    {
	rootPort = -1;
    }
    else
    {
	// Open the file and read the port number written by the first process
	HANDLE hFile = CreateFile(pszExtra, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
	    Translate_Error(GetLastError(), error_msg, "CreateFile failed ");
	    printf(error_msg);
	    return;
	}
	
	DWORD num_read = 0;
	char pBuffer[100];
	pBuffer[0] = '\0';
	char *pChar = pBuffer;
	clock_t cStart = clock();
	while (true)
	{
	    num_read = 0;
	    if (!ReadFile(hFile, pChar, 100, &num_read, NULL))
	    {
		Translate_Error(GetLastError(), error_msg, "ReadFile failed ");
		printf(error_msg);
		return;
	    }
	    if (num_read == 0)
	    {
		if (clock() - cStart > 10 * CLOCKS_PER_SEC)
		{
		    printf("Wait for process 0 to write port to temporary file timed out\n");
		    TerminateProcess(hProcess, 0);
		    return;
		}
		Sleep(100);
	    }
	    else
	    {
		for (i=0; i<(int)num_read; i++)
		{
		    if (*pChar == '\n')
			break;
		    pChar ++;
		}
		if (*pChar == '\n')
		    break;
	    }
	}
	CloseHandle(hFile);
	rootPort = atoi(pBuffer);
    }
    DeleteFile(pszExtra);
    
    // launch all the rest of the processes
    for (i=1; i<g_nHosts; i++)
    {
	if (bDoSMP)
	{
	    sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_IPROC=%d|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_COMNIC=%s|MPICH_SHM_LOW=0|MPICH_SHM_HIGH=%d",
		&pszExtra[2], i, g_nHosts, pszHost, rootPort, pszHost, g_nHosts-1);
	}
	else
	{
	    sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_IPROC=%d|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_COMNIC=%s",
		&pszExtra[2], i, g_nHosts, pszHost, rootPort, pszHost);
	}
	
	SetEnvironmentVariables(pszEnv);
	pEnv = GetEnvironmentStrings();
	
	if (CreateProcess(
	    NULL,
	    pszCmdLine,
	    NULL, NULL, FALSE,
	    IDLE_PRIORITY_CLASS, 
	    pEnv,
	    NULL,
	    &saInfo, &psInfo))
	{
	    hProcess[i] = psInfo.hProcess;
	    CloseHandle(psInfo.hThread);
	}
	else
	{
	    int error = GetLastError();
	    Translate_Error(error, error_msg, "CreateProcess failed: ");
	    printf("Unable to launch '%s', error %d: %s", pszCmdLine, error, error_msg);
	    return;
	}
	
	RemoveEnvironmentVariables(pszEnv);
	FreeEnvironmentStrings(pEnv);
    }
    
    // Wait for all the processes to terminate
    WaitForLotsOfObjects(g_nHosts, hProcess);
    
    for (i=0; i<g_nHosts; i++)
	CloseHandle(hProcess[i]);
    delete hProcess;
}
#endif
