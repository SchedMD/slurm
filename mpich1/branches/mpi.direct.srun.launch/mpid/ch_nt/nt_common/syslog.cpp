#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN 
#include <windows.h>

#include "syslog.h"

#ifndef _MAX_PATH
#define _MAX_PATH 260
#endif

#define LOG_MSG      ((DWORD)0x00000001L)
#define APP_LOG_PATH "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\"
#define SYS_LOG_PATH "SYSTEM\\CurrentControlSet\\Services\\EventLog\\System\\"

_declspec(thread) HANDLE log = NULL;

int add_source(char* source, int facility);
int add_event(int priority, char* logmsg);

int openlog(char* source, int facility) 
{
    int status = TRUE;
    
#ifdef DEBUG_SYSLOG
    printf("openlog source %s facillity %d\n", source, facility);
#endif
    if (log != NULL)
    {
	status = closelog();
    }
    if (status == TRUE)
    {
	return add_source(source, facility);
    }
    else
    {
	return FALSE;
    }
}

int syslog(int priority, char* logformat,...) 
{
    _declspec(thread) static char logmsg[MAX_LOG_MSG_SIZE]="";
    va_list args;
    int status = TRUE;
    
    va_start(args, logformat);
    _vsnprintf(logmsg, MAX_LOG_MSG_SIZE, logformat, args);
    logmsg[MAX_LOG_MSG_SIZE-1] = 0; 
    va_end(args);
    
#ifdef DEBUG_SYSLOG
    printf("syslog priority %d logmsg %s\n", priority, logmsg);
#endif
    if (log == NULL)
    {
	status = openlog("mpich_app", LOG_APP);
    }
    if (status == TRUE)
    {
	return add_event(priority, logmsg);
    }
    else
    {
	return FALSE;
    }
}

int closelog(void) 
{
    int status;
    if (log != NULL)
    {
	status = DeregisterEventSource(log);
	log = NULL;
	return status;
    }
    else
    {
	return TRUE;
    }
}

int add_source(char* source, int facility)
{
    _declspec(thread) static char reg_key[_MAX_PATH]="";
    _declspec(thread) static char log_msg_dll[_MAX_PATH]="%SystemRoot%\\system32\\mpicherr.dll";
    HKEY hkey;
    DWORD data;
    long status;
    
    if (facility == LOG_APP)
    { 
	strncpy(reg_key, APP_LOG_PATH, _MAX_PATH); 
    }
    else
    {
	if (facility == LOG_SYS)
	{
	    strncpy(reg_key, SYS_LOG_PATH, _MAX_PATH); 
	}
	else 
	{
#ifdef DEBUG_SYSLOG
	    printf("invalid facility value of %d\n", facility);
#endif
	    return FALSE;
	}
    }
    strncat(reg_key, source, _MAX_PATH-strlen(reg_key)-1);
#ifdef DEBUG_SYSLOG
    printf("opening key %s--\n", reg_key);
#endif
    
    status = RegCreateKeyEx(HKEY_LOCAL_MACHINE,
	reg_key,
	0,
	"\0",
	REG_OPTION_NON_VOLATILE,
	KEY_ALL_ACCESS | KEY_WRITE,
	NULL,
	&hkey,
	&data);
    if (status != ERROR_SUCCESS)
    {
#ifdef DEBUG_SYSLOG
	printf("registry error\n");
#endif
	return FALSE;
    }
    
    if (data == REG_CREATED_NEW_KEY)
    {
#ifdef DEBUG_SYSLOG
	printf("new event source\n");
#endif
	status = RegSetValueEx(hkey,
	    "EventMessageFile",
	    0,
	    REG_EXPAND_SZ,
	    (LPBYTE)log_msg_dll,
	    strlen(log_msg_dll) + 1);
	if (status != ERROR_SUCCESS)
	{
	    RegCloseKey(hkey);
#ifdef DEBUG_SYSLOG
	    printf("registry error\n");
#endif
	    return FALSE;
	}
	
	data = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;
	
	status = RegSetValueEx(hkey,
	    "TypesSupported",
	    0,
	    REG_DWORD,
	    (LPBYTE)&data,
	    sizeof(DWORD));
	if (status != ERROR_SUCCESS)
	{
	    RegCloseKey(hkey);
#ifdef DEBUG_SYSLOG
	    printf("registry error\n");
#endif
	    return FALSE;
	}
	
    }
    
    RegCloseKey(hkey);
    
    log = RegisterEventSource(NULL, source);
    if (log == NULL)
    {
	return FALSE;
    }
    
    return TRUE;
}

int add_event(int priority, char* logmsg)
{
    int result;
    WORD event_type;
    
    switch (priority)
    {
    case LOG_INFO:
	event_type = EVENTLOG_INFORMATION_TYPE; 
	break;
    case LOG_ERR: 
	event_type = EVENTLOG_ERROR_TYPE; 
	break;
    case LOG_WARNING:
	event_type = EVENTLOG_WARNING_TYPE;
	break;
    default: 
	{
#ifdef DEBUG_SYSLOG
	    printf("invalid priority value\n"); 
#endif 
	    return FALSE;
	}
    }
    
    result = ReportEvent(log,
	event_type,
	0,
	LOG_MSG,
	NULL,
	1,
	0,
	(const char **)&logmsg,
	NULL);
    
    return result;
}
