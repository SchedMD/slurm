#include "stdafx.h"
#include "LaunchProcess.h"

// Function name	: SetEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : char *bEnv
void SetEnvironmentVariables(char *bEnv)
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
		bEnv++;
	}
	*pChar = '\0';
	SetEnvironmentVariable(name, value);
}

// Function name	: RemoveEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : char *bEnv
void RemoveEnvironmentVariables(char *bEnv)
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
		bEnv++;
	}
	*pChar = '\0';
	SetEnvironmentVariable(name, NULL);
}

// Function name	: LaunchProcess
// Description	    : 
// Return type		: HANDLE 
// Argument         : char *cmd
// Argument         : char *env
// Argument         : char *dir
// Argument         : HANDLE *hIn
// Argument         : HANDLE *hOut
// Argument         : HANDLE *hErr
// Argument         : DWORD *pdwPid
HANDLE LaunchProcess(char *cmd, char *env, char *dir, HANDLE *hIn, HANDLE *hOut, HANDLE *hErr, DWORD *pdwPid)
{
	HANDLE hStdin, hStdout, hStderr;
	HANDLE hPipeStdinR=NULL, hPipeStdinW=NULL;
	HANDLE hPipeStdoutR=NULL, hPipeStdoutW=NULL;
	HANDLE hPipeStderrR=NULL, hPipeStderrW=NULL;
	STARTUPINFO saInfo;
	PROCESS_INFORMATION psInfo;
	void *pEnv=NULL;
	char tSavedPath[MAX_PATH] = ".";
	HANDLE hRetVal = INVALID_HANDLE_VALUE;

	// Launching of the client processes must be synchronized because
	// stdin,out,err are redirected for the entire process, not just this thread.
	HANDLE hMutex = CreateMutex(NULL, FALSE, "mpichSimpleLaunchMutex");

	WaitForSingleObject(hMutex, INFINITE);

	// Don't handle errors, just let the process die.
	// In the future this will be configurable to allow various debugging options.
	//SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

	// Save stdin, stdout, and stderr
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	hStderr = GetStdHandle(STD_ERROR_HANDLE);
	if (hStdin == INVALID_HANDLE_VALUE || hStdout == INVALID_HANDLE_VALUE  || hStderr == INVALID_HANDLE_VALUE)
	{
		ReleaseMutex(hMutex);
		CloseHandle(hMutex);
		return INVALID_HANDLE_VALUE;
	}

	// Set the security attributes to allow handles to be inherited
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.lpSecurityDescriptor = NULL;
	saAttr.bInheritHandle = TRUE;

	// Create pipes for stdin, stdout, and stderr
	if (!CreatePipe(&hPipeStdinR, &hPipeStdinW, &saAttr, 0))
		goto CLEANUP;
	if (!CreatePipe(&hPipeStdoutR, &hPipeStdoutW, &saAttr, 0))
		goto CLEANUP;
	if (!CreatePipe(&hPipeStderrR, &hPipeStderrW, &saAttr, 0))
		goto CLEANUP;

	// Make the ends of the pipes that this process will use not inheritable
	if (!DuplicateHandle(GetCurrentProcess(), hPipeStdinW, GetCurrentProcess(), hIn, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
		goto CLEANUP;
	if (!DuplicateHandle(GetCurrentProcess(), hPipeStdoutR, GetCurrentProcess(), hOut, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
		goto CLEANUP;
	if (!DuplicateHandle(GetCurrentProcess(), hPipeStderrR, GetCurrentProcess(), hErr, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
		goto CLEANUP;
		
	// Set stdin, stdout, and stderr to the ends of the pipe the created process will use
	if (!SetStdHandle(STD_INPUT_HANDLE, hPipeStdinR))
		goto CLEANUP;
	if (!SetStdHandle(STD_OUTPUT_HANDLE, hPipeStdoutW))
		goto RESTORE_CLEANUP;
	if (!SetStdHandle(STD_ERROR_HANDLE, hPipeStderrW))
		goto RESTORE_CLEANUP;

	// Create the process
	memset(&saInfo, 0, sizeof(STARTUPINFO));
	saInfo.cb = sizeof(STARTUPINFO);
	saInfo.hStdError = hPipeStderrW;
	saInfo.hStdInput = hPipeStdinR;
	saInfo.hStdOutput = hPipeStdoutW;
	saInfo.dwFlags = STARTF_USESTDHANDLES;
	//saInfo.lpDesktop = TEXT("WinSta0\\Default");

	SetEnvironmentVariables(env);
	pEnv = GetEnvironmentStrings();

	GetCurrentDirectory(MAX_PATH, tSavedPath);
	SetCurrentDirectory(dir);

	if (CreateProcess(
		NULL,
		cmd,
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
		hRetVal = psInfo.hProcess;
		*pdwPid = psInfo.dwProcessId;

		CloseHandle(psInfo.hThread);
	}

	FreeEnvironmentStrings((TCHAR*)pEnv);
	SetCurrentDirectory(tSavedPath);
	RemoveEnvironmentVariables(env);

RESTORE_CLEANUP:
	// Restore stdin, stdout, stderr
	SetStdHandle(STD_INPUT_HANDLE, hStdin);
	SetStdHandle(STD_OUTPUT_HANDLE, hStdout);
	SetStdHandle(STD_ERROR_HANDLE, hStderr);

CLEANUP:
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
	CloseHandle(hPipeStdinR);
	CloseHandle(hPipeStdoutW);
	CloseHandle(hPipeStderrW);

	return hRetVal;
}
