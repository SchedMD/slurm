#include "mpdimpl.h"
#include "Translate_Error.h"
#include "Service.h"
#include "mpdutil.h"

void UpdateMPD(char *pszFileName)
{
    int error;
    char szExe[1024], szExeCopy[1024];
    char pszStr[4096];

    if (!GetModuleFileName(NULL, szExe, 1024))
    {
	Translate_Error(GetLastError(), pszStr);
	dbg_printf("GetModuleFileName failed.\nError: %s\n", pszStr);
	return;
    }

    strncpy(szExeCopy, szExe, 1023);
    szExeCopy[1022] = '\0';
    strcpy(&szExeCopy[strlen(szExeCopy)-4], "2.exe");

    dbg_printf("copying '%s' to '%s'\n", szExe, szExeCopy);
    if (!CopyFile(szExe, szExeCopy, FALSE))
    {
	error = GetLastError();
	Translate_Error(error, pszStr);
	err_printf("Unable to copy '%s' to '%s'\nError: %s\n", szExe, szExeCopy, pszStr);
	return;
    }

    STARTUPINFO sInfo;
    PROCESS_INFORMATION pInfo;

    GetStartupInfo(&sInfo);

    _snprintf(pszStr, 4096, "\"%s\" -iupdate -old \"%s\" -new \"%s\" -pid %d", szExeCopy, szExe, pszFileName, GetCurrentProcessId());
    //dbg_printf("launching '%s'\n", pszStr);

    if (!CreateProcess(NULL, 
	    pszStr,
	    NULL, NULL, FALSE, 
	    DETACHED_PROCESS,
	    NULL, NULL, 
	    &sInfo, &pInfo))
    {
	error = GetLastError();
	err_printf("CreateProcess failed for '%s'\n", pszStr);
	Translate_Error(error, pszStr);
	err_printf("Error: %s\n", pszStr);
	return;
    }
    CloseHandle(pInfo.hProcess);
    CloseHandle(pInfo.hThread);
}

void UpdateMPD(char *pszOldFileName, char *pszNewFileName, int nPid)
{
    int error;
    char pszStr[4096];
    HANDLE hMPD;
    
    //FILE *fout;
    //fout = fopen("c:\\temp\\update.out", "w");

    // Open a handle to the running service
    hMPD = OpenProcess(SYNCHRONIZE, FALSE, nPid);
    if (hMPD == NULL)
    {
	error = GetLastError();
	Translate_Error(error, pszStr);
	//fprintf(fout, "OpenProcess(%d) failed, %s\n", nPid, pszStr);
	//fclose(fout);
	CloseHandle(hMPD);
	return;
    }

    // Stop the service
    CmdStopService();

    // Wait for the service to exit
    if (WaitForSingleObject(hMPD, 20000) != WAIT_OBJECT_0)
    {
	error = GetLastError();
	Translate_Error(error, pszStr);
	//fprintf(fout, "Waiting for the old mpd to stop failed. %s\n", pszStr);
	//fclose(fout);
	CloseHandle(hMPD);
	return;
    }

    CloseHandle(hMPD);

    // Delete the old service
    if (!DeleteFile(pszOldFileName))
    {
	error = GetLastError();
	Translate_Error(error, pszStr);
	//fprintf(fout, "DeleteFile(%s) failed.\nError: %s\n", pszOldFileName, pszStr);
	//fclose(fout);
	return;
    }

    // Move the new service to the old service's spot
    if (!MoveFile(pszNewFileName, pszOldFileName))
    {
	error = GetLastError();
	Translate_Error(error, pszStr);
	//fprintf(fout, "MoveFile(%s,%s) failed.\nError: %s\n", pszNewFileName, pszOldFileName, pszStr);
	//fclose(fout);
	return;
    }

    char szExe[1024];

    if (!GetModuleFileName(NULL, szExe, 1024))
    {
	Translate_Error(GetLastError(), pszStr);
	//fprintf(fout, "GetModuleFileName failed.\nError: %s\n", pszStr);
	return;
    }

    STARTUPINFO sInfo;
    PROCESS_INFORMATION pInfo;

    GetStartupInfo(&sInfo);

    _snprintf(pszStr, 4096, "\"%s\" -startdelete \"%s\"", pszOldFileName, szExe);
    //fprintf(fout, "launching '%s'\n", pszStr);

    if (!CreateProcess(NULL, 
	    pszStr,
	    NULL, NULL, FALSE, 
	    DETACHED_PROCESS,
	    NULL, NULL, 
	    &sInfo, &pInfo))
    {
	error = GetLastError();
	//fprintf(fout, "CreateProcess failed for '%s'\n", pszStr);
	Translate_Error(error, pszStr);
	//fprintf(fout, "Error: %s\n", pszStr);
	//fclose(fout);
	return;
    }
    CloseHandle(pInfo.hProcess);
    CloseHandle(pInfo.hThread);

    //fclose(fout);
}

void RestartMPD()
{
    int error;
    char szExe[1024];
    char pszStr[2048];

    if (!GetModuleFileName(NULL, szExe, 1024))
    {
	Translate_Error(GetLastError(), pszStr);
	dbg_printf("GetModuleFileName failed.\nError: %s\n", pszStr);
	return;
    }

    STARTUPINFO sInfo;
    PROCESS_INFORMATION pInfo;

    GetStartupInfo(&sInfo);

    _snprintf(pszStr, 2048, "\"%s\" -restart", szExe);
    //dbg_printf("launching '%s'\n", pszStr);

    if (!CreateProcess(NULL, 
	    pszStr,
	    NULL, NULL, FALSE, 
	    DETACHED_PROCESS,
	    NULL, NULL, 
	    &sInfo, &pInfo))
    {
	error = GetLastError();
	err_printf("CreateProcess failed for '%s'\n", pszStr);
	Translate_Error(error, pszStr);
	err_printf("Error: %s\n", pszStr);
	return;
    }
    CloseHandle(pInfo.hProcess);
    CloseHandle(pInfo.hThread);
}
