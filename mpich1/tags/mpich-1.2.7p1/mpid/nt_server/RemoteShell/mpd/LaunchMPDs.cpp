#include "LaunchMPDs.h"
#include <stdio.h>
#include "LeftThread.h"
#include "RightThread.h"
// RemoteShellServer must be compiled before the following files are generated
#include "..\RemoteShellServer\RemoteShellServer.h"
#include <objbase.h>
#include "..\RemoteShellServer\RemoteShellserver_i.c"
#include "..\Common\Translate_Error.h"
#include "..\Common\MPIJobDefs.h"
#include "global.h"

char g_pszAccount[100] = "";
char g_pszPassword[100] = "";

// Function name	: DCOMInit
// Description	    : 
// Return type		: void 
void DCOMInit()
{
	HRESULT hr;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

	hr = CoInitializeSecurity(NULL, -1, NULL, NULL,
		RPC_C_AUTHN_LEVEL_CONNECT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
		//RPC_C_AUTHN_LEVEL_PKT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
	if (FAILED(hr))
	{
		if (hr == RPC_E_TOO_LATE)
		{
			printf("CoInitializeSecurity failed in Connect(RemoteShell) because it has already been set.\n");
			fflush(stdout);
		}
		else
		{
			char error_msg[256];
			Translate_HRError(hr, error_msg);
			printf("CoInitializeSecurity failed in Connect(RemoteShell)\nError: %s", error_msg);
			fflush(stdout);
		}
	}

}

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
	
	printf("error: %s\n", str);
	fflush(stdout);
	LocalFree(str);
}

// Function name	: GetAccountAndPassword
// Description	    : 
// Return type		: void 
void GetAccountAndPassword()
{
	char ch=0;
	int index = 0;
	
	do
	{
		printf("account: ");
		fflush(stdout);
		gets(g_pszAccount);
	} while (strlen(g_pszAccount) == 0);
	
	printf("password: ");
	fflush(stdout);
	
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD dwMode;
	if (!GetConsoleMode(hStdin, &dwMode))
		dwMode = ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT;
	SetConsoleMode(hStdin, dwMode & ~ENABLE_ECHO_INPUT);
	gets(g_pszPassword);
	SetConsoleMode(hStdin, dwMode);
	
	printf("\n");fflush(stdout);
}

// Function name	: Connect
// Description	    : 
// Return type		: bool 
// Argument         : char *host
// Argument         : IRemoteShell *&pLaunch
bool Connect(char *host, IRemoteShell *&pLaunch)
{
	HRESULT hr;
	MULTI_QI qi;
	WCHAR wHost[100], localhost[100];
	COSERVERINFO server;
	DWORD length = 100;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

	if (FAILED(hr))
	{
		printf("CoInitialize() failed.\n");fflush(stdout);
		PrintError(hr);
		return false;
	}

	qi.pIID = &IID_IRemoteShell;
	qi.pItf = NULL;

	//MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, host, -1, wHost, 100);
	mbstowcs(wHost, host, strlen(host)+1);

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
			printf("Unable to connect to %s: ", host);fflush(stdout);
			PrintError(hr);
			return false;
		}
	}
	else
	{
		hr = CoCreateInstanceEx(CLSID_RemoteShell, NULL, CLSCTX_REMOTE_SERVER, &server, 1, &qi);
		if (FAILED(hr))
		{
			printf("Unable to connect to %s: ", host);fflush(stdout);
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

// Function name	: LaunchMPD
// Description	    : 
// Return type		: void 
// Argument         : LaunchMPDArg *pArg
void LaunchMPD(LaunchMPDArg *pArg)
{
	// connect to host
	// launch mpd
	// get output from mpd
	// write host and port to pArg from the mpd output
	// set ready event
	// wait for pArg->pRight ready event
	// put host and port from pArg->pRight out to the mpd

	// marshall pRemoteShell back to main thread
	// exit thread

	// OR

	// loop and eat the output of mpd
	// exit thread

	LONG error = 0;
	BSTR berror_msg;
	BSTR bExe, bDir, bEnv;
	HRESULT hr = S_OK;
	IRemoteShell *pLaunch = NULL;

	berror_msg = SysAllocString(L"");
	WCHAR wTemp[1024];
	if (pArg->timeout > 0)
	{
		if (pArg->pHostInfo->bPrimaryMPD)
			swprintf(wTemp, L"mpd.exe -spawns %d -nodbs -noconsole -timeout %d", pArg->pHostInfo->nSpawns, pArg->timeout);
		else
			swprintf(wTemp, L"mpd.exe -spawns %d -nodbs -noconsole -nopipe -timeout %d", pArg->pHostInfo->nSpawns, pArg->timeout);
	}
	else
	{
		if (pArg->pHostInfo->bPrimaryMPD)
			swprintf(wTemp, L"mpd.exe -spawns %d -nodbs -noconsole", pArg->pHostInfo->nSpawns);
		else
			swprintf(wTemp, L"mpd.exe -spawns %d -nodbs -noconsole -nopipe", pArg->pHostInfo->nSpawns);
	}
	bExe = SysAllocString(wTemp);
	bDir = SysAllocString(L".");
	bEnv = SysAllocString(L"");
	
	if (Connect(pArg->pHostInfo->pszHost, pLaunch))
	{
		error = 0;
		long pid;

		BSTR bAccount, bPassword;
		WCHAR wAccount[100], wPassword[100];
		mbstowcs(wAccount, g_pszAccount, strlen(g_pszAccount)+1);
		mbstowcs(wPassword, g_pszPassword, strlen(g_pszPassword)+1);
		bAccount = SysAllocString(wAccount);
		bPassword = SysAllocString(wPassword);
		hr = pLaunch->LaunchProcess(bExe, bEnv, bDir, bAccount, bPassword, &pid, &error, &berror_msg);
		SysFreeString(bAccount);
		SysFreeString(bPassword);
		
		if (SUCCEEDED(hr))
		{
			if (error)
			{
				if (wcslen(berror_msg) < 1)
				{
					wprintf(L"LaunchProcess failed: Error(%d), %s", error, berror_msg);
					fflush(stdout);
				}
				else
				{
					wprintf(L"%s", berror_msg);
					fflush(stdout);
				}
				pLaunch->Release();
				CoUninitialize();
				SysFreeString(berror_msg);
				SysFreeString(bExe);
				SysFreeString(bDir);
				return;
			}
			
			error = 0;
			
			long more = 1, from = 0, nState = 0;
			VARIANT *v = new VARIANT;
			DWORD num_elements;
			DWORD num_written;
			void *pBuf;
			char *pCurrent = pArg->pszHost;
			bool bInPortString = false, bFinished = false, bSkipNewlineCharacter = false;
			char pszPortString[100];
			HANDLE hStdout, hStderr;
			hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
			hStderr = GetStdHandle(STD_ERROR_HANDLE);
			
			v->vt = VT_UI1 | VT_ARRAY;
			SAFEARRAYBOUND bound;
			bound.lLbound = 0;
			bound.cElements = 0;
			v->parray = SafeArrayCreate(VT_UI1, 1, &bound);
			
			while (more)
			{
				error = 0;
				hr = pLaunch->GetProcessOutput(v, &nState, &error, &berror_msg);
				if (FAILED(hr))
				{
					printf("DCOM failure: GetProcessOutput()\n");fflush(stdout);
					PrintError(hr);
					SysFreeString(berror_msg);
					SysFreeString(bExe);
					SysFreeString(bDir);
					VariantClear(v);
					delete v;
					return;
				}
				if (error)
				{
					if (wcslen(berror_msg) < 1)
					{
						wprintf(L"Error %d: %s", error, berror_msg);
						fflush(stdout);
					}
					else
					{
						wprintf(L"%s", berror_msg);
						fflush(stdout);
					}
					SysFreeString(berror_msg);
					SysFreeString(bExe);
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
						/*
						printf("[%d]'", pArg->nPort);
						fwrite(pBuf, num_elements, 1, stdout);
						printf("'");fflush(stdout);
						//*/
						if (bFinished)
						{
							if (nState & RSH_OUTPUT_STDOUT)
								WriteFile(hStdout, pBuf, num_elements, &num_written, NULL);
							else
								WriteFile(hStderr, pBuf, num_elements, &num_written, NULL);
							FlushFileBuffers(hStdout);
							FlushFileBuffers(hStderr);
						}
						else
						//if (!bFinished)
						{
							for (unsigned int i=0; i<num_elements; i++)
							{
								if (*(char*)pBuf == '\r' || *(char*)pBuf == '\n')
								{
									if (bSkipNewlineCharacter)
									{
										bSkipNewlineCharacter = false;
									}
									else
									{
										if (bInPortString)
										{
											*pCurrent = '\0';
											if (pszPortString[0] == '\r' || pszPortString[0] == '\n')
												pArg->nPort = atoi(&pszPortString[1]);
											else
												pArg->nPort = atoi(pszPortString);
											//printf("%d = '%s'\n", pArg->nPort, pszPortString);
											SetEvent(pArg->hReadyEvent);
											bFinished = true;
											
											WaitForSingleObject(pArg->pRight->hReadyEvent, INFINITE);
											
											//////////////
											SAFEARRAYBOUND bound;
											VARIANT vInput;
											void *pBuf;
											char buffer[256];
											
											VariantInit(&vInput);
											bound.lLbound = 0;
											sprintf(buffer, "%s\n%d\n", pArg->pRight->pszHost, pArg->pRight->nPort);
											bound.cElements = strlen(buffer);
											vInput.vt = VT_UI1 | VT_ARRAY;
											vInput.parray = SafeArrayCreate(VT_UI1, 1, &bound);
											
											SafeArrayAccessData(vInput.parray, &pBuf);
											memcpy(pBuf, buffer, strlen(buffer));
											SafeArrayUnaccessData(vInput.parray);
											
											error = 0;
											hr = pLaunch->PutProcessInput(vInput, &error, &berror_msg);
											if (FAILED(hr))
											{
												VariantClear(&vInput);
												printf("PutProcessInput failed: %d\n", hr);fflush(stdout);
												PrintError(hr);
												break;
											}
											if (error)
											{
												VariantClear(&vInput);
												if (wcslen(berror_msg) < 1)
												{
													wprintf(L"PutProcessInput failed: %d %s\n", error, berror_msg);
													fflush(stdout);
												}
												else
												{
													wprintf(L"PutProcessInput failed: %s\n", berror_msg);
													fflush(stdout);
												}
												break;
											}
											VariantClear(&vInput);
											//////////////
											
											break; // jump out of the for loop
										}
										else
										{
											*pCurrent = '\0';
											bSkipNewlineCharacter = true;
											pCurrent = pszPortString;
											//printf("'%s'\n", pArg->pszHost);
											bInPortString = true;
										}
									}
								}
								else
								{
									bSkipNewlineCharacter = false;
									*pCurrent = *(char*)pBuf;
									pCurrent++;
								}
								pBuf = (char*)pBuf + 1;
							}
						}
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
		}
		else
		{
			printf("LaunchProcess failed: ");
			PrintError(hr);
			fflush(stdout);
		}
			
		//pLaunch->Abort(&error, &berror_msg);
		pLaunch->Release();
	
		CoUninitialize();
	}
	else
	{
		printf("Unable to connect to '%s'\n", pArg->pszHost);fflush(stdout);
	}

	SysFreeString(berror_msg);
	SysFreeString(bExe);
	SysFreeString(bDir);
}

// Function name	: LaunchMPDs
// Description	    : 
// Return type		: void 
// Argument         : HostNode *pHosts
// Argument         : HANDLE *phLeftThread
// Argument         : HANDLE *phRightThread
void LaunchMPDs(HostNode *pHosts, HANDLE *phLeftThread, HANDLE *phRightThread, int timeout)
{
	HANDLE hThread;
	LaunchMPDArg *pLaunchArg;
	LaunchMPDArg *pArg;

	if (pHosts == NULL)
	{
		printf("no hosts specified, exiting\n");
		ExitProcess(1);
		//printf("making a ring of one on the local machine\n");
		// ...
	}

	DCOMInit();

	GetAccountAndPassword();

	pLaunchArg = new LaunchMPDArg;
	pLaunchArg->hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	pLaunchArg->pHostInfo = NULL;

	pArg = new LaunchMPDArg;
	pArg->hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	pArg->timeout = timeout;
	pLaunchArg->pRight = pArg;

	DWORD dwThreadID;
	*phLeftThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LeftThread, pLaunchArg, 0, &dwThreadID);
	*phRightThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RightThread, pLaunchArg, 0, &dwThreadID);

	{
		unsigned long nLocalIP = -1;
		char localhost[100];
		gethostname(localhost, 100);
		NT_get_ip(localhost, &nLocalIP);
		HostNode *n = pHosts;
		while (n != NULL)
		{
			if (n->bPrimaryMPD)
			{
				unsigned long nIP;
				NT_get_ip(n->pszHost, &nIP);
				if (nLocalIP == nIP)
					n->bPrimaryMPD = false;
			}
			n = n->pNext;
		}
	}

	while (pHosts != NULL)
	{
		pArg->pHostInfo = pHosts;
		if (pHosts->pNext != NULL)
		{
			pArg->pRight = new LaunchMPDArg;
			pArg->pRight->hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		}
		else
			pArg->pRight = pLaunchArg;

		DWORD dwThreadID;
		hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LaunchMPD, pArg, 0, &dwThreadID);
		CloseHandle(hThread);

		pArg = pArg->pRight;
		pArg->timeout = timeout;
		pHosts = pHosts->pNext;
	}

	CoUninitialize();
}
