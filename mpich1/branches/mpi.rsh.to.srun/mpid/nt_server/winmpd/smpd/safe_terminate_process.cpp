#define STRICT
#include <windows.h>

BOOL SafeTerminateProcess(HANDLE hProcess, UINT uExitCode)
{
    DWORD dwTID, dwCode, dwErr = 0;
    HANDLE hProcessDup = INVALID_HANDLE_VALUE;
    HANDLE hRT = NULL;
    HINSTANCE hKernel = GetModuleHandle("Kernel32");
    BOOL bSuccess = FALSE;

    BOOL bDup = DuplicateHandle(GetCurrentProcess(),
	hProcess,
	GetCurrentProcess(),
	&hProcessDup,
	PROCESS_ALL_ACCESS,
	FALSE,
	0);

    if (GetExitCodeProcess((bDup) ? hProcessDup : hProcess, &dwCode) &&
	(dwCode == STILL_ACTIVE))
    {
	FARPROC pfnExitProc;

	pfnExitProc = GetProcAddress(hKernel, "ExitProcess");

	if (pfnExitProc)
	{
	    hRT = CreateRemoteThread((bDup) ? hProcessDup : hProcess,
		NULL,
		0,
		// This relies on the probability that Kernel32.dll is mapped to the same place on all processes
		// If it gets relocated, this function will produce spurious results
		(LPTHREAD_START_ROUTINE)pfnExitProc,
		(PVOID)uExitCode, 0, &dwTID);
	}
	
	if (hRT == NULL)
	    dwErr = GetLastError();
    }
    else
    {
	dwErr = ERROR_PROCESS_ABORTED;
    }

    if (hRT)
    {
	if (WaitForSingleObject((bDup) ? hProcessDup : hProcess, 30000) == WAIT_OBJECT_0)
	    bSuccess = TRUE;
	else
	{
	    dwErr = ERROR_TIMEOUT;
	    bSuccess = FALSE;
	}
	CloseHandle(hRT);
    }

    if (bDup)
	CloseHandle(hProcessDup);

    if (!bSuccess)
	SetLastError(dwErr);

    return bSuccess;
}
