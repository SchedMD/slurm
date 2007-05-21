#include "GetStringOpt.h"
#include "mpdimpl.h"
#include "mpdutil.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include "Translate_Error.h"
#include "safe_terminate_process.h"

long g_nNumProcsRunning = 0;
HANDLE g_hProcessStructMutex = NULL;

struct LaunchThreadStruct
{
    LaunchThreadStruct();
    void Print();

    char pszHost[MAX_HOST_LENGTH];
    char pszSrcHost[MAX_HOST_LENGTH];
    char pszSrcId[10];
    char pszEnv[MAX_CMD_LENGTH];
    char pszMap[MAX_CMD_LENGTH];
    char pszDir[MAX_PATH];
    char pszCmd[MAX_CMD_LENGTH];
    char pszAccount[40];
    char pszPassword[300];
    char pszStdin[MAX_HOST_LENGTH];
    char pszStdout[MAX_HOST_LENGTH];
    char pszStderr[MAX_HOST_LENGTH];
    bool bMergeOutErr;
    bool bUseDebugFlag;
    int priorityClass;
    int priority;
    bool bAttachToWorkstation;

    int nPid;
    int nKRank;
    char pszError[MAX_CMD_LENGTH];
    int nExitCode;
    HANDLE hProcess;
    HANDLE hThread;

    LaunchThreadStruct *pNext;
};

LaunchThreadStruct::LaunchThreadStruct()
{
    pszHost[0] = '\0';
    pszSrcHost[0] = '\0';
    pszSrcId[0] = '\0';
    pszEnv[0] = '\0';
    pszMap[0] = '\0';
    pszDir[0] = '\0';
    pszCmd[0] = '\0';
    pszAccount[0] = '\0';
    pszPassword[0] = '\0';
    pszStdin[0] = '\0';
    pszStdout[0] = '\0';
    pszStderr[0] = '\0';
    bMergeOutErr = false;
    bUseDebugFlag = false;
    priorityClass = BELOW_NORMAL_PRIORITY_CLASS;
    priority = THREAD_PRIORITY_NORMAL;
    bAttachToWorkstation = false;

    nPid = -1;
    nKRank = 0;
    pszError[0] = '\0';
    nExitCode = -1;
    hProcess = NULL;
    hThread = NULL;
    
    pNext = NULL;
}

void LaunchThreadStruct::Print()
{
    dbg_printf("LAUNCH:\n");
    dbg_printf(" user: %s\n", pszAccount);
    dbg_printf(" %s(%s) -> %s %s\n", pszSrcHost, pszSrcId, pszHost, pszCmd);
    if (pszDir[0] != '\0')
    {
	dbg_printf(" dir: ");
	int n = strlen(pszDir);
	if (n > 70)
	{
	    char pszTemp[71];
	    char *pszCur = pszDir;
	    bool bFirst = true;
	    while (n > 0)
	    {
		strncpy(pszTemp, pszCur, 70);
		pszTemp[70] = '\0';
		if (bFirst)
		{
		    printf("%s\n", pszTemp);
		    bFirst = false;
		}
		else
		    printf("      %s\n", pszTemp);
		pszCur += 70;
		n -= 70;
	    }
	}
	else
	    printf("%s\n", pszDir);
    }
    if (pszEnv[0] != '\0')
    {
	char pszEnv2[MAX_CMD_LENGTH];
	char pszCheck[100];
	char *token;
	int i,n;
	strncpy(pszEnv2, pszEnv, MAX_CMD_LENGTH);
	pszEnv2[MAX_CMD_LENGTH-1] = '\0';

	token = strstr(pszEnv2, "PMI_PWD=");
	if (token != NULL)
	{
	    strncpy(pszCheck, &token[8], 100);
	    pszCheck[99] = '\0';
	    token = strtok(pszCheck, " '|\n");
	    n = strlen(pszCheck);
	    token = strstr(pszEnv2, "PMI_PWD=");
	    token = &token[8];
	    if (n > 0)
	    {
		if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		    n--;
		if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		    n--;
		for (i=0; i<n; i++)
		    token[i] = '*';
	    }
	}

	printf(" env: ");
	n = strlen(pszEnv2);
	if (n > 70)
	{
	    char pszTemp[71];
	    char *pszCur = pszEnv2;
	    bool bFirst = true;
	    while (n > 0)
	    {
		strncpy(pszTemp, pszCur, 70);
		pszTemp[70] = '\0';
		if (bFirst)
		{
		    printf("%s\n", pszTemp);
		    bFirst = false;
		}
		else
		    printf("      %s\n", pszTemp);
		pszCur += 70;
		n -= 70;
	    }
	}
	else
	    printf("%s\n", pszEnv2);
    }
    if (pszMap[0] != '\0')
	printf(" map = %s\n", pszMap);
    printf(" stdin|out|err: %s|%s|%s\n", pszStdin, pszStdout, pszStderr);
    printf(" krank: %d\n", nKRank);
    //printf("\n");
    fflush(stdout);
}

LaunchThreadStruct *g_pProcessList = NULL;

bool snprintf_update(char *&pszStr, int &length, char *pszFormat, ...)
{
    va_list list;
    int n;

    va_start(list, pszFormat);
    n = _vsnprintf(pszStr, length, pszFormat, list);
    va_end(list);

    if (n < 0)
    {
	pszStr[length-1] = '\0';
	length = 0;
	return false;
    }

    pszStr = &pszStr[n];
    length = length - n;

    return true;
}

static void ProcessToString(LaunchThreadStruct *p, char *pszStr, int length)
{
    if (!snprintf_update(pszStr, length, "PROCESS:\n"))
	return;
    if (p->pszAccount[0] != '\0')
    {
	if (!snprintf_update(pszStr, length, " user: %s\n", p->pszAccount))
	    return;
    }
    else
    {
	if (!snprintf_update(pszStr, length, " user: <single user mode>\n"))
	    return;
    }
    if (!snprintf_update(pszStr, length, " %s(%s) -> %s %s\n", p->pszSrcHost, p->pszSrcId, p->pszHost, p->pszCmd))
	return;
    if (p->pszDir[0] != '\0')
    {
	if (!snprintf_update(pszStr, length, " dir: "))
	    return;
	int n = strlen(p->pszDir);
	if (n > 70)
	{
	    char pszTemp[71];
	    char *pszCur = p->pszDir;
	    bool bFirst = true;
	    while (n > 0)
	    {
		strncpy(pszTemp, pszCur, 70);
		pszTemp[70] = '\0';
		if (bFirst)
		{
		    if (!snprintf_update(pszStr, length, "%s\n", pszTemp))
			return;
		    bFirst = false;
		}
		else
		{
		    if (!snprintf_update(pszStr, length, "      %s\n", pszTemp))
			return;
		}
		pszCur += 70;
		n -= 70;
	    }
	}
	else
	{
	    if (!snprintf_update(pszStr, length, "%s\n", p->pszDir))
		return;
	}
    }
    if (p->pszEnv[0] != '\0')
    {
	char pszEnv2[MAX_CMD_LENGTH];
	char pszCheck[100];
	char *token;
	int i,n;
	strncpy(pszEnv2, p->pszEnv, MAX_CMD_LENGTH);
	pszEnv2[MAX_CMD_LENGTH-1] = '\0';

	token = strstr(pszEnv2, "PMI_PWD=");
	if (token != NULL)
	{
	    strncpy(pszCheck, &token[8], 100);
	    pszCheck[99] = '\0';
	    token = strtok(pszCheck, " '|\n");
	    n = strlen(pszCheck);
	    token = strstr(pszEnv2, "PMI_PWD=");
	    token = &token[8];
	    if (n > 0)
	    {
		if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		    n--;
		if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		    n--;
		for (i=0; i<n; i++)
		    token[i] = '*';
	    }
	}

	if (!snprintf_update(pszStr, length, " env: "))
	    return;
	n = strlen(pszEnv2);
	if (n > 70)
	{
	    char pszTemp[71];
	    char *pszCur = pszEnv2;
	    bool bFirst = true;
	    while (n > 0)
	    {
		strncpy(pszTemp, pszCur, 70);
		pszTemp[70] = '\0';
		if (bFirst)
		{
		    if (!snprintf_update(pszStr, length, "%s\n", pszTemp))
			return;
		    bFirst = false;
		}
		else
		{
		    if (!snprintf_update(pszStr, length, "      %s\n", pszTemp))
			return;
		}
		pszCur += 70;
		n -= 70;
	    }
	}
	else
	{
	    if (!snprintf_update(pszStr, length, "%s\n", pszEnv2))
		return;
	}
    }
    if (!snprintf_update(pszStr, length, " stdin|out|err: %s|%s|%s\n", p->pszStdin, p->pszStdout, p->pszStderr))
	return;
    if (!snprintf_update(pszStr, length, " krank: %d\n", p->nKRank))
	return;
}

void statProcessList(char *pszOutput, int length)
{
    LaunchThreadStruct *p;
    int nBytesAvailable = length;

    *pszOutput = '\0';
    length--; // leave room for the null character

    // lock the process list while using it
    WaitForSingleObject(g_hProcessStructMutex, INFINITE);

    if (g_pProcessList == NULL)
    {
	ReleaseMutex(g_hProcessStructMutex);
	return;
    }

    try {
    p = g_pProcessList;
    while (p)
    {
	ProcessToString(p, pszOutput, length);
	length = length - strlen(pszOutput);
	pszOutput = &pszOutput[strlen(pszOutput)];
	p = p->pNext;
    }
    } catch (...)
    {
	err_printf("exception caught in stat ps command.\n");
	strcpy(pszOutput, "internal error");
    }
    ReleaseMutex(g_hProcessStructMutex);
}

void RemoveProcessStruct(LaunchThreadStruct *p)
{
    WaitForSingleObject(g_hProcessStructMutex, INFINITE);

    LaunchThreadStruct *pTrailer = g_pProcessList;

    // Remove p from the list
    if (p == NULL)
    {
	ReleaseMutex(g_hProcessStructMutex);
	return;
    }

    if (p == g_pProcessList)
	g_pProcessList = g_pProcessList->pNext;
    else
    {
	while (pTrailer && pTrailer->pNext != p)
	    pTrailer = pTrailer->pNext;
	if (pTrailer)
	    pTrailer->pNext = p->pNext;
    }

    // Close any open handles
    if (p->hProcess != NULL)
	CloseHandle(p->hProcess);
    if (p->hThread != NULL)
	CloseHandle(p->hThread);

    UnmapUserDrives(p->pszMap);

    //dbg_printf("removing ProcessStruct[%d]\n", p->nPid);
    // free the structure
    delete p;

    ReleaseMutex(g_hProcessStructMutex);
}

void LaunchThread(LaunchThreadStruct *pArg)
{
    DWORD dwExitCode;
    char pszStr[MAX_CMD_LENGTH];
    char pszError[MAX_PATH];
    bool bProcessAborted = false;
    SYSTEMTIME stime;
    char timestamp[256];
    HANDLE hIn, hOut, hErr;
    int nError;

    pArg->Print();

    if (strlen(pArg->pszEnv))
    {
	sprintf(pszStr, "|MPD_ID=%s", pArg->pszSrcId);
	strcat(pArg->pszEnv, pszStr);
    }
    else
    {
	sprintf(pArg->pszEnv, "MPD_ID=%s", pArg->pszSrcId);
    }
    pszStr[0] = '\0';
    if (g_bSingleUser)
    {
	if (!MapUserDrives(pArg->pszMap, pArg->pszAccount, pArg->pszPassword, pszStr))
	{
	}
	pArg->hProcess = LaunchProcess(pArg->pszCmd, pArg->pszEnv, pArg->pszDir, 
	    //BELOW_NORMAL_PRIORITY_CLASS, THREAD_PRIORITY_NORMAL,
	    pArg->priorityClass, pArg->priority,
	    &hIn, &hOut, &hErr, &pArg->nPid, &nError, pszStr, pArg->bUseDebugFlag);
    }
    else
    {
	if (pArg->pszAccount[0] == '\0')
	{
	    if (g_bMPDUserCapable)
	    {
		if (g_bUseMPDUser)
		{
		    _snprintf(pszStr, MAX_CMD_LENGTH, "launched src=%s dest=%s id=%s error=LaunchProcess failed, invalid mpd user for anonymous launch.", g_pszHost, pArg->pszSrcHost, pArg->pszSrcId);
		}
		else
		{
		    _snprintf(pszStr, MAX_CMD_LENGTH, "launched src=%s dest=%s id=%s error=LaunchProcess failed, anonymous launch not enabled on '%s'.", g_pszHost, pArg->pszSrcHost, pArg->pszSrcId, g_pszHost);
		}
	    }
	    else
	    {
		_snprintf(pszStr, MAX_CMD_LENGTH, "launched src=%s dest=%s id=%s error=LaunchProcess failed, anonymous launch request attempted on node without that capability enabled.", g_pszHost, pArg->pszSrcHost, pArg->pszSrcId);
	    }
	    ContextWriteString(g_pRightContext, pszStr);
	    RemoveProcessStruct(pArg);
	    InterlockedDecrement(&g_nNumProcsRunning);
	    return;
	}
	pArg->hProcess = LaunchProcessLogon(pArg->pszAccount, pArg->pszPassword, 
				pArg->pszCmd, pArg->pszEnv, pArg->pszMap, pArg->pszDir, 
				//BELOW_NORMAL_PRIORITY_CLASS, THREAD_PRIORITY_NORMAL,
				pArg->priorityClass, pArg->priority,
				&hIn, &hOut, &hErr, &pArg->nPid, &nError, pszStr, pArg->bUseDebugFlag);
    }
    if (pArg->hProcess == INVALID_HANDLE_VALUE)
    {
	Translate_Error(nError, pszError, pszStr);
	_snprintf(pszStr, MAX_CMD_LENGTH, "launched src=%s dest=%s id=%s error=LaunchProcess failed, %s", g_pszHost, pArg->pszSrcHost, pArg->pszSrcId, pszError);
	ContextWriteString(g_pRightContext, pszStr);
	RemoveProcessStruct(pArg);
	InterlockedDecrement(&g_nNumProcsRunning);
	return;
    }

    _snprintf(pszStr, MAX_CMD_LENGTH, "launched pid=%d src=%s dest=%s id=%s", pArg->nPid, g_pszHost, pArg->pszSrcHost, pArg->pszSrcId);
    ContextWriteString(g_pRightContext, pszStr);

    if (!ConnectAndRedirectInput(hIn, pArg->pszStdin, pArg->hProcess, pArg->nPid, pArg->nKRank))
    {
	if (!SafeTerminateProcess(pArg->hProcess, 1000001))
	{
	    if (GetLastError() != ERROR_PROCESS_ABORTED)
		TerminateProcess(pArg->hProcess, 1000006);
	}
    }
    else 
    {
	if (pArg->bMergeOutErr)
	{
	    if (!ConnectAndRedirect2Outputs(hOut, hErr, pArg->pszStdout, pArg->hProcess, pArg->nPid, pArg->nKRank))
	    {
		if (!SafeTerminateProcess(pArg->hProcess, 1000002))
		{
		    if (GetLastError() != ERROR_PROCESS_ABORTED)
			TerminateProcess(pArg->hProcess, 1000007);
		}
	    }
	}
	else
	{
	    if (!ConnectAndRedirectOutput(hOut, pArg->pszStdout, pArg->hProcess, pArg->nPid, pArg->nKRank, 1))
	    {
		if (!SafeTerminateProcess(pArg->hProcess, 1000003))
		{
		    if (GetLastError() != ERROR_PROCESS_ABORTED)
			TerminateProcess(pArg->hProcess, 1000008);
		}
	    }
	    else if (!ConnectAndRedirectOutput(hErr, pArg->pszStderr, pArg->hProcess, pArg->nPid, pArg->nKRank, 2))
	    {
		if (!SafeTerminateProcess(pArg->hProcess, 1000004))
		{
		    if (GetLastError() != ERROR_PROCESS_ABORTED)
			TerminateProcess(pArg->hProcess, 1000009);
		}
	    }
	}
    }

    if (pArg->bUseDebugFlag)
    {
	DebugWaitForProcess(bProcessAborted, pszError);
    }

    WaitForSingleObject(pArg->hProcess, INFINITE);
    GetLocalTime(&stime);
    dwExitCode = 123456789;
    GetExitCodeProcess(pArg->hProcess, &dwExitCode);
    pArg->nExitCode = dwExitCode;
    CloseHandle(pArg->hProcess);
    pArg->hProcess = NULL;

#ifdef FOO
    if (dwExitCode == ERROR_WAIT_NO_CHILDREN)
    {
	sprintf(pszError, "unexpected process termination.");
	bProcessAborted = true;
    }
#endif

    sprintf(timestamp, "%d.%d.%d.%dh.%dm.%ds.%dms", 
	stime.wYear, stime.wMonth, stime.wDay, 
	stime.wHour, stime.wMinute, stime.wSecond, stime.wMilliseconds);
    if (bProcessAborted)
    {
	_snprintf(pszStr, MAX_CMD_LENGTH, "exitcode code=%d src=%s dest=%s id=%s time=%s error=%s", 
	    pArg->nExitCode, g_pszHost, pArg->pszSrcHost, pArg->pszSrcId, timestamp, pszError);
    }
    else
    {
	_snprintf(pszStr, MAX_CMD_LENGTH, "exitcode code=%d src=%s dest=%s id=%s time=%s", 
	    pArg->nExitCode, g_pszHost, pArg->pszSrcHost, pArg->pszSrcId, timestamp);
    }
    dbg_printf("...process %d exited, sending <%s>\n", pArg->nKRank, pszStr);
    ContextWriteString(g_pRightContext, pszStr);

    RemoveProcessStruct(pArg);
    InterlockedDecrement(&g_nNumProcsRunning);
}

void ShutdownAllProcesses()
{
    //DWORD dwExitCode;
    WaitForSingleObject(g_hProcessStructMutex, INFINITE);
    LaunchThreadStruct *p = g_pProcessList;
    while (p)
    {
	if (p->hProcess)
	{
	    if (!SafeTerminateProcess(p->hProcess, 1000005))
	    {
		if (GetLastError() != ERROR_PROCESS_ABORTED)
		{
		    if (!TerminateProcess(p->hProcess, 1000006))
		    {
			InterlockedDecrement(&g_nNumProcsRunning);
		    }
		}
	    }
	    /*
	    if (GetExitCodeProcess(p->hProcess, &dwExitCode))
	    {
		if (dwExitCode == STILL_ACTIVE)
		{
		    if (!TerminateProcess(p->hProcess, 0))
		    {
			err_printf("TerminateProcess failed for process %d, %d, error %d\n", p->hProcess, p->nPid, GetLastError());
			// If I can't stop a process for some reason,
			// decrement its value so this function doesn't hang
			InterlockedDecrement(&g_nNumProcsRunning);
		    }
		}
	    }
	    */
	}
	p = p->pNext;
    }
    ReleaseMutex(g_hProcessStructMutex);

    // Wait for all the threads to clean up the terminated processes
    while (g_nNumProcsRunning > 0)
	Sleep(250);
}

void MPD_KillProcess(int nPid)
{
    //DWORD dwExitCode;
    WaitForSingleObject(g_hProcessStructMutex, INFINITE);
    LaunchThreadStruct *p = g_pProcessList;
    while (p)
    {
	if (p->nPid == nPid)
	{
	    //dbg_printf("MPD_KillProcess found pid %d\n", nPid);
	    if (p->hProcess && (p->hProcess != INVALID_HANDLE_VALUE))
	    {
		if (!SafeTerminateProcess(p->hProcess, 987654321))
		{
		    if (GetLastError() != ERROR_PROCESS_ABORTED)
		    {
			if (p->hProcess == NULL)
			{
			    // If the process handle is lost for some reason,
			    // decrement its value so this function doesn't hang
			    InterlockedDecrement(&g_nNumProcsRunning);
			}
			else
			{
			    if (!TerminateProcess(p->hProcess, 123456789))
			    {
				err_printf("TerminateProcess failed for process - handle(0x%p), pid(%d), error %d\n", p->hProcess, p->nPid, GetLastError());
				// If I can't stop a process for some reason,
				// decrement its value so this function doesn't hang
				InterlockedDecrement(&g_nNumProcsRunning);
			    }
			}
		    }
		}
		/*
		//dbg_printf("MPD_KillProcess found valid hProcess 0x%x\n", p->hProcess);
		if (GetExitCodeProcess(p->hProcess, &dwExitCode))
		{
		    if (dwExitCode == STILL_ACTIVE)
		    {
			//dbg_printf("MPD_KillProcess - terminating process\n");
			if (!TerminateProcess(p->hProcess, 0))
			{
			    err_printf("TerminateProcess failed for process %d, %d, error %d\n", p->hProcess, p->nPid, GetLastError());
			    // If I can't stop a process for some reason,
			    // decrement its value so this function doesn't hang
			    InterlockedDecrement(&g_nNumProcsRunning);
			    // Should I also remove the LaunchThreadStruct p?
			    // If there are lots of failed process terminations, this will lead to wasted memory allocation.
			}
		    }
		}
		else
		{
		    err_printf("MPD_KillProcess - GetExitCodeProcess failed, error %d\n", GetLastError());
		}
		*/
	    }
	    ReleaseMutex(g_hProcessStructMutex);
	    return;
	}
	p = p->pNext;
    }
    ReleaseMutex(g_hProcessStructMutex);
}

void Launch(char *pszStr)
{
    char sTemp[10];
    HANDLE hTemp;
    LaunchThreadStruct *pArg = new LaunchThreadStruct;
    int iter;

    if (GetStringOpt(pszStr, "g", sTemp))
	pArg->bUseDebugFlag = (stricmp(sTemp, "yes") == 0);
    if (GetStringOpt(pszStr, "k", sTemp))
	pArg->nKRank = atoi(sTemp);
    else
	pArg->nKRank = 0;
    if (!GetStringOpt(pszStr, "h", pArg->pszHost))
	strncpy(pArg->pszHost, g_pszHost, MAX_HOST_LENGTH);
    GetStringOpt(pszStr, "src", pArg->pszSrcHost);
    GetStringOpt(pszStr, "id", pArg->pszSrcId);
    GetStringOpt(pszStr, "e", pArg->pszEnv);
    GetStringOpt(pszStr, "m", pArg->pszMap);
    GetStringOpt(pszStr, "d", pArg->pszDir);
    GetStringOpt(pszStr, "c", pArg->pszCmd);
    if (GetStringOpt(pszStr, "a", pArg->pszAccount))
    {
	GetStringOpt(pszStr, "p", pArg->pszPassword);
	DecodePassword(pArg->pszPassword);
    }
    else
    {
	if (g_bMPDUserCapable && g_bUseMPDUser)
	{
	    strcpy(pArg->pszAccount, g_pszMPDUserAccount);
	    strcpy(pArg->pszPassword, g_pszMPDUserPassword);
	}
	else
	{
	    pArg->pszAccount[0] = '\0';
	    pArg->pszPassword[0] = '\0';
	}
    }
    GetStringOpt(pszStr, "0", pArg->pszStdin);
    GetStringOpt(pszStr, "1", pArg->pszStdout);
    GetStringOpt(pszStr, "2", pArg->pszStderr);
    if (GetStringOpt(pszStr, "r", sTemp))
    {
	int c,p;
	char *token;
	token = strtok(sTemp, ":");
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

    /* pszStr gets obliterated at this point */
    if (GetStringOpt(pszStr, "12", pszStr))
    {
	strncpy(pArg->pszStdout, pszStr, MAX_HOST_LENGTH);
	strncpy(pArg->pszStderr, pszStr, MAX_HOST_LENGTH);
	pArg->bMergeOutErr = true;
    }
    if (GetStringOpt(pszStr, "012", pszStr))
    {
	strncpy(pArg->pszStdin, pszStr, MAX_HOST_LENGTH);
	strncpy(pArg->pszStdout, pszStr, MAX_HOST_LENGTH);
	strncpy(pArg->pszStderr, pszStr, MAX_HOST_LENGTH);
	pArg->bMergeOutErr = true;
    }

    WaitForSingleObject(g_hProcessStructMutex, INFINITE);
    if (!g_pProcessList)
    {
	g_pProcessList = pArg;
    }
    else
    {
	pArg->pNext = g_pProcessList;
	g_pProcessList = pArg;
    }
    ReleaseMutex(g_hProcessStructMutex);

    InterlockedIncrement(&g_nNumProcsRunning);

    DWORD dwThreadID;
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hTemp = pArg->hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LaunchThread, pArg, 0, &dwThreadID);
	if (hTemp != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hTemp == NULL)
    {
	err_printf("Launch: CreateThread failed, error %d\n", GetLastError());
	InterlockedDecrement(&g_nNumProcsRunning);
    }

    return;
}

void ConcatenateProcessesToString(char *pszStr)
{
    char pszLine[4096];
    WaitForSingleObject(g_hProcessStructMutex, INFINITE);
    LaunchThreadStruct *p = g_pProcessList;
    if (p)
    {
	_snprintf(pszLine, 4096, "%s:\n", g_pszHost);
	strncat(pszStr, pszLine, MAX_CMD_LENGTH - 1 - strlen(pszStr));
    }
    while (p)
    {
	_snprintf(pszLine, 4096, "%04d : %s\n", p->nPid, p->pszCmd);
	strncat(pszStr, pszLine, MAX_CMD_LENGTH - 1 - strlen(pszStr));
	p = p->pNext;
    }
    ReleaseMutex(g_hProcessStructMutex);
}
