#include <stdio.h>
#include <stdlib.h>
#include "LaunchProcess.h"
#include "global.h"
#include "mpirun.h"
#include "mpdutil.h"

// Function name    : WaitToBreak
// Description	    : This function is called in a thread if Ctrl-C is hit before the LaunchThreads have been created (very rare).
// Return type	    : void 
void WaitToBreak()
{
    WaitForSingleObject(g_hBreakReadyEvent, INFINITE);
    if (easy_send(g_sockBreak, "x", 1) == SOCKET_ERROR)
	easy_send(g_sockStopIOSignalSocket, "x", 1);
}

// Function name	: CtrlHandlerRoutine
// Description	    : 
// Return type		: BOOL
// Argument         : DWORD dwCtrlType
// Notes            : returning TRUE means this function handled the event.
//                    returning FALSE means the next handler routine should be called.
static bool g_bFirst = true;
HANDLE g_hLaunchThreadsRunning = CreateEvent(NULL, TRUE, TRUE, NULL);
BOOL WINAPI CtrlHandlerRoutine(DWORD dwCtrlType)
{
    bool bOK;

    // What is the default behavior for a LOGOFF event?
    // If I return FALSE will the process be killed?
    if (dwCtrlType == CTRL_LOGOFF_EVENT)
	return FALSE;

    g_bSuppressErrorOutput = true;

    // Don't abort while the launch threads are running because it can leave
    // processes running.
    if (WaitForSingleObject(g_hLaunchThreadsRunning, 0) == WAIT_OBJECT_0)
    {
	if (!g_bFirst)
	{
	    fprintf(stderr, "aborting\n");
	    
	    // Hit Ctrl-C twice and I'll exit
	    if (g_bDoMultiColorOutput)
	    {
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), g_ConsoleAttribute);
	    }
	    ExitProcess(12344321);
	}
	SetEvent(g_hAbortEvent);
	g_bFirst = false;
	return TRUE;
    }

    if (g_bUseJobHost && !g_bNoMPI)
	UpdateJobState("ABORTING");

    // Hit Ctrl-C once and I'll try to kill the remote processes
    if (g_bFirst)
    {
	fprintf(stderr, "BREAK - attempting to kill processes\n(hit break again to do a hard abort)\n");
	
	// Signal all the threads to stop
	SetEvent(g_hAbortEvent);
	
	bOK = true;
	if (g_sockBreak != INVALID_SOCKET)
	{
	    // Send a break command to WaitForExitCommands
	    if (easy_send(g_sockBreak, "x", 1) == SOCKET_ERROR)
	    {
		PrintError(WSAGetLastError(), "easy_send(break) failed\n");
		bOK = false;
	    }
	}
	else
	{
	    // Start a thread to wait until a break can be issued.  This happens
	    // if you hit Ctrl-C before all the process threads have been created.
	    DWORD dwThreadId;
	    HANDLE hThread;
	    for (int iter=0; iter<CREATE_THREAD_RETRIES; iter++)
	    {
		hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)WaitToBreak, NULL, 0, &dwThreadId);
		if (hThread != NULL)
		    break;
		Sleep(CREATE_THREAD_SLEEP_TIME);
	    }
	    if (hThread == NULL)
		bOK = false;
	    else
		CloseHandle(hThread);
	}
	if (!bOK)
	{
	    bOK = true;
	    // If you cannot issue a break signal, send a stop signal to the io threads
	    if (g_sockStopIOSignalSocket != INVALID_SOCKET)
	    {
		if (easy_send(g_sockStopIOSignalSocket, "x", 1) == SOCKET_ERROR)
		{
		    PrintError(WSAGetLastError(), "easy_send(stop) failed\n");
		    bOK =false;
		}
	    }
	    else
		bOK = false;
	    if (!bOK)
	    {
		if (g_bDoMultiColorOutput)
		{
		    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), g_ConsoleAttribute);
		}
		ExitProcess(1); // If you cannot issue either a break or stop signal, exit
	    }
	}

	g_bFirst = false;
	return TRUE;
    }
    
    fprintf(stderr, "aborting\n");

    // Hit Ctrl-C twice and I'll exit
    if (g_bDoMultiColorOutput)
    {
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), g_ConsoleAttribute);
    }

    // Issue a stop signal
    if (g_sockStopIOSignalSocket != INVALID_SOCKET)
    {
	if (easy_send(g_sockStopIOSignalSocket, "x", 1) == SOCKET_ERROR)
	    PrintError(WSAGetLastError(), "easy_send(stop) failed\n");
    }
    Sleep(2000); // Give a little time for the kill commands to get sent out?
    ExitProcess(1);
    return TRUE;
}
