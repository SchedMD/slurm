#include "mpdimpl.h"
#include "mpdutil.h"
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include "Service.h"
#include "GetStringOpt.h"
#include "database.h"
#include "Translate_Error.h"
#include <time.h>

struct TmpFileStruct
{
    char pszFileName[MAX_PATH];
    TmpFileStruct *pNext;
};

TmpFileStruct *g_pTmpFileList = NULL;

void statTmp(char *pszOutput, int length)
{
    TmpFileStruct *p;

    *pszOutput = '\0';
    length--; // leave room for the null character

    if (g_pTmpFileList == NULL)
	return;

    if (!snprintf_update(pszOutput, length, "TMP FILES:\n"))
	return;

    p = g_pTmpFileList;
    while (p)
    {
	if (!snprintf_update(pszOutput, length, " '%s'\n", p->pszFileName))
	    return;
	p = p->pNext;
    }
}

void CreateTmpFile(char *pszFileName, bool bDelete /*= true*/)
{
    char pszDir[MAX_PATH] = "C:\\";
    char pszTemp[MAX_PATH];
    char *namepart;
    
    if (!ReadMPDRegistry("temp", pszDir))
	dbg_printf("no temp directory specified, using c:\\\n");
    
    if (GetTempFileName(pszDir, "mpi", 0, pszTemp) == 0)
    {
	int nError = GetLastError();
	Translate_Error(nError, pszFileName, "FAIL ");
	err_printf("GetTempFileName(%s) failed, %s", pszDir, pszFileName);
	return;
    }
    
    GetFullPathName(pszTemp, MAX_PATH, pszFileName, &namepart);

    if (bDelete)
    {
	// Add this name to the global list
	// These names will be matched with corresponding "deletetmpfile" commands
	// All remaining files will be deleted when the mpd exits
	TmpFileStruct *pNode = new TmpFileStruct;
	strncpy(pNode->pszFileName, pszFileName, MAX_PATH);
	pNode->pNext = g_pTmpFileList;
	g_pTmpFileList = pNode;
    }
}

bool DeleteTmpFile(char *pszFileName)
{
    TmpFileStruct *pNode, *pTrailer;
    pTrailer = pNode = g_pTmpFileList;
    while (pNode)
    {
	if (stricmp(pszFileName, pNode->pszFileName) == 0)
	{
	    if (pNode == g_pTmpFileList)
	    {
		g_pTmpFileList = g_pTmpFileList->pNext;
		delete pNode;
	    }
	    else
	    {
		pTrailer->pNext = pNode->pNext;
		delete pNode;
	    }
	    return (DeleteFile(pszFileName) == TRUE);
	}
	if (pTrailer != pNode)
	    pTrailer = pTrailer->pNext;
	pNode = pNode->pNext;
    }
    return false;
}

void RemoveAllTmpFiles()
{
    TmpFileStruct *pNode;
    while (g_pTmpFileList)
    {
	pNode = g_pTmpFileList;
	g_pTmpFileList = g_pTmpFileList->pNext;
	if (strlen(pNode->pszFileName) > 0)
	    DeleteFile(pNode->pszFileName);
	delete pNode;
    }
}

#define DEFAULT_MPICH_ROOT_TIMEOUT 7

int GetPortFromFile(char *pszFileName, int nPid, int *nPort)
{
    int nError;
    DWORD num_read = 0;
    char pBuffer[100] = "";
    char *pChar = pBuffer;
    clock_t cStart;
    HANDLE hProcess = NULL;
    DWORD dwExitCode;
    int nTimeout = DEFAULT_MPICH_ROOT_TIMEOUT;

    if (ReadMPDRegistry("timeout", pBuffer))
    {
	nTimeout = atoi(pBuffer);
	if (nTimeout > 1000)
	    nTimeout = nTimeout / 1000;
	if (nTimeout < 1)
	    nTimeout = 1;
	pBuffer[0] = '\0';
    }

    HANDLE hFile = CreateFile(pszFileName, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
	nError = GetLastError();
	return nError;
    }
    
    cStart = clock();
    while (true)
    {
	num_read = 0;
	if (!ReadFile(hFile, pChar, 100, &num_read, NULL))
	{
	    nError = GetLastError();
	    CloseHandle(hFile);
	    DeleteTmpFile(pszFileName);
	    if (hProcess)
		CloseHandle(hProcess);
	    return nError;
	}
	if (num_read == 0)
	{
	    if (!hProcess)
	    {
		hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, nPid);
		if (hProcess == NULL)
		{
		    int error = GetLastError();
		    if (error == ERROR_INVALID_PARAMETER)
		    {
			int nExitCode = ConsoleGetExitCode(nPid);
			if (nExitCode != -1 && nExitCode != -2)
			{
			    if (nExitCode == ERROR_WAIT_NO_CHILDREN)
				return -2;
			    *nPort = nExitCode;
			    return -3;
			}
		    }
		}
	    }
	    if (hProcess)
	    {
		if (GetExitCodeProcess(hProcess, &dwExitCode))
		{
		    if (dwExitCode != STILL_ACTIVE)
		    {
			CloseHandle(hProcess);
			if (dwExitCode == ERROR_WAIT_NO_CHILDREN)
			    return -2;
			*nPort = dwExitCode;
			return -3;
		    }
		}
	    }
	    if (clock() - cStart > nTimeout * CLOCKS_PER_SEC)
	    {
		CloseHandle(hFile);
		DeleteTmpFile(pszFileName);
		if (hProcess)
		    CloseHandle(hProcess);
		return -1;
	    }
	    Sleep(100);
	}
	else
	{
	    for (unsigned int i=0; i<num_read; i++)
	    {
		if (*pChar == '\n')
		    break;
		pChar ++;
	    }
	    if (*pChar == '\n')
		break;
	}
    }
    CloseHandle(hFile);
    DeleteTmpFile(pszFileName);
    
    *nPort = atoi(pBuffer);
    
    if (hProcess)
	CloseHandle(hProcess);
    return 0;
}
