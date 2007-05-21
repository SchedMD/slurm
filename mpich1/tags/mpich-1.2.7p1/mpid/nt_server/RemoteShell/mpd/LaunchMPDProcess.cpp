#include "LaunchMPDProcess.h"
#include "StringOpt.h"
#include "sockets.h"
#include "Command.h"
#include "TerminalClientThread.h"
#include <stdio.h>

struct ProcessNode
{
	HANDLE hProcess;
	HANDLE hAbortEvent;
	ProcessNode *pNext;
	char pszCmdLine[2048];
	DWORD nPid;
	int nGroupID;
};

ProcessNode *g_pProcessList = NULL;
HANDLE g_hLaunchMutex = CreateMutex(NULL, FALSE, NULL);

// Function name	: LaunchMPDProcess
// Description	    : 
// Return type		: void 
// Argument         : LaunchMPDProcessArg *pArg
void LaunchMPDProcess(LaunchMPDProcessArg *pArg)
{
	char pBuffer[4096];
	char pszEnv[1024] = "";
	char pszDir[MAX_PATH] = ".";
	char pszCmd[1024] = "";
	char pszArg[1024] = "";
	char pszCmdLine[4096] = "";
	char pszStdinHost[100] = "";
	char pszStdoutHost[100] = "";
	char pszStderrHost[100] = "";
	int nStdinPort = 0, nStdoutPort = 0, nStderrPort = 0;
	int nGroupID = -1, nGroupRank = -1;
	char *token;
	HANDLE hStdin, hStdout, hStderr;
	HANDLE hStdoutPipeW=NULL, hStderrPipeW = NULL, hStdinPipeR=NULL;
	HANDLE hStdoutPipeR=NULL,                      hStdinPipeW=NULL;
	HANDLE hTempPipe=NULL;
	STARTUPINFO saInfo;
	PROCESS_INFORMATION psInfo;
	void *pEnv=NULL;
	char pszSavedPath[MAX_PATH] = ".";
	bool bSuccess = false;
	int nError;
	unsigned long nSrcIP;
	int nSrcPort;
	LaunchNode *pLaunchNode;

	//printf("cmd before: %s\n", pArg->pszCommand);fflush(stdout);
	GetStringOpt(pArg->pszCommand, 'e', pszEnv);
	GetStringOpt(pArg->pszCommand, 'd', pszDir);
	GetStringOpt(pArg->pszCommand, 'c', pszCmd);
	GetStringOpt(pArg->pszCommand, 'a', pszArg);
	//printf("cmd after : %s\n", pArg->pszCommand);fflush(stdout);
	//printf("pszArg: %s\n", pszArg);fflush(stdout);

	if (GetStringOpt(pArg->pszCommand, '0', pBuffer) == 0)
	{
		token = strtok(pBuffer, ":");
		if (token != NULL)
		{
			strcpy(pszStdinHost, token);
			token = strtok(NULL, "");
			if (token != NULL)
			{
				nStdinPort = atoi(token);
			}
		}
	}

	if (GetStringOpt(pArg->pszCommand, '1', pBuffer) == 0)
	{
		token = strtok(pBuffer, ":");
		if (token != NULL)
		{
			strcpy(pszStdoutHost, token);
			token = strtok(NULL, "");
			if (token != NULL)
			{
				nStdoutPort = atoi(token);
			}
		}
	}

	if (GetStringOpt(pArg->pszCommand, '2', pBuffer) == 0)
	{
		token = strtok(pBuffer, ":");
		if (token != NULL)
		{
			strcpy(pszStderrHost, token);
			token = strtok(NULL, "");
			if (token != NULL)
			{
				nStderrPort = atoi(token);
			}
		}
	}

	if (GetStringOpt(pArg->pszCommand, 'g', pBuffer) == 0)
		nGroupID = atoi(pBuffer);

	if (GetStringOpt(pArg->pszCommand, 'r', pBuffer) == 0)
		nGroupRank = atoi(pBuffer);

	pLaunchNode = pArg->pNode;
	nSrcIP = pArg->nSrcIP;
	nSrcPort = pArg->nSrcPort;

	delete pArg->pszCommand;
	delete pArg;
	pArg = NULL;

	// Create the command line
	HMODULE hModule = GetModuleHandle(NULL);
	if (!GetModuleFileName(hModule, pszCmdLine, 4096))
		strcpy(pszCmdLine, "mpd.exe");
	if (pszCmd[0] == '\"')
	{
		if (strlen(pszArg))
			sprintf(pBuffer, " -cmd %s -args \"%s\"", pszCmd, pszArg);
		else
			sprintf(pBuffer, " -cmd %s", pszCmd);
	}
	else
	{
		if (strlen(pszArg))
			sprintf(pBuffer, " -cmd \"%s\" -args \"%s\"", pszCmd, pszArg);
		else
			sprintf(pBuffer, " -cmd \"%s\"", pszCmd);
	}
	strcat(pszCmdLine, pBuffer);
	if (strlen(pszEnv) > 0)
	{
		sprintf(pBuffer, " -env \"%s\"", pszEnv);
		strcat(pszCmdLine, pBuffer);
	}
	if (strlen(pszDir) > 0)
	{
		sprintf(pBuffer, " -dir \"%s\"", pszDir);
		strcat(pszCmdLine, pBuffer);
	}
	if (nStdinPort != 0)
	{
		sprintf(pBuffer, " -0 %s:%d", pszStdinHost, nStdinPort);
		strcat(pszCmdLine, pBuffer);
	}
	if (nStdoutPort != 0)
	{
		sprintf(pBuffer, " -1 %s:%d", pszStdoutHost, nStdoutPort);
		strcat(pszCmdLine, pBuffer);
	}
	if (nStderrPort != 0)
	{
		sprintf(pBuffer, " -2 %s:%d", pszStderrHost, nStderrPort);
		strcat(pszCmdLine, pBuffer);
	}
	if (nGroupID != -1)
	{
		sprintf(pBuffer, " -group %d", nGroupID);
		strcat(pszCmdLine, pBuffer);
	}
	if (nGroupRank != -1)
	{
		sprintf(pBuffer, " -rank %d", nGroupRank);
		strcat(pszCmdLine, pBuffer);
	}

	//printf("launching:\n'%s'\n", pszCmdLine); fflush(stdout);

	WaitForSingleObject(g_hLaunchMutex, INFINITE);

	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

	// Get and save the current standard handles
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	if (hStdin == INVALID_HANDLE_VALUE)
	{
		nError = GetLastError();
		printf("0 GetStdHandle failed for stdin, error %d\n", nError);fflush(stdout);
		ReleaseMutex(g_hLaunchMutex);
		return;
	}
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStdout == INVALID_HANDLE_VALUE)
	{
		nError = GetLastError();
		printf("0 GetStdHandle failed for stdout, error %d\n", nError);fflush(stdout);
		ReleaseMutex(g_hLaunchMutex);
		return;
	}
	hStderr = GetStdHandle(STD_ERROR_HANDLE);
	if (hStderr == INVALID_HANDLE_VALUE)
	{
		nError = GetLastError();
		printf("0 GetStdHandle failed for stderr, error %d\n", nError);fflush(stdout);
		ReleaseMutex(g_hLaunchMutex);
		return;
	}

	// Set the security attributes to allow handles to be inherited
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.lpSecurityDescriptor = NULL;
	saAttr.bInheritHandle = TRUE;

	// Create an event to be signalled to abort the mpd child process
	HANDLE hAbortEvent = CreateEvent(&saAttr, TRUE, FALSE, NULL);
	sprintf(pBuffer, " -hAbortEvent %u", (int)hAbortEvent);
	strcat(pszCmdLine, pBuffer);
	//printf("hAbortEvent: %d\n", (int)hAbortEvent);fflush(stdout);

	//printf("launching:\n'%s'\n", pszCmdLine); fflush(stdout);

	// Create pipes for stdin, stdout

	// Stdout
	if (!CreatePipe(&hTempPipe, &hStdoutPipeW, &saAttr, 0))
	{
		nError = GetLastError();
		goto CLEANUP;
	}
	// Make the read end of the stdout pipe not inheritable
	if (!DuplicateHandle(GetCurrentProcess(), hTempPipe, 
		GetCurrentProcess(), &hStdoutPipeR, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
	{
		nError = GetLastError();
		goto CLEANUP;
	}

	// Stdin
	if (!CreatePipe(&hStdinPipeR, &hTempPipe, &saAttr, 0))
	{
		nError = GetLastError();
		goto CLEANUP;
	}
	// Make the write end of the stdin pipe not inheritable
	if (!DuplicateHandle(GetCurrentProcess(), hTempPipe, 
		GetCurrentProcess(), &hStdinPipeW, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
	{
		nError = GetLastError();
		goto CLEANUP;
	}

	// Set stdin, stdout, and stderr to the ends of the pipe the created process will use
	if (!SetStdHandle(STD_INPUT_HANDLE, hStdinPipeR))
	{
		nError = GetLastError();
		goto CLEANUP;
	}
	if (!SetStdHandle(STD_OUTPUT_HANDLE, hStdoutPipeW))
	{
		nError = GetLastError();
		goto RESTORE_CLEANUP;
	}
	if (!SetStdHandle(STD_ERROR_HANDLE, hStdoutPipeW))
	{
		nError = GetLastError();
		goto RESTORE_CLEANUP;
	}

	// Set up the STARTINFO structure
	memset(&saInfo, 0, sizeof(STARTUPINFO));
	saInfo.cb         = sizeof(STARTUPINFO);
	saInfo.hStdInput  = hStdinPipeR;
	saInfo.hStdOutput = hStdoutPipeW;
	saInfo.hStdError  = hStdoutPipeW;
	saInfo.dwFlags    = STARTF_USESTDHANDLES;

	if (CreateProcess(
		NULL,
		pszCmdLine,
		NULL, NULL, TRUE,
		//DETACHED_PROCESS | IDLE_PRIORITY_CLASS, 
		//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS,
		CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP,
		//DETACHED_PROCESS | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP,
		//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_SUSPENDED, 
		NULL, //pEnv,
		NULL,
		&saInfo, &psInfo))
	{
		CloseHandle(psInfo.hThread);
		bSuccess = true;
	}
	else
	{
		nError = GetLastError();
		printf("CreateProcess failed for '%s', error %d\n", pszCmdLine, nError);
	}

RESTORE_CLEANUP:
	// Restore stdin, stdout, stderr
	if (!SetStdHandle(STD_INPUT_HANDLE, hStdin))
	{
		nError = GetLastError();
		printf("SetStdHandle failed to restore stdin, error %d\n", nError);
	}
	if (!SetStdHandle(STD_OUTPUT_HANDLE, hStdout))
	{
		nError = GetLastError();
		printf("SetStdHandle failed to restore stdout, error %d\n", nError);
	}
	if (!SetStdHandle(STD_ERROR_HANDLE, hStderr))
	{
		nError = GetLastError();
		printf("SetStdHandle failed to restore stderr, error %d\n", nError);
	}

CLEANUP:

	CloseHandle(hStdoutPipeW);
	CloseHandle(hStdinPipeR);

	if (bSuccess)
	{
		MPD_CMD_HANDLE hCmd;
		CommandData cmd;
		char *pBuf;

		ProcessNode *p = new ProcessNode;
		strcpy(p->pszCmdLine, pszCmdLine);
		p->nPid = psInfo.dwProcessId;
		//printf("%d\n", psInfo.dwProcessId);fflush(stdout);
		p->nGroupID = nGroupID;
		p->hAbortEvent = hAbortEvent;
		p->hProcess = psInfo.hProcess;
		p->pNext = g_pProcessList;
		g_pProcessList = p;

		cmd.nCommand = MPD_CMD_LAUNCH_RET;
		pBuf = cmd.pCommandBuffer;
		*((unsigned long *)pBuf) = nSrcIP;
		pBuf = pBuf + sizeof(unsigned long);
		*((int *)pBuf) = nSrcPort;
		pBuf = pBuf + sizeof(int);
		*((LaunchNode **)pBuf) = pLaunchNode;
		pBuf = pBuf + sizeof(LaunchNode *);
		*((DWORD*)pBuf) = psInfo.dwProcessId;
		cmd.hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int) + sizeof(LaunchNode*) + sizeof(DWORD);
		hCmd = InsertCommand(cmd);
		WaitForCommand(hCmd);

		ReleaseMutex(g_hLaunchMutex);

		// Tell everyone on the ring that the load on this node has increased by one process.
		cmd.nCommand = MPD_CMD_INCREMENT;
		cmd.hCmd.nBufferLength = 0;
		//printf("Inserting INCREMENT command.\n");fflush(stdout);
		hCmd = InsertCommand(cmd);
		WaitForCommand(hCmd);

		TerminalClientThreadArg *pArg = new TerminalClientThreadArg;
		pArg->hInput = hStdoutPipeR;
		pArg->hOutput = hStdinPipeW;
		TerminalClientThread(pArg);
		// The process should already have exited causing the TerminalClientThread function to return.
		WaitForSingleObject(psInfo.hProcess, 1000);
		// Remove the process handle from the list by setting it to NULL
		WaitForSingleObject(g_hLaunchMutex, 10000);
		p = g_pProcessList;
		while (p != NULL)
		{
			if (p->hProcess == psInfo.hProcess)
			{
				p->hProcess = NULL;
				break;
			}
			p = p->pNext;
		}
		ReleaseMutex(g_hLaunchMutex);

		// Tell everyone on the ring that the load on this node has decreased by one process.
		cmd.nCommand = MPD_CMD_DECREMENT;
		cmd.hCmd.nBufferLength = 0;
		//printf("Inserting DECREMENT command.\n");fflush(stdout);
		hCmd = InsertCommand(cmd);
		WaitForCommand(hCmd);

		// Send the exit code back
		cmd.nCommand = MPD_CMD_LAUNCH_EXITCODE;
		pBuf = cmd.pCommandBuffer;
		*((unsigned long *)pBuf) = nSrcIP;
		pBuf = pBuf + sizeof(unsigned long);
		*((int *)pBuf) = nSrcPort;
		pBuf = pBuf + sizeof(int);
		*((LaunchNode **)pBuf) = pLaunchNode;
		pBuf = pBuf + sizeof(LaunchNode *);
		GetExitCodeProcess(psInfo.hProcess, (DWORD*)pBuf);
		//printf("Process exiting, send exit code: %d\n", *(DWORD*)pBuf);fflush(stdout);
		pBuf = pBuf + sizeof(DWORD);
		*((int *)pBuf) = nGroupID;
		pBuf = pBuf + sizeof(int);
		*((int *)pBuf) = nGroupRank;
		cmd.hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int) + sizeof(LaunchNode*) + sizeof(DWORD) + sizeof(int) + sizeof(int);
		hCmd = InsertCommand(cmd);
		WaitForCommand(hCmd);

		CloseHandle(psInfo.hProcess);
	}
	else
	{
		//printf("0\n");fflush(stdout);
		ReleaseMutex(g_hLaunchMutex);
	}

	CloseHandle(hStdoutPipeR);
	CloseHandle(hStdinPipeW);
}

// Function name	: KillRemainingMPDProcesses
// Description	    : 
// Return type		: void 
void KillRemainingMPDProcesses()
{
	DWORD exitCode;
	ProcessNode *p;

	WaitForSingleObject(g_hLaunchMutex, INFINITE);
	while (g_pProcessList)
	{
		p = g_pProcessList;
		g_pProcessList = g_pProcessList->pNext;
		//printf("setting abort event\n");fflush(stdout);
		SetEvent(p->hAbortEvent);
		if (p->hProcess != NULL)
		{
			WaitForSingleObject(p->hProcess, 1000);
			GetExitCodeProcess(p->hProcess, &exitCode);
			if (exitCode == STILL_ACTIVE)
			{
				printf("terminating process\n");fflush(stdout);
				TerminateProcess(p->hProcess, 0);
			}
			CloseHandle(p->hProcess);
		}
		CloseHandle(p->hAbortEvent);
		delete p;
	}
	ReleaseMutex(g_hLaunchMutex);
	CloseHandle(g_hLaunchMutex);
	g_hLaunchMutex = NULL;
}

// Function name	: KillMPDProcess
// Description	    : 
// Return type		: void 
// Argument         : int nPid
void KillMPDProcess(int nPid)
{
	DWORD exitCode;
	ProcessNode *p = g_pProcessList;
	WaitForSingleObject(g_hLaunchMutex, INFINITE);
	while (p)
	{
		if (p->nPid == (DWORD)nPid)
		{
			SetEvent(p->hAbortEvent);
			if (p->hProcess != NULL)
			{
				WaitForSingleObject(p->hProcess, 4000);
				GetExitCodeProcess(p->hProcess, &exitCode);
				if (exitCode == STILL_ACTIVE)
					TerminateProcess(p->hProcess, 0);
				CloseHandle(p->hProcess);
				p->hProcess = NULL;
			}
		}
		p = p->pNext;
	}
	ReleaseMutex(g_hLaunchMutex);
}

// Function name	: KillMPDProcesses
// Description	    : 
// Return type		: void 
// Argument         : int nGroupID
void KillMPDProcesses(int nGroupID)
{
	DWORD exitCode;
	ProcessNode *p = g_pProcessList;
	WaitForSingleObject(g_hLaunchMutex, INFINITE);
	while (p)
	{
		if (p->nGroupID == nGroupID)
		{
			SetEvent(p->hAbortEvent);
			if (p->hProcess != NULL)
			{
				WaitForSingleObject(p->hProcess, 4000);
				GetExitCodeProcess(p->hProcess, &exitCode);
				if (exitCode == STILL_ACTIVE)
					TerminateProcess(p->hProcess, 0);
				CloseHandle(p->hProcess);
				p->hProcess = NULL;
			}
		}
		p = p->pNext;
	}
	ReleaseMutex(g_hLaunchMutex);
}

// Function name	: PrintMPDProcessesToBuffer
// Description	    : 
// Return type		: void 
// Argument         : char *pBuffer
// Argument         : char *pszHostPort
void PrintMPDProcessesToBuffer(char *pBuffer, char *pszHostPort)
{
	bool bFirst = true;
	*pBuffer = '\0';

	ProcessNode *p = g_pProcessList;
	while (p)
	{
		if (p->hProcess != NULL)
		{
			if (bFirst)
			{
				if (pszHostPort != NULL)
				{
					sprintf(pBuffer, "%s\n", pszHostPort);
					pBuffer = pBuffer + strlen(pBuffer);
				}
				bFirst = false;
			}
			sprintf(pBuffer, "%d:%d:%s\n", p->nPid, p->nGroupID, p->pszCmdLine);
			pBuffer = pBuffer + strlen(pBuffer);
		}
		p = p->pNext;
	}
	ReleaseMutex(g_hLaunchMutex);
}
