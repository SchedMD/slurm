#include "ManageProcess.h"
#include "sockets.h"
#include <stdio.h>

struct PipeSocketArg
{
	char pszHost[100];
	int nPort;
	HANDLE hPipe;
	char c;
};

// Function name	: RedirectOutputToSocket
// Description	    : 
// Return type		: void 
// Argument         : PipeSocketArg *pArg
void RedirectOutputToSocket(PipeSocketArg *pArg)
{
	SOCKET sock;
	WSAEVENT sock_event;
	char buffer[1024];
	DWORD dwNumRead;
	HANDLE hPipe = pArg->hPipe;
	char c = pArg->c;
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

	if (NT_create_bind_socket(&sock, &sock_event, 0))
		printf("create_bind failed\n");
	if (NT_connect(sock, pArg->pszHost, pArg->nPort))
		printf("connect to %s:%d failed\n", pArg->pszHost, pArg->nPort);
	delete pArg;
	if (SendBlocking(sock, &c, 1, 0) != SOCKET_ERROR)
	{
		while (ReadFile(hPipe, buffer, 1024, &dwNumRead, NULL))
		{
			if (SendBlocking(sock, buffer, dwNumRead, 0) == SOCKET_ERROR)
				break;
		}
	}

	NT_closesocket(sock, sock_event);
}

// Function name	: RedirectSocketToInput
// Description	    : 
// Return type		: void 
// Argument         : PipeSocketArg *pArg
void RedirectSocketToInput(PipeSocketArg *pArg)
{
	SOCKET sock;
	WSAEVENT sock_event;
	char buffer[1024];
	DWORD dwNumWritten;
	HANDLE hPipe = pArg->hPipe;
	char c = pArg->c;

	if (NT_create_bind_socket(&sock, &sock_event, 0))
		printf("create_bind failed\n");
	if (NT_connect(sock, pArg->pszHost, pArg->nPort))
		printf("connect to %s:%d failed\n", pArg->pszHost, pArg->nPort);
	delete pArg;
	if (SendBlocking(sock, &c, 1, 0) != SOCKET_ERROR)
	{
		while (ReceiveBlocking(sock, sock_event, buffer, 1, 0) == 0)
		{
			WriteFile(hPipe, buffer, 1, &dwNumWritten, NULL);
		}
	}

	NT_closesocket(sock, sock_event);
}

// Function name	: SetEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : char *pEnv
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
	char name[MAX_PATH] = "", value[MAX_PATH] = "";
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
		pEnv++;
	}
	*pChar = '\0';
	SetEnvironmentVariable(name, NULL);
}

struct AbortMPDArg
{
	HANDLE hAbortEvent;
	HANDLE hProcess;
};

// Function name	: AbortMPDThread
// Description	    : 
// Return type		: void 
// Argument         : AbortMPDArg *pArg
void AbortMPDThread(AbortMPDArg *pArg)
{
	HANDLE hEvent[2];

	hEvent[0] = pArg->hAbortEvent;
	hEvent[1] = pArg->hProcess;
	delete pArg;

	if (WaitForMultipleObjects(2, hEvent, FALSE, INFINITE) == WAIT_OBJECT_0)
	{
		CloseHandle(hEvent[0]);
		TerminateProcess(hEvent[1], 1);
		ExitProcess(0);
	}
	CloseHandle(hEvent[0]);
}

// Function name	: ManageProcess
// Description	    : 
// Return type		: void 
// Argument         : char *pszCmdLine
// Argument         : char *pszArgs
// Argument         : char *pszEnv
// Argument         : char *pszDir
// Argument         : int nGroupID
// Argument         : int nGroupRank
// Argument         : char *pszIn
// Argument         : char *pszOut
// Argument         : char *pszErr
// Argument         : HANDLE hAbortEvent
void ManageProcess(char *pszCmdLine, char *pszArgs, char *pszEnv, char *pszDir, int nGroupID, int nGroupRank, char *pszIn, char *pszOut, char *pszErr, HANDLE hAbortEvent)
{
	HANDLE hInThread = NULL, hOutThread = NULL, hErrThread = NULL;
	char pBuffer[100];
	BOOL bSuccess = FALSE;
	HANDLE hStdin, hStdout, hStderr;
	HANDLE hStdoutPipeW=NULL, hStderrPipeW = NULL, hStdinPipeR=NULL;
	HANDLE hStdoutPipeR=NULL, hStderrPipeR = NULL, hStdinPipeW=NULL;
	HANDLE hTempPipe=NULL;
	HANDLE hUser = NULL;
	STARTUPINFO saInfo;
	PROCESS_INFORMATION psInfo;
	void *pEnv=NULL;
	TCHAR tSavedPath[MAX_PATH] = TEXT(".");
	int nError;
	char *pCmd;

	SetEnvironmentVariable("MPICH_USE_MPD", "1");
	if (nGroupID != -1)
	{
		sprintf(pBuffer, "%d", nGroupID);
		SetEnvironmentVariable("MPD_GROUP_ID", pBuffer);
	}

	if (nGroupRank != -1)
	{
		sprintf(pBuffer, "%d", nGroupRank);
		SetEnvironmentVariable("MPD_GROUP_RANK", pBuffer);
	}

	// Don't handle errors, just let the process die.
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

	// Save stdin, stdout, and stderr
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	hStderr = GetStdHandle(STD_ERROR_HANDLE);
	if (hStdin == INVALID_HANDLE_VALUE || hStdout == INVALID_HANDLE_VALUE  || hStderr == INVALID_HANDLE_VALUE)
	{
		nError = GetLastError();
		return;
	}

	// Set the security attributes to allow handles to be inherited
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.lpSecurityDescriptor = NULL;
	saAttr.bInheritHandle = TRUE;

	// Create pipes for stdin, stdout, and stderr

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

	// Stderr
	if (!CreatePipe(&hTempPipe, &hStderrPipeW, &saAttr, 0))
	{
		nError = GetLastError();
		goto CLEANUP;
	}
	// Make the read end of the stderr pipe not inheritable
	if (!DuplicateHandle(GetCurrentProcess(), hTempPipe, 
		GetCurrentProcess(), &hStderrPipeR, 
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
	if (!SetStdHandle(STD_ERROR_HANDLE, hStderrPipeW))
	{
		nError = GetLastError();
		goto RESTORE_CLEANUP;
	}

	// Set up the STARTINFO structure
	memset(&saInfo, 0, sizeof(STARTUPINFO));
	saInfo.cb         = sizeof(STARTUPINFO);
	saInfo.hStdInput  = hStdinPipeR;
	saInfo.hStdOutput = hStdoutPipeW;
	saInfo.hStdError  = hStderrPipeW;
	saInfo.dwFlags    = STARTF_USESTDHANDLES;

	// Set the environment variables
	SetEnvironmentVariables(pszEnv);
	pEnv = GetEnvironmentStrings();

	// Create the process

	// Attempt to change into the directory passed into the function
	GetCurrentDirectory(MAX_PATH, tSavedPath);
	if (!SetCurrentDirectory(pszDir))
	{
		nError = GetLastError();
	}

	pCmd = pszCmdLine;
	if (strlen(pszArgs))
	{
		char *pTemp = new char[strlen(pszCmdLine) + strlen(pszArgs) + 2];
		sprintf(pTemp, "%s %s", pszCmdLine, pszArgs);
		pszCmdLine = pTemp;
	}

	if (CreateProcess(
		NULL,
		pszCmdLine,
		NULL, NULL, TRUE,
		//DETACHED_PROCESS | IDLE_PRIORITY_CLASS, 
		//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS,
		CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP,
		//DETACHED_PROCESS | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP,
		//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_SUSPENDED, 
		pEnv,
		NULL,
		&saInfo, &psInfo))
	{
		CloseHandle(psInfo.hThread);
		bSuccess = TRUE;
	}
	else
	{
		nError = GetLastError();
		//Translate_Error(*nError, error_msg, L"LaunchProcess:CreateProcessAsUser failed: ");
	}

	if (pCmd != pszCmdLine)
		delete pszCmdLine;
	FreeEnvironmentStrings((char*)pEnv);
	SetCurrentDirectory(tSavedPath);
	RemoveEnvironmentVariables(pszEnv);
	SetEnvironmentVariable("MPICH_USE_MPD", NULL);
	SetEnvironmentVariable("MPD_GROUP_ID", NULL);
	SetEnvironmentVariable("MPD_GROUP_RANK", NULL);

RESTORE_CLEANUP:
	// Restore stdin, stdout, stderr
	if (!SetStdHandle(STD_INPUT_HANDLE, hStdin))
	{
		nError = GetLastError();
	}
	if (!SetStdHandle(STD_OUTPUT_HANDLE, hStdout))
	{
		nError = GetLastError();
	}
	if (!SetStdHandle(STD_ERROR_HANDLE, hStderr))
	{
		nError = GetLastError();
	}

CLEANUP:
	CloseHandle(hStdoutPipeW);
	CloseHandle(hStderrPipeW);
	CloseHandle(hStdinPipeR);

	if (bSuccess)
	{
		HANDLE hThreads[3];
		int nThreads = 0;
		char *token;

		if (pszIn != NULL && strlen(pszIn) > 0)
		{
			PipeSocketArg *pArg = new PipeSocketArg;
			pArg->c = 0;
			pArg->hPipe = hStdinPipeW;
			strcpy(pArg->pszHost, pszIn);
			token = strtok(pArg->pszHost, ":");
			token = strtok(NULL, "");
			if (token != NULL)
				pArg->nPort = atoi(token);
			DWORD dwThreadID;
			hInThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectSocketToInput, pArg, 0, &dwThreadID);
			hThreads[nThreads] = hInThread;
			nThreads++;
		}
		if (pszOut != NULL && strlen(pszOut) > 0)
		{
			PipeSocketArg *pArg = new PipeSocketArg;
			pArg->c = 1;
			pArg->hPipe = hStdoutPipeR;
			strcpy(pArg->pszHost, pszOut);
			token = strtok(pArg->pszHost, ":");
			token = strtok(NULL, "");
			if (token != NULL)
				pArg->nPort = atoi(token);
			DWORD dwThreadID;
			hOutThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectOutputToSocket, pArg, 0, &dwThreadID);
			hThreads[nThreads] = hOutThread;
			nThreads++;
		}
		if (pszErr != NULL && strlen(pszErr) > 0)
		{
			PipeSocketArg *pArg = new PipeSocketArg;
			pArg->c = 2;
			pArg->hPipe = hStderrPipeR;
			strcpy(pArg->pszHost, pszErr);
			token = strtok(pArg->pszHost, ":");
			token = strtok(NULL, "");
			if (token != NULL)
				pArg->nPort = atoi(token);
			DWORD dwThreadID;
			hErrThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectOutputToSocket, pArg, 0, &dwThreadID);
			hThreads[nThreads] = hErrThread;
			nThreads++;
		}

		AbortMPDArg *pArg = new AbortMPDArg;
		pArg->hAbortEvent = hAbortEvent;
		pArg->hProcess = psInfo.hProcess;
		DWORD dwThreadID;
		CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AbortMPDThread, pArg, 0, &dwThreadID));

		if (nThreads > 0)
		{
			DWORD exitCode;
			WaitForMultipleObjects(nThreads, hThreads, FALSE, INFINITE);
			if (hOutThread != NULL)
				if (WaitForSingleObject(hOutThread, 1000) != WAIT_OBJECT_0)
					TerminateThread(hOutThread, 0);
			if (hErrThread != NULL)
				if (WaitForSingleObject(hErrThread, 1000) != WAIT_OBJECT_0)
					TerminateThread(hErrThread, 0);
			WaitForSingleObject(psInfo.hProcess, 1000);
			GetExitCodeProcess(psInfo.hProcess, &exitCode);
			if (exitCode == STILL_ACTIVE)
				TerminateProcess(psInfo.hProcess, 0);
		}
		else
			WaitForSingleObject(psInfo.hProcess, INFINITE);
	}

	CloseHandle(hStdoutPipeR);
	CloseHandle(hStderrPipeR);
	CloseHandle(hStdinPipeW);
	CloseHandle(psInfo.hProcess);
	if (hInThread != NULL)
		CloseHandle(hInThread);
	if (hOutThread != NULL)
		CloseHandle(hOutThread);
	if (hErrThread != NULL)
		CloseHandle(hErrThread);
}
