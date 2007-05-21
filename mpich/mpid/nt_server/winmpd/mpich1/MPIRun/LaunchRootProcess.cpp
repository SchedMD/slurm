#include "LaunchProcess.h"
#include <stdio.h>
#include "global.h"
#include "..\Common\MPIJobDefs.h"
#include "Translate_Error.h"
#include "mpdutil.h"
#include "mpd.h"
#include "RedirectIO.h"
#include <stdlib.h>
#include <ctype.h>

struct LaunchThreadStruct
{
    LaunchThreadStruct();

    char pszSrcId[10];
    char pszEnv[MAX_CMD_LENGTH];
    char pszMap[MAX_CMD_LENGTH];
    char pszDir[MAX_PATH];
    char pszCmd[MAX_CMD_LENGTH];
    int priorityClass;
    int priority;

    int nPid;
    int nKRank;
    char pszError[MAX_CMD_LENGTH];
    int nExitCode;
    HANDLE hProcess;
    HANDLE hThread;

    SOCKET sock;

    bool bReady;
    int nError;
    bool bProcessExited;
    char timestamp[256];
};

LaunchThreadStruct::LaunchThreadStruct()
{
    pszSrcId[0] = '\0';
    pszEnv[0] = '\0';
    pszMap[0] = '\0';
    pszDir[0] = '\0';
    pszCmd[0] = '\0';
    priorityClass = BELOW_NORMAL_PRIORITY_CLASS;
    priority = THREAD_PRIORITY_NORMAL;

    nPid = -1;
    nKRank = 0;
    pszError[0] = '\0';
    nExitCode = -1;
    hProcess = NULL;
    hThread = NULL;

    sock = INVALID_SOCKET;

    bReady = false;
    nError = ERROR_SUCCESS;
    bProcessExited = false;
    timestamp[0] = '\0';
}

bool GetStringOpt(char *pszStr, char *pszName, char *pszValue, char *pszDelim = "=");

static bool GetStringOpt(char *pszStr, char *pszName, char *pszValue, char *pszDelim /*= "="*/)
{
    char *pszFirst, *pszDelimLoc, *pszLast;
    bool bFirst = true;

    if (pszStr == NULL || pszName == NULL || pszValue == NULL)
	return false;

    while (true)
    {
	// Find the name
	pszFirst = strstr(pszStr, pszName);
	if (pszFirst == NULL)
	    return false;

	// Check to see if we have matched a sub-string
	if (bFirst)
	{
	    bFirst = false;
	    if ((pszFirst != pszStr) && (!isspace(*(pszFirst-1))))
	    {
		pszStr = pszFirst + strlen(pszName);
		continue;
	    }
	}
	else
	{
	    if (!isspace(*(pszFirst-1)))
	    {
		pszStr = pszFirst + strlen(pszName);
		continue;
	    }
	}

	// Skip over any white space after the name
	pszDelimLoc = &pszFirst[strlen(pszName)];
	while (isspace(*pszDelimLoc))
	    pszDelimLoc++;

	// Find the deliminator
	if (strnicmp(pszDelimLoc, pszDelim, strlen(pszDelim)) != 0)
	{
	    //pszStr = &pszDelimLoc[strlen(pszDelim)];
	    pszStr = pszDelimLoc;
	    continue;
	}
	
	// Skip over the deliminator and any white space
	pszFirst = &pszDelimLoc[strlen(pszDelim)];
	while (isspace(*pszFirst))
	    pszFirst++;

	if (*pszFirst == '\'')
	{
	    pszFirst++;
	    while (*pszFirst != '\'' && *pszFirst != '\0')
	    {
		*pszValue++ = *pszFirst++;
	    }
	    *pszValue = '\0';
	    break;
	}
	else
	{
	    // Find the next deliminator
	    pszLast = strstr(pszFirst, pszDelim);
	    if (pszLast == NULL)
	    {
		strcpy(pszValue, pszFirst);
		break;
	    }
	    
	    // Back up over any white space and name preceding the second deliminator
	    pszLast--;
	    while (pszLast > pszFirst && isspace(*pszLast))
		pszLast--;
	    while (pszLast > pszFirst && !isspace(*pszLast))
		pszLast--;
	    while (pszLast > pszFirst && isspace(*pszLast))
		pszLast--;
	    
	    // Copy the data between first and last
	    pszLast++;
	    strncpy(pszValue, pszFirst, pszLast-pszFirst);
	    pszValue[pszLast-pszFirst] = '\0';
	}
	break;
    }
    return true;
}

// Function name	: SetEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : char *bEnv
static void SetEnvironmentVariables(char *bEnv)
{
    char name[MAX_PATH]="", value[MAX_PATH]="";
    char *pChar;
    
    pChar = name;
    while (*bEnv != '\0')
    {
	if (*bEnv == '=')
	{
	    *pChar = '\0';
	    pChar = value;
	}
	else
	{
	    if (*bEnv == '|')
	    {
		*pChar = '\0';
		pChar = name;
		SetEnvironmentVariable(name, value);
	    }
	    else
	    {
		*pChar = *bEnv;
		pChar++;
	    }
	}
	bEnv++;
    }
    *pChar = '\0';
    SetEnvironmentVariable(name, value);
}

// Function name	: RemoveEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : char *bEnv
static void RemoveEnvironmentVariables(char *bEnv)
{
    char name[MAX_PATH]="", value[MAX_PATH]="";
    char *pChar;
    
    pChar = name;
    while (*bEnv != '\0')
    {
	if (*bEnv == '=')
	{
	    *pChar = '\0';
	    pChar = value;
	}
	else
	{
	    if (*bEnv == '|')
	    {
		*pChar = '\0';
		pChar = name;
		SetEnvironmentVariable(name, NULL);
	    }
	    else
	    {
		*pChar = *bEnv;
		pChar++;
	    }
	}
	bEnv++;
    }
    *pChar = '\0';
    SetEnvironmentVariable(name, NULL);
}

void LaunchThread(LaunchThreadStruct *pArg)
{
    DWORD dwExitCode;
    char pszStr[MAX_CMD_LENGTH];
    SYSTEMTIME stime;
    void *pEnv=NULL;
    char tSavedPath[MAX_PATH] = ".";
    DWORD launch_flag;
    STARTUPINFO saInfo;
    PROCESS_INFORMATION psInfo;

    try {
    if (pArg->pszSrcId[0] != '\0')
    {
	if (strlen(pArg->pszEnv))
	{
	    sprintf(pszStr, "|MPD_ID=%s", pArg->pszSrcId);
	    strcat(pArg->pszEnv, pszStr);
	}
	else
	{
	    sprintf(pArg->pszEnv, "MPD_ID=%s", pArg->pszSrcId);
	}
    }
    pszStr[0] = '\0';

    //printf("setting environment variables:\n%s\n", pArg->pszEnv);fflush(stdout);
    SetEnvironmentVariables(pArg->pszEnv);
    pEnv = GetEnvironmentStrings();
    
    GetCurrentDirectory(MAX_PATH, tSavedPath);
    SetCurrentDirectory(pArg->pszDir);
    
    launch_flag = CREATE_SUSPENDED | pArg->priorityClass;

    memset(&saInfo, 0, sizeof(STARTUPINFO));
    saInfo.cb = sizeof(STARTUPINFO);

    if (CreateProcess(
	NULL,
	pArg->pszCmd,
	NULL, NULL, TRUE,
	launch_flag,
	pEnv,
	NULL,
	&saInfo, &psInfo))
    {
	SetThreadPriority(psInfo.hThread, pArg->priority);
	ResumeThread(psInfo.hThread);
	pArg->hProcess = psInfo.hProcess;
	pArg->nPid = psInfo.dwProcessId;
	pArg->nError = ERROR_SUCCESS;
	CloseHandle(psInfo.hThread);
    }
    else
    {
	pArg->nError = GetLastError();
	sprintf(pArg->pszError, "CreateProcess(%s) failed, error %d", pArg->pszCmd, pArg->nError);
    }
    
    FreeEnvironmentStrings((TCHAR*)pEnv);
    SetCurrentDirectory(tSavedPath);
    RemoveEnvironmentVariables(pArg->pszEnv);

    pArg->bReady = true;

    WaitForSingleObject(pArg->hProcess, INFINITE);
    GetLocalTime(&stime);
    dwExitCode = 123456789;
    GetExitCodeProcess(pArg->hProcess, &dwExitCode);
    pArg->nExitCode = dwExitCode;
    CloseHandle(pArg->hProcess);
    pArg->hProcess = NULL;

    sprintf(pArg->timestamp, "%d.%d.%d.%dh.%dm.%ds.%dms", 
	stime.wYear, stime.wMonth, stime.wDay, 
	stime.wHour, stime.wMinute, stime.wSecond, stime.wMilliseconds);

    pArg->bProcessExited = true;

    while (pArg->bReady)
	Sleep(200);
    sprintf(pszStr, "%d", pArg->nExitCode);
    //printf("LaunchThread sending '%s' as a result of the getexitcode command.\n", pszStr);fflush(stdout);
    WriteString(pArg->sock, pszStr);
    } catch (... )
    {
    }
}

void RootSocketThread(LaunchThreadStruct *pArg)
{
    char str[1024];

    try {
    while (ReadString(pArg->sock, str))
    {
	//printf("RootSocketThread: read %s\n", str);fflush(stdout);
	if (strnicmp(str, "getexitcodewait ", 16) == 0)
	{
	    pArg->bReady = false;
	}
	else if (strnicmp(str, "kill ", 5) == 0)
	{
	    //SafeTerminateProcess(pArg->hProcess, 12121212);
	    TerminateProcess(pArg->hProcess, 12121212);
	}
	else if (strnicmp(str, "getexittime ", 12) == 0)
	{
	    if (pArg->bProcessExited)
		WriteString(pArg->sock, pArg->timestamp);
	    else
		WriteString(pArg->sock, "ACTIVE");
	}
	else if (strnicmp(str, "getmpifinalized ", 15) == 0)
	{
	    WriteString(pArg->sock, "yes");
	}
	else if (strnicmp(str, "freeprocess ", 12) == 0)
	{
	    WriteString(pArg->sock, "SUCCESS");
	}
	else if (stricmp(str, "done") == 0)
	{
	    easy_closesocket(pArg->sock);
	    return;
	}
	else if (strstr(str, "dbget") && strstr(str, "finalized"))
	{
	    WriteString(pArg->sock, "true");
	}
	else
	{
	    printf("RootSocketThread: unknown command - %s, responding with SUCCESS\n", str);fflush(stdout);
	    WriteString(pArg->sock, "SUCCESS");
	}
    }
    } catch (...)
    {
    }
}

/*@
   LaunchRootProcess - 

   Parameters:
+  char *launch_str
.  SOCKET *sock
-  int *pid

   Notes:
   This function launches the root process and pretends that it is connected to an mpd.
   Currently the commands that it must handle are:
    waitforexitcode
    kill
    getexittime
    getmpifinalized
    freeprocess
    done
@*/
int LaunchRootProcess(char *launch_str, SOCKET *sock, int *pid)
{
    SOCKET read_sock, write_sock;
    char sTemp[10];
    HANDLE hTemp;
    LaunchThreadStruct *pArg;
    char pszStr[256];
    DWORD dwThreadID;
    int iter;

    // initialize to invalid
    *pid = -1;

    // make a pipe
    MakeLoop(&read_sock, &write_sock);
    if (read_sock == INVALID_SOCKET || write_sock == INVALID_SOCKET)
    {
	return -1;
    }
    *sock = write_sock;

    // create the launch thread structure
    pArg = new LaunchThreadStruct;
    pArg->sock = read_sock;
    if (GetStringOpt(launch_str, "k", sTemp))
	pArg->nKRank = atoi(sTemp);
    else
	pArg->nKRank = 0;
    GetStringOpt(launch_str, "id", pArg->pszSrcId);
    GetStringOpt(launch_str, "e", pArg->pszEnv);
    GetStringOpt(launch_str, "m", pArg->pszMap);
    GetStringOpt(launch_str, "d", pArg->pszDir);
    GetStringOpt(launch_str, "c", pArg->pszCmd);
    if (GetStringOpt(launch_str, "r", pszStr))
    {
	int c,p;
	char *token;
	token = strtok(pszStr, ":");
	if (token)
	{
	    c = atoi(token);
	    switch (c)
	    {
	    case 0:
		pArg->priorityClass = IDLE_PRIORITY_CLASS;
		break;
	    case 1:
		pArg->priorityClass = BELOW_NORMAL_PRIORITY_CLASS;
		break;
	    case 2:
		pArg->priorityClass = NORMAL_PRIORITY_CLASS;
		break;
	    case 3:
		pArg->priorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
		break;
	    case 4:
		pArg->priorityClass = HIGH_PRIORITY_CLASS;
		break;
	    default:
		pArg->priorityClass = BELOW_NORMAL_PRIORITY_CLASS;
		break;
	    }
	    token = strtok(NULL, " \n");
	    if (token != NULL)
	    {
		p = atoi(token);
		switch (p)
		{
		case 0:
		    pArg->priority = THREAD_PRIORITY_IDLE;
		    break;
		case 1:
		    pArg->priority = THREAD_PRIORITY_LOWEST;
		    break;
		case 2:
		    pArg->priority = THREAD_PRIORITY_BELOW_NORMAL;
		    break;
		case 3:
		    pArg->priority = THREAD_PRIORITY_NORMAL;
		    break;
		case 4:
		    pArg->priority = THREAD_PRIORITY_ABOVE_NORMAL;
		    break;
		case 5:
		    pArg->priority = THREAD_PRIORITY_HIGHEST;
		    break;
		default:
		    pArg->priority = THREAD_PRIORITY_NORMAL;
		    break;
		}
	    }
	}
    }

    // launch the process
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hTemp = pArg->hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LaunchThread, pArg, 0, &dwThreadID);
	if (hTemp != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hTemp == NULL)
    {
	err_printf("LaunchRootProcess: CreateThread(LaunchThread) failed, error %d\n", GetLastError());
    }

    // launch barrier
    while (!pArg->bReady)
	Sleep(200);

    if (pArg->nError != ERROR_SUCCESS)
    {
	err_printf("LaunchRootProcess: launch error - %s\n", pArg->pszError);
	return pArg->nError;
    }

    // save the process id
    *pid = pArg->nPid;
    //printf("The root process id is %d\n", pArg->nPid);fflush(stdout);

    // create the thread to monitor the fake mpd socket
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hTemp = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RootSocketThread, (LPVOID)pArg, 0, &dwThreadID);
	if (hTemp != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hTemp == NULL)
    {
	err_printf("LaunchRootProcess: CreateThread(RootSocketThread) failed, error %d\n", GetLastError());
    }

    return 0;
}
