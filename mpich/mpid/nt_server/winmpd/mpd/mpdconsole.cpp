#include "mpdimpl.h"
#include "mpdutil.h"
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include "Service.h"
#include "database.h"
#include "GetStringOpt.h"
#include "Translate_Error.h"

#define TIMESTAMP_LENGTH 256

enum LaunchStatus
{
    LAUNCH_SUCCESS,
    LAUNCH_PENDING,
    LAUNCH_FAIL,
    LAUNCH_EXITED,
    LAUNCH_INVALID
};

struct LaunchStateStruct
{
    LaunchStateStruct();
    ~LaunchStateStruct();
    int nId;
    int nBfd;
    int nPid;
    LaunchStatus nStatus;
    char pszError[256];
    int nExitCode;
    bool bPidRequested;
    bool bExitStateRequested;
    char pszHost[MAX_HOST_LENGTH];
    char timestamp[TIMESTAMP_LENGTH];
    bool bMPIFinalized;
    HANDLE hMutex;
    LaunchStateStruct *pNext;
};

LaunchStateStruct::LaunchStateStruct()
{
    hMutex = CreateMutex(NULL, FALSE, NULL);
    nId = 0;
    nBfd = INVALID_SOCKET;
    nPid = -1;
    nStatus = LAUNCH_INVALID;
    pszError[0] = '\0';
    nExitCode = 0;
    bPidRequested = false;
    bExitStateRequested = false;
    pszHost[0] = '\0';
    timestamp[0] = '\0';
    bMPIFinalized = false;
    pNext = NULL;
}

LaunchStateStruct::~LaunchStateStruct()
{
    CloseHandle(hMutex);
}

LONG g_nCurrentLaunchId = 0;
LaunchStateStruct *g_pLaunchList = NULL;

static void LaunchToString(LaunchStateStruct *p, char *pszStr, int length)
{
    if (!snprintf_update(pszStr, length, "LAUNCH STRUCT:\n"))
	return;

    if (!snprintf_update(pszStr, length, " id: %d\n pid: %d\n host: %s\n sock: %d\n exitcode: %d\n status: ", 
	p->nId, p->nPid, p->pszHost, p->nBfd, p->nExitCode))
	return;
    switch (p->nStatus)
    {
    case LAUNCH_SUCCESS:
	if (!snprintf_update(pszStr, length, "LAUNCH_SUCCESS\n"))
	    return;
	break;
    case LAUNCH_PENDING:
	if (!snprintf_update(pszStr, length, "LAUNCH_PENDING\n"))
	    return;
	break;
    case LAUNCH_FAIL:
	if (!snprintf_update(pszStr, length, "LAUNCH_FAIL\n"))
	    return;
	break;
    case LAUNCH_EXITED:
	if (!snprintf_update(pszStr, length, "LAUNCH_EXITED\n"))
	    return;
	break;
    case LAUNCH_INVALID:
	if (!snprintf_update(pszStr, length, "LAUNCH_INVALID\n"))
	    return;
	break;
    default:
	if (!snprintf_update(pszStr, length, "unknown - %d\n", p->nStatus))
	    return;
	break;
    }
    if (p->bPidRequested)
    {
	if (!snprintf_update(pszStr, length, " bPidRequested = true\n"))
	    return;
    }
    if (p->bExitStateRequested)
    {
	if (!snprintf_update(pszStr, length, " bExitStateRequested = true\n"))
	    return;
    }
    if (strlen(p->pszError))
    {
	if (!snprintf_update(pszStr, length, " error: %s\n", p->pszError))
	    return;
    }
    if (strlen(p->timestamp))
    {
	if (!snprintf_update(pszStr, length, " timestamp: %s\n", p->timestamp))
	    return;
    }
}

void statLaunchList(char *pszOutput, int length)
{
    LaunchStateStruct *p;

    *pszOutput = '\0';
    length--; // leave room for the null character

    if (g_pLaunchList == NULL)
	return;

    p = g_pLaunchList;
    while (p)
    {
	LaunchToString(p, pszOutput, length);
	length = length - strlen(pszOutput);
	pszOutput = &pszOutput[strlen(pszOutput)];
	p = p->pNext;
    }
}

LaunchStateStruct* GetLaunchStruct(int nId)
{
    LaunchStateStruct *p = g_pLaunchList;
    while (p)
    {
	if (p->nId == nId)
	    return p;
	p = p->pNext;
    }
    return NULL;
}

int ConsoleGetExitCode(int nPid)
{
    LaunchStateStruct *pLS = GetLaunchStruct(nPid);
    if (pLS != NULL)
    {
	if (pLS->nStatus == LAUNCH_EXITED)
	{
	    return pLS->nExitCode;
	}
	return -1;
    }
    return -2;
}

bool RemoveStateStruct(LaunchStateStruct *p)
{
    bool bReturn;
    LaunchStateStruct *pTrailer = g_pLaunchList;

    // Remove p from the list
    if (p == NULL)
	return true;

    if (p == g_pLaunchList)
    {
	g_pLaunchList = g_pLaunchList->pNext;
	bReturn = true;
    }
    else
    {
	while (pTrailer && pTrailer->pNext != p)
	    pTrailer = pTrailer->pNext;
	if (pTrailer)
	{
	    pTrailer->pNext = p->pNext;
	    bReturn = true;
	}
	else
	{
	    bReturn = false;
	}
    }

    //dbg_printf("removing LaunchStateStruct[%d]\n", p->nId);
    // free the structure
    delete p;
    return bReturn;
}

void SavePid(int nId, int nPid)
{
    LaunchStateStruct *p;
    
    p = GetLaunchStruct(nId);

    if (p != NULL)
    {
	WaitForSingleObject(p->hMutex, INFINITE);
	p->nStatus = LAUNCH_SUCCESS;
	p->nPid = nPid;
	strcpy(p->pszError, "ERROR_SUCCESS");
	ReleaseMutex(p->hMutex);
	if (p->bPidRequested)
	{
	    char pszStr[20];
	    _snprintf(pszStr, 20, "%d", p->nPid);
	    easy_send(p->nBfd, pszStr, strlen(pszStr)+1);
	    p->bPidRequested = false;
	}
    }
}

void SaveError(int nId, char *pszError)
{
    LaunchStateStruct *p;
    
    p = GetLaunchStruct(nId);
    if (p != NULL)
    {
	WaitForSingleObject(p->hMutex, INFINITE);
	p->nStatus = LAUNCH_FAIL;
	strncpy(p->pszError, pszError, 256);
	ReleaseMutex(p->hMutex);
	if (p->bPidRequested)
	{
	    easy_send(p->nBfd, "-1", strlen("-1")+1);
	    p->bPidRequested = false;
	}
	if (p->bExitStateRequested)
	{
	    InformBarriers(nId, p->nExitCode);
	    easy_send(p->nBfd, "FAIL", strlen("FAIL")+1);
	    p->bExitStateRequested = false;
	}
    }
}

void SaveTimestamp(int nId, char *timestamp)
{
    LaunchStateStruct *p;
    
    p = GetLaunchStruct(nId);
    if (p != NULL)
    {
	WaitForSingleObject(p->hMutex, INFINITE);
	strncpy(p->timestamp, timestamp, TIMESTAMP_LENGTH);
	p->timestamp[TIMESTAMP_LENGTH-1] = '\0';
	ReleaseMutex(p->hMutex);
    }
}

bool SaveMPIFinalized(int nId)
{
    LaunchStateStruct *p;

    p = GetLaunchStruct(nId);
    if (p != NULL)
    {
	dbg_printf("setting mpifinalized for launchid %d\n", nId);
	p->bMPIFinalized = true;
	return true;
    }
    return false;
}

void SaveExitCode(int nId, int nExitCode)
{
    char pszStr[30];
    LaunchStateStruct *p;
    
    p = GetLaunchStruct(nId);
    if (p != NULL)
    {
	WaitForSingleObject(p->hMutex, INFINITE);
	p->nStatus = LAUNCH_EXITED;
	p->nExitCode = nExitCode;
	ReleaseMutex(p->hMutex);
	InformBarriers(nId, nExitCode);
	if (p->bExitStateRequested)
	{
	    _snprintf(pszStr, 30, "%d:%d", nExitCode, p->nPid);
	    easy_send(p->nBfd, pszStr, strlen(pszStr)+1);
	    p->bExitStateRequested = false;
	    dbg_printf("SaveExitCode:Sending exit code %d:%d:%s\n", nId, nExitCode, p->timestamp);
	}
    }
    else
    {
	err_printf("ERROR: Saving exit code for launchid %d failed\n", nId);
    }
}

void GetNameKeyValue(char *str, char *name, char *key, char *value)
{
    bool bName = false;
    bool bKey = false;
    bool bValue = false;

    //dbg_printf("GetNameKeyValue(");

    if ((name != NULL) && (!GetStringOpt(str, "name", name)))
    {
	bName = true;
    }
    /*
    else
    {
	if (name != NULL)
	{
	    dbg_printf("name='%s' ", name);
	}
    }
    */
    if ((key != NULL) && (!GetStringOpt(str, "key", key)))
    {
	bKey = true;
    }
    /*
    else
    {
	if (key != NULL)
	{
	    dbg_printf("key='%s' ", key);
	}
    }
    */
    if ((value != NULL) && (!GetStringOpt(str, "value", value)))
    {
	bValue = true;
    }
    /*
    else
    {
	if (value != NULL)
	{
	    dbg_printf("value='%s'", value);
	}
    }
    */

    char str2[MAX_CMD_LENGTH];
    char *token;
    if (bName)
    {
	strncpy(str2, str, MAX_CMD_LENGTH);
	str2[MAX_CMD_LENGTH-1] = '\0';
	token = strtok(str2, ":");
	if (token != NULL)
	{
	    strcpy(name, token);
	    //dbg_printf("name='%s' ", name);
	    if (bKey)
	    {
		token = strtok(NULL, ":");
		if (token != NULL)
		{
		    strcpy(key, token);
		    //dbg_printf("key='%s' ", key);
		    if (bValue)
		    {
			token = strtok(NULL, ":");
			if (token != NULL)
			{
			    strcpy(value, token);
			    //dbg_printf("value='%s'", value);
			}
		    }
		}
	    }
	}
    }
    else if (bKey)
    {
	strncpy(str2, str, MAX_CMD_LENGTH);
	str2[MAX_CMD_LENGTH-1] = '\0';
	token = strtok(str2, ":");
	if (token != NULL)
	{
	    strcpy(key, token);
	    //dbg_printf("key='%s' ", key);
	    if (bValue)
	    {
		token = strtok(NULL, ":");
		if (token != NULL)
		{
		    strcpy(value, token);
		    //dbg_printf("value='%s'", value);
		}
	    }
	}
    }
    else if (bValue)
    {
	strcpy(value, str);
	//dbg_printf("value='%s'", value);
    }

    //dbg_printf(")\n");

    //dbg_printf("GetNameKeyValue('%s' '%s' '%s')\n", name ? name : "NULL", key ? key : "NULL", value ? value : "NULL");
}

static void ParseAccountDomain(char *DomainAccount, char *tAccount, char *tDomain)
{
    char *pCh, *pCh2;
    
    pCh = DomainAccount;
    pCh2 = tDomain;
    while ((*pCh != '\\') && (*pCh != '\0'))
    {
	*pCh2 = *pCh;
	pCh++;
	pCh2++;
    }
    if (*pCh == '\\')
    {
	pCh++;
	strcpy(tAccount, pCh);
	*pCh2 = '\0';
    }
    else
    {
	strcpy(tAccount, DomainAccount);
	tDomain[0] = '\0';
    }
}

HANDLE BecomeUser(char *domainaccount, char *password, int *pnError)
{
    HANDLE hUser;
    char account[50], domain[50], *pszDomain;
    ParseAccountDomain(domainaccount, account, domain);
    if (strlen(domain) < 1)
	pszDomain = NULL;
    else
	pszDomain = domain;

    WaitForSingleObject(g_hLaunchMutex, 10000);

    if (!LogonUser(
	account,
	pszDomain, 
	password,
	LOGON32_LOGON_INTERACTIVE, 
	//LOGON32_LOGON_BATCH,  // quicker?
	LOGON32_PROVIDER_DEFAULT, 
	&hUser))
    {
	*pnError = GetLastError();
	ReleaseMutex(g_hLaunchMutex);
	return (HANDLE)-1;
    }

    if (!ImpersonateLoggedOnUser(hUser))
    {
	*pnError = GetLastError();
	CloseHandle(hUser);
	ReleaseMutex(g_hLaunchMutex);
	if (!g_bSingleUser)
	    RevertToSelf();
	return (HANDLE)-1;
    }

    ReleaseMutex(g_hLaunchMutex);

    return hUser;
}

FILE* CreateCheckFile(char *pszFullFileName, bool bReplace, bool bCreateDir, char *pszError)
{
    char pszPath[MAX_PATH];
    char *pszFileName, *p1, *p2;
    FILE *fout;

    if (bCreateDir)
    {
	if (!TryCreateDir(pszFullFileName, pszError))
	    return NULL;
    }
    strncpy(pszPath, pszFullFileName, MAX_PATH);
    p1 = strrchr(pszPath, '\\');
    p2 = strrchr(pszPath, '/');
    pszFileName = max(p1, p2);
    *pszFileName = '\0';
    pszFileName++;
    //dbg_printf("pszPath: '%s', pszFileName: '%s'\n", pszPath, pszFileName);
    if (!SetCurrentDirectory(pszPath))
    {
	sprintf(pszError, "SetCurrentDirectory(%s) failed, error %d", pszPath, GetLastError());
	return NULL;
    }

    if (bReplace)
    {
	fout = fopen(pszFileName, "wb");
    }
    else
    {
	fout = fopen(pszFileName, "r");
	if (fout != NULL)
	{
	    sprintf(pszError, "file exists");
	    fclose(fout);
	    return NULL;
	}
	fclose(fout);
	fout = fopen(pszFileName, "wb");
    }
    if (fout == NULL)
    {
	sprintf(pszError, "fopen failed, error %d", GetLastError());
	return NULL;
    }

    return fout;
}

HANDLE ParseBecomeUser(MPD_Context *p, char *pszInputStr, bool bMinusOneOnError)
{
    int nError;
    HANDLE hUser = NULL;

    if (!g_bSingleUser)
    {
	if (!p->bFileInitCalled)
	{
	    if (bMinusOneOnError)
		WriteString(p->sock, "-1");
	    WriteString(p->sock, "ERROR - no account and password provided");
	    return (HANDLE)-1;
	}
	hUser = BecomeUser(p->pszFileAccount, p->pszFilePassword, &nError);
	if (hUser == (HANDLE)-1)
	{
	    char pszStr[256];
	    Translate_Error(nError, pszStr, "ERROR - ");
	    if (bMinusOneOnError)
		WriteString(p->sock, "-1");
	    WriteString(p->sock, pszStr);
	    return (HANDLE)-1;
	}
    }
    return hUser;
}

void LoseTheUser(HANDLE hUser)
{
    if (!g_bSingleUser)
    {
	RevertToSelf();
	if (hUser != NULL)
	    CloseHandle(hUser);
    }
}

static void ConsolePutFile(SOCKET sock, char *pszInputStr)
{
    char pszFileName[MAX_PATH];
    int nLength;
    int nNumRead;
    FILE *fin;
    char pBuffer[TRANSFER_BUFFER_SIZE];
    char pszStr[256];
    int nError;

    // Get the file name
    if (!GetStringOpt(pszInputStr, "name", pszFileName))
    {
	WriteString(sock, "-1");
	WriteString(sock, "ERROR - no file name provided");
	return;
    }

    // Open the file
    fin = fopen(pszFileName, "rb");
    if (fin == NULL)
    {
	nError = GetLastError();
	Translate_Error(nError, pszStr, "ERROR - fopen failed, ");
	WriteString(sock, "-1");
	WriteString(sock, pszStr);
	return;
    }

    // Send the size
    fseek(fin, 0, SEEK_END);
    nLength = ftell(fin);
    if (nLength == -1)
    {
	nError = GetLastError();
	Translate_Error(nError, pszStr, "ERROR - Unable to determine the size of the file, ");
	WriteString(sock, "-1");
	WriteString(sock, pszStr);
	return;
    }
    sprintf(pszStr, "%d", nLength);
    WriteString(sock, pszStr);

    // Rewind back to the beginning
    fseek(fin, 0, SEEK_SET);

    // Send the data
    while (nLength)
    {
	nNumRead = min(nLength, TRANSFER_BUFFER_SIZE);
	nNumRead = fread(pBuffer, 1, nNumRead, fin);
	if (nNumRead < 1)
	{
	    err_printf("fread failed, %d\n", ferror(fin));
	    fclose(fin);
	    return;
	}
	if (easy_send(sock, pBuffer, nNumRead) == SOCKET_ERROR)
	{
	    err_printf("sending file data failed, file=%s, error=%d", pszFileName, WSAGetLastError());
	    fclose(fin);
	    return;
	}
	//dbg_printf("%d bytes sent\n", nNumRead);fflush(stdout);
	nLength -= nNumRead;
    }
    fclose(fin);
}

static void ConsoleGetFile(SOCKET sock, char *pszInputStr)
{
    bool bReplace = true, bCreateDir = false;
    char pszFileName[MAX_PATH];
    char pszStr[256];
    int nLength;
    FILE *fout;
    char pBuffer[TRANSFER_BUFFER_SIZE];
    int nNumRead;
    int nNumWritten;

    if (GetStringOpt(pszInputStr, "replace", pszStr))
    {
	bReplace = (stricmp(pszStr, "yes") == 0);
    }
    if (GetStringOpt(pszInputStr, "createdir", pszStr))
    {
	bCreateDir = (stricmp(pszStr, "yes") == 0);
    }
    if (GetStringOpt(pszInputStr, "length", pszStr))
    {
	nLength = atoi(pszStr);
	//dbg_printf("nLength: %d\n", nLength);
    }
    else
    {
	WriteString(sock, "ERROR - length not provided");
	return;
    }
    if (nLength < 1)
    {
	WriteString(sock, "ERROR - invalid length");
	return;
    }

    if (!GetStringOpt(pszInputStr, "name", pszFileName))
    {
	WriteString(sock, "ERROR - no file name provided");
	return;
    }

    //dbg_printf("creating file '%s'\n", pszFileName);
    fout = CreateCheckFile(pszFileName, bReplace, bCreateDir, pszStr);

    if (fout == NULL)
    {
	WriteString(sock, pszStr);
	return;
    }

    //dbg_printf("SEND\n");
    WriteString(sock, "SEND");

    while (nLength)
    {
	nNumRead = min(nLength, TRANSFER_BUFFER_SIZE);
	if (easy_receive(sock, pBuffer, nNumRead) == SOCKET_ERROR)
	{
	    err_printf("ERROR: easy_receive failed, error %d\n", WSAGetLastError());
	    fclose(fout);
	    DeleteFile(pszFileName);
	    return;
	}
	nNumWritten = fwrite(pBuffer, 1, nNumRead, fout);
	if (nNumWritten != nNumRead)
	{
	    err_printf("ERROR: received %d bytes but only wrote %d bytes\n", nNumRead, nNumWritten);
	}
	//dbg_printf("%d bytes read, %d bytes written\n", nNumRead, nNumWritten);
	nLength -= nNumRead;
    }

    fclose(fout);

    WriteString(sock, "SUCCESS");
}

static void GetDirectoryFiles(SOCKET sock, char *pszInputStr)
{
    char pszPath[MAX_PATH];
    char pszStr[MAX_CMD_LENGTH];
    int nFolders = 0, nFiles = 0;
    WIN32_FIND_DATA data;
    HANDLE hFind;

    if (!GetStringOpt(pszInputStr, "path", pszPath))
    {
	WriteString(sock, "ERROR: no path specified");
	return;
    }
    if (strlen(pszPath) < 1)
    {
	WriteString(sock, "ERROR: empty path specified");
	return;
    }

    if (pszPath[strlen(pszPath)-1] != '\\')
    {
	strcat(pszPath, "\\");
    }
    strcat(pszPath, "*");

    // Count the files and folders
    // What if the contents change between the counting and the sending?
    hFind = FindFirstFile(pszPath, &data);

    if (hFind == INVALID_HANDLE_VALUE)
    {
	Translate_Error(GetLastError(), pszStr, "ERROR: ");
	WriteString(sock, pszStr);
	return;
    }

    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
	if (strcmp(data.cFileName, ".") && strcmp(data.cFileName, ".."))
	    nFolders++;
    }
    else
	nFiles++;

    while (FindNextFile(hFind, &data))
    {
	if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
	    if (strcmp(data.cFileName, ".") && strcmp(data.cFileName, ".."))
		nFolders++;
	}
	else
	    nFiles++;
    }

    FindClose(hFind);

    // Send the folders
    sprintf(pszStr, "%d", nFolders);
    WriteString(sock, pszStr);

    hFind = FindFirstFile(pszPath, &data);

    if (hFind == INVALID_HANDLE_VALUE)
	return;

    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
	if (strcmp(data.cFileName, ".") && strcmp(data.cFileName, ".."))
	    WriteString(sock, data.cFileName);
    }

    while (FindNextFile(hFind, &data))
    {
	if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
	    if (strcmp(data.cFileName, ".") && strcmp(data.cFileName, ".."))
		WriteString(sock, data.cFileName);
	}
    }

    FindClose(hFind);

    // Send the files
    sprintf(pszStr, "%d", nFiles);
    WriteString(sock, pszStr);

    hFind = FindFirstFile(pszPath, &data);

    if (hFind == INVALID_HANDLE_VALUE)
	return;

    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
	WriteString(sock, data.cFileName);
	if (data.nFileSizeHigh > 0)
	    sprintf(pszStr, "%d:%d", data.nFileSizeLow, data.nFileSizeHigh);
	else
	    sprintf(pszStr, "%d", data.nFileSizeLow);
	WriteString(sock, pszStr);
    }

    while (FindNextFile(hFind, &data))
    {
	if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
	{
	    WriteString(sock, data.cFileName);
	    if (data.nFileSizeHigh > 0)
		sprintf(pszStr, "%d:%d", data.nFileSizeLow, data.nFileSizeHigh);
	    else
		sprintf(pszStr, "%d", data.nFileSizeLow);
	    WriteString(sock, pszStr);
	}
    }

    FindClose(hFind);
}

static void HandleDBCommandRead(MPD_Context *p)
{
    char name[MAX_DBS_NAME_LEN+1] = "";
    char key[MAX_DBS_KEY_LEN+1] = "";
    char value[MAX_DBS_VALUE_LEN+1] = "";
    char pszStr[MAX_CMD_LENGTH] = "";

    if (strnicmp(p->pszIn, "dbput ", 6) == 0)
    {
	GetNameKeyValue(&p->pszIn[6], name, key, value);
	if (dbs_put(name, key, value) == DBS_SUCCESS)
	    ContextWriteString(p, DBS_SUCCESS_STR);
	else
	    ContextWriteString(p, DBS_FAIL_STR);
    }
    else if (strnicmp(p->pszIn, "dbget ", 6) == 0)
    {
	GetNameKeyValue(&p->pszIn[6], name, key, NULL);
	if (dbs_get(name, key, value) == DBS_SUCCESS)
	    ContextWriteString(p, value);
	else
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "dbget src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[6]);
	    ContextWriteString(g_pRightContext, pszStr);
	}
    }
    else if (stricmp(p->pszIn, "dbcreate") == 0)
    {
	// Create the database locally
	if (dbs_create(name) == DBS_SUCCESS)
	{
	    // Write the name back to the user
	    ContextWriteString(p, name);
	    // Create the database on all the other nodes
	    _snprintf(pszStr, MAX_CMD_LENGTH, "dbcreate src=%s sock=%d name=%s", g_pszHost, p->sock, name);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else
	{
	    ContextWriteString(p, DBS_FAIL_STR);
	}
    }
    else if (strnicmp(p->pszIn, "dbcreate ", 9) == 0)
    {
	GetNameKeyValue(&p->pszIn[9], name, NULL, NULL);
	if (dbs_create_name_in(name) == DBS_SUCCESS)
	{
	    ContextWriteString(p, DBS_SUCCESS_STR);
	    // Create the database on all the other nodes
	    _snprintf(pszStr, MAX_CMD_LENGTH, "dbcreate src=%s sock=%d name=%s", g_pszHost, p->sock, name);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else
	{
	    ContextWriteString(p, DBS_FAIL_STR);
	}
    }
    else if (strnicmp(p->pszIn, "dbdestroy ", 10) == 0)
    {
	// forward the destroy command
	_snprintf(pszStr, MAX_CMD_LENGTH, "dbdestroy src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[10]);
	ContextWriteString(g_pRightContext, pszStr);

	// destroy the database locally
	GetNameKeyValue(&p->pszIn[10], name, NULL, NULL);
	if (dbs_destroy(name) == DBS_SUCCESS)
	    ContextWriteString(p, DBS_SUCCESS_STR);
	else
	    ContextWriteString(p, DBS_FAIL_STR);
    }
    else if (strnicmp(p->pszIn, "dbfirst ", 8) == 0)
    {
	GetNameKeyValue(&p->pszIn[8], name, NULL, NULL);
	if (dbs_first(name, key, value) == DBS_SUCCESS)
	{
	    // forward the first command
	    _snprintf(pszStr, MAX_CMD_LENGTH, "dbfirst src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[8]);
	    ContextWriteString(g_pRightContext, pszStr);

	    if (*key == '\0')
	    {
		// If the local database is empty, forward a dbnext command
		_snprintf(pszStr, MAX_CMD_LENGTH, "dbnext src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[8]);
		ContextWriteString(g_pRightContext, pszStr);
	    }
	    else
	    {
		_snprintf(p->pszOut, MAX_CMD_LENGTH, "key=%s value=%s", key, value);
		ContextWriteString(p, p->pszOut);
	    }
	}
	else
	{
	    ContextWriteString(p, DBS_FAIL_STR);
	}
    }
    else if (strnicmp(p->pszIn, "dbnext ", 7) == 0)
    {
	GetNameKeyValue(&p->pszIn[7], name, NULL, NULL);
	if (dbs_next(name, key, value) == DBS_SUCCESS)
	{
	    if (*key == '\0')
	    {
		_snprintf(pszStr, MAX_CMD_LENGTH, "dbnext src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[7]);
		ContextWriteString(g_pRightContext, pszStr);
	    }
	    else
	    {
		_snprintf(p->pszOut, MAX_CMD_LENGTH, "key=%s value=%s", key, value);
		ContextWriteString(p, p->pszOut);
	    }
	}
	else
	{
	    ContextWriteString(p, DBS_FAIL_STR);
	}
    }
    else if (stricmp(p->pszIn, "dbfirstdb") == 0)
    {
	if (dbs_firstdb(name) == DBS_SUCCESS)
	{
	    if (*name == '\0')
		strcpy(p->pszOut, DBS_END_STR);
	    else
		_snprintf(p->pszOut, MAX_CMD_LENGTH, "name=%s", name);
	    ContextWriteString(p, p->pszOut);
	}
	else
	{
	    ContextWriteString(p, DBS_FAIL_STR);
	}
    }
    else if (stricmp(p->pszIn, "dbnextdb") == 0)
    {
	if (dbs_nextdb(name) == DBS_SUCCESS)
	{
	    if (*name == '\0')
		strcpy(p->pszOut, DBS_END_STR);
	    else
		_snprintf(p->pszOut, MAX_CMD_LENGTH, "name=%s", name);
	    ContextWriteString(p, p->pszOut);
	}
	else
	{
	    ContextWriteString(p, DBS_FAIL_STR);
	}
    }
    else if (strnicmp(p->pszIn, "dbdelete ", 9) == 0)
    {
	// Attempt to delete locally
	GetNameKeyValue(&p->pszIn[9], name, key, NULL);
	if (dbs_delete(name, key) == DBS_SUCCESS)
	{
	    ContextWriteString(p, DBS_SUCCESS_STR);
	}
	else
	{
	    // forward the delete command
	    _snprintf(pszStr, MAX_CMD_LENGTH, "dbdelete src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[9]);
	    ContextWriteString(g_pRightContext, pszStr);
	}
    }
    else
    {
	err_printf("unknown command '%s'", p->pszIn);
    }
}

void statConfig(char *pszOutput, int length)
{
    pszOutput[0] = '\0';
    MPDRegistryToString(pszOutput, length);
}

void HandleConsoleRead(MPD_Context *p)
{
    char pszStr[MAX_CMD_LENGTH];

    dbg_printf("ConsoleRead[%d]: '%s'\n", p->sock, p->pszIn);
    switch(p->nLLState)
    {
    case MPD_READING_CMD:
	if (strnicmp(p->pszIn, "db", 2) == 0)
	{
	    HandleDBCommandRead(p);
	}
	else if (strnicmp(p->pszIn, "launch ", 7) == 0)
	{
	    char pszHost[MAX_HOST_LENGTH];
	    LaunchStateStruct *pLS = new LaunchStateStruct;
	    pLS->nStatus = LAUNCH_PENDING;
	    strcpy(pLS->pszError, "LAUNCH_PENDING");
	    pLS->nId = InterlockedIncrement(&g_nCurrentLaunchId);
	    pLS->nBfd = p->sock;
	    pLS->pNext = g_pLaunchList;
	    if (!GetStringOpt(&p->pszIn[7], "h", pLS->pszHost))
	    {
		strncpy(pLS->pszHost, g_pszHost, MAX_HOST_LENGTH);
		pLS->pszHost[MAX_HOST_LENGTH-1] = '\0';
	    }
	    g_pLaunchList = pLS;

	    /* write the launch result back first to avoid a timeout */
	    sprintf(pszStr, "%d", pLS->nId);
	    if (ContextWriteString(p, pszStr) == SOCKET_ERROR)
	    {
		err_printf("ContextWriteString(\"%s\") failed to write the launch id, error %d\n"
		    "unable to launch '%s'\n", pszStr, WSAGetLastError(), p->pszIn);
		break;
	    }

	    _snprintf(pszStr, MAX_CMD_LENGTH, "launch src=%s id=%d %s", g_pszHost, pLS->nId, &p->pszIn[7]);
	    if (GetStringOpt(pszStr, "h", pszHost))
	    {
		if ((stricmp(pszHost, g_pszHost) == 0) || (strcmp(pszHost, g_pszIP) == 0))
		    Launch(pszStr);
		else
		    ContextWriteString(g_pRightContext, pszStr);
	    }
	    else
		Launch(pszStr); // No host provided so launch locally
	    /*
	    sprintf(pszStr, "%d", pLS->nId);
	    ContextWriteString(p, pszStr);
	    */
	}
	else if (strnicmp(p->pszIn, "getpid ", 7) == 0)
	{
	    LaunchStateStruct *pLS = GetLaunchStruct(atoi(&p->pszIn[7]));
	    if (pLS != NULL)
	    {
		WaitForSingleObject(pLS->hMutex, INFINITE);
		if (pLS->nStatus == LAUNCH_PENDING)
		{
		    pLS->bPidRequested = true;
		    ReleaseMutex(pLS->hMutex);
		}
		else
		{
		    if (pLS->nStatus == LAUNCH_SUCCESS)
			sprintf(pszStr, "%d", pLS->nPid);
		    else
			strcpy(pszStr, "-1");
		    ReleaseMutex(pLS->hMutex);
		    ContextWriteString(p, pszStr);
		}
	    }
	    else
	    {
		ContextWriteString(p, "-1");
	    }
	}
	else if (strnicmp(p->pszIn, "getexitcode ", 12) == 0)
	{
	    LaunchStateStruct *pLS = GetLaunchStruct(atoi(&p->pszIn[12]));
	    if (pLS != NULL)
	    {
		WaitForSingleObject(pLS->hMutex, INFINITE);
		if (pLS->nStatus == LAUNCH_EXITED)
		{
		    dbg_printf("HandleConsoleRead:Sending exit code %d for launchid %d\n", pLS->nExitCode, atoi(&p->pszIn[12]));
		    sprintf(pszStr, "%d", pLS->nExitCode);
		}
		else
		{
		    if (pLS->nStatus == LAUNCH_SUCCESS)
			strcpy(pszStr, "ACTIVE");
		    else
			strcpy(pszStr, "FAIL");
		}
		ReleaseMutex(pLS->hMutex);
		ContextWriteString(p, pszStr);
	    }
	    else
	    {
		ContextWriteString(p, "FAIL");
	    }
	}
	else if (strnicmp(p->pszIn, "getexitcodewait ", 16) == 0)
	{
	    LaunchStateStruct *pLS = GetLaunchStruct(atoi(&p->pszIn[16]));
	    if (pLS != NULL)
	    {
		WaitForSingleObject(pLS->hMutex, INFINITE);
		if (pLS->nStatus == LAUNCH_SUCCESS)
		{
		    pLS->bExitStateRequested = true;
		    ReleaseMutex(pLS->hMutex);
		}
		else
		{
		    if (pLS->nStatus == LAUNCH_EXITED)
		    {
			dbg_printf("sending exit code %d:%d\n", atoi(&p->pszIn[16]), pLS->nExitCode);
			sprintf(pszStr, "%d", pLS->nExitCode);
		    }
		    else
			strcpy(pszStr, "FAIL");
		    ReleaseMutex(pLS->hMutex);
		    ContextWriteString(p, pszStr);
		}
	    }
	    else
	    {
		ContextWriteString(p, "FAIL");
	    }
	}
	else if (strnicmp(p->pszIn, "getexittime ", 12) == 0)
	{
	    LaunchStateStruct *pLS = GetLaunchStruct(atoi(&p->pszIn[12]));
	    if (pLS != NULL)
	    {
		if (strlen(pLS->timestamp))
		{
		    dbg_printf("sending exit time %d:%s\n", atoi(&p->pszIn[12]), pLS->timestamp);
		    strcpy(pszStr, pLS->timestamp);
		}
		else
		{
		    if (pLS->nStatus == LAUNCH_SUCCESS)
		    {
			strcpy(pszStr, "ACTIVE");
		    }
		    else
		    {
			if (pLS->timestamp[0] == '\0')
			{
			    strcpy(pszStr, "unknown");
			}
			else
			{
			    dbg_printf("sending exit time %d:%s\n", atoi(&p->pszIn[12]), pLS->timestamp);
			    strcpy(pszStr, pLS->timestamp);
			}
		    }
		}
		ContextWriteString(p, pszStr);
	    }
	    else
	    {
		ContextWriteString(p, "FAIL");
	    }
	}
	else if (strnicmp(p->pszIn, "getmpifinalized ", 16) == 0)
	{
	    LaunchStateStruct *pLS = GetLaunchStruct(atoi(&p->pszIn[16]));
	    if (pLS != NULL)
	    {
		if (pLS->bMPIFinalized)
		{
		    dbg_printf("sending mpifinalized launchid(%d)\n", atoi(&p->pszIn[16]));
		    strcpy(pszStr, "yes");
		}
		else
		{
		    dbg_printf("sending not mpifinalized launchid(%d)\n", atoi(&p->pszIn[16]));
		    strcpy(pszStr, "no");
		}
		ContextWriteString(p, pszStr);
	    }
	    else
	    {
		ContextWriteString(p, "FAIL");
	    }
	}
	else if (strnicmp(p->pszIn, "setMPIFinalized ", 16) == 0)
	{
	    if (SaveMPIFinalized(atoi(&p->pszIn[16])))
		ContextWriteString(p, "SUCCESS");
	    else
		ContextWriteString(p, "FAIL");
	}
	else if (strnicmp(p->pszIn, "setdbgoutput ", 13) == 0)
	{
	    if (SetDbgRedirection(&p->pszIn[13]))
	    {
		SYSTEMTIME s;
		GetSystemTime(&s);
		dbg_printf("[%d.%d.%d %dh:%dm:%ds] starting redirection to log file.\n", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute, s.wSecond);
		WriteMPDRegistry("RedirectToLogfile", "yes");
		WriteMPDRegistry("LogFile", &p->pszIn[13]);
		ContextWriteString(p, "SUCCESS");
	    }
	    else
	    {
		WriteMPDRegistry("RedirectToLogfile", "no");
		ContextWriteString(p, "FAIL");
	    }
	}
	else if (strnicmp(p->pszIn, "canceldbgoutput", 15) == 0)
	{
	    SYSTEMTIME s;
	    GetSystemTime(&s);
	    dbg_printf("[%d.%d.%d %dh:%dm:%ds] stopping redirection to log file.\n", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute, s.wSecond);
	    CancelDbgRedirection();
	    WriteMPDRegistry("RedirectToLogfile", "no");
	    ContextWriteString(p, "SUCCESS");
	}
	else if (strnicmp(p->pszIn, "geterror ", 9) == 0)
	{
	    LaunchStateStruct *pLS = GetLaunchStruct(atoi(&p->pszIn[9]));
	    if (pLS != NULL)
		ContextWriteString(p, pLS->pszError);
	    else
		ContextWriteString(p, "invalid launch id");
	}
	else if (strnicmp(p->pszIn, "freeprocess ", 12) == 0)
	{
	    if (RemoveStateStruct(GetLaunchStruct(atoi(&p->pszIn[12]))))
		ContextWriteString(p, "SUCCESS");
	    else
		ContextWriteString(p, "FAIL");
	}
	else if (strnicmp(p->pszIn, "kill ", 5) == 0)
	{
	    char pszTemp1[MAX_HOST_LENGTH], pszTemp2[10];
	    if (GetStringOpt(p->pszIn, "host", pszTemp1) && GetStringOpt(p->pszIn, "pid", pszTemp2))
	    {
		strncat(p->pszIn, " src=", MAX_CMD_LENGTH - 1 - strlen(p->pszIn));
		strncat(p->pszIn, g_pszHost, MAX_CMD_LENGTH - 1 - strlen(p->pszIn));
		ContextWriteString(g_pRightContext, p->pszIn);
	    }
	    else
	    {
		LaunchStateStruct *pLS = GetLaunchStruct(atoi(&p->pszIn[5]));
		if (pLS != NULL)
		{
		    _snprintf(pszStr, MAX_CMD_LENGTH, "kill src=%s host=%s pid=%d", g_pszHost, pLS->pszHost, pLS->nPid);
		    ContextWriteString(g_pRightContext, pszStr);
		}
		else
		{
		    // kill does not return a value, so it cannot return an error either
		    //EnqueueWrite(p, "invalid launch id", MPD_WRITING_RESULT);
		}
	    }
	}
	else if (strnicmp(p->pszIn, "setmpduser ", 11) == 0)
	{
	    if (g_bMPDUserCapable)
	    {
		char pszAccount[100];
		char pszPassword[300];
		if (GetStringOpt(&p->pszIn[11], "a", pszAccount))
		{
		    if (GetStringOpt(&p->pszIn[11], "p", pszPassword))
		    {
			DecodePassword(pszPassword);
			if (mpdSetupCryptoClient())
			{
			    if (mpdSavePasswordToRegistry(pszAccount, pszPassword, true))
			    {
				//WriteMPDRegistry("UseMPDUser", "yes");
				strcpy(g_pszMPDUserAccount, pszAccount);
				strcpy(g_pszMPDUserPassword, pszPassword);
				strcpy(pszStr, "SUCCESS");
			    }
			    else
			    {
				_snprintf(pszStr, MAX_CMD_LENGTH, "FAIL - %s", mpdCryptGetLastErrorString());
			    }
			}
			else
			{
			    _snprintf(pszStr, MAX_CMD_LENGTH, "FAIL - %s", mpdCryptGetLastErrorString());
			}
		    }
		    else
		    {
			_snprintf(pszStr, MAX_CMD_LENGTH, "FAIL - password not specified");
		    }
		}
		else
		{
		    _snprintf(pszStr, MAX_CMD_LENGTH, "FAIL - account not specified");
		}
	    }
	    else
	    {
		_snprintf(pszStr, MAX_CMD_LENGTH, "FAIL - command not enabled");
	    }
	    ContextWriteString(p, pszStr);
	}
	else if (stricmp(p->pszIn, "clrmpduser") == 0)
	{
	    if (g_bMPDUserCapable)
	    {
		if (mpdDeletePasswordRegistryEntry())
		{
		    g_bUseMPDUser = false;
		    WriteMPDRegistry("UseMPDUser", "no");
		    strcpy(pszStr, "SUCCESS");
		}
		else
		{
		    _snprintf(pszStr, MAX_CMD_LENGTH, "FAIL - %s", mpdCryptGetLastErrorString());
		}
	    }
	    else
	    {
		_snprintf(pszStr, MAX_CMD_LENGTH, "FAIL - command not enabled");
	    }
	    ContextWriteString(p, pszStr);
	}
	else if (stricmp(p->pszIn, "enablempduser") == 0)
	{
	    if (g_bMPDUserCapable)
	    {
		//char pszAccount[100], pszPassword[100];
		//if (ReadMPDRegistry("mpdAccount", pszAccount, false))
		if (mpdReadPasswordFromRegistry(g_pszMPDUserAccount, g_pszMPDUserPassword))
		{
		    g_bUseMPDUser = true;
		    WriteMPDRegistry("UseMPDUser", "yes");
		    strcpy(pszStr, "SUCCESS");
		}
		else
		{
		    strcpy(pszStr, "FAIL - mpdsetuser must be called to set an account before enablempduser can be called.\n");
		}
	    }
	    else
	    {
		_snprintf(pszStr, MAX_CMD_LENGTH, "FAIL - command not enabled");
	    }
	    ContextWriteString(p, pszStr);
	}
	else if (stricmp(p->pszIn, "disablempduser") == 0)
	{
	    if (g_bMPDUserCapable)
	    {
		g_bUseMPDUser = false;
		WriteMPDRegistry("UseMPDUser", "no");
		ContextWriteString(p, "SUCCESS");
	    }
	    else
	    {
		ContextWriteString(p, "FAIL - command not enabled");
	    }
	}
	else if (strnicmp(p->pszIn, "stat ", 5) == 0)
	{
    	    char pszHost[MAX_HOST_LENGTH];
	    if (!GetStringOpt(p->pszIn, "host", pszHost))
	    {
		strncat(p->pszIn, " host=", MAX_CMD_LENGTH - 1 - strlen(p->pszIn));
		strncat(p->pszIn, g_pszHost, MAX_CMD_LENGTH - 1 - strlen(p->pszIn));
	    }
	    _snprintf(pszStr, MAX_CMD_LENGTH, "stat src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[5]);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (strnicmp(p->pszIn, "validate ", 9) == 0)
	{
	    char pszAccount[100], pszPassword[300], pszCache[10];
	    bool bUseCache = true;
	    int error;
	    _snprintf(pszStr, MAX_CMD_LENGTH, "FAIL - invalid arguments");
	    if (GetStringOpt(&p->pszIn[9], "a", pszAccount))
	    {
		if (GetStringOpt(&p->pszIn[9], "p", pszPassword))
		{
		    DecodePassword(pszPassword);
		    if (GetStringOpt(&p->pszIn[9], "c", pszCache))
		    {
			if (stricmp(pszCache, "no") == 0)
			    bUseCache = false;
		    }
		    if (ValidateUser(pszAccount, pszPassword, bUseCache, &error))
		    {
			_snprintf(pszStr, MAX_CMD_LENGTH, "SUCCESS");
		    }
		    else
		    {
			Translate_Error(error, pszStr, "FAIL - ");
		    }
		}
	    }
	    ContextWriteString(p, pszStr);
	}
	else if (strnicmp(p->pszIn, "freecached", 10) == 0)
	{
	    char pszHost[MAX_HOST_LENGTH];
	    if (!GetStringOpt(p->pszIn, "host", pszHost))
		strcpy(pszHost, g_pszHost);
	    _snprintf(pszStr, MAX_CMD_LENGTH, "freecached src=%s sock=%d host=%s", g_pszHost, p->sock, pszHost);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (stricmp(p->pszIn, "killall") == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "killall src=%s", g_pszHost);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (stricmp(p->pszIn, "hosts") == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "hosts src=%s sock=%d result=%s", g_pszHost, p->sock, g_pszHost);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (strnicmp(p->pszIn, "next ", 5) == 0)
	{
	    int n = atoi(&p->pszIn[5]);
	    if ((n > 0) || (n < 16384))
	    {
		n--;
		ContextWriteString(p, g_pszHost);
		if (n > 0)
		{
		    _snprintf(pszStr, MAX_CMD_LENGTH, "next src=%s sock=%d n=%d", g_pszHost, p->sock, n);
		    ContextWriteString(g_pRightContext, pszStr);
		}
	    }
	    else
	    {
		ContextWriteString(p, "Error: invalid number of hosts requested");
	    }
	}
	else if (strnicmp(p->pszIn, "barrier ", 8) == 0)
	{
	    char pszName[100], pszCount[10];
	    if (GetStringOpt(p->pszIn, "name", pszName))
	    {
		if (GetStringOpt(p->pszIn, "count", pszCount))
		{
		    SetBarrier(pszName, atoi(pszCount), p->sock);
		    _snprintf(pszStr, MAX_CMD_LENGTH, "barrier src=%s name=%s count=%s", g_pszHost, pszName, pszCount);
		    ContextWriteString(g_pRightContext, pszStr);
		}
		else
		    ContextWriteString(p, "Error: invalid barrier command, no count specified");
	    }
	    else
		ContextWriteString(p, "Error: invalid barrier command, no name specified");
	}
	else if (stricmp(p->pszIn, "ps") == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "ps src=%s sock=%d result=", g_pszHost, p->sock);
	    ConcatenateProcessesToString(pszStr);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (stricmp(p->pszIn, "extract") == 0)
	{
	    if (!Extract(true))
	    {
		err_printf("Extract failed\n");
	    }
	    p->nLLState = MPD_READING_CMD;
	}
	else if (stricmp(p->pszIn, "done") == 0)
	{
	    p->bDeleteMe = true;
	    p->nState = MPD_INVALID;
	}
	else if (stricmp(p->pszIn, "set nodes") == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "lefthost src=%s host=%s", g_pszHost, g_pszHost);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (strnicmp(p->pszIn, "set ", 4) == 0)
	{
	    char pszKey[100], *pszValue;
	    int nLength;
	    pszValue = strstr(p->pszIn, "=");
	    if (pszValue != NULL)
	    {
		nLength = pszValue - &p->pszIn[4];
		memcpy(pszKey, &p->pszIn[4], nLength);
		pszKey[nLength] = '\0';
		pszValue++;
		_snprintf(pszStr, MAX_CMD_LENGTH, "set src=%s key=%s value=%s", g_pszHost, pszKey, pszValue);
		ContextWriteString(g_pRightContext, pszStr);
	    }
	}
	else if (strnicmp(p->pszIn, "lset ", 5) == 0)
	{
	    char pszKey[100], *pszValue;
	    int nLength;
	    pszValue = strstr(p->pszIn, "=");
	    if (pszValue != NULL)
	    {
		nLength = pszValue - &p->pszIn[5];
		memcpy(pszKey, &p->pszIn[5], nLength);
		pszKey[nLength] = '\0';
		pszValue++;
		WriteMPDRegistry(pszKey, pszValue);
	    }
	}
	else if (strnicmp(p->pszIn, "lget ", 5) == 0)
	{
	    pszStr[0] = '\0';
	    ReadMPDRegistry(&p->pszIn[5], pszStr);
	    ContextWriteString(p, pszStr);
	}
	else if (strnicmp(p->pszIn, "ldelete ", 8) == 0)
	{
	    DeleteMPDRegistry(&p->pszIn[8]);
	}
	else if (strnicmp(p->pszIn, "insert ", 7) == 0)
	{
	    if (!InsertIntoRing(&p->pszIn[7]))
	    {
		_snprintf(pszStr, MAX_CMD_LENGTH, "%s failed\n", p->pszIn);
		ContextWriteString(p, pszStr);
	    }
	    else
	    {
		p->nLLState = MPD_READING_CMD;
	    }
	}
	else if (stricmp(p->pszIn, "shutdown") == 0)
	{
	    ServiceStop();
	}
	else if (stricmp(p->pszIn, "exitall") == 0)
	{
	    g_bExitAllRoot = true;
	    ContextWriteString(g_pRightContext, "exitall");
	}
	else if (stricmp(p->pszIn, "version") == 0)
	{
	    GetMPDVersion(pszStr, MAX_CMD_LENGTH);
	    ContextWriteString(p, pszStr);
	}
	else if (stricmp(p->pszIn, "mpich version") == 0)
	{
	    GetMPICHVersion(pszStr, MAX_CMD_LENGTH);
	    ContextWriteString(p, pszStr);
	}
	else if (stricmp(p->pszIn, "config") == 0)
	{
	    pszStr[0] = '\0';
	    MPDRegistryToString(pszStr, MAX_CMD_LENGTH);
	    ContextWriteString(p, pszStr);
	}
	else if (stricmp(p->pszIn, "print") == 0)
	{
	    int nSent;
	    char *buf, *pBuf;
	    int size;
	    FILE *fout = tmpfile();
	    
	    PrintState(fout);

	    size = ftell(fout);
	    //dbg_printf("print command wrote %d bytes to tmp file\n", size);
	    fseek( fout, 0L, SEEK_SET );
	    buf = new char[size+1];
	    pBuf = buf;
	    WaitForSingleObject(p->hMutex, INFINITE);
	    while (size)
	    {
		nSent = fread(pBuf, 1, size, fout);
		if (nSent == size)
		{
		    pBuf[size] = '\0';
		    easy_send(p->sock, pBuf, size+1);
		}
		else
		    easy_send(p->sock, pBuf, nSent);
		size = size - nSent;
		pBuf = pBuf + nSent;
	    }
	    ReleaseMutex(p->hMutex);
	    delete buf;
	    fclose(fout);
	}
	else if (strnicmp(p->pszIn, "createforwarder ", 16) == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "createforwarder src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[16]);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (strnicmp(p->pszIn, "stopforwarder ", 14) == 0)
	{
	    char pszHost[100];
	    if (GetStringOpt(p->pszIn, "host", pszHost))
	    {
		char *token = strtok(pszHost, ":");
		if (token != NULL)
		{
		    token = strtok(NULL, "\n");
		    if (token != NULL)
		    {
			int nPort = atoi(token);
			if (nPort > 0)
			{
			    _snprintf(&p->pszIn[14], MAX_CMD_LENGTH - 14, "host=%s port=%d", pszHost, nPort);
			}
		    }
		}
	    }
	    else
	    {
		if (GetStringOpt(p->pszIn, "port", pszHost))
		{
		    strncat(p->pszIn, " host=", MAX_CMD_LENGTH - 1 - strlen(p->pszIn));
		    strncat(p->pszIn, g_pszHost, MAX_CMD_LENGTH - 1 - strlen(p->pszIn));
		}
		else
		{
		    if (strstr(p->pszIn, ":") != NULL)
		    {
			strncpy(pszHost, &p->pszIn[14], 100);
			pszHost[99] = '\0';
			char *token = strtok(pszHost, ":");
			if (token != NULL)
			{
			    token = strtok(NULL, "\n");
			    if (token != NULL)
			    {
				int nPort = atoi(token);
				if (nPort > 0)
				{
				    _snprintf(&p->pszIn[14], MAX_CMD_LENGTH - 14, "host=%s port=%d", pszHost, nPort);
				}
			    }
			}
		    }
		    else
		    {
			int nPort = atoi(&p->pszIn[14]);
			if (nPort > 0)
			{
			    _snprintf(&p->pszIn[14], MAX_CMD_LENGTH - 14, "host=%s port=%d", g_pszHost, nPort);
			}
		    }
		}
	    }
	    _snprintf(pszStr, MAX_CMD_LENGTH, "stopforwarder src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[14]);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (stricmp(p->pszIn, "forwarders") == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "forwarders src=%s sock=%d result=", g_pszHost, p->sock);
	    ConcatenateForwardersToString(pszStr);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (stricmp(p->pszIn, "killforwarders") == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "killforwarders src=%s", g_pszHost);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (strnicmp(p->pszIn, "createtmpfile ", 14) == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "createtmpfile src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[14]);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (strnicmp(p->pszIn, "deletetmpfile ", 14) == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "deletetmpfile src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[14]);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (strnicmp(p->pszIn, "mpich1readint ", 14) == 0)
	{
	    _snprintf(pszStr, MAX_CMD_LENGTH, "mpich1readint src=%s sock=%d %s", g_pszHost, p->sock, &p->pszIn[14]);
	    ContextWriteString(g_pRightContext, pszStr);
	}
	else if (strnicmp(p->pszIn, "putfile ", 8) == 0)
	{
	    HANDLE hUser;
	    hUser = ParseBecomeUser(p, &p->pszIn[8], false);
	    if (hUser != (HANDLE)-1)
	    {
		ConsoleGetFile(p->sock, &p->pszIn[8]);
		LoseTheUser(hUser);
	    }
	}
	else if (strnicmp(p->pszIn, "getfile ", 8) == 0)
	{
	    HANDLE hUser;
	    hUser = ParseBecomeUser(p, &p->pszIn[8], true);
	    if (hUser != (HANDLE)-1)
	    {
		ConsolePutFile(p->sock, &p->pszIn[8]);
		LoseTheUser(hUser);
	    }
	}
	else if (strnicmp(p->pszIn, "getdir ", 7) == 0)
	{
	    HANDLE hUser;
	    hUser = ParseBecomeUser(p, &p->pszIn[7], false);
	    if (hUser != (HANDLE)-1)
	    {
		GetDirectoryFiles(p->sock, &p->pszIn[7]);
		LoseTheUser(hUser);
	    }
	}
	else if (strnicmp(p->pszIn, "fileinit ", 9) == 0)
	{
	    if (GetStringOpt(p->pszIn, "account", p->pszFileAccount) && 
		GetStringOpt(p->pszIn, "password", p->pszFilePassword))
	    {
		DecodePassword(p->pszFilePassword);
		p->bFileInitCalled = true;
	    }
	}
	else if (strnicmp(p->pszIn, "update ", 7) == 0)
	{
	    UpdateMPD(&p->pszIn[7]);
	}
	else if (strnicmp(p->pszIn, "updatempich ", 12) == 0)
	{
	    UpdateMPICH(&p->pszIn[12]);
	    ContextWriteString(p, "SUCCESS");
	}
	else if (strnicmp(p->pszIn, "updatempichd ", 13) == 0)
	{
	    UpdateMPICHd(&p->pszIn[13]);
	    ContextWriteString(p, "SUCCESS");
	}
	else if (stricmp(p->pszIn, "restart") == 0)
	{
	    ContextWriteString(p, "Restarting mpd...");
	    RestartMPD();
	}
	else
	{
	    err_printf("console socket read unknown command: '%s'\n", p->pszIn);
	    p->nLLState = MPD_READING_CMD;
	}
	break;
    default:
	err_printf("unexpected read in console state %d, '%s'\n", p->nLLState, p->pszIn);
	p->nLLState = MPD_READING_CMD;
	break;
    }
}
