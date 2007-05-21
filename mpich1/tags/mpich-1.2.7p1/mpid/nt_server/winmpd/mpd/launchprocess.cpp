#include "mpdimpl.h"

HANDLE g_hLaunchMutex = NULL;

struct CachedUserNode
{
    HANDLE hUser;
    char account[40];
    char domain[40];
    char password[100];
    SYSTEMTIME timestamp;
    CachedUserNode *pNext;
};

CachedUserNode *g_pCachedList = NULL;

void statCachedUsers(char *pszOutput, int length)
{
    CachedUserNode *pIter;

    *pszOutput = '\0';
    length--;

    pIter = g_pCachedList;
    while (pIter)
    {
	if (pIter->domain[0] != '\0')
	    snprintf_update(pszOutput, length, "USER: %s\\%s\n", pIter->domain, pIter->account);
	else
	    snprintf_update(pszOutput, length, "USER: %s\n", pIter->account);
	pIter = pIter->pNext;
    }
}

void CacheUserHandle(char *account, char *domain, char *password, HANDLE hUser)
{
    CachedUserNode *pNode;

    pNode = new CachedUserNode;
    strcpy(pNode->account, account);
    if (domain != NULL)
	strcpy(pNode->domain, domain);
    else
	pNode->domain[0] = '\0';
    strcpy(pNode->password, password);
    pNode->hUser = hUser;
    GetSystemTime(&pNode->timestamp);
    pNode->pNext = g_pCachedList;
    g_pCachedList = pNode;
}

void RemoveCachedUser(HANDLE hUser)
{
    CachedUserNode *pTrailer, *pIter;

    if (g_pCachedList == NULL)
	return;

    if (g_pCachedList->hUser == hUser)
    {
	pIter = g_pCachedList;
	g_pCachedList = g_pCachedList->pNext;
	CloseHandle(pIter->hUser);
	delete pIter;
	return;
    }

    pTrailer = g_pCachedList;
    pIter = g_pCachedList->pNext;
    while (pIter)
    {
	if (pIter->hUser == hUser)
	{
	    pTrailer->pNext = pIter->pNext;
	    CloseHandle(pIter->hUser);
	    delete pIter;
	    return;
	}
	pTrailer = pTrailer->pNext;
	pIter = pIter->pNext;
    }
}

void RemoveAllCachedUsers()
{
    CachedUserNode *pIter;
    while (g_pCachedList)
    {
	pIter = g_pCachedList;
	g_pCachedList = g_pCachedList->pNext;
	CloseHandle(pIter->hUser);
	delete pIter;
    }
}

HANDLE GetCachedUser(char *account, char *domain, char *password)
{
    CachedUserNode *pIter;
    SYSTEMTIME now;

    pIter = g_pCachedList;
    while (pIter)
    {
	if (strcmp(pIter->account, account) == 0)
	{
	    if (domain != NULL)
	    {
		if (strcmp(pIter->domain, domain) == 0)
		{
		    if (strcmp(pIter->password, password) == 0)
		    {
			GetSystemTime(&now);
			if (now.wDay != pIter->timestamp.wDay)
			{
			    // throw away cached handles not created on the same day
			    RemoveCachedUser(pIter->hUser);
			    return INVALID_HANDLE_VALUE;
			}
			return pIter->hUser;
		    }
		}
	    }
	    else
	    {
		if (strcmp(pIter->password, password) == 0)
		{
		    return pIter->hUser;
		}
	    }
	}
	pIter = pIter->pNext;
    }

    return INVALID_HANDLE_VALUE;
}

HANDLE GetUserHandle(char *account, char *domain, char *password, int *pError)
{
    HANDLE hUser;
    int error;
    int num_tries = 3;

    // attempt to get a cached handle
    hUser = GetCachedUser(account, domain, password);
    if (hUser != INVALID_HANDLE_VALUE)
	return hUser;

    // logon the user
    while (!LogonUser(
	account,
	domain, 
	password,
	LOGON32_LOGON_INTERACTIVE, 
	LOGON32_PROVIDER_DEFAULT, 
	&hUser))
    {
	error = GetLastError();
	if (error == ERROR_NO_LOGON_SERVERS)
	{
	    if (num_tries)
		Sleep(250);
	    else
	    {
		*pError = error;
		return INVALID_HANDLE_VALUE;
	    }
	    num_tries--;
	}
	else
	{
	    *pError = error;
	    return INVALID_HANDLE_VALUE;
	}
    }

    // cache the user handle
    CacheUserHandle(account, domain, password, hUser);

    return hUser;
}

HANDLE GetUserHandleNoCache(char *account, char *domain, char *password, int *pError)
{
    HANDLE hUser;
    int error;
    int num_tries = 3;

    // logon the user
    while (!LogonUser(
	account,
	domain, 
	password,
	LOGON32_LOGON_INTERACTIVE, 
	LOGON32_PROVIDER_DEFAULT, 
	&hUser))
    {
	error = GetLastError();
	if (error == ERROR_NO_LOGON_SERVERS)
	{
	    if (num_tries)
		Sleep(250);
	    else
	    {
		*pError = error;
		return INVALID_HANDLE_VALUE;
	    }
	    num_tries--;
	}
	else
	{
	    *pError = error;
	    return INVALID_HANDLE_VALUE;
	}
    }

    // cache the user handle
    CacheUserHandle(account, domain, password, hUser);

    return hUser;
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

// Function name	: LaunchProcess
// Description	    : 
// Return type		: HANDLE 
HANDLE LaunchProcess(char *cmd, char *env, char *dir, int priorityClass, int priority, HANDLE *hIn, HANDLE *hOut, HANDLE *hErr, int *pdwPid, int *nError, char *pszError, bool bDebug)
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
    DWORD launch_flag;
    
    // Launching of the client processes must be synchronized because
    // stdin,out,err are redirected for the entire process, not just this thread.
    WaitForSingleObject(g_hLaunchMutex, INFINITE);
    
    // Don't handle errors, just let the process die.
    // In the future this will be configurable to allow various debugging options.
#ifdef USE_SET_ERROR_MODE
    DWORD dwOriginalErrorMode;
    dwOriginalErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    
    // Save stdin, stdout, and stderr
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    hStderr = GetStdHandle(STD_ERROR_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE || hStdout == INVALID_HANDLE_VALUE  || hStderr == INVALID_HANDLE_VALUE)
    {
	*nError = GetLastError();
	strcpy(pszError, "GetStdHandle failed, ");
	ReleaseMutex(g_hLaunchMutex);
	return INVALID_HANDLE_VALUE;
    }
    
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
    //saInfo.lpDesktop = "WinSta0\\Default";
    //saInfo.wShowWindow = SW_SHOW;
    
    SetEnvironmentVariables(env);
    pEnv = GetEnvironmentStrings();
    
    GetCurrentDirectory(MAX_PATH, tSavedPath);
    SetCurrentDirectory(dir);
    
    launch_flag = 
	//DETACHED_PROCESS | IDLE_PRIORITY_CLASS;
	//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS;
	//CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
	//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP;
	//DETACHED_PROCESS | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP;
	//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_SUSPENDED;
	CREATE_SUSPENDED | CREATE_NO_WINDOW | priorityClass;
    if (bDebug)
	launch_flag = launch_flag | DEBUG_PROCESS;

    if (CreateProcess(
	NULL,
	cmd,
	NULL, NULL, TRUE,
	launch_flag,
	pEnv,
	NULL,
	&saInfo, &psInfo))
    {
	SetThreadPriority(psInfo.hThread, priority);
	ResumeThread(psInfo.hThread);
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
    SetStdHandle(STD_INPUT_HANDLE, hStdin);
    SetStdHandle(STD_OUTPUT_HANDLE, hStdout);
    SetStdHandle(STD_ERROR_HANDLE, hStderr);
    
CLEANUP:
    ReleaseMutex(g_hLaunchMutex);
    CloseHandle(hPipeStdinR);
    CloseHandle(hPipeStdoutW);
    CloseHandle(hPipeStderrW);
    
#ifdef USE_SET_ERROR_MODE
    SetErrorMode(dwOriginalErrorMode);
#endif

    return hRetVal;
}

static void ParseAccountDomain(char *DomainAccount, char *tAccount, char *tDomain)
{
    char *pCh, *pCh2;
    
    pCh = DomainAccount;
    pCh2 = tDomain;
    while ((*pCh != '\\') && (*pCh != '\0'))
    {
	*pCh2 = *pCh;
	pCh++;
	pCh2++;
    }
    if (*pCh == '\\')
    {
	pCh++;
	strcpy(tAccount, pCh);
	*pCh2 = L'\0';
    }
    else
    {
	strcpy(tAccount, DomainAccount);
	tDomain[0] = '\0';
    }
}

bool ValidateUser(char *pszAccount, char *pszPassword, bool bUseCache, int *pError)
{
    HANDLE hUser;
    char account[100], domain[100] = "", *pszDomain;

    ParseAccountDomain(pszAccount, account, domain);
    if (strlen(domain) < 1)
	pszDomain = NULL;
    else
	pszDomain = domain;

    if (bUseCache)
	hUser = GetUserHandle(account, pszDomain, pszPassword, pError);
    else
	hUser = GetUserHandleNoCache(account, pszDomain, pszPassword, pError);

    // does hUser need to be closed?

    return (hUser != INVALID_HANDLE_VALUE);
}

//#define USE_WINDOW_STATIONS
#undef USE_WINDOW_STATIONS

#ifdef USE_WINDOW_STATIONS

// Insert code here to figure out a way to launch a process in the interactive WorkStation and Desktop

HWINSTA hwinstaSave = NULL;
HDESK hdeskSave = NULL;
HWINSTA hwinstaUser = NULL;
HDESK hdeskUser = NULL;

bool AttachToWorkstation()
{
    DWORD dwThreadId;
 
    // Ensure connection to service window station and desktop, and 
    // save their handles. 

    hwinstaSave = GetProcessWindowStation(); 
    dwThreadId = GetCurrentThreadId(); 
    hdeskSave = GetThreadDesktop(dwThreadId); 
 
    // connect to the User's window station and desktop. 

    //RpcImpersonateClient(h); 
    hwinstaUser = OpenWindowStation("WinSta0", TRUE, MAXIMUM_ALLOWED); 
    if (hwinstaUser == NULL) 
    { 
        //RpcRevertToSelf();
	err_printf("AttachToWorkstation:OpenWindowStation failed, error %d.\n", GetLastError());
        return false; 
    } 
    SetProcessWindowStation(hwinstaUser); 
    hdeskUser = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, TRUE, MAXIMUM_ALLOWED); 
    /*
	DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW | DESKTOP_ENUMERATE |
	DESKTOP_HOOKCONTROL | DESKTOP_JOURNALPLAYBACK | DESKTOP_JOURNALRECORD |
	DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP | DESKTOP_WRITEOBJECTS);
	*/
    //RpcRevertToSelf(); 
    if (hdeskUser == NULL) 
    { 
        SetProcessWindowStation(hwinstaSave); 
        CloseWindowStation(hwinstaUser);
	err_printf("AttachToWorkstation:OpenInputDesktop failed, error %d\n", GetLastError());
        return false;
    } 
    SetThreadDesktop(hdeskUser);

    return true;
}

bool DetachFromWorkstation()
{
    // Restore window station and desktop. 
    SetThreadDesktop(hdeskSave); 
    SetProcessWindowStation(hwinstaSave); 
    CloseDesktop(hdeskUser); 
    CloseWindowStation(hwinstaUser); 
    return true;
}
#endif /* USE_WINDOW_STATIONS */

HANDLE LaunchProcessLogon(char *domainaccount, char *password, char *cmd, char *env, char *map, char *dir, int priorityClass, int priority, HANDLE *hIn, HANDLE *hOut, HANDLE *hErr, int *pdwPid, int *nError, char *pszError, bool bDebug)
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
    char account[100], domain[100] = "", *pszDomain = NULL;
    HANDLE hUser;
    int num_tries;
    int error;
    DWORD launch_flag;
    
    // Launching of the client processes must be synchronized because
    // stdin,out,err are redirected for the entire process, not just this thread.
    WaitForSingleObject(g_hLaunchMutex, INFINITE);
    
    // Don't handle errors, just let the process die.
    // In the future this will be configurable to allow various debugging options.
#ifdef USE_SET_ERROR_MODE
    DWORD dwOriginalErrorMode;
    dwOriginalErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    
    // Save stdin, stdout, and stderr
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    hStderr = GetStdHandle(STD_ERROR_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE || hStdout == INVALID_HANDLE_VALUE  || hStderr == INVALID_HANDLE_VALUE)
    {
	*nError = GetLastError();
	strcpy(pszError, "GetStdHandle failed, ");
	ReleaseMutex(g_hLaunchMutex);
#ifdef USE_SET_ERROR_MODE
	SetErrorMode(dwOriginalErrorMode);
#endif
	return INVALID_HANDLE_VALUE;
    }
    
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
	CloseHandle(*hIn);
	goto CLEANUP;
    }
    if (!DuplicateHandle(GetCurrentProcess(), hPipeStderrR, GetCurrentProcess(), hErr, 
	0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
    {
	*nError = GetLastError();
	strcpy(pszError, "DuplicateHandle failed, ");
	CloseHandle(*hIn);
	CloseHandle(*hOut);
	goto CLEANUP;
    }
    
    // Set stdin, stdout, and stderr to the ends of the pipe the created process will use
    if (!SetStdHandle(STD_INPUT_HANDLE, hPipeStdinR))
    {
	*nError = GetLastError();
	strcpy(pszError, "SetStdHandle failed, ");
	CloseHandle(*hIn);
	CloseHandle(*hOut);
	CloseHandle(*hErr);
	goto CLEANUP;
    }
    if (!SetStdHandle(STD_OUTPUT_HANDLE, hPipeStdoutW))
    {
	*nError = GetLastError();
	strcpy(pszError, "SetStdHandle failed, ");
	CloseHandle(*hIn);
	CloseHandle(*hOut);
	CloseHandle(*hErr);
	goto RESTORE_CLEANUP;
    }
    if (!SetStdHandle(STD_ERROR_HANDLE, hPipeStderrW))
    {
	*nError = GetLastError();
	strcpy(pszError, "SetStdHandle failed, ");
	CloseHandle(*hIn);
	CloseHandle(*hOut);
	CloseHandle(*hErr);
	goto RESTORE_CLEANUP;
    }
    
    // Create the process
    memset(&saInfo, 0, sizeof(STARTUPINFO));
    saInfo.cb         = sizeof(STARTUPINFO);
    saInfo.hStdInput  = hPipeStdinR;
    saInfo.hStdOutput = hPipeStdoutW;
    saInfo.hStdError  = hPipeStderrW;
    saInfo.dwFlags    = STARTF_USESTDHANDLES;
    //saInfo.lpDesktop = "WinSta0\\Default";
    //saInfo.wShowWindow = SW_SHOW;
    
    SetEnvironmentVariables(env);
    pEnv = GetEnvironmentStrings();
    
    ParseAccountDomain(domainaccount, account, domain);
    if (strlen(domain) < 1)
	pszDomain = NULL;
    else
	pszDomain = domain;

    hUser = GetUserHandle(account, pszDomain, password, nError);
    if (hUser == INVALID_HANDLE_VALUE)
    {
	strcpy(pszError, "LogonUser failed, ");
	FreeEnvironmentStrings((TCHAR*)pEnv);
	SetCurrentDirectory(tSavedPath);
	RemoveEnvironmentVariables(env);
	CloseHandle(*hIn);
	CloseHandle(*hOut);
	CloseHandle(*hErr);
	goto RESTORE_CLEANUP;
    }

    if (ImpersonateLoggedOnUser(hUser))
    {
	if (!MapUserDrives(map, domainaccount, password, pszError))
	{
	    err_printf("LaunchProcessLogon:MapUserDrives(%s, %s) failed, %s", map, domainaccount, pszError);
	}

	GetCurrentDirectory(MAX_PATH, tSavedPath);
	SetCurrentDirectory(dir);

	launch_flag = 
	    //DETACHED_PROCESS | IDLE_PRIORITY_CLASS;
	    //CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS;
	    //CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
	    //CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP;
	    //DETACHED_PROCESS | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP;
	    //CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_SUSPENDED;
	    CREATE_SUSPENDED | CREATE_NO_WINDOW | priorityClass;
	if (bDebug)
	    launch_flag = launch_flag | DEBUG_PROCESS;

#ifdef USE_WINDOW_STATIONS
	AttachToWorkstation();
#endif

	num_tries = 4;
	do
	{
	    if (CreateProcessAsUser(
		hUser,
		NULL,
		cmd,
		NULL, NULL, TRUE,
		launch_flag,
		pEnv,
		NULL,
		&saInfo, &psInfo))
	    {
		SetThreadPriority(psInfo.hThread, priority);
		ResumeThread(psInfo.hThread);
		hRetVal = psInfo.hProcess;
		*pdwPid = psInfo.dwProcessId;
		
		CloseHandle(psInfo.hThread);
		num_tries = 0;
	    }
	    else
	    {
		error = GetLastError();
		if (error == ERROR_REQ_NOT_ACCEP)
		{
		    Sleep(1000);
		    num_tries--;
		    if (num_tries == 0)
		    {
			*nError = error;
			strcpy(pszError, "CreateProcessAsUser failed, ");
		    }
		}
		else
		{
		    *nError = error;
		    strcpy(pszError, "CreateProcessAsUser failed, ");
		    num_tries = 0;
		}
	    }
	} while (num_tries);
	//RevertToSelf(); // If you call RevertToSelf, the network mapping goes away.
#ifdef USE_WINDOW_STATIONS
	DetachFromWorkstation();
#endif
    }
    else
    {
	*nError = GetLastError();
	strcpy(pszError, "ImpersonateLoggedOnUser failed, ");
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
    ReleaseMutex(g_hLaunchMutex);
    CloseHandle(hPipeStdinR);
    CloseHandle(hPipeStdoutW);
    CloseHandle(hPipeStderrW);

#ifdef USE_SET_ERROR_MODE
    SetErrorMode(dwOriginalErrorMode);
#endif

    return hRetVal;
}
