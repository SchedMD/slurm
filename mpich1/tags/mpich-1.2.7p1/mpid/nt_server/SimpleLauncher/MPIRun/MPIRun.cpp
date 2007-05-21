#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include "GetOpt.h"
#include <tchar.h>
#include "RunLocal.h"
#include "global.h"
#include "LaunchProcess.h"

// Function name	: PrintUsage
// Description	    : 
// Return type		: void 
void PrintUsage()
{
	printf("Usage:\n");
	printf(" MPIRun [flags] [-tcp] -localonly numprocs executable [args ...]\n");
	printf(" MPIRun [flags] -port LauncherPortNumber configfile [args ...]\n");
	printf(" flags\n");
	printf("  -env \"var1=val1|var2=val2|var3=val3...\"\n");
	printf("  -mpichport number (port number for the root process to listen on)\n");
	printf("\n");
	printf("Config file format:\n");
	printf("   >[port RootPortNumber]\n");
	printf("   >exe c:\\temp\\mpiprogram.exe\n");
	printf("     OR \"c:\\temp\\sub directory\\mpiprogram.exe\"\n");
	printf("     OR \\\\host\\share\\mpiprogram.exe\n");
	printf("   >[env var1=val1|var2=val2|var3=val3...]\n");
	printf("   >[args arg1 arg2 ...]\n");
	printf("   >hosts\n");
	printf("   >hostname1 #procs\n");
	printf("   >hostname2 #procs\n");
	printf("   >hostname3 #procs\n");
	printf("   >...\n");
	printf("\n");
	printf("bracketed lines are optional\n");
	printf("\n");
}

// Function name	: ParseConfigFile
// Description	    : 
// Return type		: void 
// Argument         : LPTSTR filename
void ParseConfigFile(LPTSTR filename)
{
	FILE *fin;
	TCHAR buffer[1024] = TEXT("");

	fin = _tfopen(filename, TEXT("r"));
	if (fin == NULL)
	{
		_tprintf(TEXT("Unable to open file: %s\n"), filename);
		ExitProcess(1);
	}

	while (_fgetts(buffer, 1024, fin))
	{
		// Check for the name of the executable
		if (_tcsnicmp(buffer, TEXT("exe "), 4) == 0)
		{
			TCHAR *pChar = &buffer[4];
			while (_istspace(*pChar))
				pChar++;
			_tcscpy(g_pszExe, pChar);
			pChar = &g_pszExe[_tcslen(g_pszExe)-1];
			while (_istspace(*pChar) && (pChar >= g_pszExe))
			{
				*pChar = '\0';
				pChar--;
			}
		}
		else
		// Check for the port for mpich to use
		if (_tcsnicmp(buffer, TEXT("port "), 5) == 0)
		{
			TCHAR *pChar = &buffer[5];
			while (_istspace(*pChar))
				pChar++;
			g_nMPICHPort = _ttoi(pChar);
		}
		else
		// Check for program arguments
		if (_tcsnicmp(buffer, TEXT("args "), 5) == 0)
		{
			TCHAR *pChar = &buffer[5];
			while (_istspace(*pChar))
				pChar++;
			_tcscpy(g_pszArgs, pChar);
			pChar = &g_pszArgs[_tcslen(g_pszArgs)-1];
			while (_istspace(*pChar) && (pChar >= g_pszArgs))
			{
				*pChar = '\0';
				pChar--;
			}
		}
		else
		// Check for environment variables
		if (_tcsnicmp(buffer, TEXT("env "), 4) == 0)
		{
			TCHAR *pChar = &buffer[4];
			while (_istspace(*pChar))
				pChar++;
			_tcscpy(g_pszEnv, pChar);
			pChar = &g_pszEnv[_tcslen(g_pszEnv)-1];
			while (_istspace(*pChar) && (pChar >= g_pszEnv))
			{
				*pChar = '\0';
				pChar--;
			}
		}
		else
		// Check for hosts
		if (_tcsnicmp(buffer, TEXT("hosts"), 5) == 0)
		{
			TCHAR *pChar;
			LPTSTR token = NULL;
			while (true)
			{
				if (_fgetts(buffer, 1024, fin) == NULL)
				{
					_tprintf(TEXT("There must be at least one host specified after the hosts tag in the config file.\n"));
					fclose(fin);
					ExitProcess(1);
				}
				token = _tcstok(buffer, TEXT(" \t\r\n"));
				if (token != NULL)
				{
					if (*token == _T('#'))
						token = NULL;
					else
					{
						_tcscpy(g_pszFirstHost, token);
						token = _tcstok(NULL, TEXT(" \t\r\n"));
						g_nFirstSMPProcs = (token == NULL) ? 1 : _ttoi(token);
						break;
					}
				}
			}
			g_nHosts = 1;

			HostNode *list = NULL, *tail = NULL;
			while (_fgetts(buffer, 1024, fin))
			{
				if (buffer[0] != _T('#'))
				{
					pChar = &buffer[_tcslen(buffer)-1];
					// Trim trailing white space
					while (_istspace(*pChar) && (pChar >= buffer))
					{
						*pChar = '\0';
						pChar--;
					}
					// If there is anything left on the line, consider it a host name
					if (_tcslen(buffer) > 0)
					{
						if (list == NULL)
						{
							tail = list = new HostNode;
							list->next = NULL;
							LPTSTR token = _tcstok(buffer, TEXT(" \t\r\n"));
							if (token != NULL)
								_tcscpy(list->host, token);
							token = _tcstok(NULL, TEXT(" \t\r\n"));
							list->nSMPProcs = (token == NULL) ? 1 : _ttoi(token);
						}
						else
						{
							tail->next = new HostNode;
							tail = tail->next;
							tail->next = NULL;
							LPTSTR token = _tcstok(buffer, TEXT(" \t\r\n"));
							if (token != NULL)
								_tcscpy(tail->host, token);
							token = _tcstok(NULL, TEXT(" \t\r\n"));
							tail->nSMPProcs = (token == NULL) ? 1 : _ttoi(token);
						}
						g_nHosts++;
					}
				}
			}
			fclose(fin);

			// Insert the first host into the list
			g_pHosts = new HostNode;
			strcpy(g_pHosts->host, g_pszFirstHost);
			g_pHosts->nSMPProcs = g_nFirstSMPProcs;
			g_pHosts->next = list;

			return;
		}
	}
	fclose(fin);
}

// Function name	: CtrlHandlerRoutine
// Description	    : 
// Return type		: BOOL WINAPI 
// Argument         : DWORD dwCtrlType
BOOL WINAPI CtrlHandlerRoutine(DWORD dwCtrlType)
{
	fprintf(stderr, "User break\n");
	//fprintf(stderr, "Remote processes termination not implemented.\n");
	ExitProcess(1);
	return TRUE;
}

// Function name	: main
// Description	    : 
// Return type		: void 
// Argument         : int argc
// Argument         : char *argv[]
void main(int argc, char *argv[])
{
	SetConsoleCtrlHandler(CtrlHandlerRoutine, TRUE);

	GetOpt(argc, argv, "-env", g_pszEnv);
	GetOpt(argc, argv, "-port", &g_nPort);
	GetOpt(argc, argv, "-mpichport", &g_nMPICHPort);


	// Start up the Winsock2 dll
	WSADATA wsaData;
	int err;
	err = WSAStartup( MAKEWORD( 2, 0 ), &wsaData );
	if (err != 0)
	{
		printf("Unable to load the winsock dll. Error %d\n", err);
		return;
	}

	if (GetOpt(argc, argv, "-localonly", &g_nHosts))
	{
		bool bDoSMP = !GetOpt(argc, argv, "-tcp");
		if (g_nHosts < 1)
		{
			printf("Error: must specify a number greater than 0 after the -localonly option\n");
			return;
		}
		if (argc < 2)
		{
			printf("Error: not enough arguments.\n");
			return;
		}
		TCHAR pszTempExe[MAX_PATH], *namepart;
		_tcscpy(g_pszExe, argv[1]);
		GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
		// Quote the executable in case there are spaces in the path
		_stprintf(g_pszExe, TEXT("\"%s\""), pszTempExe);
		g_pszArgs[0] = TEXT('\0');
		for (int i=2; i<argc; i++)
		{
			_tcscat(g_pszArgs, argv[i]);
			if (i < argc-1)
				_tcscat(g_pszArgs, TEXT(" "));
		}
		RunLocal(bDoSMP);
	}
	else
	{
		if (argc < 2)
		{
			PrintUsage();
			ExitProcess(0);
		}

		ParseConfigFile(argv[1]);
		if ((_tcslen(g_pszArgs) > 0) && (argc > 2))
			_tcscat(g_pszArgs, TEXT(" "));
		for (int i=2; i<argc; i++)
		{
			_tcscat(g_pszArgs, argv[i]);
			if (i < argc-1)
				_tcscat(g_pszArgs, TEXT(" "));
		}

		// Figure out how many processes to launch
		int nProc = 0;
		HostNode *n = g_pHosts;
		while (n)
		{
			nProc += n->nSMPProcs;
			n = n->next;
		}

		// Launch the processes
		int iproc = 0;
		char pszJobID[100];
		char pszEnv[MAX_PATH] = "";
		char pszDir[MAX_PATH] = ".";
		HANDLE *pThread;
		char pBuffer[MAX_PATH];
		int nShmLow, nShmHigh;

		GetTempFileName(".", "mpi", 0, pszJobID);
		DeleteFile(pszJobID);

		sprintf(pszEnv, "MPICH_JOBID=%s|MPICH_NPROC=%d|MPICH_ROOTHOST=%s|MPICH_ROOTPORT=%d",
				&pszJobID[2], nProc, g_pHosts->host, g_nMPICHPort);

		GetCurrentDirectory(MAX_PATH, pszDir);

		pThread = new HANDLE[nProc];

		while (g_pHosts)
		{
			nShmLow = iproc;
			nShmHigh = iproc + g_pHosts->nSMPProcs - 1;
			for (int i=0; i<g_pHosts->nSMPProcs; i++)
			{
				LaunchProcessArg *arg = new LaunchProcessArg;
				arg->i = iproc;
				arg->nPort = g_nPort;
				strcpy(arg->pszCmdLine, g_pszExe);
				if (strlen(g_pszArgs) > 0)
				{
					strcat(arg->pszCmdLine, " ");
					strcat(arg->pszCmdLine, g_pszArgs);
				}
				strcpy(arg->pszDir, pszDir);
				strcpy(arg->pszEnv, pszEnv);
				strcpy(arg->pszHost, g_pHosts->host);

				sprintf(pBuffer, "|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", iproc, nShmLow, nShmHigh);
				strcat(arg->pszEnv, pBuffer);

				if (strlen(g_pszEnv) > 0)
				{
					strcat(arg->pszEnv, "|");
					strcat(arg->pszEnv, g_pszEnv);
				}

				//printf("Environment:\n%s\n", arg->pszEnv);
				DWORD dwThreadID;
				pThread[iproc] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LaunchProcessSocket, arg, 0, &dwThreadID);
				if (pThread[iproc] == NULL)
				{
					printf("Unable to create LaunchProcess thread\n");
					ExitProcess(1);
				}
				iproc++;
			}

			HostNode *n = g_pHosts;
			g_pHosts = g_pHosts->next;
			delete n;
		}

		WaitForMultipleObjects(nProc, pThread, TRUE, INFINITE);
		for (i=0; i<nProc; i++)
			CloseHandle(pThread[i]);
		delete pThread;
	}

	WSACleanup();
}
