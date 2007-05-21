#include "LaunchProcess.h"
#include <stdio.h>
#include "global.h"
#include "RedirectInput.h"
#include "..\Common\MPIJobDefs.h"
#include "..\Common\Translate_Error.h"

// Function name	: Connect
// Description	    : 
// Return type		: bool 
// Argument         : LPWSTR host
// Argument         : IRemoteShell *&pLaunch
bool Connect(LPWSTR host, IRemoteShell *&pLaunch)
{
	HRESULT hr;
	MULTI_QI qi;
	WCHAR localhost[100];
	COSERVERINFO server;
	DWORD length = 100;

	qi.pIID = &IID_IRemoteShell;
	qi.pItf = NULL;

	server.dwReserved1 = 0;
	server.dwReserved2 = 0;
	server.pAuthInfo = NULL;
	server.pwszName = host;

	GetComputerNameW(localhost, &length);

	if (_wcsicmp(localhost, host) == 0)
	{
		hr = CoCreateInstanceEx(CLSID_RemoteShell, NULL, CLSCTX_SERVER, &server, 1, &qi);
		if (FAILED(hr))
		{
			wprintf(L"Unable to connect to %s: ", host);
			PrintError(hr);
			return false;
		}
	}
	else
	{
		hr = CoCreateInstanceEx(CLSID_RemoteShell, NULL, CLSCTX_REMOTE_SERVER, &server, 1, &qi);
		if (FAILED(hr))
		{
			wprintf(L"Unable to connect to %s: ", host);
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

// Function name	: AbortThread
// Description	    : 
// Return type		: void 
// Argument         : IStream *pStream
void AbortThread(IStream *pStream)
{
	IRemoteShell *pLaunch=NULL;
	HRESULT hr=S_OK;
	long error=0;
	BSTR berror_msg;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

	CoGetInterfaceAndReleaseStream (pStream, IID_IRemoteShell, (void**) &pLaunch);

	WaitForSingleObject(g_hAbortEvent, INFINITE);

	if (g_bNormalExit)
	{
		pLaunch->Release();
		CoUninitialize();
		return;
	}
	
	error = 0;
	berror_msg = SysAllocString(L"");
	hr = pLaunch->Abort(&error, &berror_msg);
	if (FAILED(hr))
	{
		printf("Abort failed\n");
		PrintError(hr);
	}
	if (error)
	{
		if (wcslen(berror_msg) < 1)
			wprintf(L"Abort failed: %d\n", error);
		else
			wprintf(L"Abort failed: %s\n", berror_msg);
	}

	pLaunch->Release();
	CoUninitialize();
}

// Function name	: LaunchProcess
// Description	    : 
// Return type		: void 
// Argument         : LaunchProcessArg *arg
void LaunchProcess(LaunchProcessArg *arg)
{
	DWORD length = 100;
	RedirectInputThreadArg *rarg = NULL;
	HANDLE hRIThread = NULL;
	IRemoteShell *pLaunch;
	long error, pid;
	BSTR berror_msg;
	BSTR bFilename;
	BSTR bCmdLine, bEnv, bDir;
	HRESULT hr;
	DWORD dwThreadID;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		printf("CoInitializeEx failed: ");
		PrintError(hr);
		delete arg;
		return;
	}

	// Connect to RemoteShell
	if (Connect(arg->pszHost, pLaunch))
	{
		berror_msg = SysAllocString(L"");
		if (arg->i == 0)
		{
			/*
			WCHAR wFilename[MAX_PATH];
			//swprintf(wFilename, L"Global\\%s.ipcfile", arg->pszJobID);
			swprintf(wFilename, L"%s.ipcfile", arg->pszJobID);
			//wprintf(L"LaunchProcess creating mapping '%s'\n", wFilename);fflush(stdout);
			bFilename = SysAllocString(wFilename);
			hr = pLaunch->CreateFileMapping(bFilename, &error, &berror_msg);
			if (FAILED(hr))
			{
				wprintf(L"LaunchProcess:CreateFileMapping failed on %s\n", arg->pszHost);
				PrintError(hr);
				pLaunch->Release();
				delete arg;
				SysFreeString(bFilename);
				ExitProcess(1);
			}
			if (error)
			{
				pLaunch->Release();
				wprintf(L"LaunchProces:%s", berror_msg);
				delete arg;
				SysFreeString(bFilename);
				ExitProcess(1);
			}
			wcscat(arg->pszEnv, L"|MPICH_EXTRA=shm:");
			wcscat(arg->pszEnv, bFilename);
			SysFreeString(bFilename);
			/*/
			bFilename = SysAllocString(L"");
			error = 0;
			hr = pLaunch->CreateTempFile(&bFilename, &error, &berror_msg);
			if (FAILED(hr))
			{
				wprintf(L"LaunchProcess:CreateTempFile failed on %s\n", arg->pszHost);
				PrintError(hr);
				pLaunch->Release();
				delete arg;
				SysFreeString(bFilename);
				ExitProcess(1);
			}
			if (error)
			{
				pLaunch->Release();
				wprintf(L"LaunchProces:CreateTempFile failed on %s: %s\n", arg->pszHost, berror_msg);
				delete arg;
				SysFreeString(bFilename);
				ExitProcess(1);
			}
			wcscat(arg->pszEnv, L"|MPICH_EXTRA=");
			wcscat(arg->pszEnv, bFilename);
			//*/
		}

		bCmdLine = SysAllocString(arg->pszCmdLine);
		bDir = SysAllocString(arg->pszDir);
		bEnv = SysAllocString(arg->pszEnv);
		berror_msg = SysAllocString(L"");

		//wprintf(L"Launching:\n exe: %s\n dir: %s\n env: %s\n", bCmdLine, bDir, bEnv);
		// LaunchProcess
		error = 0;
		BSTR bNull = SysAllocString(L"");
		if (arg->i == 0)
		{
			BSTR bAccount, bPassword;
			bAccount = SysAllocString(arg->pszAccount);
			bPassword = SysAllocString(arg->pszPassword);
			if (arg->bLogon)
				hr = pLaunch->GrantAccessToDesktop(bAccount, bPassword, &error, &berror_msg);
			else
				hr = pLaunch->GrantAccessToDesktop(bNull, bNull, &error, &berror_msg);
			SysFreeString(bAccount);
			SysFreeString(bPassword);
		}
		if (arg->bLogon)
		{
			BSTR bAccount, bPassword;
			bAccount = SysAllocString(arg->pszAccount);
			bPassword = SysAllocString(arg->pszPassword);
			//if (arg->i == 0) pLaunch->GrantAccessToDesktop(bAccount, bPassword, &error, &berror_msg);
			//printf("LaunchInteractiveMPIProcess\n");fflush(stdout);
			hr = pLaunch->LaunchProcess(bCmdLine, bEnv, bDir, bAccount, bPassword, &pid, &error, &berror_msg);
			SysFreeString(bAccount);
			SysFreeString(bPassword);
		}
		else
			hr = pLaunch->LaunchProcess(bCmdLine, bEnv, bDir, bNull, bNull, &pid, &error, &berror_msg);
		SysFreeString(bCmdLine);
		SysFreeString(bDir);
		SysFreeString(bEnv);
		SysFreeString(bNull);

		if (FAILED(hr))
		{
			printf("LaunchProcess failed on %s.\n", arg->pszHost);
			PrintError(hr);
			pLaunch->Release();
			SysFreeString(berror_msg);
			CoUninitialize();
			if (arg->i == 0)
			{
				SysFreeString(bFilename);
				ExitProcess(1);
			}
			delete arg;
			return;
		}
		if (error)
		{
			if (wcslen(berror_msg) < 1)
				wprintf(L"LaunchProcessThread:LaunchProcess on %s failed: Error %d\n", arg->pszHost, error);
			else
				wprintf(L"LaunchProcessThread:LaunchProcess on %s failed: %s\n", arg->pszHost, berror_msg);
			wprintf(L"Unable to launch %s\n", arg->pszCmdLine);
			pLaunch->Release();
			CoUninitialize();
			SysFreeString(berror_msg);
			if (arg->i == 0)
			{
				SysFreeString(bFilename);
				ExitProcess(1);
			}
			delete arg;
			return;
		}
		
		// Get the port number and redirect input to the first process
		if (arg->i == 0)
		{
			error = 0;
			/*
			hr = pLaunch->GetPortFromMapping(&g_nRootPort, &error, &berror_msg);
			if (FAILED(hr))
			{
				pLaunch->Release();
				printf("LaunchProcess:GetPortFromMapping failed\n");
				delete arg;
				ExitProcess(1);
			}
			if (error)
			{
				pLaunch->Release();
				wprintf(L"LaunchProcess:%s", berror_msg);
				delete arg;
				ExitProcess(1);
			}
			/*/
			hr = pLaunch->GetPortFromFile(bFilename, &g_nRootPort, &error, &berror_msg);
			SysFreeString(bFilename);
			if (FAILED(hr))
			{
				pLaunch->Release();
				printf("LaunchProcess:GetPortFromFile failed\n");
				delete arg;
				ExitProcess(1);
			}
			if (error)
			{
				pLaunch->Release();
				if (error == ERROR_WAIT_NO_CHILDREN)
				{
					wprintf(L"LaunchProcess, %s, failed on %s because the executable did not load.\nThis can happen when a dll needed by the executable is not found on the machine or in the path.\n",
						arg->pszCmdLine, arg->pszHost);
				}
				else
					wprintf(L"LaunchProcess failed on %s:%s", arg->pszHost, berror_msg);
				delete arg;
				ExitProcess(1);
			}
			//*/

			rarg = new RedirectInputThreadArg;
			rarg->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

			rarg->ppStream = new IStream*;
			// Marshall pLaunch to a thread which redirects user input
			hr = CoMarshalInterThreadInterfaceInStream(IID_IRemoteShell, pLaunch, rarg->ppStream);
			if (FAILED(hr))
			{
				pLaunch->Release();
				SysFreeString(berror_msg);
				printf("LaunchProcess:CoMarshalInterThreadInterfaceInStream failed.\n");
				PrintError(hr);
				CoUninitialize();
				delete rarg;
				delete arg;
				ExitProcess(1);
			}
			
			hRIThread = CreateThread(
				NULL, 0, 
				(LPTHREAD_START_ROUTINE)RedirectInputThread,
				rarg, 
				0, &dwThreadID);
			
			if (hRIThread == NULL)
			{
				error = GetLastError();
				printf("CreateThread failed: error %d\n", error);
				PrintError(error);
				pLaunch->Release();
				SysFreeString(berror_msg);
				CoUninitialize();
				delete rarg;
				delete arg;
				ExitProcess(1);
			}
		}
		
		IStream *pStream;
		HANDLE hAbortThread = NULL;
		// Marshall pLaunch to a thread which waits for an abort event
		hr = CoMarshalInterThreadInterfaceInStream(IID_IRemoteShell, pLaunch, &pStream);
		if (FAILED(hr))
		{
			pLaunch->Release();
			SysFreeString(berror_msg);
			printf("LaunchProcess:CoMarshalInterThreadInterfaceInStream failed.\n");
			PrintError(hr);
			CoUninitialize();
			delete rarg;
			delete arg;
			ExitProcess(1);
		}
		
		hAbortThread = CreateThread(
			NULL, 0, 
			(LPTHREAD_START_ROUTINE)AbortThread,
			pStream, 
			0, &dwThreadID);
		
		if (hAbortThread == NULL)
		{
			error = GetLastError();
			printf("CreateThread failed: error %d\n", error);
			PrintError(error);
			pLaunch->Release();
			SysFreeString(berror_msg);
			CoUninitialize();
			delete rarg;
			delete arg;
			ExitProcess(1);
		}
		//CloseHandle(hAbortThread);
		if (g_pAbortThreads != NULL)
		    g_pAbortThreads[arg->i] = hAbortThread;
		
		// Redirect output
		HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
		HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);

		if (hStdout == INVALID_HANDLE_VALUE || hStderr == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			_tprintf(TEXT("GetStdHandle failed: Error %d\n"), error);
			pLaunch->Release();
			CoUninitialize();
			SysFreeString(berror_msg);
			if (arg->i == 0) SetEvent(rarg->hEvent);
			delete arg;
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
			hr = pLaunch->GetProcessOutput(v, &nState, &error, &berror_msg);
			if (FAILED(hr))
			{
				_tprintf(TEXT("LaunchProcess:GetProcessOutput failed\n"));
				PrintError(hr);
				SysFreeString(berror_msg);
				VariantClear(v);
				delete v;
				if (arg->i == 0) SetEvent(rarg->hEvent);
				delete arg;
				return;
			}
			if (error)
			{
				if (wcslen(berror_msg) < 1)
					wprintf(L"LaunchProcess:GetProcessOutput:Error %d", error);
				else
					wprintf(L"LaunchProcess:%s", berror_msg);
				SysFreeString(berror_msg);
				VariantClear(v);
				delete v;
				if (arg->i == 0) SetEvent(rarg->hEvent);
				delete arg;
				return;
			}
			
			more = nState & RSH_OUTPUT_MORE;

			if (v->parray != NULL)
			{
				num_elements = v->parray->rgsabound->cElements;
				if (num_elements > 0)
				{
					SafeArrayAccessData(v->parray, &pBuf);
					WaitForSingleObject(g_hConsoleOutputMutex, 5000);
#ifdef MULTI_COLOR_OUTPUT
					if (nState & RSH_OUTPUT_STDOUT)
					{
						SetConsoleTextAttribute(hStdout, aConsoleColorAttribute[arg->i%NUM_OUTPUT_COLORS]);
						WriteFile(hStdout, pBuf, num_elements, &num_written, NULL);
						SetConsoleTextAttribute(hStdout, g_ConsoleAttribute);
					}
					else
					{
						SetConsoleTextAttribute(hStderr, aConsoleColorAttribute[arg->i%NUM_OUTPUT_COLORS]);
						WriteFile(hStderr, pBuf, num_elements, &num_written, NULL);
						SetConsoleTextAttribute(hStderr, g_ConsoleAttribute);
					}
#else
					if (nState & RSH_OUTPUT_STDOUT)
						WriteFile(hStdout, pBuf, num_elements, &num_written, NULL);
					else
						WriteFile(hStderr, pBuf, num_elements, &num_written, NULL);
#endif
					FlushFileBuffers(hStdout);
					FlushFileBuffers(hStderr);
					ReleaseMutex(g_hConsoleOutputMutex);
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

		// Stop the redirect input thread
		if (arg->i == 0)
		{
			SetEvent(rarg->hEvent);
			WaitForSingleObject(hRIThread, 5000);
			CloseHandle(hRIThread);
		}

		pLaunch->Release();

		CoUninitialize();
	}
	else
	{
		wprintf(L"Connect to %s failed\n", arg->pszHost);
		if (arg->i == 0)
			ExitProcess(1);
	}
		
	delete arg;
}

// Function name	: LaunchProcessWithMSH
// Description	    : 
// Return type		: void 
// Argument         : LaunchProcessArg *arg
void LaunchProcessWithMSH(LaunchProcessArg *arg)
{
	IRemoteShell *pLaunch;
	long error;
	BSTR berror_msg;
	BSTR bFilename;
	HRESULT hr;
	
	if (arg->i == 0)
	{
		hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
		if (FAILED(hr))
		{
			printf("CoInitializeEx failed: ");
			PrintError(hr);
			delete arg;
			return;
		}
		
		// Connect to RemoteShell
		if (!Connect(arg->pszHost, pLaunch))
		{
			wprintf(L"Connect to %s failed\n", arg->pszHost);
			ExitProcess(1);
		}
		berror_msg = SysAllocString(L"");
		bFilename = SysAllocString(L"");
		error = 0;
		hr = pLaunch->CreateTempFile(&bFilename, &error, &berror_msg);
		if (FAILED(hr))
		{
			wprintf(L"LaunchProcess:CreateTempFile failed on %s\n", arg->pszHost);
			PrintError(hr);
			pLaunch->Release();
			delete arg;
			SysFreeString(bFilename);
			ExitProcess(1);
		}
		if (error)
		{
			pLaunch->Release();
			wprintf(L"LaunchProces:CreateTempFile failed on %s: %s", arg->pszHost, berror_msg);
			delete arg;
			SysFreeString(bFilename);
			ExitProcess(1);
		}
		wcscat(arg->pszEnv, L"|MPICH_EXTRA=");
		wcscat(arg->pszEnv, bFilename);
	}
	
	WCHAR wCmdLine[1024];
	swprintf(wCmdLine, L"msh ");
	if (arg->bLogon)
	{
		wcscat(wCmdLine, L"-account \"");
		wcscat(wCmdLine, arg->pszAccount);
		wcscat(wCmdLine, L"\" -password \"");
		wcscat(wCmdLine, arg->pszPassword);
		wcscat(wCmdLine, L"\" ");
	}
	if (wcslen(arg->pszEnv))
	{
		wcscat(wCmdLine, L"-env \"");
		wcscat(wCmdLine, arg->pszEnv);
		wcscat(wCmdLine, L"\" ");
	}
	if (wcslen(arg->pszDir))
	{
		wcscat(wCmdLine, L"-dir \"");
		wcscat(wCmdLine, arg->pszDir);
		wcscat(wCmdLine, L"\" ");
	}
	wcscat(wCmdLine, arg->pszHost);
	wcscat(wCmdLine, L" ");
	wcscat(wCmdLine, arg->pszCmdLine);
	
	PROCESS_INFORMATION psInfo;
	STARTUPINFOW saInfo;
	memset(&saInfo, 0, sizeof(STARTUPINFO));
	saInfo.cb         = sizeof(STARTUPINFO);
	if (CreateProcessW(
		NULL,
		wCmdLine,
		NULL, NULL, TRUE,
		0,
		//DETACHED_PROCESS | IDLE_PRIORITY_CLASS, 
		//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS,
		//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP,
		//DETACHED_PROCESS | IDLE_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP,
		//CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS | CREATE_SUSPENDED, 
		NULL,
		NULL,
		&saInfo, &psInfo))
	{
		//wprintf(L"'%s'\n", wCmdLine);
		CloseHandle(psInfo.hThread);
	}
	else
		ExitProcess(1);
	
	if (arg->i == 0)
	{
		// Get the port number and redirect input to the first process
		error = 0;
		hr = pLaunch->GetPortFromFile(bFilename, &g_nRootPort, &error, &berror_msg);
		SysFreeString(bFilename);
		if (FAILED(hr))
		{
			pLaunch->Release();
			printf("LaunchProcess:GetPortFromFile failed\n");
			delete arg;
			ExitProcess(1);
		}
		if (error)
		{
			pLaunch->Release();
			wprintf(L"LaunchProcess:%s", berror_msg);
			delete arg;
			ExitProcess(1);
		}
		
		pLaunch->Release();
		
		CoUninitialize();
	}
	
	WaitForSingleObject(psInfo.hProcess, INFINITE);
	CloseHandle(psInfo.hProcess);
	
	delete arg;
}
