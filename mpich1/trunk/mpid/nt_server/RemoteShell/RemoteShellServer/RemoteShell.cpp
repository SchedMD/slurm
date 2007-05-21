// RemoteShell.cpp : Implementation of CRemoteShell
#include "stdafx.h"
#include "RemoteShellServer.h"
#include "RemoteShell.h"
#include "..\Common\Translate_Error.h"
#include <time.h>
#include <stdio.h>
#include "AccessDesktop.h"

/////////////////////////////////////////////////////////////////////////////
// CRemoteShell

// Function name	: CRemoteShell::~CRemoteShell
// Description	    : 
// Return type		: 
CRemoteShell::~CRemoteShell()
{
	try{

	if (m_hProcess)
	{
		DWORD exitCode;
		GetExitCodeProcess(m_hProcess, &exitCode);
		if (exitCode == STILL_ACTIVE)
			TerminateProcess(m_hProcess, 0);
		CloseHandle(m_hProcess);
	}
	m_hProcess = NULL;

	if (m_hStdoutThread != NULL)
	{
		TerminateThread(m_hStdoutThread, 0);
		CloseHandle(m_hStdoutThread);
		m_hStdoutThread = NULL;
	}

	if (m_hStderrThread != NULL)
	{
		TerminateThread(m_hStderrThread, 0);
		CloseHandle(m_hStderrThread);
		m_hStderrThread = NULL;
	}

	if (m_hOutputMutex)
		CloseHandle(m_hOutputMutex);
	m_hOutputMutex = NULL;

	if (m_hOutputEvent)
		CloseHandle(m_hOutputEvent);
	m_hOutputEvent = NULL;

	if (m_hStdoutPipeR)
		CloseHandle(m_hStdoutPipeR);
	m_hStdoutPipeR = NULL;

	if (m_hStderrPipeR)
		CloseHandle(m_hStderrPipeR);
	m_hStderrPipeR = NULL;

	if (m_hStdinPipeW)
		CloseHandle(m_hStdinPipeW);
	m_hStdinPipeW = NULL;

	if (m_pOutList)
	{
		ChunkNode *n;

		while (m_pOutList != NULL)
		{
			n = m_pOutList;
			m_pOutList = m_pOutList->pNext;
			if (n->pData != NULL)
				delete n->pData;
			delete n;
		}
	}
	m_pOutList = NULL;
	m_pOutListTail = NULL;

	if (m_pMapping && m_hMapping)
		UnmapViewOfFile(m_pMapping);
	if (m_hMapping)
		CloseHandle(m_hMapping);
	m_pMapping = NULL;
	m_hMapping = NULL;

	} catch(...){
		LogMsg(TEXT("Exception thrown in CRemoteShell destructor.\n"));
	}
}

// Function name	: RedirectStdout
// Description	    : 
// Return type		: void 
// Argument         : CRemoteShell *pCom
void RedirectStdout(CRemoteShell *pCom)
{
	unsigned char buffer[1024];
	DWORD num_read = 1024;

	while (num_read > 0)
	{
		num_read = 1024;
		if (ReadFile(pCom->m_hStdoutPipeR, buffer, num_read, &num_read, NULL))
		{
			if (num_read > 0)
			{
				DLogMsg(TEXT("RedirectStdout: %d bytes read from pipe, about to add to list.\n"), num_read);
				// Insert a node in the list.
				ChunkNode *n = new ChunkNode;
				n->pData = new char[num_read];
				memcpy(n->pData, buffer, num_read);
				n->dwSize = num_read;
				n->pNext = NULL;
				n->bStdError = false;
				WaitForSingleObject(pCom->m_hOutputMutex, INFINITE);
				if (pCom->m_pOutListTail == NULL)
				{
					pCom->m_pOutList = pCom->m_pOutListTail = n;
				}
				else
				{
					pCom->m_pOutListTail->pNext = n;
					pCom->m_pOutListTail = n;
				}
				SetEvent(pCom->m_hOutputEvent);
				ReleaseMutex(pCom->m_hOutputMutex);
				DLogMsg(TEXT("RedirectStdout: data added to m_pOutList.\n"));
			}
			else
			{
				// ReadFile returned zero bytes so the pipes must have closed.
				DLogMsg(TEXT("RedirectStdout: zero bytes read from pipe.\n"));
				WaitForSingleObject(pCom->m_hOutputMutex, INFINITE);
				CloseHandle(pCom->m_hStdoutPipeR);
				//CloseHandle(pCom->m_hStderrPipeR);
				CloseHandle(pCom->m_hStdinPipeW);
				pCom->m_hStdoutPipeR = NULL;
				//pCom->m_hStderrPipeR = NULL;
				pCom->m_hStdinPipeW = NULL;
				ReleaseMutex(pCom->m_hOutputMutex);
			}
		}
		else
		{
			// ReadFile failed so the process must have exited.
			DLogMsg(TEXT("RedirectStdout: ReadFile failed.\n"));
			CloseHandle(pCom->m_hStdoutPipeR);
			//CloseHandle(pCom->m_hStderrPipeR);
			CloseHandle(pCom->m_hStdinPipeW);
			pCom->m_hStdoutPipeR = NULL;
			//pCom->m_hStderrPipeR = NULL;
			pCom->m_hStdinPipeW = NULL;
			break;
		}
	}

	// Insert a node indicating the end of the stream.
	DLogMsg(TEXT("RedirectStdout: inserting last node to signal no more data.\n"));
	ChunkNode *n = new ChunkNode;
	n->dwSize = 0; // <---- 0 size indicates end of stream.
	n->pData = NULL; // <---- NULL data indicates end of stream.
	n->pNext = NULL;
	n->bStdError = false;
	GetExitCodeProcess(pCom->m_hProcess, &n->dwExitCode);
	WaitForSingleObject(pCom->m_hOutputMutex, INFINITE);
	if (pCom->m_pOutListTail == NULL)
		pCom->m_pOutList = pCom->m_pOutListTail = n;
	else
	{
		pCom->m_pOutListTail->pNext = n;
		pCom->m_pOutListTail = n;
	}
	SetEvent(pCom->m_hOutputEvent);

	// Close the thread handle because the thread is about to exit.
	// Other threads may access this thread handle but no other thread
	// is checking to see when this thread exits so I close the handle here.
	// These two lines of code are protected by m_hOutputMutex
	//CloseHandle(pCom->m_hStdoutThread);
	//pCom->m_hStdoutThread = NULL;

	ReleaseMutex(pCom->m_hOutputMutex);
}

// Function name	: RedirectStderr
// Description	    : 
// Return type		: void 
// Argument         : CRemoteShell *pCom
void RedirectStderr(CRemoteShell *pCom)
{
	unsigned char buffer[1024];
	DWORD num_read = 1024;

	while (num_read > 0)
	{
		num_read = 1024;
		if (ReadFile(pCom->m_hStderrPipeR, buffer, num_read, &num_read, NULL))
		{
			if (num_read > 0)
			{
				DLogMsg(TEXT("RedirectStdout: %d bytes read from pipe, about to add to list.\n"), num_read);
				// Insert a node in the list.
				ChunkNode *n = new ChunkNode;
				n->pData = new char[num_read];
				memcpy(n->pData, buffer, num_read);
				n->dwSize = num_read;
				n->pNext = NULL;
				n->bStdError = true;
				WaitForSingleObject(pCom->m_hOutputMutex, INFINITE);
				if (pCom->m_pOutListTail == NULL)
				{
					pCom->m_pOutList = pCom->m_pOutListTail = n;
				}
				else
				{
					pCom->m_pOutListTail->pNext = n;
					pCom->m_pOutListTail = n;
				}
				SetEvent(pCom->m_hOutputEvent);
				ReleaseMutex(pCom->m_hOutputMutex);
				DLogMsg(TEXT("RedirectStdout: data added to m_pOutList.\n"));
			}
			else
			{
				// ReadFile returned zero bytes so the pipes must have closed.
				DLogMsg(TEXT("RedirectStdout: zero bytes read from pipe.\n"));
				WaitForSingleObject(pCom->m_hOutputMutex, INFINITE);
				//CloseHandle(pCom->m_hStdoutPipeR);
				CloseHandle(pCom->m_hStderrPipeR);
				CloseHandle(pCom->m_hStdinPipeW);
				//pCom->m_hStdoutPipeR = NULL;
				pCom->m_hStderrPipeR = NULL;
				pCom->m_hStdinPipeW = NULL;
				ReleaseMutex(pCom->m_hOutputMutex);
			}
		}
		else
		{
			// ReadFile failed so the process must have exited.
			DLogMsg(TEXT("RedirectStdout: ReadFile failed.\n"));
			//CloseHandle(pCom->m_hStdoutPipeR);
			CloseHandle(pCom->m_hStderrPipeR);
			CloseHandle(pCom->m_hStdinPipeW);
			//pCom->m_hStdoutPipeR = NULL;
			pCom->m_hStderrPipeR = NULL;
			pCom->m_hStdinPipeW = NULL;
			break;
		}
	}

	/*
	// Insert a node indicating the end of the stream.
	DLogMsg(TEXT("RedirectStdout: inserting last node to signal no more data.\n"));
	ChunkNode *n = new ChunkNode;
	n->dwSize = 0; // <---- 0 size indicates end of stream.
	n->pData = NULL; // <---- NULL data indicates end of stream.
	n->pNext = NULL;
	GetExitCodeProcess(pCom->m_hProcess, &n->dwExitCode);
	//*/
	WaitForSingleObject(pCom->m_hOutputMutex, INFINITE);
	/*
	if (pCom->m_pOutListTail == NULL)
		pCom->m_pOutList = pCom->m_pOutListTail = n;
	else
	{
		pCom->m_pOutListTail->pNext = n;
		pCom->m_pOutListTail = n;
	}
	SetEvent(pCom->m_hOutputEvent);
	//*/

	// Close the thread handle because the thread is about to exit.
	// Other threads may access this thread handle but no other thread
	// is checking to see when this thread exits so I close the handle here.
	// These two lines of code are protected by m_hOutputMutex
	//CloseHandle(pCom->m_hStderrThread);
	//pCom->m_hStderrThread = NULL;

	ReleaseMutex(pCom->m_hOutputMutex);
}

// Function name	: SetEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : BSTR bEnv
void SetEnvironmentVariables(BSTR bEnv)
{
	TCHAR name[MAX_PATH]=_T(""), value[MAX_PATH]=_T("");
	WCHAR wName[MAX_PATH]=L"", wValue[MAX_PATH]=L"";
	WCHAR *pChar;

	pChar = wName;
	while (*bEnv != L'\0')
	{
		if (*bEnv == L'=')
		{
			*pChar = L'\0';
			pChar = wValue;
		}
		else
		if (*bEnv == L'|')
		{
			*pChar = L'\0';
			pChar = wName;
#ifdef UNICODE
			wcscpy(name, wName);
			wcscpy(value, wValue);
#else
			wcstombs(name, wName, wcslen(wName)+1);
			wcstombs(value, wValue, wcslen(wValue)+1);
#endif
			SetEnvironmentVariable(name, value);
		}
		else
		{
			*pChar = *bEnv;
			pChar++;
		}
		bEnv++;
	}
	*pChar = L'\0';
#ifdef UNICODE
	wcscpy(name, wName);
	wcscpy(value, wValue);
#else
	wcstombs(name, wName, wcslen(wName)+1);
	wcstombs(value, wValue, wcslen(wValue)+1);
#endif
	//LogWMsg(L"name: '%s', value: '%s'\n", wName, wValue);
	SetEnvironmentVariable(name, value);
}

// Function name	: RemoveEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : BSTR bEnv
void RemoveEnvironmentVariables(BSTR bEnv)
{
	TCHAR name[MAX_PATH]=_T("");
	WCHAR wName[MAX_PATH]=L"", wValue[MAX_PATH]=L"";
	WCHAR *pChar;

	pChar = wName;
	while (*bEnv != L'\0')
	{
		if (*bEnv == L'=')
		{
			*pChar = L'\0';
			pChar = wValue;
		}
		else
		if (*bEnv == L'|')
		{
			*pChar = L'\0';
			pChar = wName;
#ifdef UNICODE
			wcscpy(name, wName);
#else
			wcstombs(name, wName, wcslen(wName)+1);
#endif
			SetEnvironmentVariable(name, NULL);
		}
		else
		{
			*pChar = *bEnv;
			pChar++;
		}
		bEnv++;
	}
	*pChar = L'\0';
#ifdef UNICODE
	wcscpy(name, wName);
#else
	wcstombs(name, wName, wcslen(wName)+1);
#endif
	SetEnvironmentVariable(name, NULL);
}

// Function name	: ParseAccountDomain
// Description	    : 
// Return type		: void 
// Argument         : LPCWSTR bAccount
// Argument         : LPTSTR tAccount
// Argument         : LPTSTR tDomain
void ParseAccountDomain(LPCWSTR bAccount, LPTSTR tAccount, LPTSTR tDomain)
{
	WCHAR wAccount[100]=L"", wDomain[100]=L"", *pwCh2;
	LPCWSTR pwCh;

	pwCh = bAccount;
	pwCh2 = wDomain;
	while ((*pwCh != L'\\') && (*pwCh != L'\0'))
	{
		*pwCh2 = *pwCh;
		pwCh++;
		pwCh2++;
	}
	if (*pwCh == L'\\')
	{
		pwCh++;
		wcscpy(wAccount, pwCh);
		*pwCh2 = L'\0';
	}
	else
	{
		wcscpy(wAccount, bAccount);
		wDomain[0] = L'\0';
	}
#ifdef UNICODE
	wcscpy(tAccount, wAccount);
	wcscpy(tDomain, wDomain);
#else
	wcstombs(tAccount, wAccount, wcslen(wAccount)+1);
	wcstombs(tDomain, wDomain, wcslen(wDomain)+1);
#endif
}

// Function name	: ParseAccountDomainW
// Description	    : 
// Return type		: void 
// Argument         : LPCWSTR bAccount
// Argument         : LPWSTR wAccount
// Argument         : LPWSTR wDomain
void ParseAccountDomainW(LPCWSTR bAccount, LPWSTR wAccount, LPWSTR wDomain)
{
	WCHAR *pwCh2;
	LPCWSTR pwCh;

	wAccount[0] = L'\0';
	wDomain[0] = L'\0';

	pwCh = bAccount;
	pwCh2 = wDomain;
	while ((*pwCh != L'\\') && (*pwCh != L'\0'))
	{
		*pwCh2 = *pwCh;
		pwCh++;
		pwCh2++;
	}
	if (*pwCh == L'\\')
	{
		pwCh++;
		wcscpy(wAccount, pwCh);
		*pwCh2 = L'\0';
	}
	else
	{
		wcscpy(wAccount, bAccount);
		wDomain[0] = L'\0';
	}
}

// Function name	: CRemoteShell::LaunchProcess
// Description	    : 
// Return type		: STDMETHODIMP 
// Argument         : BSTR bCmdLine
// Argument         : BSTR bEnv
// Argument         : BSTR bDir
// Argument         : BSTR bAccount
// Argument         : BSTR bPassword
// Argument         : long *nPid
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::LaunchProcess(BSTR bCmdLine, BSTR bEnv, BSTR bDir, BSTR bAccount, BSTR bPassword, long *nPid, long *nError, BSTR *bErrorMsg)
{
	WCHAR error_msg[256];
	BOOL bSuccess = FALSE;
	HANDLE hStdin, hStdout, hStderr;
	HANDLE hStdoutPipeW=NULL, hStderrPipeW = NULL, hStdinPipeR=NULL;
	HANDLE hTempPipe=NULL;
	HANDLE hUser = NULL;
	STARTUPINFO saInfo;
	PROCESS_INFORMATION psInfo;
	void *pEnv=NULL;
	TCHAR tCmdLine[MAX_PATH];
	TCHAR tSavedPath[MAX_PATH] = TEXT(".");
	TCHAR tAccount[256], tPassword[256], tDomain[256], *psztDomain;
	HANDLE hImpersonatedToken;
	HRESULT hr;

	try{
	DLogMsg(TEXT("LaunchProcess called: %u\n"), this);
	DLogWMsg(L"\n     Launching:\n        %s\n        %s\n\n", bCmdLine, bEnv);

#ifdef UNICODE
	wcscpy(tCmdLine, bCmdLine);
	//swprintf(tCmdLine, L"cmd.exe /c %s", bCmdLine);
#else
	wcstombs(tCmdLine, bCmdLine, wcslen(bCmdLine)+1);
	//char sTempBuffer[256];
	//wcstombs(sTempBuffer, bCmdLine, wcslen(bCmdLine)+1);
	//sprintf(tCmdLine, "cmd.exe /c %s", sTempBuffer);
#endif

	// Launching of the client processes must be synchronized because
	// stdin,out,err are redirected for the entire process, not just this thread.
	if (WaitForSingleObject(g_hLaunchSyncMutex, g_nLaunchTimeout) == WAIT_TIMEOUT)
	{
		*nError = 1;
		SysReAllocString(bErrorMsg, L"LaunchProcess: Timeout while waiting for syncronization object.\n");
		LogMsg(TEXT("LaunchProcess: Timeout while waiting for syncronization object.\n"));
		return S_OK;
	}

	// Don't handle errors, just let the process die.
	// In the future this will be configurable to allow various debugging options.
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

	// Save stdin, stdout, and stderr
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	hStderr = GetStdHandle(STD_ERROR_HANDLE);
	if (hStdin == INVALID_HANDLE_VALUE || hStdout == INVALID_HANDLE_VALUE  || hStderr == INVALID_HANDLE_VALUE)
	{
		*nError = GetLastError();
		SysReAllocString(bErrorMsg, L"LaunchProcess: Unable to get standard handles.\n");
		ReleaseMutex(g_hLaunchSyncMutex);
		return S_OK;
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
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:CreatePipe failed ");
		SysReAllocString(bErrorMsg, error_msg);
		goto CLEANUP;
	}
	// Make the read end of the stdout pipe not inheritable
	if (!DuplicateHandle(GetCurrentProcess(), hTempPipe, 
		GetCurrentProcess(), &m_hStdoutPipeR, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:DuplicateHandle(StdoutPipeR) failed ");
		SysReAllocString(bErrorMsg, error_msg);
		goto CLEANUP;
	}

	// Stderr
	if (!CreatePipe(&hTempPipe, &hStderrPipeW, &saAttr, 0))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:CreatePipe failed ");
		SysReAllocString(bErrorMsg, error_msg);
		goto CLEANUP;
	}
	// Make the read end of the stderr pipe not inheritable
	if (!DuplicateHandle(GetCurrentProcess(), hTempPipe, 
		GetCurrentProcess(), &m_hStderrPipeR, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:DuplicateHandle(StderrPipeR) failed ");
		SysReAllocString(bErrorMsg, error_msg);
		goto CLEANUP;
	}

	// Stdin
	if (!CreatePipe(&hStdinPipeR, &hTempPipe, &saAttr, 0))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:CreatePipe failed");
		SysReAllocString(bErrorMsg, error_msg);
		goto CLEANUP;
	}
	// Make the write end of the stdin pipe not inheritable
	if (!DuplicateHandle(GetCurrentProcess(), hTempPipe, 
		GetCurrentProcess(), &m_hStdinPipeW, 
		0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:DuplicateHandle(StdoutPipeR) failed ");
		SysReAllocString(bErrorMsg, error_msg);
		goto CLEANUP;
	}

	// Set stdin, stdout, and stderr to the ends of the pipe the created process will use
	if (!SetStdHandle(STD_INPUT_HANDLE, hStdinPipeR))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:SetStdHandle(Input) failed ");
		SysReAllocString(bErrorMsg, error_msg);
		goto CLEANUP;
	}
	if (!SetStdHandle(STD_OUTPUT_HANDLE, hStdoutPipeW))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:SetStdHandle(Output) failed ");
		SysReAllocString(bErrorMsg, error_msg);
		goto RESTORE_CLEANUP;
	}
	if (!SetStdHandle(STD_ERROR_HANDLE, hStderrPipeW))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:SetStdHandle(Error) failed ");
		SysReAllocString(bErrorMsg, error_msg);
		goto RESTORE_CLEANUP;
	}

	// Set up the STARTINFO structure
	memset(&saInfo, 0, sizeof(STARTUPINFO));
	saInfo.cb         = sizeof(STARTUPINFO);
	saInfo.hStdInput  = hStdinPipeR;
	saInfo.hStdOutput = hStdoutPipeW;
	saInfo.hStdError  = hStderrPipeW;
	saInfo.dwFlags    = STARTF_USESTDHANDLES;
	if (m_bLaunchOnDesktop)
		saInfo.lpDesktop  = TEXT("WinSta0\\Default");

	// Set the environment variables
	SetEnvironmentVariables(bEnv);
	pEnv = GetEnvironmentStrings();

	// Get a handle to the user token either by logging in or impersonating the user.
	if (wcslen(bAccount))
	{
		// An account was passed in so use it to get the user token.
		ParseAccountDomain(bAccount, tAccount, tDomain);
		if (_tcslen(tDomain) < 1)
			psztDomain = NULL;
		else
			psztDomain = tDomain;
#ifdef UNICODE
		wcscpy(tPassword, bPassword);
#else
		wcstombs(tPassword, bPassword, wcslen(bPassword)+1);
#endif
		if (!LogonUser(
			tAccount,
			psztDomain, 
			tPassword,
			LOGON32_LOGON_INTERACTIVE, 
			LOGON32_PROVIDER_DEFAULT, 
			&hUser))
		{
			*nError = GetLastError();
			Translate_Error(*nError, error_msg, L"LaunchProcess:LogonUser failed ");
			SysReAllocString(bErrorMsg, error_msg);
			LogWMsg(L"LaunchProcess: LogonUser failed: %d, %s\n", *nError, error_msg);
			goto RESTORE_CLEANUP;
		}

	}
	else
	{
		// No account was passed in so impersonate the client to get a user token
		hr = CoImpersonateClient();
		if (FAILED(hr))
			LogMsg(TEXT("LaunchProcess:CoImpersonateClient failed - launching process with process token"));
		//if (!OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, TRUE, &hImpersonatedToken))
		if (!OpenThreadToken(GetCurrentThread(), MAXIMUM_ALLOWED, TRUE, &hImpersonatedToken))
		{
			*nError = GetLastError();
			Translate_Error(*nError, error_msg, L"LaunchProcess:OpenThreadToken failed: ");
			SysReAllocString(bErrorMsg, error_msg);
			LogWMsg(L"LaunchProcess:OpenThreadToken failed: %d, %s\n", *nError, error_msg);
			goto RESTORE_CLEANUP;
		}
		CoRevertToSelf();
		//if (!DuplicateTokenEx(hImpersonatedToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hUser))
		if (!DuplicateTokenEx(hImpersonatedToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hUser))
		{
			*nError = GetLastError();
			Translate_Error(*nError, error_msg, L"LaunchProcess:DuplicateTokenEx failed: ");
			SysReAllocString(bErrorMsg, error_msg);
			LogWMsg(L"LaunchProcess:DuplicateTokenEx failed: %d, %s\n", *nError, error_msg);
			goto RESTORE_CLEANUP;
		}
	}

	// Create the process

	//LogMsg(TEXT("impersonating user\n"));
	if (ImpersonateLoggedOnUser(hUser))
	{
		// Attempt to change into the directory passed into the function
		GetCurrentDirectory(MAX_PATH, tSavedPath);
		if (!SetCurrentDirectoryW(bDir))
		{
			int terror = GetLastError();
			char terror_msg[256];
			Translate_Error(terror, terror_msg, "LaunchProcess:SetCurrentDirectory failed ");
			LogMsg(terror_msg);
		}

		//LogMsg(TEXT("LaunchInteractiveProcess: about to launch %s.\n"), tCmdLine);
		if (CreateProcessAsUser(
			hUser,
			NULL,
			tCmdLine,
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
			m_hProcess = psInfo.hProcess;
			//ResumeThread(psInfo.hThread);
			CloseHandle(psInfo.hThread);
			LogMsg(TEXT("LaunchProcess: launched '%s'"), tCmdLine);
			bSuccess = TRUE;
			*nPid = psInfo.dwProcessId;
			m_dwProcessId = psInfo.dwProcessId;
			SysReAllocString(bErrorMsg, L"success");
			*nError = 0;
		}
		else
		{
			*nError = GetLastError();
			Translate_Error(*nError, error_msg, L"LaunchProcess:CreateProcessAsUser failed: ");
			SysReAllocString(bErrorMsg, error_msg);
			LogWMsg(L"LaunchProcess: CreateProcessAsUser failed: error %d, %s", *nError, error_msg);
			LogMsg("LaunchProcess: failed to launch '%s'", tCmdLine);
		}
		RevertToSelf();
	}
	else
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:ImpersonateLoggedOnUser failed ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"LaunchProcess: ImpersonateLoggedOnUser failed: %d, %s\n", *nError, error_msg);
	}
	CloseHandle(hUser);

	FreeEnvironmentStrings((TCHAR*)pEnv);
	SetCurrentDirectory(tSavedPath);
	RemoveEnvironmentVariables(bEnv);

RESTORE_CLEANUP:
	// Restore stdin, stdout, stderr
	if (!SetStdHandle(STD_INPUT_HANDLE, hStdin))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:SetStdHandle(restore Input) failed ");
		SysReAllocString(bErrorMsg, error_msg);
	}
	if (!SetStdHandle(STD_OUTPUT_HANDLE, hStdout))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:SetStdHandle(restore Output) failed ");
		SysReAllocString(bErrorMsg, error_msg);
	}
	if (!SetStdHandle(STD_ERROR_HANDLE, hStderr))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"LaunchProcess:SetStdHandle(restore Error) failed ");
		SysReAllocString(bErrorMsg, error_msg);
	}

	if (bSuccess)
	{
		// start threads to monitor output of pipes
		DWORD dwThreadID;
		m_hStdoutThread = CreateThread(
			NULL, 0, (LPTHREAD_START_ROUTINE)RedirectStdout,
			this, 0, &dwThreadID);
		
		if (m_hStdoutThread == NULL)
			SysReAllocString(bErrorMsg, L"Unable to create a thread to redirect standard out.\n");

		m_hStderrThread = CreateThread(
			NULL, 0, (LPTHREAD_START_ROUTINE)RedirectStderr,
			this, 0, &dwThreadID);

		if (m_hStderrThread == NULL)
			SysReAllocString(bErrorMsg, L"Unable to create a thread to redirect standard error.\n");
	}

CLEANUP:
	ReleaseMutex(g_hLaunchSyncMutex);
	CloseHandle(hStdoutPipeW);
	CloseHandle(hStderrPipeW);
	CloseHandle(hStdinPipeR);

	}catch(...){
		*nError = 1;
		ReleaseMutex(g_hLaunchSyncMutex);
		SysReAllocString(bErrorMsg, L"LaunchProcess:Exception thrown");
		LogWMsg(L"Exception thrown in LaunchProcess");
	}
	return S_OK;
}

// Function name	: CRemoteShell::GetProcessOutput
// Description	    : 
// Return type		: STDMETHODIMP 
// Argument         : VARIANT *vOutput
// Argument         : long *nState
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::GetProcessOutput(VARIANT *vOutput, long *nState, long *nError, BSTR *bErrorMsg)
{
	SAFEARRAYBOUND bound;
	void *pBuf;
	ChunkNode *node;

	try{
	VariantClear(vOutput);

	vOutput->vt = VT_UI1 | VT_ARRAY;

	// It may be worthy to put a timeout value here but as is, this would allow a user to leave
	// a session open for days without timing out.
	WaitForSingleObject(m_hOutputMutex, INFINITE);

	// After this block, m_pOutList is valid.
	if (m_pOutList == NULL)
	{
		// Nothing in the list so release the mutex and wait for something to be added to the list.
		// Release the mutex
		ReleaseMutex(m_hOutputMutex);
		// Wait for the event signalling new data has been added to the list
		WaitForSingleObject(m_hOutputEvent, INFINITE);
		// Wait for the mutex to syncronize access to the list
		WaitForSingleObject(m_hOutputMutex, INFINITE);
	}

	bound.lLbound = 0;
	bound.cElements = m_pOutList->dwSize;

	// Create an array to return the data in.
	vOutput->parray = SafeArrayCreate(VT_UI1, 1, &bound);

	// Copy the data in the list to the array
	if (m_pOutList->dwSize > 0)
	{
		SafeArrayAccessData(vOutput->parray, &pBuf);
		memcpy(pBuf, m_pOutList->pData, m_pOutList->dwSize);
		SafeArrayUnaccessData(vOutput->parray);
	}

	// Remove the node that has just been copied and signal whether there is potentially more data to come.
	node = m_pOutList;

	m_pOutList = m_pOutList->pNext;
	if (m_pOutList == NULL)
	{
		m_pOutListTail = NULL;
		ResetEvent(m_hOutputEvent);
	}

	if (node->bStdError)
		*nState = RSH_OUTPUT_STDERR;
	else
		*nState = RSH_OUTPUT_STDOUT;

	if (node->dwSize > 0)
	{
		delete node->pData;
		*nState = (*nState) | RSH_OUTPUT_MORE;
	}
	else
	{
	    if (WaitForSingleObject(m_hStdoutThread, 5000) != WAIT_OBJECT_0)
		TerminateThread(m_hStdoutThread, 0);
	    if (WaitForSingleObject(m_hStderrThread, 5000) != WAIT_OBJECT_0)
		TerminateThread(m_hStderrThread, 0);
	    CloseHandle(m_hStdoutThread);
	    CloseHandle(m_hStderrThread);
	    m_hStdoutThread = NULL;
	    m_hStderrThread = NULL;
	}

	m_dwExitCode = node->dwExitCode;

	delete node;

	ReleaseMutex(m_hOutputMutex);

	}catch(...){
		*nError = 1;
		SysReAllocString(bErrorMsg, L"GetInteractiveOutput:Exception thrown");
		LogWMsg(L"Exception thrown in GetInteractiveOutput.\n");
	}
	return S_OK;
}

// Function name	: CRemoteShell::PutProcessInput
// Description	    : 
// Return type		: STDMETHODIMP 
// Argument         : VARIANT vInput
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::PutProcessInput(VARIANT vInput, long *nError, BSTR *bErrorMsg)
{
	LPVOID pBuf;
	DWORD size, num_written;

	if (vInput.vt == (VT_UI1 | VT_ARRAY))
	{
		size = vInput.parray->rgsabound->cElements;
		SafeArrayAccessData(vInput.parray, &pBuf);
		WriteFile(m_hStdinPipeW, pBuf, size, &num_written, NULL);
		SafeArrayUnaccessData(vInput.parray);
	}

	return S_OK;
}

// Function name	: CRemoteShell::Abort
// Description	    : 
// Return type		: STDMETHODIMP 
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::Abort(long *nError, BSTR *bErrorMsg)
{
	if (m_hProcess != NULL)
	{
		*nError = 1;
		if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, m_dwProcessId))
		{
			if (WaitForSingleObject(m_hProcess, 500) == WAIT_OBJECT_0)
				*nError = 0;
		}
		if (*nError)
		{
			if (TerminateProcess(m_hProcess, 1))
				*nError = 0;
			else
			{
				*nError = GetLastError();
				WCHAR error_msg[256];
				Translate_Error(*nError, error_msg, L"Abort:TerminateProcess failed ");
				SysReAllocString(bErrorMsg, error_msg);
				LogWMsg(L"%d, %s", *nError, error_msg);
			}
		}
	}
	return S_OK;
}

// Function name	: CRemoteShell::SendBreak
// Description	    : 
// Return type		: STDMETHODIMP 
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::SendBreak(long *nError, BSTR *bErrorMsg)
{
	if (m_hProcess != NULL)
	{
		if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, m_dwProcessId))
		//if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, m_dwProcessId))
		{
			*nError = GetLastError();
			WCHAR error_msg[256];
			Translate_Error(*nError, error_msg, L"SendBreak:GenerateConsoleCtrlEvent failed ");
			SysReAllocString(bErrorMsg, error_msg);
			LogWMsg(L"processID: %d, error: %d message: %s", m_dwProcessId, *nError, error_msg);
		}
	}
	return S_OK;
}

// Function name	: CRemoteShell::CreateTempFile
// Description	    : 
// Return type		: STDMETHODIMP 
// Argument         : BSTR *bFileName
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::CreateTempFile(BSTR *bFileName, long *nError, BSTR *bErrorMsg)
{
	WCHAR wTemp[MAX_PATH];
	HRESULT hr;
	HANDLE hImpersonatedToken, hUser;
	WCHAR error_msg[256];
	HKEY hKey;

	hr = CoImpersonateClient();
	if (FAILED(hr))
		LogMsg(TEXT("CreateTempFile:CoImpersonateClient failed - creating temp file with process token"));
	//if (!OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, TRUE, &hImpersonatedToken))
	if (!OpenThreadToken(GetCurrentThread(), MAXIMUM_ALLOWED, TRUE, &hImpersonatedToken))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"CreateTempFile:OpenThreadToken failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateTempFile:OpenThreadToken failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}
	CoRevertToSelf();
	//if (!DuplicateTokenEx(hImpersonatedToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hUser))
	if (!DuplicateTokenEx(hImpersonatedToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hUser))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"CreateTempFile:DuplicateTokenEx failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateTempFile:DuplicateTokenEx failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}

	if (RegOpenKeyEx(
			HKEY_LOCAL_MACHINE, 
			MPICHKEY,
			0, KEY_READ, &hKey) != ERROR_SUCCESS)
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"CreateTempFile:RegOpenKeyEx failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateTempFile:RegOpenKeyEx failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}

	// Read the temp directory
	DWORD type, num_bytes = MAX_PATH*sizeof(WCHAR);
	WCHAR wDir[MAX_PATH];
	if (RegQueryValueExW(hKey, L"Temp", 0, &type, (BYTE *)wDir, &num_bytes) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"CreateTempFile:RegQueryValueExW failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateTempFile:RegQueryValueExW failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}
	RegCloseKey(hKey);

	if (ImpersonateLoggedOnUser(hUser))
	{
		if (GetTempFileNameW(wDir, L"mpi", 0, wTemp) == 0)
		{
			*nError = GetLastError();
			Translate_Error(*nError, wTemp, L"CreateTempFile:GetTempFileName failed ");
			LogWMsg(wTemp);
			SysReAllocString(bErrorMsg, wTemp);
			return S_OK;
		}
		
		WCHAR wFullTemp[MAX_PATH], *namepart;
		GetFullPathNameW(wTemp, MAX_PATH, wFullTemp, &namepart);
	
		RevertToSelf();

		SysReAllocString(bFileName, wFullTemp);

	}
	else
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"CreateTempFile:ImpersonateLoggedOnUser failed ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateTempFile: ImpersonateLoggedOnUser failed: %d, %s\n", *nError, error_msg);
	}
	CloseHandle(hUser);
	return S_OK;
}

// Function name	: CRemoteShell::GetPortFromFile
// Description	    : 
// Return type		: STDMETHODIMP 
// Argument         : BSTR bFileName
// Argument         : long *nPort
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::GetPortFromFile(BSTR bFileName, long *nPort, long *nError, BSTR *bErrorMsg)
{
	WCHAR error_msg[256];
	HRESULT hr;
	HANDLE hImpersonatedToken, hUser;

	hr = CoImpersonateClient();
	if (FAILED(hr))
		LogMsg(TEXT("GetPortFromFile:CoImpersonateClient failed - reading temp file with process token"));
	//if (!OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, TRUE, &hImpersonatedToken))
	if (!OpenThreadToken(GetCurrentThread(), MAXIMUM_ALLOWED, TRUE, &hImpersonatedToken))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"GetPortFromFile:OpenThreadToken failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"GetPortFromFile:OpenThreadToken failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}
	CoRevertToSelf();
	//if (!DuplicateTokenEx(hImpersonatedToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hUser))
	if (!DuplicateTokenEx(hImpersonatedToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hUser))
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"GetPortFromFile:DuplicateTokenEx failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"GetPortFromFile:DuplicateTokenEx failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}

	if (ImpersonateLoggedOnUser(hUser))
	{
		HANDLE hFile = CreateFileW(bFileName, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			*nError = GetLastError();
			Translate_Error(*nError, error_msg, L"GetPortFromFile:CreateFile failed ");
			LogWMsg(error_msg);
			LogWMsg(bFileName);
			SysReAllocString(bErrorMsg, error_msg);
			return S_OK;
		}
		
		DWORD num_read = 0;
		TCHAR pBuffer[100] = _T("");
		LPTSTR pChar = pBuffer;
		clock_t cStart = clock();
		while (true)
		{
			num_read = 0;
			if (!ReadFile(hFile, pChar, 100, &num_read, NULL))
			{
				*nError = GetLastError();
				Translate_Error(*nError, error_msg, L"GetPortFromFile:ReadFile failed ");
				LogWMsg(error_msg);
				SysReAllocString(bErrorMsg, error_msg);
				CloseHandle(hFile);
				DeleteFileW(bFileName);
				return S_OK;
			}
			if (num_read == 0)
			{
				if (clock() - cStart > 10 * CLOCKS_PER_SEC)
				{
					DWORD dwExitCode;
					if (GetExitCodeProcess(m_hProcess, &dwExitCode))
					{
						if (dwExitCode != STILL_ACTIVE)
						{
							LogMsg(TEXT("GetPortFromFile:Process has exited without writing the port number to a file. Exit code: %d"), dwExitCode);
							swprintf(error_msg, L"GetPortFromFile:Process has exited, no port number in the file.\nProcess exit code: %d", dwExitCode);
							*nError = dwExitCode;
							SysReAllocString(bErrorMsg, error_msg);
							CloseHandle(hFile);
							DeleteFileW(bFileName);
							return S_OK;
						}
					}
					LogWMsg(L"GetPortFromFile:Wait for process 0 to write port to temporary file timed out: '%s'\n", bFileName);
					swprintf(error_msg, L"GetPortFromFile:Wait for process 0 to write port to temporary file timed out: '%s'", bFileName);
					*nError = dwExitCode;
					SysReAllocString(bErrorMsg, error_msg);
					CloseHandle(hFile);
					DeleteFileW(bFileName);
					return S_OK;
				}
				Sleep(100);
			}
			else
			{
				for (unsigned int i=0; i<num_read; i++)
				{
					if (*pChar == _T('\n'))
						break;
					pChar ++;
				}
				if (*pChar == _T('\n'))
					break;
			}
		}
		CloseHandle(hFile);
		DeleteFileW(bFileName);
		
		*nPort = _ttoi(pBuffer);
		
		RevertToSelf();
	}
	else
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"GetPortFromFile:ImpersonateLoggedOnUser failed ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"GetPortFromFile: ImpersonateLoggedOnUser failed: %d, %s\n", *nError, error_msg);
	}

	CloseHandle(hUser);

	return S_OK;
}

// Function name	: CRemoteShell::GrantAccessToDesktop
// Description	    : 
// Return type		: STDMETHODIMP 
// Argument         : BSTR bAccount
// Argument         : BSTR bPassword
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::GrantAccessToDesktop(BSTR bAccount, BSTR bPassword, long *nError, BSTR *bErrorMsg)
{
	WCHAR error_msg[255];
	HANDLE hUser = NULL;
	HANDLE hImpersonatedToken = NULL;
	HRESULT hr = S_OK;
	try{
	if (wcslen(bAccount))
	{
		TCHAR tAccount[MAX_PATH], tPassword[MAX_PATH], tDomain[MAX_PATH], *psztDomain;

		ParseAccountDomain(bAccount, tAccount, tDomain);
		if (_tcslen(tDomain) < 1)
			psztDomain = NULL;
		else
			psztDomain = tDomain;
#ifdef UNICODE
		wcscpy(tPassword, bPassword);
#else
		wcstombs(tPassword, bPassword, wcslen(bPassword)+1);
#endif
		if (!LogonUser(
			tAccount,
			psztDomain, 
			tPassword,
			LOGON32_LOGON_INTERACTIVE, 
			LOGON32_PROVIDER_DEFAULT, 
			&hUser))
		{
			*nError = GetLastError();
			Translate_Error(*nError, error_msg, L"GrantAccessToDesktop:LogonUser failed: ");
			SysReAllocString(bErrorMsg, error_msg);
			LogWMsg(L"GrantAccessToDesktop:LogonUser failed: %d, %s\n", *nError, error_msg);
			return S_OK;
		}
	}
	else
	{
		// Impersonate the client and get a user token
		hr = CoImpersonateClient();
		if (FAILED(hr))
			LogMsg(TEXT("GrantAccessToDesktop:CoImpersonateClient failed"));
		if (!OpenThreadToken(GetCurrentThread(), MAXIMUM_ALLOWED, TRUE, &hImpersonatedToken))
		{
			*nError = GetLastError();
			Translate_Error(*nError, error_msg, L"GrantAccessToDesktop:OpenThreadToken failed: ");
			SysReAllocString(bErrorMsg, error_msg);
			LogWMsg(L"GrantAccessToDesktop:OpenThreadToken failed: %d, %s\n", *nError, error_msg);
			return S_OK;
		}
		CoRevertToSelf();
		if (!DuplicateTokenEx(hImpersonatedToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hUser))
		{
			*nError = GetLastError();
			Translate_Error(*nError, error_msg, L"GrantAccessToDesktop:DuplicateTokenEx failed: ");
			SysReAllocString(bErrorMsg, error_msg);
			LogWMsg(L"GrantAccessToDesktop:DuplicateTokenEx failed: %d, %s\n", *nError, error_msg);
			return S_OK;
		}
		CloseHandle(hImpersonatedToken);
		hImpersonatedToken = NULL;
	}
	
	m_bLaunchOnDesktop = MyGrantAccessToDesktop(hUser);

	CloseHandle(hUser);
	hUser = NULL;
	}catch(...)
	{
		if (hUser != NULL)
			CloseHandle(hUser);
		if (hImpersonatedToken != NULL)
			CloseHandle(hImpersonatedToken);
		LogMsg(TEXT("Exception thrown in GrantAccessToDesktop"));
	}
	return S_OK;
}

// Function name	: CRemoteShell::CreateFileMapping
// Description	    : 
// Return type		: STDMETHODIMP 
// Argument         : BSTR bName
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::CreateFileMapping(BSTR bName, long *nError, BSTR *bErrorMsg)
{
	WCHAR error_msg[255];

	if (m_pMapping && m_hMapping)
		UnmapViewOfFile(m_pMapping);
	if (m_hMapping)
		CloseHandle(m_hMapping);
	
	SECURITY_ATTRIBUTES saAttr;
	PSECURITY_DESCRIPTOR pSD;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = FALSE;
	
	// Initialize a security descriptor.
	pSD = (PSECURITY_DESCRIPTOR) LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	if (pSD == NULL) 
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"CreateFileMapping:LocalAlloc failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateFileMapping:LocalAlloc failed: %d, %s\n", *nError, error_msg);
		return S_OK;
    }
	
	if (!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION)) 
	{
		*nError = GetLastError();
		LocalFree ((HLOCAL) pSD);
		Translate_Error(*nError, error_msg, L"CreateFileMapping:InitializeSecurityDescriptor failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateFileMapping:InitializeSecurityDescriptor failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}
	
	// Add a NULL disc. ACL to the security descriptor thus allowing everyone access.
	if (!SetSecurityDescriptorDacl(pSD,
        TRUE,			// specifying a disc. ACL
        (PACL) NULL,
        FALSE))			// not a default disc. ACL
    {
		*nError = GetLastError();
		LocalFree ((HLOCAL) pSD);
		Translate_Error(*nError, error_msg, L"CreateFileMapping:SetSecurityDescriptorDacl failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateFileMapping:SetSecurityDescriptorDacl failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}
	
	saAttr.lpSecurityDescriptor = pSD;

	// Create a mapping from the page file
	m_hMapping = CreateFileMappingW(
		INVALID_HANDLE_VALUE,
		&saAttr,
		PAGE_READWRITE,
		0, sizeof(LONG),
		bName);
	
	// Free the memory for the security descriptor
    LocalFree((HLOCAL) pSD);

	if (m_hMapping == NULL)
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"CreateFileMapping:CreateFileMappingW failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateFileMapping:CreateFileMappingW failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		*nError = 1;
		SysReAllocString(bErrorMsg, L"CreateFileMapping: failure, the file already exists");
		return S_OK;
	}
		
	// Map the file and save the pointer to the base of the mapped file
	m_pMapping = (LONG*)MapViewOfFile(
		m_hMapping,
		FILE_MAP_WRITE,
		0,0,
		sizeof(LONG));
	
	if (m_pMapping == NULL)
	{
		*nError = GetLastError();
		Translate_Error(*nError, error_msg, L"CreateFileMapping:MapViewOfFile failed: ");
		SysReAllocString(bErrorMsg, error_msg);
		LogWMsg(L"CreateFileMapping:MapViewOfFile failed: %d, %s\n", *nError, error_msg);
		return S_OK;
	}

	// Initialize the data to zero
	*m_pMapping = 0;

	return S_OK;
}

// Function name	: CRemoteShell::GetPortFromMapping
// Description	    : This function reads the port from the memory mapped file.
//                    It can only be called once.
// Return type		: STDMETHODIMP 
// Argument         : long *nPort
// Argument         : long *nError
// Argument         : BSTR *bErrorMsg
STDMETHODIMP CRemoteShell::GetPortFromMapping(long *nPort, long *nError, BSTR *bErrorMsg)
{
	if (m_pMapping == NULL || m_hMapping == NULL)
	{
		*nError = 1;
		SysReAllocString(bErrorMsg, L"GetPortFromMapping failed because the mapping hasn't been created yet.");
		LogWMsg(L"GetPortFromMapping failed because the mapping hasn't been created yet.");
		return S_OK;
	}

	// Wait for the launched process to write the port number
	while (*m_pMapping == 0)
		Sleep(200);

	// Save the number
	*nPort = *m_pMapping;

	// Reset the memory region to zero, indicating that the data has been read.
	*m_pMapping = 0;
	
	UnmapViewOfFile(m_pMapping);
	CloseHandle(m_hMapping);

	m_pMapping = NULL;
	m_hMapping = NULL;

	return S_OK;
}
