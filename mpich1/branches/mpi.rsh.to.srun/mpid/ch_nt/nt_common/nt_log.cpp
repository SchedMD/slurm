//#include "stdafx.h"
#include "nt_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "syslog.h"
#include <wchar.h>

/*
#define CUSTOM_DEBUG_OUTPUT
/*/
#undef CUSTOM_DEBUG_OUTPUT
//*/

// For now there must be a temp directory
// In the future the log will be stored in the MPICH_ROOT directory
// or messages will be logged with the OS
//#define NT_TCP_LOG_FILENAME "\\temp\\nt_tcp_shm.log"

//HANDLE g_hLogMutex = CreateMutex(NULL, FALSE, "nt_tcp_shm_mutex");

// Function name	: ClearLog
// Description	    : 
// Return type		: void 
void ClearLog()
{
	//DeleteFile(NT_TCP_LOG_FILENAME);
}

// Function name	: LogMsg
// Description	    : 
// Return type		: void 
// Argument         : LPCSTR pFormat
// Argument         : ...
void LogMsg(LPCSTR pFormat, ...)
{
	TCHAR chMsg[1024];
	va_list pArg;
	
	va_start(pArg, pFormat);
	vsprintf(chMsg, pFormat, pArg);
	va_end(pArg);
	
	openlog("mpich", LOG_APP);
	syslog(LOG_INFO, chMsg);
	closelog();
	/*
	WaitForSingleObject(g_hLogMutex, INFINITE);
	FILE *fout = fopen(NT_TCP_LOG_FILENAME, "a");
	if (fout != NULL)
	{
		fprintf(fout, "%4d :%7u ", GetCurrentThreadId(), clock());
		fprintf(fout, "%s", chMsg);
		fclose(fout);
	}
	ReleaseMutex(g_hLogMutex);
	//*/
}

// Note: I could overload LogMsg to take a WCHAR* as the first argument but
//       this way I am less likely to match the WCHAR* version with a char* argument
//       or vice versa
// Function name	: LogWMsg
// Description	    : 
// Return type		: void 
// Argument         : WCHAR* pFormat
// Argument         : ...
void LogWMsg(WCHAR* pFormat, ...)
{
	char chMsg[1024];
    WCHAR    wchMsg[1024];
    va_list pArg;

    va_start(pArg, pFormat);
    vswprintf(wchMsg, pFormat, pArg);
    va_end(pArg);

	wcstombs(chMsg, wchMsg, wcslen(wchMsg)+1);
	openlog("mpich", LOG_APP);
	syslog(LOG_INFO, chMsg);
	closelog();
	/*
	WaitForSingleObject(g_hLogMutex, INFINITE);
	FILE *fout = fopen(NT_TCP_LOG_FILENAME, "a");
	if (fout != NULL)
	{
		fprintf(fout, "%4d :%7u ", GetCurrentThreadId(), clock());
		fwprintf(fout, L"%s", wchMsg);
		fclose(fout);
	}
	ReleaseMutex(g_hLogMutex);
	//*/
}

// Function name	: DLogMsg
// Description	    : 
// Return type		: void 
// Argument         : LPCTSTR pFormat
// Argument         : ...
void DLogMsg(LPCTSTR pFormat, ...)
{
#ifdef CUSTOM_DEBUG_OUTPUT
	TCHAR chMsg[1024];
	va_list pArg;
	
	va_start(pArg, pFormat);
	_vstprintf(chMsg, pFormat, pArg);
	va_end(pArg);
	
	openlog("mpich", LOG_APP);
	syslog(LOG_INFO, chMsg);
	closelog();
	/*
	WaitForSingleObject(g_hLogMutex, INFINITE);
	FILE *fout = fopen(NT_TCP_LOG_FILENAME, "a");
	if (fout != NULL)
	{
		fprintf(fout, "%4d :%7u ", GetCurrentThreadId(), clock());
		fprintf(fout, "%s", chMsg);
		fclose(fout);
	}
	ReleaseMutex(g_hLogMutex);
	//*/
#endif
}

// Function name	: DLogWMsg
// Description	    : 
// Return type		: void 
// Argument         : WCHAR* pFormat
// Argument         : ...
void DLogWMsg(WCHAR* pFormat, ...)
{
#ifdef CUSTOM_DEBUG_OUTPUT
	char chMsg[1024];
    WCHAR    wchMsg[1024];
    va_list pArg;

    va_start(pArg, pFormat);
    vswprintf(wchMsg, pFormat, pArg);
    va_end(pArg);

	wcstombs(chMsg, wchMsg, wcslen(wchMsg)+1);
	openlog("mpich", LOG_APP);
	syslog(LOG_INFO, chMsg);
	closelog();
	/*
	WaitForSingleObject(g_hLogMutex, INFINITE);
	FILE *fout = fopen(NT_TCP_LOG_FILENAME, "a");
	if (fout != NULL)
	{
		fprintf(fout, "%4d :%7u ", GetCurrentThreadId(), clock());
		fwprintf(fout, L"%s", wchMsg);
		fclose(fout);
	}
	ReleaseMutex(g_hLogMutex);
	//*/
#endif
}
