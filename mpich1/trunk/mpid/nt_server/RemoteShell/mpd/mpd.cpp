#include "sockets.h"
#include <stdio.h>
#include "GetOpt.h"
#include "Command.h"
#include "LeftThread.h"
#include "RightThread.h"
#include "PipeThread.h"
#include "TerminalClientThread.h"
#include "global.h"
#include "GetHosts.h"
#include "ManageProcess.h"

HANDLE g_hTimeoutEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

// Function name	: TimeoutThread
// Description	    : 
// Return type		: void 
// Argument         : int timeout
void TimeoutThread(int timeout)
{
	if (WaitForSingleObject(g_hTimeoutEvent, timeout) != WAIT_OBJECT_0)
		ExitProcess(1);
}

// Function name	: RedirectInput
// Description	    : 
// Return type		: void 
// Argument         : HANDLE hPipe
void RedirectInput(HANDLE hPipe)
{
	DWORD dwNumRead, dwNumWritten;
	char buffer[CMD_BUFF_SIZE+100];
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	while (ReadFile(hStdin, buffer, CMD_BUFF_SIZE+100, &dwNumRead, NULL))
	{
		WriteFile(hPipe, buffer, dwNumRead, &dwNumWritten, NULL);
	}
}

// Function name	: RedirectOutput
// Description	    : 
// Return type		: void 
// Argument         : HANDLE hPipe
void RedirectOutput(HANDLE hPipe)
{
	DWORD dwNumRead, dwNumWritten;
	char buffer[CMD_BUFF_SIZE];
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	while (ReadFile(hPipe, buffer, CMD_BUFF_SIZE, &dwNumRead, NULL))
		WriteFile(hStdout, buffer, dwNumRead, &dwNumWritten, NULL);
}

// Function name	: main
// Description	    : 
// Return type		: void 
// Argument         : int argc
// Argument         : char *argv[]
void main(int argc, char *argv[])
{
	WSADATA wsaData;
	int err;
	char buffer[1024] = "";
	HANDLE hLeftThread = NULL;
	HANDLE hRightThread = NULL;
	HANDLE hPipeThread = NULL;
	HANDLE hConsoleThread = NULL;
	bool bUsePipe = false, bUseConsole = false, bConnectPipe = false;
	char pszPipeName[MAX_PATH] = "";
	CommandData Command;
	bool bIdSet = false;
	int nSpawns = 1;
	bool bLaunchFromRegistry = false, bUseHostFile = false, bUseHosts = false;
	char pszHostFile[MAX_PATH] = "";
	int nMPDsToLaunch = 0;
	HostNode *pHosts = NULL;
	char pszCmdLine[1024], pszArgs[1024] = "", pszEnv[1024]="", pszDir[MAX_PATH]="";

	SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

	// Start the Winsock dll.
	if ((err = WSAStartup( MAKEWORD( 2, 0 ), &wsaData )) != 0)
	{
		printf("Winsock2 dll not initialized, error: %d\n", err);
		return;
	}


	if (GetOpt(argc, argv, "-cmd", pszCmdLine))
	{
		char pszIn[100] = "", pszOut[100] = "", pszErr[100] = "";
		int nGroupID = -1, nGroupRank = -1;
		int nTemp;

		GetOpt(argc, argv, "-args", pszArgs);
		GetOpt(argc, argv, "-env", pszEnv);
		GetOpt(argc, argv, "-dir", pszDir);
		GetOpt(argc, argv, "-0", pszIn);
		GetOpt(argc, argv, "-1", pszOut);
		GetOpt(argc, argv, "-2", pszErr);
		GetOpt(argc, argv, "-group", &nGroupID);
		GetOpt(argc, argv, "-rank", &nGroupRank);
		GetOpt(argc, argv, "-hAbortEvent", &nTemp);

		ManageProcess(pszCmdLine, pszArgs, pszEnv, pszDir, nGroupID, nGroupRank, pszIn, pszOut, pszErr, (HANDLE)nTemp);
		CloseCommands();
		WSACleanup();
		return;
	}
	else
	{
		bUsePipe = !GetOpt(argc, argv, "-nopipe");
		bUseConsole = !GetOpt(argc, argv, "-noconsole");
		g_bDatabaseIsLocal = !GetOpt(argc, argv, "-nodbs");
		GetOpt(argc, argv, "-spawns", &nSpawns);
		g_List.SetMySpawns(nSpawns);
		bLaunchFromRegistry = GetOpt(argc, argv, "-registry");
		if (!bLaunchFromRegistry)
			bLaunchFromRegistry = GetOpt(argc, argv, "-nregistry", &nMPDsToLaunch);
		bUseHostFile = GetOpt(argc, argv, "-hostfile", pszHostFile);
		GetOpt(argc, argv, "-n", &nMPDsToLaunch);
		bUseHosts = GetOpt(argc, argv, "-hosts");
		g_List.m_bLookupIP = !GetOpt(argc, argv, "-nolookup");
		bConnectPipe = GetOpt(argc, argv, "-pipe", buffer);
		if (bConnectPipe)
		{
			if (buffer[0] == '\\')
				strcpy(pszPipeName, buffer);
			else
				sprintf(pszPipeName, "\\\\.\\pipe\\%s", buffer);
		}
		else
		{
			if (bConnectPipe = GetOpt(argc, argv, "-pipe"))
				GetNameForPipe(pszPipeName);
		}

		if (g_bDatabaseIsLocal)
		{
			// This process owns the database so initialize the group id to 1
			char *str = new char[2];
			str[0] = '1';
			str[1] = '\0';
			g_Database.Put("global", "currentID", str, 2, false);
		}
	}
	
	if (bConnectPipe)
	{
		int error;
		DWORD dwNumWritten;
		
		printf("mpd connecting to pipe '%s'\n", pszPipeName);fflush(stdout);
		HANDLE hPipe = CreateFile(
			pszPipeName,
			GENERIC_WRITE,
			0, NULL,
			OPEN_EXISTING,
			0, NULL);
		
		if (hPipe != INVALID_HANDLE_VALUE)
		{
			HANDLE hOutputPipe, hOutputThread, hEndOutputPipe;
			
			strcat(pszPipeName, "out");
			hOutputPipe = CreateNamedPipe(
				pszPipeName,
				PIPE_ACCESS_INBOUND | FILE_FLAG_WRITE_THROUGH,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
				PIPE_UNLIMITED_INSTANCES,
				0,0,0, 
				NULL
				);
			
			if (hOutputPipe == INVALID_HANDLE_VALUE)
			{
				error = GetLastError();
				printf("Unable to create pipe: error %d on pipe '%s'\n", error, pszPipeName);
				CloseHandle(hPipe);
				ExitProcess(error);
			}
			
			WriteFile(hPipe, pszPipeName, strlen(pszPipeName)+1, &dwNumWritten, NULL);
			//printf("mpd waiting for connection back on pipe '%s'\n", pszPipeName);fflush(stdout);
			if (!ConnectNamedPipe(hOutputPipe, NULL))
			{
				error = GetLastError();
				if (error != ERROR_PIPE_CONNECTED)
				{
					printf("unable to connect to client pipe: error %d\n", error);fflush(stdout);
					CloseHandle(hPipe);
					CloseHandle(hOutputPipe);
					return;
				}
			}
			strcat(pszPipeName, "2");
			hEndOutputPipe = CreateNamedPipe(
				pszPipeName,
				PIPE_ACCESS_INBOUND | FILE_FLAG_WRITE_THROUGH,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
				PIPE_UNLIMITED_INSTANCES,
				0,0,0, 
				NULL
				);
			
			if (hEndOutputPipe == INVALID_HANDLE_VALUE)
			{
				error = GetLastError();
				printf("Unable to create pipe: error %d on pipe '%s'\n", error, pszPipeName);
				CloseHandle(hPipe);
				CloseHandle(hOutputPipe);
				ExitProcess(error);
			}
			
			WriteFile(hPipe, pszPipeName, strlen(pszPipeName)+1, &dwNumWritten, NULL);
			//printf("mpd waiting for connection back on pipe '%s'\n", pszPipeName);fflush(stdout);
			if (!ConnectNamedPipe(hEndOutputPipe, NULL))
			{
				error = GetLastError();
				if (error != ERROR_PIPE_CONNECTED)
				{
					printf("unable to connect to client pipe: error %d\n", error);fflush(stdout);
					CloseHandle(hPipe);
					CloseHandle(hOutputPipe);
					CloseHandle(hEndOutputPipe);
					return;
				}
			}
			DWORD dwThreadID;
			CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectOutput, hEndOutputPipe, 0, &dwThreadID));
			
			CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectInput, hPipe, 0, &dwThreadID));
			hOutputThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectOutput, hOutputPipe, 0, &dwThreadID);
			
			WaitForSingleObject(hOutputThread, INFINITE);
			CloseHandle(hOutputThread);
			CloseHandle(hPipe);
			CloseHandle(hOutputPipe);
		}
	}
	else
	{
		int timeout = 0;
		if (GetOpt(argc, argv, "-timeout", &timeout))
		{
			DWORD dwThreadID;
			CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)TimeoutThread, (LPVOID)timeout, 0, &dwThreadID));
		}
		if (nMPDsToLaunch > 0 || bUseHostFile || bUseHosts)
		{
			pHosts = NULL;
			if (bUseHostFile)
				pHosts = GetHostsFromFile(nMPDsToLaunch, pszHostFile);
			else if (bLaunchFromRegistry)
				pHosts = GetHostsFromRegistry(nMPDsToLaunch);
			else if (bUseHosts)
				pHosts = GetHostsFromCmdLine(argc, argv);
			else
			{
				printf("Error parsing command line\n");
				printf("No option specified to determine hosts\n");
				ExitProcess(1);
			}
			
			LaunchMPDs(pHosts, &hLeftThread, &hRightThread, timeout);
		}
		else
		{
			DWORD dwThreadID;
			hLeftThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LeftThread, NULL, 0, &dwThreadID);
			hRightThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RightThread, NULL, 0, &dwThreadID);
		}
		
		// Wait for the neighbors to connect
		while (!g_bLeftConnected || !g_bRightConnected)
			Sleep(100);
		SetEvent(g_hTimeoutEvent);
		if (bUseConsole)
		{
			if (nMPDsToLaunch > 0 || bUseHosts)
				printf("Ring established\n");
			else
				printf("Left and Right connections established\n");
			fflush(stdout);
		}

		DWORD dwThreadID;
		hPipeThread = bUsePipe ? CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)PipeThread, NULL, 0, &dwThreadID) : NULL;
			
		if (bUseConsole)
		{
			HANDLE phThreads[2];
			HANDLE hStdin, hStdout;
			TerminalClientThreadArg *pArg;
			
			hStdin = GetStdHandle(STD_INPUT_HANDLE);
			hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
			pArg = new TerminalClientThreadArg;
			pArg->hInput = hStdin;
			pArg->hOutput = hStdout;
			DWORD dwThreadID;
			hConsoleThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)TerminalClientThread, pArg, 0, &dwThreadID);
			
			phThreads[0] = hConsoleThread;
			phThreads[1] = hLeftThread;
			WaitForMultipleObjects(2, phThreads, FALSE, INFINITE);
			
			WaitForSingleObject(hLeftThread, 7000);
		}
		else
			WaitForSingleObject(hLeftThread, INFINITE);
	}

	CloseCommands();
	WSACleanup();
}
