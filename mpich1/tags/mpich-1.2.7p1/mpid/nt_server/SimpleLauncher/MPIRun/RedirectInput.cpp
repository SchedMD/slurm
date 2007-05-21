#include "RedirectInput.h"
#include <stdio.h>
#include "sockets.h"

#define G_BUF_SIZE 1024
char g_pBuffer[G_BUF_SIZE];
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
		if (!ReadFile(hStdin, g_pBuffer, G_BUF_SIZE, &g_num_read, NULL))
			return;
		if (g_num_read == 0)
			return;
		ResetEvent(g_hBufferEvent2);
		SetEvent(g_hBufferEvent1);
		WaitForSingleObject(g_hBufferEvent2, INFINITE);
	}
}

// Function name	: RedirectInputSocketThread
// Description	    : 
// Return type		: void 
// Argument         : RedirectInputThreadArg *arg
void RedirectInputSocketThread(RedirectInputThreadArg *arg)
{
	HANDLE hObject[2];
	DWORD ret_val;

	DWORD dwThreadID;
	HANDLE hRSIThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ReadStdinThread, NULL, 0, &dwThreadID);

	hObject[0] = arg->hEvent;
	hObject[1] = g_hBufferEvent1;

	while (true)
	{
		ret_val = WaitForMultipleObjects(2, hObject, FALSE, INFINITE);
		if (ret_val == WAIT_OBJECT_0+1)
		{
			if (g_num_read > 0)
				SendBlocking(arg->hSock, g_pBuffer, g_num_read, 0);
			ResetEvent(g_hBufferEvent1);
			SetEvent(g_hBufferEvent2);
		}
		else
			break;
	}
	CloseHandle(arg->hEvent);
	delete arg;
}
