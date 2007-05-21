#include "mpdimpl.h"
#include "safe_terminate_process.h"

struct WaitForSocketCloseStruct
{
    SOCKET hSocket;
    HANDLE hProcess;
};

void WaitForSocketClose(WaitForSocketCloseStruct *pArg)
{
    SOCKET hSocket;
    HANDLE hProcess, hEvent;

    hSocket = pArg->hSocket;
    hProcess = pArg->hProcess;
    delete pArg;
    pArg = NULL;

    hEvent = WSACreateEvent();
    WSAEventSelect(hSocket, hEvent, FD_CLOSE);

    if (WaitForSingleObject(hEvent, INFINITE) == WAIT_OBJECT_0)
    {
	if (hProcess != NULL)
	{
	    if (!SafeTerminateProcess(hProcess, 3000))
	    {
		if (GetLastError() != ERROR_PROCESS_ABORTED)
		    TerminateProcess(hProcess, 3001);
	    }
	}
    }

    WSACloseEvent(hEvent);
}

bool ConnectAndRedirectInput(HANDLE hIn, char *pszHostPort, HANDLE hProcess, DWORD dwPid, int nRank)
{
    DWORD dwThreadID;
    RedirectSocketArg *pArg;
    SOCKET sock;
    char pszHost[MAX_HOST_LENGTH];
    int nPort;
    char *pszPort;
    int nLength;
    char ch = 0;
    int iter;

    if ((pszHostPort == NULL) || (*pszHostPort == '\0'))
    {
	if (hIn != NULL)
	    CloseHandle(hIn);
	return true;
    }

    pszPort = strstr(pszHostPort, ":");
    if (pszPort == NULL)
    {
	if (hIn != NULL)
	    CloseHandle(hIn);
	return false;
    }
    nLength = pszPort - pszHostPort;
    strncpy(pszHost, pszHostPort, nLength);
    pszHost[nLength] = '\0';
    pszPort++;

    nPort = atoi(pszPort);

    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	err_printf("ConnectAndRedirectInput: easy_create failed, error %d\n", WSAGetLastError());
	if (hIn != NULL)
	    CloseHandle(hIn);
	return false;
    }
    if (easy_connect(sock, pszHost, nPort) == SOCKET_ERROR)
    {
	err_printf("ConnectAndRedirectInput: easy_connect(%s:%d) failed, error %d\n", pszHost, nPort, WSAGetLastError());
	easy_closesocket(sock);
	if (hIn != NULL)
	    CloseHandle(hIn);
	return false;
    }

    // Send header indicating stdin redirection
    if (easy_send(sock, &ch, sizeof(char)) == SOCKET_ERROR)
    {
	err_printf("ConnectAndRedirectInput: easy_send(%d) failed, error %d\n", sock, WSAGetLastError());
	easy_closesocket(sock);
	if (hIn != NULL)
	    CloseHandle(hIn);
	return false;
    }

    // Start thread to transfer data
    pArg = new RedirectSocketArg;
    pArg->hRead = NULL;
    pArg->sockRead = sock;
    pArg->hWrite = hIn;
    pArg->sockWrite = INVALID_SOCKET;
    pArg->bReadisPipe = false;
    pArg->bWriteisPipe = true;
    pArg->hProcess = hProcess;
    pArg->dwPid = dwPid;
    pArg->nRank = nRank;
    pArg->cType = 0;
    HANDLE hThread;
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectSocketThread, pArg, 0, &dwThreadID);
	if (hThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hThread == NULL)
    {
	err_printf("ConnectAndRedirectInput: CreateThread(RedirectSocketThread) failed, error %d\n", GetLastError());
	CloseHandle(hIn);
	easy_closesocket(sock);
	return false;
    }
    else
	CloseHandle(hThread);
    return true;
}

bool ConnectAndRedirectOutput(HANDLE hOut, char *pszHostPort, HANDLE hProcess, DWORD dwPid, int nRank, char cType)
{
    DWORD dwThreadID;
    RedirectSocketArg *pArg;
    SOCKET sock;
    char pszHost[MAX_HOST_LENGTH];
    int nPort;
    char *pszPort;
    int nLength;
    int iter;

    if ((pszHostPort == NULL) || (*pszHostPort == '\0'))
    {
	if (hOut != NULL)
	    CloseHandle(hOut);
	return true;
    }

    pszPort = strstr(pszHostPort, ":");
    if (pszPort == NULL)
    {
	if (hOut != NULL)
	    CloseHandle(hOut);
	return false;
    }
    nLength = pszPort - pszHostPort;
    strncpy(pszHost, pszHostPort, nLength);
    pszHost[nLength] = '\0';
    pszPort++;

    nPort = atoi(pszPort);
    
    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	err_printf("ConnectAndRedirectOutput: easy_create failed, error %d\n", WSAGetLastError());
	if (hOut != NULL)
	    CloseHandle(hOut);
	return false;
    }
    if (easy_connect(sock, pszHost, nPort) == SOCKET_ERROR)
    {
	err_printf("ConnectAndRedirectOutput: easy_connect(%s:%d) failed, error %d\n", pszHost, nPort, WSAGetLastError());
	easy_closesocket(sock);
	if (hOut != NULL)
	    CloseHandle(hOut);
	return false;
    }

    // Send header indicating stdout/err redirection
    if (easy_send(sock, &cType, sizeof(char)) == SOCKET_ERROR)
    {
	err_printf("ConnectAndRedirectOutput: easy_send(%d) failed, error %d\n", sock, WSAGetLastError());
	easy_closesocket(sock);
	if (hOut != NULL)
	    CloseHandle(hOut);
	return false;
    }

    // Start thread to transfer data
    pArg = new RedirectSocketArg;
    pArg->hWrite = NULL;
    pArg->sockWrite = sock;
    pArg->hRead = hOut;
    pArg->sockRead = INVALID_SOCKET;
    pArg->bReadisPipe = true;
    pArg->bWriteisPipe = false;
    pArg->hProcess = hProcess;
    pArg->dwPid = dwPid;
    pArg->nRank = nRank;
    pArg->cType = cType;
    HANDLE hThread;
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectSocketThread, pArg, 0, &dwThreadID);
	if (hThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hThread == NULL)
    {
	err_printf("ConnectAndRedirectOutput: CreateThread(RedirectSocketThread) failed, error %d\n", GetLastError());
	CloseHandle(hOut);
	easy_closesocket(sock);
	return false;
    }
    else
	CloseHandle(hThread);

    WaitForSocketCloseStruct *pArgWait;
    pArgWait = new WaitForSocketCloseStruct;
    pArgWait->hProcess = hProcess;
    pArgWait->hSocket = sock;

    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)WaitForSocketClose, pArgWait, 0, &dwThreadID);
	if (hThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hThread == NULL)
    {
	err_printf("ConnectAndRedirectOutput: CreateThread(WaitForSocketClose) failed, error %d\n", GetLastError());
	CloseHandle(hOut);
	easy_closesocket(sock);
	return false;
    }
    else
	CloseHandle(hThread);

    return true;
}

bool ConnectAndRedirect2Outputs(HANDLE hOut, HANDLE hErr, char *pszHostPort, HANDLE hProcess, DWORD dwPid, int nRank)
{
    DWORD dwThreadID;
    RedirectSocketArg *pArg;
    SOCKET sock;
    char pszHost[MAX_HOST_LENGTH];
    int nPort;
    char *pszPort;
    int nLength;
    char cType = 1;
    int iter;

    if ((pszHostPort == NULL) || (*pszHostPort == '\0'))
    {
	if (hOut != NULL)
	    CloseHandle(hOut);
	if (hErr != NULL)
	    CloseHandle(hErr);
	return true;
    }

    pszPort = strstr(pszHostPort, ":");
    if (pszPort == NULL)
    {
	if (hOut != NULL)
	    CloseHandle(hOut);
	if (hErr != NULL)
	    CloseHandle(hErr);
	return false;
    }
    nLength = pszPort - pszHostPort;
    strncpy(pszHost, pszHostPort, nLength);
    pszHost[nLength] = '\0';
    pszPort++;

    nPort = atoi(pszPort);

    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	err_printf("ConnectAndRedirect2Outputs: easy_create failed, error %d\n", WSAGetLastError());
	if (hOut != NULL)
	    CloseHandle(hOut);
	if (hErr != NULL)
	    CloseHandle(hErr);
	return false;
    }
    if (easy_connect(sock, pszHost, nPort) == SOCKET_ERROR)
    {
	err_printf("ConnectAndRedirect2Outputs: easy_connect(%s:%d) failed, error %d\n", pszHost, nPort, WSAGetLastError());
	easy_closesocket(sock);
	if (hOut != NULL)
	    CloseHandle(hOut);
	if (hErr != NULL)
	    CloseHandle(hErr);
	return false;
    }

    // Send header indicating stdout/err redirection
    if (easy_send(sock, &cType, sizeof(char)) == SOCKET_ERROR)
    {
	err_printf("ConnectAndRedirect2Outputs: easy_send(%d) '%c' failed, error %d\n", sock, cType, WSAGetLastError());
	easy_closesocket(sock);
	if (hOut != NULL)
	    CloseHandle(hOut);
	if (hErr != NULL)
	    CloseHandle(hErr);
	return false;
    }

    HANDLE hMutex = CreateMutex(NULL, FALSE, NULL);
    HANDLE hThread;

    // Start thread to transfer data from hOut
    pArg = new RedirectSocketArg;
    pArg->hWrite = NULL;
    pArg->sockWrite = sock;
    pArg->hRead = hOut;
    pArg->sockRead = INVALID_SOCKET;
    pArg->bReadisPipe = true;
    pArg->bWriteisPipe = false;
    pArg->hProcess = hProcess;
    pArg->dwPid = dwPid;
    pArg->hMutex = hMutex;
    pArg->bFreeMutex = false;
    pArg->nRank = nRank;
    pArg->cType = 1;
    pArg->hOtherThread = NULL;
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectLockedSocketThread, pArg, 0, &dwThreadID);
	if (hThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hThread == NULL)
    {
	err_printf("ConnectAndRedirect2Outputs: CreateThread(RedirectLockedSocketThread) failed, error %d\n", GetLastError());
	if (hOut != NULL)
	    CloseHandle(hOut);
	easy_closesocket(sock);
	if (hErr != NULL)
	    CloseHandle(hErr);
	return false;
    }

    // Start thread to transfer data from hErr
    pArg = new RedirectSocketArg;
    pArg->nRank = nRank;
    pArg->hWrite = NULL;
    pArg->sockWrite = sock;
    pArg->hRead = hErr;
    pArg->sockRead = INVALID_SOCKET;
    pArg->bReadisPipe = true;
    pArg->bWriteisPipe = false;
    pArg->hProcess = NULL;
    pArg->dwPid = -1;
    pArg->hMutex = hMutex;
    pArg->bFreeMutex = true;
    pArg->nRank = nRank;
    pArg->cType = 2;
    pArg->hOtherThread = hThread;
    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectLockedSocketThread, pArg, 0, &dwThreadID);
	if (hThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hThread == NULL)
    {
	err_printf("ConnectAndRedirect2Outputs: CreateThread(RedirectLockedSocketThread) failed, error %d\n", GetLastError());
	CloseHandle(pArg->hOtherThread);
	if (hOut != NULL)
	    CloseHandle(hOut);
	if (hErr != NULL)
	    CloseHandle(hErr);
	easy_closesocket(sock);
	return false;
    }
    else
	CloseHandle(hThread);

    WaitForSocketCloseStruct *pArgWait;
    pArgWait = new WaitForSocketCloseStruct;
    pArgWait->hProcess = hProcess;
    pArgWait->hSocket = sock;

    for (iter=0; iter<CREATE_THREAD_RETRIES; iter++)
    {
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)WaitForSocketClose, pArgWait, 0, &dwThreadID);
	if (hThread != NULL)
	    break;
	Sleep(CREATE_THREAD_SLEEP_TIME);
    }
    if (hThread == NULL)
    {
	err_printf("ConnectAndRedirect2Outputs: CreateThread(WaitForSocketClose) failed, error %d\n", GetLastError());
	CloseHandle(pArg->hOtherThread);
	if (hOut != NULL)
	    CloseHandle(hOut);
	if (hErr != NULL)
	    CloseHandle(hErr);
	easy_closesocket(sock);
	return false;
    }
    else
	CloseHandle(hThread);

    return true;
}
