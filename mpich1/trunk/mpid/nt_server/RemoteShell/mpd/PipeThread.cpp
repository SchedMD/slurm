#include "PipeThread.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include "Translate_Error.h"
#include "TerminalClientThread.h"

// Function name	: PipeClientThread
// Description	    : 
// Return type		: void 
// Argument         : HANDLE hPipe
void PipeClientThread(HANDLE hPipe)
{
	int error;
	DWORD dwNumRead;
	char pszPipeName[MAX_PATH], pszPipeName2[MAX_PATH];
	char *pChar = pszPipeName;

	ReadFile(hPipe, pChar, 1, &dwNumRead, NULL);
	while (*pChar != '\0')
	{
		pChar++;
		ReadFile(hPipe, pChar, 1, &dwNumRead, NULL);
	}

	//printf("PipeClientThread connecting to '%s'\n", pszPipeName);fflush(stdout);
	HANDLE hOutputPipe = CreateFile(
		pszPipeName,
		GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);
	
	if (hOutputPipe == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		printf("Unable to connect back to pipe client '%s', error %d\n", pszPipeName, error);
		ExitThread(0);
	}

	pChar = pszPipeName2;
	ReadFile(hPipe, pChar, 1, &dwNumRead, NULL);
	while (*pChar != '\0')
	{
		pChar++;
		ReadFile(hPipe, pChar, 1, &dwNumRead, NULL);
	}

	//printf("PipeClientThread connecting to '%s'\n", pszPipeName);fflush(stdout);
	HANDLE hEndOutputPipe = CreateFile(
		pszPipeName2,
		GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);
	
	if (hEndOutputPipe == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		printf("Unable to connect back to pipe client '%s', error %d\n", pszPipeName, error);
		ExitThread(0);
	}

	//printf("Pipe connection established.\n");

	TerminalClientThreadArg *pArg = new TerminalClientThreadArg;
	pArg->hInput = hPipe;
	pArg->hOutput = hOutputPipe;
	pArg->hEndOutput = hEndOutputPipe;

	/*
	HANDLE hThread;
	DWORD dwThreadID;
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)TerminalClientThread, pArg, 0, &dwThreadID);
	WaitForSingleObject(hThread, INFINITE);
	CloseHandle(hThread);
	/*/
	TerminalClientThread(pArg);
	//*/

	CloseHandle(hPipe);
	CloseHandle(hOutputPipe);
}

// Function name	: GetNameForPipe
// Description	    : 
// Return type		: void 
// Argument         : char *pszName
void GetNameForPipe(char *pszName)
{
	char pszUserName[100];
	DWORD length;

	length = 100;
	if (GetUserName(pszUserName, &length))
		sprintf(pszName, "\\\\.\\pipe\\mpd%s", pszUserName);
	else
		strcpy(pszName, "\\\\.\\pipe\\mpdpipe");
}

// Function name	: PipeThread
// Description	    : 
// Return type		: void 
void PipeThread()
{
    DWORD threadID;
	int error;
	char error_msg[256];
	char pszPipeName[256];

	GetNameForPipe(pszPipeName);

	printf("Making pipe '%s'.\n", pszPipeName);fflush(stdout);

	// Set the security attributes to allow handles to be inherited
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.lpSecurityDescriptor = NULL;
	saAttr.bInheritHandle = TRUE;

	printf("Waiting for a pipe connections.\n");fflush(stdout);
    while(true)
    {
        // ready to accept connections 
		HANDLE hServerPipe = 
			CreateNamedPipe(
				pszPipeName,
				PIPE_ACCESS_INBOUND | FILE_FLAG_WRITE_THROUGH,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
				PIPE_UNLIMITED_INSTANCES,
				0,0,0, 
				&saAttr
				);
		
		if (hServerPipe == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			Translate_Error(error, error_msg);
			printf("Unable to create pipe: %s\n", error_msg);fflush(stdout);
			ExitThread(0);
		}
		else
		{
			//printf("waiting for a pipe connection\n");
			if (!ConnectNamedPipe(hServerPipe, NULL))
			{
				error = GetLastError();
				if (error != ERROR_PIPE_CONNECTED)
				{
					Translate_Error(error, error_msg);
					printf("unable to connect to client pipe: %s", error_msg);fflush(stdout);
					ExitThread(0);
				}
			}
			//printf("pipe connection established\n");
			// Start a separate thread and send it the pipe.
			HANDLE hthread = 
				CreateThread(
				NULL, 0, 
				(LPTHREAD_START_ROUTINE)PipeClientThread,
				(LPVOID)hServerPipe,
				0, (LPDWORD)&threadID);
			if(hthread == NULL)
			{
				printf("Cannot start client thread, error: %d\n", GetLastError());fflush(stdout);
				CloseHandle(hServerPipe);
			}
			else
				CloseHandle(hthread);
		}
    }
}
