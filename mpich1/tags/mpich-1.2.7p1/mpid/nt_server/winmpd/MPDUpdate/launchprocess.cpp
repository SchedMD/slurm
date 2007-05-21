#include "stdafx.h"

// Function name	: SetEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : char *bEnv
static void SetEnvironmentVariables(char *bEnv)
{
    char name[MAX_PATH]="", value[MAX_PATH]="";
    char *pChar;

    if (!bEnv)
	return;

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
    
    if (!bEnv)
	return;

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

// Function name	: LaunchProcess
// Description	    : 
// Return type		: HANDLE 
HANDLE LaunchProcess(char *cmd, char *env, char *dir, HANDLE *hIn, HANDLE *hOut, HANDLE *hErr, int *pdwPid, int *nError, char *pszError)
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
    
    // Don't handle errors, just let the process die.
    // In the future this will be configurable to allow various debugging options.
    DWORD dwOriginalErrorMode;
    dwOriginalErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    
    // Save stdin, stdout, and stderr
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    hStderr = GetStdHandle(STD_ERROR_HANDLE);
    /*
    if (hStdin == INVALID_HANDLE_VALUE || hStdout == INVALID_HANDLE_VALUE  || hStderr == INVALID_HANDLE_VALUE)
    {
	*nError = GetLastError();
	strcpy(pszError, "GetStdHandle failed, ");
	return INVALID_HANDLE_VALUE;
    }
    */
    
    // Set the security attributes to allow handles to be inherited
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.lpSecurityDescriptor = NULL;
    saAttr.bInheritHandle = TRUE;
    
    // Create pipes for stdin, stdout, and stderr
    if (!CreatePipe(&hPipeStdinR, &hPipeStdinW, &saAttr, 0))
    {
	*nError = GetLastError();
	strcpy(pszError, "CreatePipe failed, ");
	goto CLEANUP;
    }
    if (!CreatePipe(&hPipeStdoutR, &hPipeStdoutW, &saAttr, 0))
    {
	*nError = GetLastError();
	strcpy(pszError, "CreatePipe failed, ");
	goto CLEANUP;
    }
    if (!CreatePipe(&hPipeStderrR, &hPipeStderrW, &saAttr, 0))
    {
	*nError = GetLastError();
	strcpy(pszError, "CreatePipe failed, ");
	goto CLEANUP;
    }
    
    // Make the ends of the pipes that this process will use not inheritable
    if (!DuplicateHandle(GetCurrentProcess(), hPipeStdinW, GetCurrentProcess(), hIn, 
	0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
    {
	*nError = GetLastError();
	strcpy(pszError, "DuplicateHandle failed, ");
	goto CLEANUP;
    }
    if (!DuplicateHandle(GetCurrentProcess(), hPipeStdoutR, GetCurrentProcess(), hOut, 
	0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
    {
	*nError = GetLastError();
	strcpy(pszError, "DuplicateHandle failed, ");
	goto CLEANUP;
    }
    if (!DuplicateHandle(GetCurrentProcess(), hPipeStderrR, GetCurrentProcess(), hErr, 
	0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
    {
	*nError = GetLastError();
	strcpy(pszError, "DuplicateHandle failed, ");
	goto CLEANUP;
    }
    
    // Set stdin, stdout, and stderr to the ends of the pipe the created process will use
    if (!SetStdHandle(STD_INPUT_HANDLE, hPipeStdinR))
    {
	*nError = GetLastError();
	strcpy(pszError, "SetStdHandle failed, ");
	goto CLEANUP;
    }
    if (!SetStdHandle(STD_OUTPUT_HANDLE, hPipeStdoutW))
    {
	*nError = GetLastError();
	strcpy(pszError, "SetStdHandle failed, ");
	goto RESTORE_CLEANUP;
    }
    if (!SetStdHandle(STD_ERROR_HANDLE, hPipeStderrW))
    {
	*nError = GetLastError();
	strcpy(pszError, "SetStdHandle failed, ");
	goto RESTORE_CLEANUP;
    }
    
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
    if (dir)
	SetCurrentDirectory(dir);
    
    if (CreateProcess(
	NULL,
	cmd,
	NULL, NULL, TRUE,
	//DETACHED_PROCESS | IDLE_PRIORITY_CLASS, 
	//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS,
	CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS,
	//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP,
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
    else
    {
	*nError = GetLastError();
	strcpy(pszError, "CreateProcess failed, ");
    }
    
    FreeEnvironmentStrings((TCHAR*)pEnv);
    SetCurrentDirectory(tSavedPath);
    RemoveEnvironmentVariables(env);
    
RESTORE_CLEANUP:
    // Restore stdin, stdout, stderr
    if (hStdin != INVALID_HANDLE_VALUE)
	SetStdHandle(STD_INPUT_HANDLE, hStdin);
    if (hStdout != INVALID_HANDLE_VALUE)
	SetStdHandle(STD_OUTPUT_HANDLE, hStdout);
    if (hStderr != INVALID_HANDLE_VALUE)
	SetStdHandle(STD_ERROR_HANDLE, hStderr);
    
CLEANUP:
    CloseHandle(hPipeStdinR);
    CloseHandle(hPipeStdoutW);
    CloseHandle(hPipeStderrW);
    
    SetErrorMode(dwOriginalErrorMode);

    return hRetVal;
}
