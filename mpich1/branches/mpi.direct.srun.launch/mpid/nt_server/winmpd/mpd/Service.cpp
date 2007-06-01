// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (C) 1993-1995  Microsoft Corporation.  All Rights Reserved.
//
//  MODULE:   service.c
//
//  PURPOSE:  Implements functions required by all services
//            windows.
//
//  FUNCTIONS:
//    main(int argc, char **argv);
//    service_ctrl(DWORD dwCtrlCode);
//    service_main(DWORD dwArgc, LPTSTR *lpszArgv);
//    CmdInstallService();
//    CmdRemoveService(BOOL bErrorOnNotInstalled);
//    CmdDebugService(int argc, char **argv);
//    ControlHandler ( DWORD dwCtrlType );
//    GetLastErrorText( LPTSTR lpszBuf, DWORD dwSize );
//
//  COMMENTS:
//
//  AUTHOR: Craig Link - Microsoft Developer Support
//


#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include "privileges.h"
#include "service.h"
#include <ntsecapi.h>
#include "mpdimpl.h"

// global variables
BOOL                    bDebug = FALSE;
bool                    interact = false;
bool                    bSetupRestart = true;

// internal variables
static SERVICE_STATUS          ssStatus;       // current status of the service
static SERVICE_STATUS_HANDLE   sshStatusHandle;
static DWORD                   dwErr = 0;
static TCHAR                   szErr[256];

// internal function prototypes
VOID WINAPI service_ctrl(DWORD dwCtrlCode);
VOID WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv);
BOOL WINAPI ControlHandler ( DWORD dwCtrlType );
LPTSTR GetLastErrorText( LPTSTR lpszBuf, DWORD dwSize );

//
//  FUNCTION: main
//
//  PURPOSE: entrypoint for service
//
//  PARAMETERS:
//    argc - number of command line arguments
//    argv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//    main() either performs the command line task, or
//    call StartServiceCtrlDispatcher to register the
//    main service thread.  When the this call returns,
//    the service has stopped, so exit.
//
void main(int argc, char **argv)
{
    SERVICE_TABLE_ENTRY dispatchTable[] =
    {
        { TEXT(SZSERVICENAME), (LPSERVICE_MAIN_FUNCTION)service_main },
        { NULL, NULL }
    };
    
    parseCommandLine(&argc, &argv);
    
    // if it doesn't match any of the above parameters
    // the service control manager may be starting the service
    // so we must call StartServiceCtrlDispatcher
    printf( "\nStartServiceCtrlDispatcher being called.\n" );
    printf( "This may take several seconds.  Please wait.\n" );
    fflush(stdout);
    
    if (!StartServiceCtrlDispatcher(dispatchTable))
        AddErrorToMessageLog(TEXT("StartServiceCtrlDispatcher failed."));
}



//
//  FUNCTION: service_main
//
//  PURPOSE: To perform actual initialization of the service
//
//  PARAMETERS:
//    dwArgc   - number of command line arguments
//    lpszArgv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//    This routine performs the service initialization and then calls
//    the user defined ServiceStart() routine to perform majority
//    of the work.
//
void WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv)
{
    
    // register our service control handler:
    //
    sshStatusHandle = RegisterServiceCtrlHandler( TEXT(SZSERVICENAME), service_ctrl);
    
    if (!sshStatusHandle)
        goto cleanup;
    
    // SERVICE_STATUS members that don't change in example
    //
    ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ssStatus.dwServiceSpecificExitCode = 0;
    
    
    // report the status to the service control manager.
    //
    if (!ReportStatusToSCMgr(
        SERVICE_START_PENDING, // service state
        NO_ERROR,              // exit code
        3000))                 // wait hint
        goto cleanup;
    
    
    ServiceStart( dwArgc, lpszArgv );
    
cleanup:
    
    // try to report the stopped status to the service control manager.
    //
    if (sshStatusHandle)
        (VOID)ReportStatusToSCMgr(
	SERVICE_STOPPED,
	dwErr,
	0);
    
    return;
}



//
//  FUNCTION: service_ctrl
//
//  PURPOSE: This function is called by the SCM whenever
//           ControlService() is called on this service.
//
//  PARAMETERS:
//    dwCtrlCode - type of control requested
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
VOID WINAPI service_ctrl(DWORD dwCtrlCode)
{
    // Handle the requested control code.
    //
    switch(dwCtrlCode)
    {
        // Stop the service.
        //
    case SERVICE_CONTROL_STOP:
	ssStatus.dwCurrentState = SERVICE_STOP_PENDING;
	ServiceStop();
	break;
	
        // Update the service status.
        //
    case SERVICE_CONTROL_INTERROGATE:
	break;
	
        // invalid control code
        //
    default:
	break;
	
    }
    
    ReportStatusToSCMgr(ssStatus.dwCurrentState, NO_ERROR, 0);
    
}



//
//  FUNCTION: ReportStatusToSCMgr()
//
//  PURPOSE: Sets the current status of the service and
//           reports it to the Service Control Manager
//
//  PARAMETERS:
//    dwCurrentState - the state of the service
//    dwWin32ExitCode - error code to report
//    dwWaitHint - worst case estimate to next checkpoint
//
//  RETURN VALUE:
//    TRUE  - success
//    FALSE - failure
//
//  COMMENTS:
//
BOOL ReportStatusToSCMgr(DWORD dwCurrentState,
                         DWORD dwWin32ExitCode,
                         DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;
    BOOL fResult = TRUE;
    
    
    if ( !bDebug ) // when debugging we don't report to the SCM
    {
        if (dwCurrentState == SERVICE_START_PENDING)
            ssStatus.dwControlsAccepted = 0;
        else
            ssStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	
        ssStatus.dwCurrentState = dwCurrentState;
        ssStatus.dwWin32ExitCode = dwWin32ExitCode;
        ssStatus.dwWaitHint = dwWaitHint;
	
        if ( ( dwCurrentState == SERVICE_RUNNING ) ||
	    ( dwCurrentState == SERVICE_STOPPED ) )
            ssStatus.dwCheckPoint = 0;
        else
            ssStatus.dwCheckPoint = dwCheckPoint++;
	
	
        // Report the status of the service to the service control manager.
        //
        if (!(fResult = SetServiceStatus( sshStatusHandle, &ssStatus))) {
            AddErrorToMessageLog(TEXT("SetServiceStatus"));
        }
    }
    return fResult;
}



//
//  FUNCTION: AddErrorToMessageLog(LPTSTR lpszMsg)
//
//  PURPOSE: Allows any thread to log an error message
//
//  PARAMETERS:
//    lpszMsg - text for message
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
VOID AddErrorToMessageLog(LPTSTR lpszMsg)
{
    TCHAR   szMsg[256];
    HANDLE  hEventSource;
    LPTSTR  lpszStrings[2];
    
    
    if ( !bDebug )
    {
        dwErr = GetLastError();
	
        // Use event logging to log the error.
        //
        hEventSource = RegisterEventSource(NULL, TEXT(SZSERVICENAME));
	
        _stprintf(szMsg, TEXT("%s error: %d"), TEXT(SZSERVICENAME), dwErr);
        lpszStrings[0] = szMsg;
        lpszStrings[1] = lpszMsg;
	
        if (hEventSource != NULL) {
            ReportEvent(hEventSource, // handle of event source
                EVENTLOG_ERROR_TYPE,  // event type
                0,                    // event category
                0,                    // event ID
                NULL,                 // current user's SID
                2,                    // strings in lpszStrings
                0,                    // no bytes of raw data
                (LPCTSTR*)lpszStrings,// array of error strings
                NULL);                // no raw data
	    
            (VOID) DeregisterEventSource(hEventSource);
        }
    }
}



//
//  FUNCTION: AddInfoToMessageLog(LPTSTR lpszMsg)
//
//  PURPOSE: Allows any thread to log an info message
//
//  PARAMETERS:
//    lpszMsg - text for message
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
VOID AddInfoToMessageLog(LPTSTR lpszMsg)
{
    HANDLE  hEventSource;
    LPTSTR  lpszStrings[1];
    
    
    if ( !bDebug )
    {
	printf("adding to message log\n");fflush(stdout);
        // Use event logging to log the message.
        //
        hEventSource = RegisterEventSource(NULL, TEXT(SZSERVICENAME));
	
        lpszStrings[0] = lpszMsg;
	
        if (hEventSource != NULL) {
            ReportEvent(hEventSource, // handle of event source
                EVENTLOG_INFORMATION_TYPE,  // event type
                0,                    // event category
                0,                    // event ID
                NULL,                 // current user's SID
                1,                    // strings in lpszStrings
                0,                    // no bytes of raw data
                (LPCTSTR*)lpszStrings,// array of error strings
                NULL);                // no raw data
	    
            (VOID) DeregisterEventSource(hEventSource);
        }
    }
}

//
//  FUNCTION: Setup_Service_restart( SC_HANDLE schService )
//
//  PURPOSE: Setup the service to automatically restart if it has been down for 5 minutes
//
//  PARAMETERS:
//    schService - service handle
//
//  RETURN VALUE:
//    BOOL
//
//  COMMENTS:
//    code provided by Bradley, Peter C. (MIS/CFD) [bradlepc@pweh.com]
//
static BOOL Setup_Service_restart( SC_HANDLE schService )
{
    SC_ACTION	actionList[3];
    SERVICE_FAILURE_ACTIONS schActionOptions;
    HMODULE hModule;
    BOOL ( WINAPI * ChangeServiceConfig2_fn)(SC_HANDLE hService, DWORD dwInfoLevel, LPVOID lpInfo);

    hModule = GetModuleHandle("Advapi32");
    if (hModule == NULL)
	return FALSE;

    ChangeServiceConfig2_fn = (BOOL ( WINAPI *)(SC_HANDLE, DWORD, LPVOID))GetProcAddress(hModule, "ChangeServiceConfig2A");
    if (ChangeServiceConfig2_fn == NULL)
	return FALSE;

    // The actions in this array are performed in order each time the service fails 
    // within the specified reset period.
    // This array attempts to restart mpd twice and then allow it to stay dead.
    actionList[0].Type = SC_ACTION_RESTART;
    actionList[0].Delay = 0;
    actionList[1].Type = SC_ACTION_RESTART;
    actionList[1].Delay = 0;
    actionList[2].Type = SC_ACTION_NONE;
    actionList[2].Delay = 0;
    
    schActionOptions.dwResetPeriod = (DWORD) 300;  /* 5 minute reset */
    schActionOptions.lpRebootMsg = NULL;
    schActionOptions.lpCommand = NULL;
    schActionOptions.cActions = (DWORD) (sizeof actionList / sizeof actionList[0]);
    schActionOptions.lpsaActions = actionList;
    
    return ChangeServiceConfig2_fn(schService,
	SERVICE_CONFIG_FAILURE_ACTIONS,
	&schActionOptions);
}



///////////////////////////////////////////////////////////////////
//
//  The following code handles service installation and removal
//


//
//  FUNCTION: CmdInstallService()
//
//  PURPOSE: Installs the service
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdInstallService(LPTSTR account, LPTSTR password, bool bMPDUserCapable /* = false */)
{
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    
    TCHAR szPath[1024];
    
    if ( GetModuleFileName( NULL, szPath, 1024 ) == 0 )
    {
        _tprintf(TEXT("Unable to install %s.\n%s\n"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256));
	fflush(stdout);
        return;
    }
    
    if (account == NULL)
	password = NULL;
    else
    {
	DWORD result;
	
	if (password == NULL)
	{
	    _tprintf(TEXT("No password provided for mpd user %s\n"), account);fflush(stdout);
	    return;
	}

	result = SetAccountRights(account, SE_SERVICE_LOGON_NAME);
	if (result != ERROR_SUCCESS)
	{
	    SetLastError(result);
	    _tprintf(TEXT("Unable to grant the necessary privileges to %s.\nInstallation failed. Error: %s.\n"), account, GetLastErrorText(szErr, 256));
	    fflush(stdout);
	    return;
	}
	/*
	result = SetAccountRights(account, SE_INTERACTIVE_LOGON_NAME);
	if (result != ERROR_SUCCESS)
	{
	    SetLastError(result);
	    _tprintf(TEXT("Unable to grant the necessary privileges to %s.\nInstallation failed. Error: %s.\n"), account, GetLastErrorText(szErr, 256));
	    fflush(stdout);
	    return;
	}
	result = SetAccountRights(account, SE_BATCH_LOGON_NAME);
	if (result != ERROR_SUCCESS)
	{
	    SetLastError(result);
	    _tprintf(TEXT("Unable to grant the necessary privileges to %s.\nInstallation failed. Error: %s.\n"), account, GetLastErrorText(szErr, 256));
	    fflush(stdout);
	    return;
	}
	result = SetAccountRights(account, SE_INCREASE_QUOTA_NAME);
	if (result != ERROR_SUCCESS)
	{
	    SetLastError(result);
	    _tprintf(TEXT("Unable to grant the necessary privileges to %s.\nInstallation failed. Error: %s.\n"), account, GetLastErrorText(szErr, 256));
	    fflush(stdout);
	    return;
	}
	*/
	result = SetAccountRights(account, SE_TCB_NAME);
	if (result != ERROR_SUCCESS)
	{
	    SetLastError(result);
	    _tprintf(TEXT("Unable to grant the necessary privileges to %s.\nInstallation failed. Error: %s.\n"), account, GetLastErrorText(szErr, 256));
	    fflush(stdout);
	    return;
	}
	/*
	result = SetAccountRights(account, SE_CHANGE_NOTIFY_NAME);
	if (result != ERROR_SUCCESS)
	{
	    SetLastError(result);
	    _tprintf(TEXT("Unable to grant the necessary privileges to %s.\nInstallation failed. Error: %s.\n"), account, GetLastErrorText(szErr, 256));
	    fflush(stdout);
	    return;
	}
	*/
    }

    schSCManager = OpenSCManager(
	NULL,                   // machine (NULL == local)
	NULL,                   // database (NULL == default)
	SC_MANAGER_ALL_ACCESS   // access required
	);
    if ( schSCManager )
    {
	DWORD type = SERVICE_WIN32_OWN_PROCESS;
	if (interact && account==NULL)
	    type = type | SERVICE_INTERACTIVE_PROCESS;
        schService = CreateService(
            schSCManager,               // SCManager database
            TEXT(SZSERVICENAME),        // name of service
            TEXT(SZSERVICEDISPLAYNAME), // name to display
            SERVICE_ALL_ACCESS,         // desired access
	    type,
	    SERVICE_AUTO_START,
            //SERVICE_ERROR_NORMAL,       // error control type
	    SERVICE_ERROR_IGNORE,
            szPath,                     // service's binary
            NULL,                       // no load ordering group
            NULL,                       // no tag identifier
            TEXT(SZDEPENDENCIES),       // dependencies
            account,                    // LocalSystem account if account==NULL
            password);
	
        if ( schService )
        {
	    if (bSetupRestart)
		Setup_Service_restart( schService );

	    WriteMPDRegistry("mpdUserCapable", bMPDUserCapable ? "yes" : "no");
	    
	    // Start the service
	    if (StartService(schService, 0, NULL))
		_tprintf(TEXT("%s installed.\n"), TEXT(SZSERVICEDISPLAYNAME) );
	    else
		_tprintf(TEXT("%s installed, but failed to start:\n%s.\n"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256) );
	    fflush(stdout);
            CloseServiceHandle(schService);
        }
        else
        {
            _tprintf(TEXT("CreateService failed:\n%s\n"), GetLastErrorText(szErr, 256));
	    fflush(stdout);
        }
	
        CloseServiceHandle(schSCManager);
    }
    else
    {
        _tprintf(TEXT("OpenSCManager failed:\n%s\n"), GetLastErrorText(szErr,256));
	fflush(stdout);
    }
}



//
//  FUNCTION: CmdRemoveService(BOOL bErrorOnNotInstalled)
//
//  PURPOSE: Stops and removes the service
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
BOOL CmdRemoveService(BOOL bErrorOnNotInstalled)
{
    BOOL        bRetVal = FALSE;
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    
    schSCManager = OpenSCManager(
	NULL,                   // machine (NULL == local)
	NULL,                   // database (NULL == default)
	SC_MANAGER_ALL_ACCESS   // access required
	);
    if ( schSCManager )
    {
        schService = OpenService(schSCManager, TEXT(SZSERVICENAME), SERVICE_ALL_ACCESS);
	
        if (schService)
        {
            // try to stop the service
            if ( ControlService( schService, SERVICE_CONTROL_STOP, &ssStatus ) )
            {
		_tprintf(TEXT("Stopping %s."), TEXT(SZSERVICEDISPLAYNAME));
		fflush(stdout);
                Sleep( 1000 );
		
                while( QueryServiceStatus( schService, &ssStatus ) )
                {
                    if ( ssStatus.dwCurrentState == SERVICE_STOP_PENDING )
                    {
			_tprintf(TEXT("."));
			fflush(stdout);
                        Sleep( 250 );
                    }
                    else
                        break;
                }
		
                if ( ssStatus.dwCurrentState == SERVICE_STOPPED )
		{
                    _tprintf(TEXT("\n%s stopped.\n"), TEXT(SZSERVICEDISPLAYNAME) );
		    fflush(stdout);
		}
                else
		{
                    _tprintf(TEXT("\n%s failed to stop.\n"), TEXT(SZSERVICEDISPLAYNAME) );
		    fflush(stdout);
		}
		
            }
	    
	    // Delete the registry entries for the service.
	    RegDeleteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\MPICH\\MPD");
	    
	    // now remove the service
            if( DeleteService(schService) )
	    {
                _tprintf(TEXT("%s removed.\n"), TEXT(SZSERVICEDISPLAYNAME) );
		fflush(stdout);
		bRetVal = TRUE;
	    }
            else
	    {
                _tprintf(TEXT("DeleteService failed:\n%s\n"), GetLastErrorText(szErr,256));
		fflush(stdout);
	    }
	    
	    
            CloseServiceHandle(schService);
        }
        else
	{
	    if (bErrorOnNotInstalled)
	    {
		_tprintf(TEXT("OpenService failed:\n%s\n"), GetLastErrorText(szErr,256));
		fflush(stdout);
	    }
	    else
	    {
		bRetVal = TRUE;
	    }
	}
	
        CloseServiceHandle(schSCManager);
    }
    else
    {
        _tprintf(TEXT("OpenSCManager failed:\n%s\n"), GetLastErrorText(szErr,256));
	fflush(stdout);
    }
    return bRetVal;
}

//
//  FUNCTION: CmdStopService()
//
//  PURPOSE: Stops the service
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdStopService()
{
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    
    schSCManager = OpenSCManager(
	NULL,                   // machine (NULL == local)
	NULL,                   // database (NULL == default)
	SC_MANAGER_ALL_ACCESS   // access required
	);
    if ( schSCManager )
    {
        schService = OpenService(schSCManager, TEXT(SZSERVICENAME), SERVICE_ALL_ACCESS);
	
        if (schService)
        {
            // try to stop the service
            if ( ControlService( schService, SERVICE_CONTROL_STOP, &ssStatus ) )
            {
                _tprintf(TEXT("Stopping %s."), TEXT(SZSERVICEDISPLAYNAME));
		fflush(stdout);
                Sleep( 1000 );
		
                while( QueryServiceStatus( schService, &ssStatus ) )
                {
                    if ( ssStatus.dwCurrentState == SERVICE_STOP_PENDING )
                    {
                        _tprintf(TEXT("."));
			fflush(stdout);
                        Sleep( 250 );
                    }
                    else
                        break;
                }
		
                if ( ssStatus.dwCurrentState == SERVICE_STOPPED )
		{
                    _tprintf(TEXT("\n%s stopped.\n"), TEXT(SZSERVICEDISPLAYNAME) );
		    fflush(stdout);
		}
                else
		{
                    _tprintf(TEXT("\n%s failed to stop.\n"), TEXT(SZSERVICEDISPLAYNAME) );
		    fflush(stdout);
		}
		
            }
	    
            CloseServiceHandle(schService);
        }
        else
	{
            _tprintf(TEXT("OpenService failed:\n%s\n"), GetLastErrorText(szErr,256));
	    fflush(stdout);
	}
	
        CloseServiceHandle(schSCManager);
    }
    else
    {
        _tprintf(TEXT("OpenSCManager failed:\n%s\n"), GetLastErrorText(szErr,256));
	fflush(stdout);
    }
}

//
//  FUNCTION: CmdStartService()
//
//  PURPOSE: Starts the service
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdStartService()
{
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    
    schSCManager = OpenSCManager(
	NULL,                   // machine (NULL == local)
	NULL,                   // database (NULL == default)
	SC_MANAGER_ALL_ACCESS   // access required
	);
    if ( schSCManager )
    {
        schService = OpenService(schSCManager, TEXT(SZSERVICENAME), SERVICE_ALL_ACCESS);

        if ( schService )
        {
	    // Start the service
	    if (StartService(schService, 0, NULL))
	    {
		_tprintf(TEXT("%s started.\n"), TEXT(SZSERVICEDISPLAYNAME) );
		fflush(stdout);
	    }
	    else
	    {
		_tprintf(TEXT("%s failed to start.\n%s.\n"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256) );
		fflush(stdout);
	    }
            CloseServiceHandle(schService);
        }
        else
        {
            _tprintf(TEXT("OpenService failed:\n%s\n"), GetLastErrorText(szErr,256));
	    fflush(stdout);
        }
	
        CloseServiceHandle(schSCManager);
    }
    else
    {
        _tprintf(TEXT("OpenSCManager failed:\n%s\n"), GetLastErrorText(szErr,256));
	fflush(stdout);
    }
}

//
//  FUNCTION: GetLastErrorText
//
//  PURPOSE: copies error message text to string
//
//  PARAMETERS:
//    lpszBuf - destination buffer
//    dwSize - size of buffer
//
//  RETURN VALUE:
//    destination buffer
//
//  COMMENTS:
//
LPTSTR GetLastErrorText( LPTSTR lpszBuf, DWORD dwSize )
{
    DWORD dwRet;
    LPTSTR lpszTemp = NULL;
    
    dwRet = FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |FORMAT_MESSAGE_ARGUMENT_ARRAY,
	NULL,
	GetLastError(),
	LANG_NEUTRAL,
	(LPTSTR)&lpszTemp,
	0,
	NULL );
    
    // supplied buffer is not long enough
    if ( !dwRet || ( (long)dwSize < (long)dwRet+14 ) )
        lpszBuf[0] = TEXT('\0');
    else
    {
        lpszTemp[lstrlen(lpszTemp)-2] = TEXT('\0');  //remove cr and newline character
        _stprintf( lpszBuf, TEXT("%s (error %d)"), lpszTemp, GetLastError() );
    }
    
    if ( lpszTemp )
        LocalFree((HLOCAL) lpszTemp );
    
    return lpszBuf;
}

///////////////////////////////////////////////////////////////////
//
//  The following code is for running the service as a console app
//


//
//  FUNCTION: ControlHandler ( DWORD dwCtrlType )
//
//  PURPOSE: Handled console control events
//
//  PARAMETERS:
//    dwCtrlType - type of control event
//
//  RETURN VALUE:
//    True - handled
//    False - unhandled
//
//  COMMENTS:
//
BOOL g_bHandlerCalled = FALSE;
BOOL WINAPI ControlHandler ( DWORD dwCtrlType )
{
    switch( dwCtrlType )
    {
    case CTRL_BREAK_EVENT:  // use Ctrl+C or Ctrl+Break to simulate
    case CTRL_C_EVENT:      // SERVICE_CONTROL_STOP in debug mode
	if (g_bHandlerCalled == TRUE)
	{
	    printf("ControlHandler: Exiting.\n");fflush(stdout);
	    ExitProcess(0);
	}
	_tprintf(TEXT("Stopping %s.\n"), TEXT(SZSERVICEDISPLAYNAME));
	fflush(stdout);
	ServiceStop();
	g_bHandlerCalled = TRUE;
	return TRUE;
	break;
	
    }
    return FALSE;
}

//
//  FUNCTION: CmdDebugService(int argc, char ** argv)
//
//  PURPOSE: Runs the service as a console application
//
//  PARAMETERS:
//    argc - number of command line arguments
//    argv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdDebugService(int argc, char ** argv)
{
    DWORD dwArgc;
    LPTSTR *lpszArgv;
    
#ifdef UNICODE
    lpszArgv = CommandLineToArgvW(GetCommandLineW(), &(dwArgc) );
#else
    dwArgc   = (DWORD) argc;
    lpszArgv = argv;
#endif
    
    _tprintf(TEXT("Starting %s.\n"), TEXT(SZSERVICEDISPLAYNAME));
    fflush(stdout);
    bDebug = TRUE;
    
    SetConsoleCtrlHandler( ControlHandler, TRUE );
    
    ServiceStart( dwArgc, lpszArgv );
}
