#include "stdafx.h"
#include "RemoteShellLog.h"
#include <stdio.h>
#include <time.h>
#include "syslog.h"

//#define CUSTOM_DEBUG_OUTPUT
#undef CUSTOM_DEBUG_OUTPUT

#define LOCAL_BUFFER_SIZE 1024

// Function name	: LogMsg
// Description	    : 
// Return type		: void 
// Argument         : LPCTSTR pFormat
// Argument         : ...
void LogMsg(LPCTSTR pFormat, ...)
{
	TCHAR chMsg[LOCAL_BUFFER_SIZE];
	va_list pArg;
	
	va_start(pArg, pFormat);
	_vstprintf(chMsg, pFormat, pArg);
	va_end(pArg);

	openlog("RemoteShell", LOG_APP);
#ifdef UNICODE
	char msg[LOCAL_BUFFER_SIZE];
	wcstombs(msg, chMsg, wcslen(chMsg)+1);
	syslog(LOG_INFO, msg);
#else
	syslog(LOG_INFO, chMsg);
#endif
	closelog();
}

// Note: I could overload LogMsg to take a WCHAR* as the first argument but
//       this way I am less likely to match the WCHAR* version with a char* argument
//       or vice versa.  It would also conflict with the UNICODE build.
// Function name	: LogWMsg
// Description	    : 
// Return type		: void 
// Argument         : WCHAR* pFormat
// Argument         : ...
void LogWMsg(WCHAR* pFormat, ...)
{
    WCHAR    wchMsg[LOCAL_BUFFER_SIZE];
    va_list pArg;

    va_start(pArg, pFormat);
    vswprintf(wchMsg, pFormat, pArg);
    va_end(pArg);

	openlog("RemoteShell", LOG_APP);
	char msg[LOCAL_BUFFER_SIZE];
	wcstombs(msg, wchMsg, wcslen(wchMsg)+1);
	syslog(LOG_INFO, msg);
	closelog();
}

// Function name	: DLogMsg
// Description	    : 
// Return type		: void 
// Argument         : LPCTSTR pFormat
// Argument         : ...
void DLogMsg(LPCTSTR pFormat, ...)
{
#ifdef CUSTOM_DEBUG_OUTPUT
	TCHAR chMsg[LOCAL_BUFFER_SIZE];
	va_list pArg;
	
	va_start(pArg, pFormat);
	_vstprintf(chMsg, pFormat, pArg);
	va_end(pArg);
	
	openlog("RemoteShell", LOG_APP);
#ifdef UNICODE
	char msg[LOCAL_BUFFER_SIZE];
	wcstombs(msg, chMsg, wcslen(chMsg)+1);
	syslog(LOG_INFO, msg);
#else
	syslog(LOG_INFO, chMsg);
#endif
	closelog();
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
    WCHAR    wchMsg[LOCAL_BUFFER_SIZE];
    va_list pArg;

    va_start(pArg, pFormat);
    vswprintf(wchMsg, pFormat, pArg);
    va_end(pArg);

	openlog("RemoteShell", LOG_APP);
	char msg[LOCAL_BUFFER_SIZE];
	wcstombs(msg, wchMsg, wcslen(wchMsg)+1);
	syslog(LOG_INFO, msg);
	closelog();
#endif
}
