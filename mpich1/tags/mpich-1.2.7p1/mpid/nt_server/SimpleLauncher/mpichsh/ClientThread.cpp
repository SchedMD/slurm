#include "stdafx.h"
#include "ClientThread.h"
#include "global.h"
#include <time.h>
#include "LaunchProcess.h"
#include "sockets.h"

long g_ID = 0;

struct RedirectSocketArg
{
	int i;
	bool bReadisPipe;
	SOCKET hRead;
	WSAEVENT hReadEvent;
	bool bWriteisPipe;
	SOCKET hWrite;
	WSAEVENT hWriteEvent;
	HANDLE hProcess;
	DWORD dwPid;
};

void RedirectSocketThread(RedirectSocketArg *arg)
{
	char pBuffer[1024];
	DWORD num_read, num_written;
	if (arg->bReadisPipe)
	{
		while (ReadFile((HANDLE)(arg->hRead), pBuffer, 1024, &num_read, NULL))
		{
			if (arg->bWriteisPipe)
			{
				if (!WriteFile((HANDLE)(arg->hWrite), pBuffer, num_read, &num_written, NULL))
					break;
			}
			else
			{
				if (SendBlocking(arg->hWrite, pBuffer, num_read, 0) == SOCKET_ERROR)
					break;
			}
		}
	}
	else
	{
		while (num_read = ReceiveSome(arg->hRead, arg->hReadEvent, pBuffer, 1024, 0))
		{
			if (num_read == SOCKET_ERROR)
			{
				if (arg->hProcess != NULL)
				{
					int error = 1;
					if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, arg->dwPid))
					{
						if (WaitForSingleObject(arg->hProcess, 500) == WAIT_OBJECT_0)
							error = 0;
					}
					if (error)
						TerminateProcess(arg->hProcess, 1);
				}
				break;
			}
			//SendMessage(g_hWnd, WM_USER+1, 0, (LPARAM)pBuffer);
			if (arg->bWriteisPipe)
			{
				if (!WriteFile((HANDLE)(arg->hWrite), pBuffer, num_read, &num_written, NULL))
					break;
			}
			else
			{
				if (SendBlocking(arg->hWrite, pBuffer, num_read, 0) == SOCKET_ERROR)
					break;
			}
		}
	}
	if (arg->bReadisPipe)
		CloseHandle((HANDLE)arg->hRead);
	if (arg->bWriteisPipe)
		CloseHandle((HANDLE)arg->hWrite);
	delete arg;
}

void SocketClientThread(SocketClientThreadArg *arg)
{
	int length;
	char msg[1024];
	char dir[MAX_PATH] = ".";
	char env[1024] = "";
	char cmd[MAX_PATH] = "";
	char *p, *p2;
	HANDLE hProcess;
	long id;

	id = InterlockedIncrement(&g_ID);

	if (ReceiveBlocking(arg->sock, arg->sock_event, (char*)&length, sizeof(int), 0))
	{
		Simple_closesocket(arg->sock, arg->sock_event);
		return;
	}

	if (length <= 1024)
	{
		if (ReceiveBlocking(arg->sock, arg->sock_event, msg, length, 0))
		{
			Simple_closesocket(arg->sock, arg->sock_event);
			return;
		}

		p = msg;
		if (strnicmp(p, "-dir", 4) == 0)
		{
			p = &p[5];
			p2 = dir;
			while (*p && (*p != '\"'))
			{
				*p2 = *p;
				p++;
				p2++;
			}
			*p2 = '\0';
			if (*p)
				p++;
		}

		if (strnicmp(p, "-env", 4) == 0)
		{
			p = &p[5];
			p2 = env;
			while (*p && (*p != '\"'))
			{
				*p2 = *p;
				p++;
				p2++;
			}
			*p2 = '\0';
			if (*p)
				p++;
		}

		strcpy(cmd, p);
	}

	tm *theTime;
	time_t tnum;
	time(&tnum);
	int hour;
	theTime = localtime(&tnum);
	p = &cmd[strlen(cmd)];
	while (p>=cmd && *p != '\\')
		p--;
	if (*p == '\\')
		p++;
	hour = theTime->tm_hour % 12;
	if (hour == 0)
		hour = 12;
	sprintf(msg, "%02d:%d:%02d:[%d] %s", hour, theTime->tm_min, theTime->tm_sec, id, p);
	SendMessage(g_hWnd, WM_USER+1, 0, (LPARAM)msg);

	HANDLE hIn, hOut, hErr;
	DWORD dwPid;
	RedirectSocketArg *rArg, *oArg, *eArg;

	hProcess = LaunchProcess(cmd, env, dir, &hIn, &hOut, &hErr, &dwPid);
	if (hProcess == INVALID_HANDLE_VALUE)
	{
		SendMessage(g_hWnd, WM_USER+1, 0, (LPARAM)"LaunchProcess failed");
		Simple_closesocket(arg->sock, arg->sock_event);
		return;
	}

	rArg = new RedirectSocketArg;
	rArg->hRead = arg->sock;
	rArg->hReadEvent = arg->sock_event;
	rArg->hWrite = (SOCKET)hIn;
	rArg->bReadisPipe = false;
	rArg->bWriteisPipe = true;
	oArg = new RedirectSocketArg;
	oArg->hWrite = arg->sock;
	oArg->hWriteEvent = arg->sock_event;
	oArg->hRead = (SOCKET)hOut;
	oArg->bReadisPipe = true;
	oArg->bWriteisPipe = false;
	eArg = new RedirectSocketArg;
	eArg->hWrite = arg->sock;
	eArg->hWriteEvent = arg->sock_event;
	eArg->hRead = (SOCKET)hErr;
	eArg->bReadisPipe = true;
	eArg->bWriteisPipe = false;

	DWORD dwThreadID;
	rArg->i = 0;
	rArg->hProcess = hProcess;
	rArg->dwPid = dwPid;
	CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectSocketThread, rArg, 0, &dwThreadID));
	oArg->i = 1;
	oArg->hProcess = hProcess;
	oArg->dwPid = dwPid;
	CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectSocketThread, oArg, 0, &dwThreadID));
	eArg->i = 2;
	eArg->hProcess = hProcess;
	eArg->dwPid = dwPid;
	CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectSocketThread, eArg, 0, &dwThreadID));

	WaitForSingleObject(hProcess, INFINITE);
	CloseHandle(hProcess);

	time(&tnum);
	theTime = localtime(&tnum);
	hour = theTime->tm_hour % 12;
	if (hour == 0)
		hour = 12;
	sprintf(msg, "%02d:%d:%02d:[%d] Finished: %s", hour, theTime->tm_min, theTime->tm_sec, id, p);
	SendMessage(g_hWnd, WM_USER+1, 0, (LPARAM)msg);

	if (Simple_closesocket(arg->sock, arg->sock_event) == SOCKET_ERROR)
		SendMessage(g_hWnd, WM_USER+1, 0, (LPARAM)"close socket failed");
}
