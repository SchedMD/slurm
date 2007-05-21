#include "mpdimpl.h"
#include "Service.h"
#include "database.h"

void StdinThread()
{
    char str[MAX_CMD_LENGTH];
    while (gets(str))
    {
	if (strcmp(str, "quit") == 0)
	{
	    if (ReadMPDRegistry("RevertToMultiUser", str, false))
	    {
		if (stricmp(str, "yes") == 0)
		    WriteMPDRegistry("SingleUser", "no");
		DeleteMPDRegistry("RevertToMultiUser");
	    }
	    dbg_printf("StdinThread: Exiting.\n");
	    ExitProcess(0);
	}
	if (strcmp(str, "stop") == 0)
	{
	    ServiceStop();
	}
	if (strcmp(str, "print") == 0)
	{
	    PrintState(stdout);
	}
    }
}

int EvalException(EXCEPTION_POINTERS *p)
{
    char pszError[256];
    if (p->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    {
	if (p->ExceptionRecord->ExceptionInformation[0] == 1)
	{
	    // write error
	    sprintf(pszError, "EXCEPTION_ACCESS_VIOLATION: instruction address: 0x%p, invalid write to 0x%p",
		p->ExceptionRecord->ExceptionAddress,
		p->ExceptionRecord->ExceptionInformation[1]);
	}
	else
	{
	    // read error
	    sprintf(pszError, "EXCEPTION_ACCESS_VIOLATION: instruction address: 0x%p, invalid read from 0x%p",
		p->ExceptionRecord->ExceptionAddress,
		p->ExceptionRecord->ExceptionInformation[1]);
	}
	err_printf("%s\n", pszError);
	return EXCEPTION_CONTINUE_EXECUTION;
    }
    err_printf("exception %d caught in mpd", p->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_SEARCH;
}

//
//  FUNCTION: ServiceStart
//
//  PURPOSE: Actual code of the service
//           that does the work.
//
//  PARAMETERS:
//    dwArgc   - number of command line arguments
//    lpszArgv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
VOID ServiceStart (DWORD dwArgc, LPTSTR *lpszArgv)
{
    int run_retval = RUN_EXIT;
    HANDLE stdin_thread = NULL;
    int iter;

    // report the status to the service control manager.
    if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000))
	return;
    
    // Load the path to the service executable
    char szExe[1024];
    HMODULE hModule = GetModuleHandle(NULL);
    if (!GetModuleFileName(hModule, szExe, 1024)) 
	strcpy(szExe, "mpd.exe");
    WriteMPDRegistry("path", szExe);
    
    // Initialize
    dbs_init();
    ContextInit();

    if (!bDebug)
    {
#ifndef _DEBUG
	// If we are not running in debug mode and this is the release
	// build then set the error mode to prevent popup message boxes.
#ifdef USE_SET_ERROR_MODE
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
#endif
	easy_socket_init();
	ParseRegistry(false);
    }

    // report the status to the service control manager.
    if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000))
    {
	easy_socket_finalize();
	dbs_finalize();
	ContextFinalize();
	return;
    }
    
    g_hProcessStructMutex = CreateMutex(NULL, FALSE, NULL);
    g_hForwarderMutex = CreateMutex(NULL, FALSE, NULL);
    g_hLaunchMutex = CreateMutex(NULL, FALSE, NULL);
    g_hBarrierStructMutex = CreateMutex(NULL, FALSE, NULL);

    InitMPDUser();

    do
    {
	// report the status to the service control manager.
	if (!ReportStatusToSCMgr(SERVICE_RUNNING, NO_ERROR, 0))
	{
	    easy_socket_finalize();
	    dbs_finalize();
	    ContextFinalize();
	    return;
	}

	////////////////////////////////////////////////////////
	//
	// Service is now running, perform work until shutdown
	//
	
	AddInfoToMessageLog("MPICH_MPD Daemon service started.");
	
	if (stdin_thread == NULL && bDebug)
	{
	    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
	    {
		stdin_thread = CreateThread(
		    NULL, 0,
		    (LPTHREAD_START_ROUTINE)StdinThread,
		    NULL, 0, NULL);
		if (stdin_thread != NULL)
		    break;
		Sleep(CREATE_THREAD_SLEEP_TIME);
	    }
	    if (stdin_thread == NULL)
	    {
		err_printf("ServiceStart:CreateThread(stdin_thread) failed, error %d\n", GetLastError());
	    }
	}

	__try {
	    run_retval = RUN_RESTART;
	    run_retval = Run();
	    if (run_retval == RUN_RESTART)
	    {
		warning_printf("Run returned RUN_RESTART, restarting mpd.");
	    }
	} __except ( EvalException(GetExceptionInformation()) )	{}
	
	RemoveAllContexts();
	
    } while (run_retval == RUN_RESTART);
    
    CloseHandle(g_hProcessStructMutex);
    CloseHandle(g_hForwarderMutex);
    CloseHandle(g_hLaunchMutex);
    CloseHandle(g_hBarrierStructMutex);

    if (stdin_thread != NULL)
    {
	TerminateThread(stdin_thread, 0);
	CloseHandle(stdin_thread);
    }
    
    easy_socket_finalize();
    dbs_finalize();
    ContextFinalize();
    AddInfoToMessageLog("MPICH_MPD Daemon service stopped.");

    SetEvent(g_hBombDiffuseEvent);
    if (g_hBombThread != NULL)
    {
	if (WaitForSingleObject(g_hBombThread, 5000) == WAIT_TIMEOUT)
	{
	    TerminateThread(g_hBombThread, 0);
	}
	CloseHandle(g_hBombThread);
    }
    CloseHandle(g_hBombDiffuseEvent);
    dbg_printf("ServiceStart: exiting.\n");
}
