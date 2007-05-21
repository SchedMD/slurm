#include "localonly.h"
#include "..\Common\Translate_Error.h"
#include <stdio.h>
#include <time.h>
#include "WaitThread.h"

// Function name	: SetEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : LPTSTR pEnv
void SetEnvironmentVariables(LPTSTR pEnv)
{
	TCHAR name[MAX_PATH]=_T(""), value[MAX_PATH]=_T("");
	TCHAR *pChar;

	pChar = name;
	while (*pEnv != _T('\0'))
	{
		if (*pEnv == _T('='))
		{
			*pChar = _T('\0');
			pChar = value;
		}
		else
		if (*pEnv == _T('|'))
		{
			*pChar = _T('\0');
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
	*pChar = _T('\0');
	SetEnvironmentVariable(name, value);
}

// Function name	: RemoveEnvironmentVariables
// Description	    : 
// Return type		: void 
// Argument         : LPTSTR pEnv
void RemoveEnvironmentVariables(LPTSTR pEnv)
{
	TCHAR name[MAX_PATH]=_T(""), value[MAX_PATH]=_T("");
	TCHAR *pChar;

	pChar = name;
	while (*pEnv != _T('\0'))
	{
		if (*pEnv == _T('='))
		{
			*pChar = _T('\0');
			pChar = value;
		}
		else
		if (*pEnv == _T('|'))
		{
			*pChar = _T('\0');
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
	*pChar = _T('\0');
	SetEnvironmentVariable(name, NULL);
}

// Function name	: RunLocal
// Description	    : 
// Return type		: void 
// Argument         : bool bDoSMP
void RunLocal(bool bDoSMP)
{
	DWORD size = 100;
	TCHAR pszHost[100], pszCmdLine[MAX_PATH], error_msg[256], pszEnv[256], pszExtra[MAX_PATH];
	STARTUPINFO saInfo;
	PROCESS_INFORMATION psInfo;
	LPTSTR pEnv;
	int rootPort=0;
	HANDLE *hProcess = new HANDLE[g_nHosts];
			
	GetComputerName(pszHost, &size);

	wsprintf(pszCmdLine, TEXT("%s %s"), g_pszExe, g_pszArgs);

	GetTempFileName(_T("."), _T("mpi"), 0, pszExtra);
	// This produces a name in the form: ".\XXXmpi.tmp"
	// \ is illegal in named objects so use &pszExtra[2] instead of pszExtra for the JobID
	if (bDoSMP)
	{
		wsprintf(pszEnv, _T("MPICH_JOBID=%s|MPICH_IPROC=0|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_EXTRA=%s|MPICH_COMNIC=%s|MPICH_SHM_LOW=0|MPICH_SHM_HIGH=%d"),
				&pszExtra[2], g_nHosts, pszHost, -1, pszExtra, pszHost, g_nHosts-1);
	}
	else
	{
		wsprintf(pszEnv, _T("MPICH_JOBID=%s|MPICH_IPROC=0|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_EXTRA=%s|MPICH_COMNIC=%s"),
				&pszExtra[2], g_nHosts, pszHost, -1, pszExtra, pszHost);
	}

	SetEnvironmentVariables(pszEnv);
	if (_tcslen(g_pszEnv) > 0)
		SetEnvironmentVariables(g_pszEnv);
	pEnv = GetEnvironmentStrings();

	GetStartupInfo(&saInfo);

	// launch first process
	if (CreateProcess(
		NULL,
		pszCmdLine,
		NULL, NULL, FALSE,
		IDLE_PRIORITY_CLASS, 
		pEnv,
		NULL,
		&saInfo, &psInfo))
	{
		hProcess[0] = psInfo.hProcess;
		CloseHandle(psInfo.hThread);
	}
	else
	{
		int error = GetLastError();
		Translate_Error(error, error_msg, TEXT("CreateProcess failed: "));
		_tprintf(TEXT("Unable to launch '%s', error %d: %s"), pszCmdLine, error, error_msg);
		return;
	}

	RemoveEnvironmentVariables(pszEnv);
	FreeEnvironmentStrings(pEnv);

	if (g_bNoMPI)
	{
		rootPort = -1;
	}
	else
	{
		// Open the file and read the port number written by the first process
		HANDLE hFile = CreateFile(pszExtra, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			Translate_Error(GetLastError(), error_msg, TEXT("CreateFile failed "));
			_tprintf(error_msg);
			return;
		}
		
		DWORD num_read = 0;
		TCHAR pBuffer[100];
		pBuffer[0] = '\0';
		TCHAR *pChar = pBuffer;
		clock_t cStart = clock();
		while (true)
		{
			num_read = 0;
			if (!ReadFile(hFile, pChar, 100, &num_read, NULL))
			{
				Translate_Error(GetLastError(), error_msg, TEXT("ReadFile failed "));
				_tprintf(error_msg);
				return;
			}
			if (num_read == 0)
			{
				if (clock() - cStart > 10 * CLOCKS_PER_SEC)
				{
					_tprintf(TEXT("Wait for process 0 to write port to temporary file timed out\n"));
					TerminateProcess(hProcess, 0);
					return;
				}
				Sleep(100);
			}
			else
			{
				for (int i=0; i<(int)num_read; i++)
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
		rootPort = _ttoi(pBuffer);
	}
	DeleteFile(pszExtra);

	// launch all the rest of the processes
	for (int i=1; i<g_nHosts; i++)
	{
		if (bDoSMP)
		{
			wsprintf(pszEnv, _T("MPICH_JOBID=%s|MPICH_IPROC=%d|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_COMNIC=%s|MPICH_SHM_LOW=0|MPICH_SHM_HIGH=%d"),
				&pszExtra[2], i, g_nHosts, pszHost, rootPort, pszHost, g_nHosts-1);
		}
		else
		{
			wsprintf(pszEnv, _T("MPICH_JOBID=%s|MPICH_IPROC=%d|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d|MPICH_COMNIC=%s"),
				&pszExtra[2], i, g_nHosts, pszHost, rootPort, pszHost);
		}
		
		SetEnvironmentVariables(pszEnv);
		pEnv = GetEnvironmentStrings();
		
		if (CreateProcess(
			NULL,
			pszCmdLine,
			NULL, NULL, FALSE,
			IDLE_PRIORITY_CLASS, 
			pEnv,
			NULL,
			&saInfo, &psInfo))
		{
			hProcess[i] = psInfo.hProcess;
			CloseHandle(psInfo.hThread);
		}
		else
		{
			int error = GetLastError();
			Translate_Error(error, error_msg, TEXT("CreateProcess failed: "));
			_tprintf(TEXT("Unable to launch '%s', error %d: %s"), pszCmdLine, error, error_msg);
			return;
		}
		
		RemoveEnvironmentVariables(pszEnv);
		FreeEnvironmentStrings(pEnv);
	}

	// Wait for all the processes to terminate
	WaitForLotsOfObjects(g_nHosts, hProcess);

	for (i=0; i<g_nHosts; i++)
		CloseHandle(hProcess[i]);
	delete hProcess;
}
