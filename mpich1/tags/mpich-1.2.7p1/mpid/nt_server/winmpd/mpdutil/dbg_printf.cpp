#include "mpd.h"
#include "GetStringOpt.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>
#include <service.h>

static bool g_bRedirectOutputToFile = false;
static char g_pszFileName[1024];
static HANDLE g_hRedirectMutex = CreateMutex(NULL, FALSE, NULL);

bool SetDbgRedirection(char *filename)
{
    FILE *fout;
    strcpy(g_pszFileName, filename);
    fout = fopen(g_pszFileName, "a+");
    if (fout != NULL)
    {
	fclose(fout);
	g_bRedirectOutputToFile = true;
	return true;
    }

    g_bRedirectOutputToFile = false;
    return false;
}

void CancelDbgRedirection()
{
    g_bRedirectOutputToFile = false;
}

void dbg_printf(char *str, ...)
{
    char pszStr[8192];
    char pszCheck[100];
    va_list list;
    int n, i;
    char *token;

    // Write to a temporary string
    va_start(list, str);
    vsprintf(pszStr, str, list);
    va_end(list);

    // modify the output
    if (GetStringOpt(pszStr, "p", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    if (GetStringOpt(pszStr, "pwd", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    token = strstr(pszStr, "PMI_PWD=");
    if (token != NULL)
    {
	strncpy(pszCheck, &token[8], 100);
	pszCheck[99] = '\0';
	token = strtok(pszCheck, " '|\n");
	n = strlen(pszCheck);
	token = strstr(pszStr, "PMI_PWD=");
	token = &token[8];
	if (n > 0)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    if (GetStringOpt(pszStr, "password", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    // optionally print the output to a file
    if (g_bRedirectOutputToFile)
    {
	if (WaitForSingleObject(g_hRedirectMutex, 2500) == WAIT_OBJECT_0)
	{
	    FILE *fout;
	    fout = fopen(g_pszFileName, "a+");
	    if (fout)
	    {
		fprintf(fout, "%s", pszStr);
		fclose(fout);
	    }
	    ReleaseMutex(g_hRedirectMutex);
	}
    }

    // print the modified string
    printf("%s", pszStr);
    fflush(stdout);
}

static HANDLE g_hColorOutputMutex = CreateMutex(NULL, FALSE, NULL);
void dbg_printf_color(unsigned short color, char *str, ...)
{
    char pszStr[8192];
    char pszCheck[100];
    va_list list;
    int n, i;
    char *token;
    static HANDLE hOut;
    static CONSOLE_SCREEN_BUFFER_INFO info;
    static bFirst = true;
    DWORD num_written;

    // Write to a temporary string
    va_start(list, str);
    vsprintf(pszStr, str, list);
    va_end(list);

    // modify the output
    if (GetStringOpt(pszStr, "p", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    if (GetStringOpt(pszStr, "pwd", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    token = strstr(pszStr, "PMI_PWD=");
    if (token != NULL)
    {
	strncpy(pszCheck, &token[8], 100);
	pszCheck[99] = '\0';
	token = strtok(pszCheck, " '|\n");
	n = strlen(pszCheck);
	token = strstr(pszStr, "PMI_PWD=");
	token = &token[8];
	if (n > 0)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    if (GetStringOpt(pszStr, "password", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    // print the modified string
    WaitForSingleObject(g_hColorOutputMutex, INFINITE);

    if (bFirst)
    {
	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE)
	{
	    printf("Unable to get the standard output handle, error %d\n", GetLastError());
	    printf("%s", pszStr);
	    fflush(stdout);
	    ReleaseMutex(g_hColorOutputMutex);
	    return;
	}
	GetConsoleScreenBufferInfo(hOut, &info);
	bFirst = false;
    }

    if (!SetConsoleTextAttribute(hOut, color))
    {
	if (GetLastError() == ERROR_INVALID_HANDLE)
	{
	    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	    if (hOut == INVALID_HANDLE_VALUE)
	    {
		printf("Unable to get the standard output handle, error %d\n", GetLastError());
		printf("%s", pszStr);
		fflush(stdout);
		ReleaseMutex(g_hColorOutputMutex);
		return;
	    }
	}
	else
	{
	    printf("SetConsoleTextAttribute failed, error %d\n", GetLastError());
	    fflush(stdout);
	}
    }
    if (WriteFile(hOut, pszStr, strlen(pszStr), &num_written, NULL))
	FlushFileBuffers(hOut);
    else
    {
	printf("%s", pszStr);
	fflush(stdout);
    }
    SetConsoleTextAttribute(hOut, info.wAttributes);

    ReleaseMutex(g_hColorOutputMutex);

    // optionally print the output to a file
    if (g_bRedirectOutputToFile)
    {
	if (WaitForSingleObject(g_hRedirectMutex, 2500) == WAIT_OBJECT_0)
	{
	    FILE *fout;
	    fout = fopen(g_pszFileName, "a+");
	    if (fout)
	    {
		fprintf(fout, "%s", pszStr);
		fclose(fout);
	    }
	    ReleaseMutex(g_hRedirectMutex);
	}
    }
}

void log_error(char *lpszMsg)
{
    char    szMsg[256];
    HANDLE  hEventSource;
    char   *lpszStrings[2];
    
    // Use event logging to log the error.
    //
    hEventSource = RegisterEventSource(NULL, SZSERVICENAME);
    
    sprintf(szMsg, "%s error", SZSERVICENAME);
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
	
	DeregisterEventSource(hEventSource);
    }
}

void err_printf(char *str, ...)
{
    char pszStr[8192];
    char pszCheck[100];
    va_list list;
    int n, i;
    char *token;

    // Write to a temporary string
    va_start(list, str);
    vsprintf(pszStr, str, list);
    va_end(list);

    // modify the output
    if (GetStringOpt(pszStr, "p", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    if (GetStringOpt(pszStr, "pwd", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    token = strstr(pszStr, "PMI_PWD=");
    if (token != NULL)
    {
	strncpy(pszCheck, &token[8], 100);
	pszCheck[99] = '\0';
	token = strtok(pszCheck, " '|\n");
	n = strlen(pszCheck);
	token = strstr(pszStr, "PMI_PWD=");
	token = &token[8];
	if (n > 0)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    if (GetStringOpt(pszStr, "password", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    // optionally print the output to a file
    if (g_bRedirectOutputToFile)
    {
	if (WaitForSingleObject(g_hRedirectMutex, 2500) == WAIT_OBJECT_0)
	{
	    FILE *fout;
	    fout = fopen(g_pszFileName, "a+");
	    if (fout)
	    {
		fprintf(fout, "%s", pszStr);
		fclose(fout);
	    }
	    ReleaseMutex(g_hRedirectMutex);
	}
    }

    // print error
    //fprintf(stderr, "----- ERROR --------\n");
    fprintf(stderr, "%s", pszStr);
    log_error(pszStr);
    /*
    for (int i=0; i<g_call_index; i++) 
    { 
	printf("%s\n", g_call_stack[i]); 
    }
    */
    //fprintf(stderr, "--------------------\n");
    fflush(stderr); 
}

void log_warning(char *lpszMsg)
{
    char    szMsg[256];
    HANDLE  hEventSource;
    char   *lpszStrings[2];
    
    // Use event logging to log the error.
    //
    hEventSource = RegisterEventSource(NULL, SZSERVICENAME);
    
    sprintf(szMsg, "%s error", SZSERVICENAME);
    lpszStrings[0] = szMsg;
    lpszStrings[1] = lpszMsg;
    
    if (hEventSource != NULL) {
	ReportEvent(hEventSource, // handle of event source
	    EVENTLOG_WARNING_TYPE,  // event type
	    0,                    // event category
	    0,                    // event ID
	    NULL,                 // current user's SID
	    2,                    // strings in lpszStrings
	    0,                    // no bytes of raw data
	    (LPCTSTR*)lpszStrings,// array of error strings
	    NULL);                // no raw data
	
	DeregisterEventSource(hEventSource);
    }
}

void warning_printf(char *str, ...)
{
    char pszStr[8192];
    char pszCheck[100];
    va_list list;
    int n, i;
    char *token;

    // Write to a temporary string
    va_start(list, str);
    vsprintf(pszStr, str, list);
    va_end(list);

    // modify the output
    if (GetStringOpt(pszStr, "p", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    if (GetStringOpt(pszStr, "pwd", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    token = strstr(pszStr, "PMI_PWD=");
    if (token != NULL)
    {
	strncpy(pszCheck, &token[8], 100);
	pszCheck[99] = '\0';
	token = strtok(pszCheck, " '|\n");
	n = strlen(pszCheck);
	token = strstr(pszStr, "PMI_PWD=");
	token = &token[8];
	if (n > 0)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    if (GetStringOpt(pszStr, "password", pszCheck))
    {
	token = strstr(pszStr, pszCheck);
	n = strlen(pszCheck);
	if (token != NULL)
	{
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    if (pszCheck[n-1] == '\r' || pszCheck[n-1] == '\n')
		n--;
	    for (i=0; i<n; i++)
		token[i] = '*';
	}
    }

    // optionally print the output to a file
    if (g_bRedirectOutputToFile)
    {
	if (WaitForSingleObject(g_hRedirectMutex, 2500) == WAIT_OBJECT_0)
	{
	    FILE *fout;
	    fout = fopen(g_pszFileName, "a+");
	    if (fout)
	    {
		fprintf(fout, "%s", pszStr);
		fclose(fout);
	    }
	    ReleaseMutex(g_hRedirectMutex);
	}
    }

    // print error
    //fprintf(stderr, "----- ERROR --------\n");
    fprintf(stderr, "%s", pszStr);
    log_warning(pszStr);
    /*
    for (int i=0; i<g_call_index; i++) 
    { 
	printf("%s\n", g_call_stack[i]); 
    }
    */
    //fprintf(stderr, "--------------------\n");
    fflush(stderr); 
}
