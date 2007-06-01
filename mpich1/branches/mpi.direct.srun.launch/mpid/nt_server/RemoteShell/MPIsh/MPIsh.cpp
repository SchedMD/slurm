#include <stdio.h>
#include <tchar.h>
#include "..\Common\MPICH_pwd.h"
#include "..\Common\MPIJobDefs.h"
#include <conio.h>
// RemoteShellServer must be compiled before the following files are generated
#include "..\RemoteShellServer\RemoteShellServer.h"
#include <objbase.h>
#include "..\RemoteShellServer\RemoteShellserver_i.c"
#include <time.h>
#include "..\Common\Translate_Error.h"
#include "..\Common\MPICH_pwd.h"
#include "..\Common\GetOpt.h"

// Global variables
long g_nNproc = 1;
VARIANT g_vHosts, g_vSMPInfo;
TCHAR g_pszAccount[100] = _T(""), g_pszPassword[100] = _T("");
TCHAR g_pszExe[MAX_PATH] = _T(""), g_pszArgs[MAX_PATH] = _T("");
TCHAR g_pszFirstHost[100] = _T("");
IRemoteShell *g_pLaunch = NULL;
HANDLE g_hFinishedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

// Function name	: PrintError
// Description	    : 
// Return type		: void 
// Argument         : HRESULT hr
void PrintError(HRESULT hr)
{
	HLOCAL str;
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		0,
		hr,
		MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
		(LPTSTR) &str,
		0,0);
	
	_tprintf(TEXT("error: %s\n"), str);
	LocalFree(str);
}

// Function name	: Connect
// Description	    : 
// Return type		: bool 
// Argument         : LPTSTR host
// Argument         : IRemoteShell *&pLaunch
bool Connect(LPTSTR host, IRemoteShell *&pLaunch)
{
	HRESULT hr;
	MULTI_QI qi;
	WCHAR wHost[100], localhost[100];
	COSERVERINFO server;
	DWORD length = 100;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

	if (FAILED(hr))
	{
		_tprintf(TEXT("CoInitialize() failed.\n"));
		PrintError(hr);
		return false;
	}

	hr = CoInitializeSecurity(NULL, -1, NULL, NULL,
		RPC_C_AUTHN_LEVEL_CONNECT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
		//RPC_C_AUTHN_LEVEL_PKT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
	if (FAILED(hr))
	{
		if (hr == RPC_E_TOO_LATE)
			printf("CoInitializeSecurity failed in Connect(RemoteShell) because it has already been set.\n");
		else
		{
			char error_msg[256];
			Translate_HRError(hr, error_msg);
			printf("CoInitializeSecurity failed in Connect(RemoteShell)\nError: %s", error_msg);
		}
	}

	qi.pIID = &IID_IRemoteShell;
	qi.pItf = NULL;

#ifdef UNICODE
	wcscpy(wHost, host);
#else
	//MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, host, -1, wHost, 100);
	mbstowcs(wHost, host, strlen(host)+1);
#endif

	server.dwReserved1 = 0;
	server.dwReserved2 = 0;
	server.pAuthInfo = NULL;
	server.pwszName = wHost;

	GetComputerNameW(localhost, &length);

	if (_wcsicmp(localhost, wHost) == 0)
	{
		hr = CoCreateInstanceEx(CLSID_RemoteShell, NULL, CLSCTX_SERVER, &server, 1, &qi);
		if (FAILED(hr))
		{
			_tprintf(TEXT("Unable to connect to %s: "), host);
			PrintError(hr);
			return false;
		}
	}
	else
	{
		hr = CoCreateInstanceEx(CLSID_RemoteShell, NULL, CLSCTX_REMOTE_SERVER, &server, 1, &qi);
		if (FAILED(hr))
		{
			_tprintf(TEXT("Unable to connect to %s: "), host);
			PrintError(hr);
			return false;
		}
	}

	if (SUCCEEDED(qi.hr))
	{
		pLaunch = (IRemoteShell*)qi.pItf;
		return true;
	}

	return false;
}

// Function name	: PrintOptions
// Description	    : 
// Return type		: void 
void PrintOptions()
{
	_tprintf(TEXT("Usage:\n"));
	_tprintf(TEXT("   msh host\n"));
	_tprintf(TEXT("   msh -logon host\n"));
}

// Function name	: CtrlHandlerRoutine
// Description	    : 
// Return type		: BOOL WINAPI 
// Argument         : DWORD dwCtrlType
//bool g_bFirst = true;
BOOL WINAPI CtrlHandlerRoutine(DWORD dwCtrlType)
{
	fprintf(stderr, "User break\n");
	if (g_pLaunch != NULL)
	{
		BSTR berror_msg = SysAllocString(L"");
		LONG error;
		HRESULT hr;
		/*
		if (g_bFirst)
		{
			fprintf(stderr, "Forwarding break signal\n");
			g_pLaunch->SendBreak(&error, &berror_msg);
			SysFreeString(berror_msg);
			g_bFirst = false;
		}
		else
		//*/
		{
			error = 0;
			hr = g_pLaunch->Abort(&error, &berror_msg);
			if (FAILED(hr))
			{
				printf("Abort failed.\n");
				PrintError(hr);
			}
			if (error)
				wprintf(L"Abort failed: %s", berror_msg);
			SysFreeString(berror_msg);
			g_pLaunch->Release();
			ExitProcess(1);
		}
	}
	return TRUE;
}

struct RedirectInputThreadArg
{
	HANDLE hEvent;
	IStream **ppStream;
};

char g_pBuffer[1024];
HANDLE g_hBufferEvent1 = CreateEvent(NULL, TRUE, FALSE, NULL);
HANDLE g_hBufferEvent2 = CreateEvent(NULL, TRUE, FALSE, NULL);
DWORD g_num_read = 0;

// Function name	: ReadStdinThread
// Description	    : 
// Return type		: void 
void ReadStdinThread()
{
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	while (true)
	{
		if (!ReadFile(hStdin, g_pBuffer, 1024, &g_num_read, NULL))
			return;
		if (g_num_read == 0)
			return;
		ResetEvent(g_hBufferEvent2);
		SetEvent(g_hBufferEvent1);
		WaitForSingleObject(g_hBufferEvent2, INFINITE);
	}
}

// Function name	: RedirectInputThread
// Description	    : 
// Return type		: void 
// Argument         : RedirectInputThreadArg *arg
void RedirectInputThread(RedirectInputThreadArg *arg)
{
	IRemoteShell *pLaunch=NULL;
	HRESULT hr=S_OK;
	HANDLE hObject[2];
	long error=0;
	BSTR berror_msg;
	DWORD ret_val;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

	berror_msg = SysAllocString(L"");
	CoGetInterfaceAndReleaseStream (*arg->ppStream, IID_IRemoteShell, (void**) &pLaunch);
	delete arg->ppStream;

	DWORD dwThreadID;
	HANDLE hRSIThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ReadStdinThread, NULL, 0, &dwThreadID);

	hObject[0] = arg->hEvent;
	hObject[1] = g_hBufferEvent1;

	berror_msg = SysAllocString(L"");
	while (true)
	{
		ret_val = WaitForMultipleObjects(2, hObject, FALSE, INFINITE);
		if (ret_val == WAIT_OBJECT_0+1)
		{
			if (g_num_read > 0)
			{
				SAFEARRAYBOUND bound;
				VARIANT vInput;
				void *pBuf;

				VariantInit(&vInput);
				bound.lLbound = 0;
				bound.cElements = g_num_read;
				vInput.vt = VT_UI1 | VT_ARRAY;
				vInput.parray = SafeArrayCreate(VT_UI1, 1, &bound);

				SafeArrayAccessData(vInput.parray, &pBuf);
				memcpy(pBuf, g_pBuffer, g_num_read);
				SafeArrayUnaccessData(vInput.parray);

				error = 0;
				hr = pLaunch->PutProcessInput(vInput, &error, &berror_msg);
				if (FAILED(hr))
				{
					VariantClear(&vInput);
					printf("PutProcessInput failed: %d\n", hr);
					PrintError(hr);
					break;
				}
				if (error)
				{
					VariantClear(&vInput);
					if (wcslen(berror_msg) < 1)
						wprintf(L"PutProcessInput failed: %d %s\n", error, berror_msg);
					else
						wprintf(L"PutProcessInput failed: %s\n", berror_msg);
					break;
				}
				VariantClear(&vInput);
			}
			ResetEvent(g_hBufferEvent1);
			SetEvent(g_hBufferEvent2);
		}
		else
		{
			//printf("g_hFinishedEvent signalled\n");
			break;
		}
	}

	pLaunch->Release();
	CoUninitialize();
}

// Function name	: GetAccountAndPassword
// Description	    : Attempts to read the password from the registry, 
//	                  upon failure it requests the user to provide one
// Return type		: void 
void GetAccountAndPassword()
{
	if (!ReadPasswordFromRegistry(g_pszAccount, g_pszPassword))
	{
		TCHAR ch=0;
		int index = 0;
		
		do
		{
			_ftprintf(stderr, TEXT("account: "));
			fflush(stderr);
			_getts(g_pszAccount);
		} while (_tcslen(g_pszAccount) == 0);
		
		_ftprintf(stderr, TEXT("password: "));
		fflush(stderr);

		HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
		DWORD dwMode;
		if (!GetConsoleMode(hStdin, &dwMode))
			dwMode = ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT;
		SetConsoleMode(hStdin, dwMode & ~ENABLE_ECHO_INPUT);
		_getts(g_pszPassword);
		SetConsoleMode(hStdin, dwMode);
		
		_ftprintf(stderr, TEXT("\n"));
	}
}

// Function name	: main
// Description	    : 
// Return type		: void 
// Argument         : int argc
// Argument         : TCHAR *argv[]
void main(int argc, TCHAR *argv[])
{
	TCHAR pszDir[MAX_PATH] = TEXT("."), pszEnv[1024] = TEXT("");
	SetConsoleCtrlHandler(CtrlHandlerRoutine, TRUE);

	if (argc == 1)
	{
		PrintOptions();
		return;
	}

	GetOpt(argc, argv, _T("-account"), g_pszAccount);
	GetOpt(argc, argv, _T("-password"), g_pszPassword);
	GetOpt(argc, argv, _T("-env"), pszEnv);
	if (GetOpt(argc, argv, _T("-logon")))
		GetAccountAndPassword();
	if (!GetOpt(argc, argv, "-dir", pszDir))
		GetCurrentDirectory(MAX_PATH, pszDir);

	_tcscpy(g_pszFirstHost, argv[1]);

	if (argc == 2)
		_tcscpy(g_pszExe, TEXT("cmd.exe /Q"));
	else
	{
		_tcscpy(g_pszExe, TEXT("cmd.exe /c "));
		for (int i=2; i<argc; i++)
		{
			_tcscat(g_pszExe, argv[i]);
			if (i < argc-1)
				_tcscat(g_pszExe, TEXT(" "));
		}
	}

	LONG error = 0;
	BSTR berror_msg;
	BSTR bCommonArgs, bExe, bDir, bEnv;
	HRESULT hr = S_OK;

	berror_msg = SysAllocString(L"");
#ifdef UNICODE
	bExe = SysAllocString(g_pszExe);
	bCommonArgs = SysAllocString(g_pszArgs);
	bDir = SysAllocString(pszDir);
	bEnv = SysAllocString(pszEnv);
#else
	WCHAR wTemp[1024];
	mbstowcs(wTemp, g_pszExe, strlen(g_pszExe)+1);
	bExe = SysAllocString(wTemp);
	mbstowcs(wTemp, g_pszArgs, strlen(g_pszArgs)+1);
	bCommonArgs = SysAllocString(wTemp);
	mbstowcs(wTemp, pszDir, strlen(pszDir)+1);
	bDir = SysAllocString(wTemp);
	mbstowcs(wTemp, pszEnv, strlen(pszEnv)+1);
	bEnv = SysAllocString(wTemp);
#endif

	if (Connect(g_pszFirstHost, g_pLaunch))
	{
		error = 0;
		long pid;

		if (_tcslen(g_pszAccount))
		{
			BSTR bAccount, bPassword;
			WCHAR wAccount[100], wPassword[100];
#ifdef UNICODE
			bAccount = SysAllocString(g_pszAccount);
			bPassword = SysAllocString(g_pszPassword);
#else
			mbstowcs(wAccount, g_pszAccount, strlen(g_pszAccount)+1);
			mbstowcs(wPassword, g_pszPassword, strlen(g_pszPassword)+1);
			bAccount = SysAllocString(wAccount);
			bPassword = SysAllocString(wPassword);
#endif
			hr = g_pLaunch->GrantAccessToDesktop(bAccount, bPassword, &error, &berror_msg);
			hr = g_pLaunch->LaunchProcess(bExe, bEnv, bDir, bAccount, bPassword, &pid, &error, &berror_msg);
			SysFreeString(bAccount);
			SysFreeString(bPassword);
		}
		else
		{
			BSTR bNull;
			bNull = SysAllocString(L"");
			hr = g_pLaunch->GrantAccessToDesktop(bNull, bNull, &error, &berror_msg);
			hr = g_pLaunch->LaunchProcess(bExe, bEnv, bDir, bNull, bNull, &pid, &error, &berror_msg);
			SysFreeString(bNull);
		}
		
		if (SUCCEEDED(hr))
		{
			if (error)
			{
				if (wcslen(berror_msg) < 1)
					wprintf(L"LaunchProcess failed: Error(%d), %s", error, berror_msg);
				else
					wprintf(L"%s", berror_msg);
				g_pLaunch->Release();
				CoUninitialize();
				SysFreeString(berror_msg);
				SysFreeString(bExe);
				SysFreeString(bCommonArgs);
				SysFreeString(bDir);
				return;
			}
			
			error = 0;
			
			RedirectInputThreadArg *pArg = new RedirectInputThreadArg;
			pArg->hEvent = g_hFinishedEvent;
			pArg->ppStream = new IStream*;
			// Marshall pLaunch to a thread which redirects user input
			hr = CoMarshalInterThreadInterfaceInStream(IID_IRemoteShell, g_pLaunch, pArg->ppStream);
			if (FAILED(hr))
			{
				g_pLaunch->Abort(&error, &berror_msg);
				g_pLaunch->Release();
				SysFreeString(berror_msg);
				printf("CoMarshalInterThreadInterfaceInStream failed.\n");
				CoUninitialize();
				delete pArg;
				return;
			}
			
			DWORD dwThreadID;
			HANDLE hRedirectInputThread = CreateThread(
				NULL, 0, 
				(LPTHREAD_START_ROUTINE)RedirectInputThread,
				pArg, 
				0, &dwThreadID);
			
			if (hRedirectInputThread == NULL)
			{
				error = GetLastError();
				printf("CreateThread failed: error %d\n", error);
				g_pLaunch->Abort(&error, &berror_msg);
				g_pLaunch->Release();
				SysFreeString(berror_msg);
				CoUninitialize();
				delete pArg;
				return;
			}
			
			HANDLE hStdout, hStderr;
			hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
			hStderr = GetStdHandle(STD_ERROR_HANDLE);
			if (hStdout == INVALID_HANDLE_VALUE || hStderr == INVALID_HANDLE_VALUE)
			{
				error = GetLastError();
				_tprintf(TEXT("GetStdHandle failed: Error %d\n"), error);
				g_pLaunch->Abort(&error, &berror_msg);
				g_pLaunch->Release();
				CoUninitialize();
				SysFreeString(berror_msg);
				SysFreeString(bExe);
				SysFreeString(bCommonArgs);
				SysFreeString(bDir);
				return;
			}
			
			long more = 1, from = 0, nState = 0;
			VARIANT *v = new VARIANT;
			DWORD num_written, num_elements;
			void *pBuf;
			
			v->vt = VT_UI1 | VT_ARRAY;
			SAFEARRAYBOUND bound;
			bound.lLbound = 0;
			bound.cElements = 0;
			v->parray = SafeArrayCreate(VT_UI1, 1, &bound);
			
			while (more)
			{
				error = 0;
				hr = g_pLaunch->GetProcessOutput(v, &nState, &error, &berror_msg);
				if (FAILED(hr))
				{
					_tprintf(TEXT("DCOM failure: GetProcessOutput()\n"));
					PrintError(hr);
					SysFreeString(berror_msg);
					SysFreeString(bExe);
					SysFreeString(bCommonArgs);
					SysFreeString(bDir);
					VariantClear(v);
					delete v;
					return;
				}
				if (error)
				{
					if (wcslen(berror_msg) < 1)
						wprintf(L"Error %d: %s", error, berror_msg);
					else
						wprintf(L"%s", berror_msg);
					SysFreeString(berror_msg);
					SysFreeString(bExe);
					SysFreeString(bCommonArgs);
					SysFreeString(bDir);
					VariantClear(v);
					delete v;
					return;
				}

				more = nState & RSH_OUTPUT_MORE;

				if (v->parray != NULL)
				{
					num_elements = v->parray->rgsabound->cElements;
					if (num_elements > 0)
					{
						SafeArrayAccessData(v->parray, &pBuf);
						if (nState & RSH_OUTPUT_STDOUT)
							WriteFile(hStdout, pBuf, num_elements, &num_written, NULL);
						else
							WriteFile(hStderr, pBuf, num_elements, &num_written, NULL);
						FlushFileBuffers(hStdout);
						FlushFileBuffers(hStderr);
						SafeArrayUnaccessData(v->parray);
						// Destroy the array.
						SafeArrayDestroy(v->parray);
						// Create a new and empty array.
						v->parray = SafeArrayCreate(VT_UI1, 1, &bound);
					}
					else
					{
						SafeArrayDestroy(v->parray);
						v->parray = SafeArrayCreate(VT_UI1, 1, &bound);
					}
				}
				else
				{
					v->parray = SafeArrayCreate(VT_UI1, 1, &bound);
				}
			}
			
			SafeArrayDestroy(v->parray);
			delete v;
			
			// Signal the redirect input thread to terminate
			SetEvent(g_hFinishedEvent);
			WaitForSingleObject(hRedirectInputThread, 1000);
			CloseHandle(hRedirectInputThread);
		}
		else
		{
			_tprintf(TEXT("LaunchProcess failed: "));
			PrintError(hr);
			fflush(stdout);
		}
			
		//g_pLaunch->Abort(&error, &berror_msg);
		g_pLaunch->Release();
		
		CoUninitialize();
	}
	else
	{
		_tprintf(TEXT("Unable to connect to '%s'\n"), g_pszFirstHost);
	}
	
	SysFreeString(berror_msg);
	SysFreeString(bExe);
	SysFreeString(bCommonArgs);
	SysFreeString(bDir);
}
