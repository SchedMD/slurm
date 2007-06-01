#include "mpdimpl.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include "Translate_Error.h"
#include "safe_terminate_process.h"

void RedirectSocketThread(RedirectSocketArg *arg)
{
    char pBuffer[1024+sizeof(int)+sizeof(char)+sizeof(int)];
    DWORD num_read, num_written;
    
    if (arg->bReadisPipe)
    {
	// The format is pBuffer[int nDataLength | char cType | int nRank | char[] data]
	pBuffer[sizeof(int)] = arg->cType; // The type never changes
	*(int*)&pBuffer[sizeof(int)+sizeof(char)] = arg->nRank; // The rank never changes
	
	while (ReadFile(arg->hRead, &pBuffer[sizeof(int)+sizeof(char)+sizeof(int)], 1024, &num_read, NULL))
	{
	    if (arg->bWriteisPipe)
	    {
		if (!WriteFile(arg->hWrite, pBuffer, num_read, &num_written, NULL))
		    break;
	    }
	    else
	    {
		*(int*)pBuffer = num_read;
		if (easy_send(arg->sockWrite, pBuffer, num_read + sizeof(int) + sizeof(char) + sizeof(int)) == SOCKET_ERROR)
		{
		    // Kill the process if the socket to redirect output is closed
		    if (arg->hProcess != NULL)
		    {
			if (!SafeTerminateProcess(arg->hProcess, 10000010))
			{
			    if (GetLastError() != ERROR_PROCESS_ABORTED)
				TerminateProcess(arg->hProcess, 1);
			}
		    }
		    break;
		}
	    }
	}
    }
    else
    {
	while (num_read = easy_receive_some(arg->sockRead, pBuffer, 1024))
	{
	    if (num_read == SOCKET_ERROR || num_read == 0)
	    {
		// Kill the process if the socket to redirect input is closed
		if (arg->hProcess != NULL)
		{
		    if (!SafeTerminateProcess(arg->hProcess, 10000011))
		    {
			if (GetLastError() != ERROR_PROCESS_ABORTED)
			    TerminateProcess(arg->hProcess, 1);
		    }
		}
		break;
	    }
	    if (arg->bWriteisPipe)
	    {
		if (!WriteFile(arg->hWrite, pBuffer, num_read, &num_written, NULL))
		    break;
	    }
	    else
	    {
		if (easy_send(arg->sockWrite, pBuffer, num_read) == SOCKET_ERROR)
		    break;
	    }
	}
    }
    if (arg->bReadisPipe)
	CloseHandle(arg->hRead);
    if (arg->bWriteisPipe)
	CloseHandle(arg->hWrite);
    if (arg->sockRead != INVALID_SOCKET)
    {
	easy_closesocket(arg->sockRead);
	arg->sockRead = INVALID_SOCKET;
    }
    if (arg->sockWrite != INVALID_SOCKET)
    {
	easy_closesocket(arg->sockWrite);
	arg->sockWrite = INVALID_SOCKET;
    }
    delete arg;
}

void RedirectLockedSocketThread(RedirectSocketArg *arg)
{
    char pBuffer[1024+sizeof(int)+sizeof(char)+sizeof(int)];
    DWORD num_read, num_written;

    if (arg->bReadisPipe)
    {
	// The format is pBuffer[int nDataLength | char cType | int nRank | char[] data]
	pBuffer[sizeof(int)] = arg->cType; // The type never changes
	*(int*)&pBuffer[sizeof(int)+sizeof(char)] = arg->nRank; // The rank never changes
	
	while (ReadFile(arg->hRead, &pBuffer[sizeof(int)+sizeof(char)+sizeof(int)], 1024, &num_read, NULL))
	{
	    if (arg->bWriteisPipe)
	    {
		if (!WriteFile(arg->hWrite, pBuffer, num_read, &num_written, NULL))
		    break;
	    }
	    else
	    {
		if (num_read == 0)
		    break;
		WaitForSingleObject(arg->hMutex, INFINITE);
		*(int*)pBuffer = num_read;
		if (easy_send(arg->sockWrite, pBuffer, num_read + sizeof(int) + sizeof(char) + sizeof(int)) == SOCKET_ERROR)
		{
		    // Kill the process if the socket to redirect output is closed
		    if (arg->hProcess != NULL)
		    {
			if (!SafeTerminateProcess(arg->hProcess, 10000012))
			{
			    if (GetLastError() != ERROR_PROCESS_ABORTED)
				TerminateProcess(arg->hProcess, 1);
			}
		    }
		    ReleaseMutex(arg->hMutex);
		    break;
		}
		ReleaseMutex(arg->hMutex);
	    }
	}
    }
    else
    {
	while (num_read = easy_receive_some(arg->sockRead, pBuffer, 1024))
	{
	    if (num_read == SOCKET_ERROR || num_read == 0)
	    {
		// Kill the process if the socket to redirect input is closed
		if (arg->hProcess != NULL)
		{
		    if (!SafeTerminateProcess(arg->hProcess, 10000012))
		    {
			if (GetLastError() != ERROR_PROCESS_ABORTED)
			    TerminateProcess(arg->hProcess, 1);
		    }
		}
		break;
	    }
	    if (arg->bWriteisPipe)
	    {
		if (!WriteFile(arg->hWrite, pBuffer, num_read, &num_written, NULL))
		    break;
	    }
	    else
	    {
		if (easy_send(arg->sockWrite, pBuffer, num_read) == SOCKET_ERROR)
		    break;
	    }
	}
    }
    if (arg->bReadisPipe)
	CloseHandle(arg->hRead);
    if (arg->bWriteisPipe)
	CloseHandle(arg->hWrite);
    if (arg->sockRead != INVALID_SOCKET)
    {
	easy_closesocket(arg->sockRead);
	arg->sockRead = INVALID_SOCKET;
    }
    if (arg->bFreeMutex)
    {
	WaitForSingleObject(arg->hOtherThread, INFINITE);
	if (arg->sockWrite != INVALID_SOCKET)
	{
	    dbg_printf("closing output redirection socket %d, rank %d\n", arg->sockWrite, arg->nRank);
	    if (easy_closesocket(arg->sockWrite) == SOCKET_ERROR)
	    {
		err_printf("ERROR: easy_closesocket(%d) failed, error %d\n", arg->sockWrite, WSAGetLastError());
	    }
	    arg->sockWrite = INVALID_SOCKET;
	}
	if (arg->hMutex != NULL)
	    CloseHandle(arg->hMutex);
    }
    if (arg->hOtherThread)
	CloseHandle(arg->hOtherThread);
    delete arg;
}
