#include "LeftThread.h"
#include "sockets.h"
#include "Command.h"
#include <stdio.h>
#include "global.h"
#include "GetReturnThread.h"
#include "LaunchMPDProcess.h"
#include "LaunchNode.h"
#include "GetCPUsage.h"

// Function name	: LeftThread
// Description	    : 
// Return type		: void 
// Argument         : LaunchMPDArg *pArg
void LeftThread(LaunchMPDArg *pArg)
{
	SOCKET sock, listen_sock;
	WSAEVENT sock_event, listen_sock_event;
	char host[100] = "";
	int port = 0;
	int error;
	unsigned long nLocalIP, nTempIP, nGetIdentifier;
	int nLocalPort, nTempPort;
	CommandData Command;
	MPD_CMD_HANDLE hCommand;
	char pszDbsID[256], *pszDbsKey;
	void *pDbsValue;
	int n;
	bool bPutPersistent = true;
	char *pBuf;
	char pShortBuffer[100];
	LaunchNode *pLaunchNode = NULL;

	if (error = NT_create_bind_socket(&listen_sock, &listen_sock_event, 0))
	{
		printf("LeftThread: create and bind listen socket failed, error %d\n", error);
		ExitProcess(error);
	}

	if (WSAEventSelect(listen_sock, listen_sock_event, FD_ACCEPT) == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		printf("LeftThread: WSAEventSelect(FD_ACCEPT) failed for the listen socket, error %d", error);
		ExitProcess(error);
	}

	if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		printf("LeftThread: listen failed, error %d\n", error);
		ExitProcess(error);
	}

	NT_get_sock_info(listen_sock, host, &port);

	if (pArg == NULL)
	{
		printf("%s\n%d\n", host, port);
		fflush(stdout);
	}
	else
	{
		strcpy(pArg->pszHost, host);
		pArg->nPort = port;
		SetEvent(pArg->hReadyEvent);
	}

	while (true)
	{
		sock = accept(listen_sock, NULL, NULL);

		if (sock != INVALID_SOCKET)
			break;
		error = GetLastError();
		if (error == WSAEWOULDBLOCK)
		{
			WSAResetEvent(listen_sock_event);
			WSAEventSelect(listen_sock, listen_sock_event, FD_ACCEPT);
		}
		Sleep(100);
	}
	NT_closesocket(listen_sock, listen_sock_event);

	if ((sock_event = WSACreateEvent()) == WSA_INVALID_EVENT)
	{
		error = WSAGetLastError();
		printf("LeftThread: WSACreateEvent failed, error %d\n", error);
		ExitProcess(error);
	}
	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		printf("LeftThread: WSAEventSelect failed, error %d\n", error);
		ExitProcess(error);
	}

	//printf("socket accepted\n");
	NT_get_sock_info(sock, host, &nLocalPort);
	g_List.SetMyID(host, nLocalPort);
	g_List.GetMyID(&nLocalIP, &nLocalPort);

	g_bLeftConnected = true;

	while (true)
	{
		if (ReceiveBlocking(sock, sock_event, (char*)&Command.hCmd, sizeof(CommandHeader), 0))
			break;
		//printf("[%d.%d.%d.%d:%d:%d]\n", (int)(Command.hCmd.nSrcIP & 0xff), (int)((Command.hCmd.nSrcIP >> 8) & 0xff), (int)((Command.hCmd.nSrcIP >> 16) & 0xff),	(int)((Command.hCmd.nSrcIP >> 24) & 0xff), Command.hCmd.nSrcPort, (int)Command.hCmd.cCommand);
		if (Command.hCmd.nBufferLength > CMD_BUFF_SIZE)
		{
			printf("Command buffer too long, length: %d, exiting\n", Command.hCmd.nBufferLength);
			ExitProcess(1);
		}

		if (Command.hCmd.nSrcIP == nLocalIP && Command.hCmd.nSrcPort == nLocalPort)
		{
			// This is a command sent from myself which has traversed the entire ring.
			// Either handle the command, or eat up the data and throw it away.
			switch (Command.hCmd.cCommand)
			{
			case MPD_CMD_HOSTS:
				if (Command.hCmd.nBufferLength > 0)
					ReceiveBlocking(sock, sock_event, Command.hCmd.pData->pCommandBuffer, Command.hCmd.nBufferLength, 0);
				Command.hCmd.pData->pCommandBuffer[Command.hCmd.nBufferLength] = '\0';
				Command.hCmd.pData->hCmd.nBufferLength = Command.hCmd.nBufferLength + 1;
				MarkCommandCompleted(Command.hCmd.pData);
				break;
			case MPD_CMD_CPUSAGE:
				if (Command.hCmd.nBufferLength > 0)
					ReceiveBlocking(sock, sock_event, Command.hCmd.pData->pCommandBuffer, Command.hCmd.nBufferLength, 0);
				Command.hCmd.pData->pCommandBuffer[Command.hCmd.nBufferLength] = '\0';
				Command.hCmd.pData->hCmd.nBufferLength = Command.hCmd.nBufferLength + 1;
				MarkCommandCompleted(Command.hCmd.pData);
				break;
			case MPD_CMD_PS:
				if (Command.hCmd.nBufferLength > 0)
					ReceiveBlocking(sock, sock_event, Command.hCmd.pData->pCommandBuffer, Command.hCmd.nBufferLength, 0);
				Command.hCmd.pData->pCommandBuffer[Command.hCmd.nBufferLength] = '\0';
				Command.hCmd.pData->hCmd.nBufferLength = Command.hCmd.nBufferLength + 1;
				MarkCommandCompleted(Command.hCmd.pData);
				break;
			case MPD_CMD_DESTROY_RING:
				printf("DestroyRing command received ...");fflush(stdout);
				KillRemainingMPDProcesses();
				printf(" Exiting\n");fflush(stdout);
				//Sleep(100);
				//ExitProcess(0);
				ExitThread(0);
				break;
			case MPD_CMD_RUN_THE_RING:
				// Command has finished running the loop
				MarkCommandCompleted(Command.hCmd.pData);
				break;
			case MPD_CMD_PRINT_DATABASE:
				if (Command.hCmd.nBufferLength > 0)
					ReceiveBlocking(sock, sock_event, Command.hCmd.pData->pCommandBuffer, Command.hCmd.nBufferLength, 0);
				Command.hCmd.pData->hCmd.nBufferLength = Command.hCmd.nBufferLength;
				if (g_bDatabaseIsLocal)
				{
					Command.hCmd.pData->hCmd.nBufferLength = CMD_BUFF_SIZE;
					g_Database.PrintStateToBuffer(Command.hCmd.pData->pCommandBuffer, &Command.hCmd.pData->hCmd.nBufferLength);
				}
				MarkCommandCompleted(Command.hCmd.pData);
				break;
			case MPD_CMD_LAUNCH:
				ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
				pBuf = Command.pCommandBuffer;
				pLaunchNode = *(LaunchNode**)pBuf;
				pBuf = pBuf + sizeof(LaunchNode*);
				nTempIP = *(unsigned long *)pBuf;
				pBuf = pBuf + sizeof(unsigned long);
				nTempPort = *(int *)pBuf;
				pBuf = pBuf + sizeof(int);
				if (nTempIP == nLocalIP && nTempPort == nLocalPort)
				{
					//printf("launch command received: '%s'\n", pBuf);fflush(stdout);
					LaunchMPDProcessArg *pArg = new LaunchMPDProcessArg;
					pArg->nIP = nLocalIP;
					pArg->nPort = nLocalPort;
					pArg->nSrcIP = Command.hCmd.nSrcIP;
					pArg->nSrcPort = Command.hCmd.nSrcPort;
					pArg->pszCommand = new char[strlen(pBuf)+1];
					pArg->pNode = pLaunchNode;
					strcpy(pArg->pszCommand, pBuf);
					//printf("launching '%s'\n", pArg->pszCommand);fflush(stdout);
					DWORD dwThreadID;
					CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LaunchMPDProcess, pArg, 0, &dwThreadID));
				}
				else
				{
					// The launch command made it around the ring without anyone satisfying it.
					// It must have bogus host:port values so just throw it away.
					char *pName;
					in_addr in;
					in.S_un.S_addr = nTempIP;
					pName = inet_ntoa(in);
					printf("Unfulfilled launch command for host: %s:%d\n", pName, nTempPort);
					fflush(stdout);
				}
				break;
				/*
			case MPD_CMD_LAUNCH_RET:
				ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
				pBuf = Command.pCommandBuffer;
				nTempIP = *((unsigned long *)pBuf);
				pBuf = pBuf + sizeof(unsigned long);
				nTempPort = *((int *)pBuf);
				if (nTempIP == nLocalIP && nTempPort == nLocalPort)
				{
					pBuf = pBuf + sizeof(int);
					pLaunchNode = *((LaunchNode **)pBuf);
					pBuf = pBuf + sizeof(LaunchNode*);
					pLaunchNode->Set(*((DWORD*)pBuf));
				}
				else
				{
					Command.nCommand = MPD_CMD_FORWARD;
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				break;
				//*/
			default:
				if (Command.hCmd.nBufferLength > 0)
				{
					char *pBuffer = new char[Command.hCmd.nBufferLength];
					ReceiveBlocking(sock, sock_event, pBuffer, Command.hCmd.nBufferLength, 0);
					delete pBuffer;
				}
			}
		}
		else
		{
			switch (Command.hCmd.cCommand)
			{
			case MPD_CMD_ADD:
				Command.nCommand = MPD_CMD_FORWARD;
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(unsigned long), 0);
				nTempIP = *((unsigned long *)pBuf);
				pBuf += sizeof(unsigned long);
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(int), 0);
				nTempPort = *((int *)pBuf);
				pBuf += sizeof(int);
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(int), 0);
				n = *((int *)pBuf);
				pBuf += sizeof(int);
				Command.hCmd.nBufferLength = sizeof(unsigned long) + 2 * sizeof(int);
				
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);

				g_List.Add(nTempIP, nTempPort, n);
				//printf("[%d.%d.%d.%d:%d:%d]\n", (int)(nTempIP & 0xff), (int)((nTempIP >> 8) & 0xff), (int)((nTempIP >> 16) & 0xff),	(int)((nTempIP >> 24) & 0xff), nTempPort, n);
				break;
			case MPD_CMD_REMOVE:
				Command.nCommand = MPD_CMD_FORWARD;
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(unsigned long), 0);
				nTempIP = *((unsigned long *)pBuf);
				pBuf += sizeof(unsigned long);
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(int), 0);
				nTempPort = *((int *)pBuf);
				Command.hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int);
				
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);

				g_List.Remove(nTempIP, nTempPort);
				break;
			case MPD_CMD_INCREMENT:
				Command.nCommand = MPD_CMD_FORWARD;
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(unsigned long), 0);
				nTempIP = *((unsigned long *)pBuf);
				pBuf += sizeof(unsigned long);
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(int), 0);
				nTempPort = *((int *)pBuf);
				Command.hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int);
				
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);

				g_List.Increment(nTempIP, nTempPort);
				break;
			case MPD_CMD_DECREMENT:
				Command.nCommand = MPD_CMD_FORWARD;
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(unsigned long), 0);
				nTempIP = *((unsigned long *)pBuf);
				pBuf += sizeof(unsigned long);
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(int), 0);
				nTempPort = *((int *)pBuf);
				Command.hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int);
				
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);

				g_List.Decrement(nTempIP, nTempPort);
				break;
			case MPD_CMD_ENABLE:
				Command.nCommand = MPD_CMD_FORWARD;
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(unsigned long), 0);
				nTempIP = *((unsigned long *)pBuf);
				pBuf += sizeof(unsigned long);
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(int), 0);
				nTempPort = *((int *)pBuf);
				Command.hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int);
				
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);

				g_List.Enable(nTempIP, nTempPort);
				break;
			case MPD_CMD_DISABLE:
				Command.nCommand = MPD_CMD_FORWARD;
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(unsigned long), 0);
				nTempIP = *((unsigned long *)pBuf);
				pBuf += sizeof(unsigned long);
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(int), 0);
				nTempPort = *((int *)pBuf);
				Command.hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int);
				
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);

				g_List.Disable(nTempIP, nTempPort);
				break;
			case MPD_CMD_DELETE_ID:
				if (g_bDatabaseIsLocal)
				{
					ReceiveBlocking(sock, sock_event, (char*)&n, sizeof(int), 0);
					ReceiveBlocking(sock, sock_event, pszDbsID, n, 0);
					g_Database.Delete(pszDbsID);
				}
				else
				{
					Command.nCommand = MPD_CMD_FORWARD;
					ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				break;
			case MPD_CMD_DELETE_KEY:
				if (g_bDatabaseIsLocal)
				{
					ReceiveBlocking(sock, sock_event, (char*)&n, sizeof(int), 0);
					ReceiveBlocking(sock, sock_event, pszDbsID, n, 0);
					ReceiveBlocking(sock, sock_event, (char*)&n, sizeof(int), 0);
					pszDbsKey = new char[n];
					ReceiveBlocking(sock, sock_event, pszDbsKey, n, 0);
					g_Database.Delete(pszDbsID, pszDbsKey);
					delete pszDbsKey;
				}
				else
				{
					Command.nCommand = MPD_CMD_FORWARD;
					ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				break;
			case MPD_CMD_PUTC:
				bPutPersistent = false;
			case MPD_CMD_PUT:
				if (g_bDatabaseIsLocal)
				{
					ReceiveBlocking(sock, sock_event, (char*)&n, sizeof(int), 0);
					ReceiveBlocking(sock, sock_event, pszDbsID, n, 0);
					ReceiveBlocking(sock, sock_event, (char*)&n, sizeof(int), 0);
					pszDbsKey = new char[n];
					ReceiveBlocking(sock, sock_event, pszDbsKey, n, 0);
					ReceiveBlocking(sock, sock_event, (char*)&n, sizeof(int), 0);
					pDbsValue = new char[n];
					ReceiveBlocking(sock, sock_event, (char*)pDbsValue, n, 0);
					g_Database.Put(pszDbsID, pszDbsKey, pDbsValue, n, bPutPersistent);
				}
				else
				{
					Command.nCommand = MPD_CMD_FORWARD;
					ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				bPutPersistent = true;
				break;
			case MPD_CMD_GET:
				if (g_bDatabaseIsLocal)
				{
					Command.hCmd.nBufferLength = 2 * sizeof(unsigned long) + 2 * sizeof(int);
					ReceiveBlocking(sock, sock_event, (char*)&nTempIP, sizeof(unsigned long), 0);
					ReceiveBlocking(sock, sock_event, (char*)&nTempPort, sizeof(int), 0);
					ReceiveBlocking(sock, sock_event, (char*)&nGetIdentifier, sizeof(unsigned long), 0);
					pBuf = Command.pCommandBuffer;
					*((unsigned long *)pBuf) = nTempIP;
					pBuf += sizeof(unsigned long);
					*((int *)pBuf) = nTempPort;
					pBuf += sizeof(int);
					*((unsigned long *)pBuf) = nGetIdentifier;
					pBuf += sizeof(unsigned long);
					ReceiveBlocking(sock, sock_event, (char*)&n, sizeof(int), 0);
					ReceiveBlocking(sock, sock_event, pszDbsID, n, 0);
					ReceiveBlocking(sock, sock_event, (char*)&n, sizeof(int), 0);
					pszDbsKey = new char[n];
					ReceiveBlocking(sock, sock_event, pszDbsKey, n, 0);
					Command.hCmd.cCommand = MPD_CMD_GETRETURN;
					Command.hCmd.nSrcIP = nLocalIP;
					Command.hCmd.nSrcPort = nLocalPort;
				
					GetReturnThreadArg *pArg = new GetReturnThreadArg;
					pArg->command = Command;
					strcpy(pArg->pszDbsID, pszDbsID);
					pArg->pszDbsKey = pszDbsKey;

					DWORD dwThreadID;
					CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)GetReturnThread, pArg, 0, &dwThreadID));
				}
				else
				{
					Command.nCommand = MPD_CMD_FORWARD;
					ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				break;
			case MPD_CMD_GETRETURN:
				Command.nCommand = MPD_CMD_FORWARD;
				Command.hCmd.nBufferLength = 2 * sizeof(unsigned long) + 2 * sizeof(int);
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(unsigned long), 0); // IP
				nTempIP = *((unsigned long *)pBuf);
				pBuf += sizeof(unsigned long);
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(int), 0); // Port
				nTempPort = *((int *)pBuf);
				pBuf += sizeof(int);
				ReceiveBlocking(sock, sock_event, pBuf, sizeof(unsigned long), 0); // GetIdentifier
				nGetIdentifier = *((unsigned long *)pBuf);
				pBuf += sizeof(unsigned long);
				ReceiveBlocking(sock, sock_event, (char*)&n, sizeof(int), 0); // length
				*((int *)pBuf) = n;
				pBuf += sizeof(int);
				if (nTempIP == nLocalIP && nTempPort == nLocalPort)
				{
					ReceiveBlocking(sock, sock_event, ((CommandData *)nGetIdentifier)->pCommandBuffer, n, 0);
					((CommandData *)nGetIdentifier)->hCmd.nBufferLength = n;
					MarkCommandCompleted((CommandData *)nGetIdentifier);
				}
				else
				{
					ReceiveBlocking(sock, sock_event, pBuf, n, 0); // value
					Command.hCmd.nBufferLength += n;
				
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				break;
			case MPD_CMD_HOSTS:
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, Command.hCmd.nBufferLength, 0);
				pBuf += Command.hCmd.nBufferLength;
				sprintf(pBuf, "%s:%d\n", host, nLocalPort);
				Command.hCmd.nBufferLength += strlen(pBuf);
				Command.nCommand = MPD_CMD_FORWARD;

				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				break;
			case MPD_CMD_CPUSAGE:
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, Command.hCmd.nBufferLength, 0);
				pBuf += Command.hCmd.nBufferLength;
				sprintf(pBuf, "%s:%d %d ", host, nLocalPort, GetCPUsage());
				Command.hCmd.nBufferLength += strlen(pBuf);
				Command.nCommand = MPD_CMD_FORWARD;

				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				break;
			case MPD_CMD_PS:
				pBuf = Command.pCommandBuffer;
				ReceiveBlocking(sock, sock_event, pBuf, Command.hCmd.nBufferLength, 0);
				pBuf += Command.hCmd.nBufferLength;
				//sprintf(pBuf, "%s:%d\n", host, nLocalPort);
				//PrintMPDProcessesToBuffer(pBuf+strlen(pBuf));
				sprintf(pShortBuffer, "%s:%d", host, nLocalPort);
				PrintMPDProcessesToBuffer(pBuf, pShortBuffer);
				Command.hCmd.nBufferLength += strlen(pBuf);
				Command.nCommand = MPD_CMD_FORWARD;

				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				break;
			case MPD_CMD_DESTROY_RING:
				printf("DestroyRing command received ...");fflush(stdout);
				Command.nCommand = MPD_CMD_FORWARD;
				Command.hCmd.nBufferLength = 0;
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				KillRemainingMPDProcesses();
				printf(" Exiting\n");
				ExitProcess(0);
				break;
			case MPD_CMD_RUN_THE_RING:
				Command.nCommand = MPD_CMD_FORWARD;
				Command.hCmd.nBufferLength = 0;
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				break;
			case MPD_CMD_PRINT_LISTS:
				Command.nCommand = MPD_CMD_FORWARD;
				Command.hCmd.nBufferLength = 0;
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				g_List.Print();
				break;
			case MPD_CMD_PRINT_DATABASE:
				if (Command.hCmd.nBufferLength > 0)
					ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
				if (g_bDatabaseIsLocal)
				{
					Command.hCmd.nBufferLength = CMD_BUFF_SIZE;
					g_Database.PrintStateToBuffer(Command.pCommandBuffer, &Command.hCmd.nBufferLength);
					Command.hCmd.nBufferLength++;
				}
				Command.nCommand = MPD_CMD_FORWARD;
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				break;
			case MPD_CMD_LAUNCH:
				ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
				pBuf = Command.pCommandBuffer;
				pLaunchNode = *(LaunchNode **)pBuf;
				pBuf = pBuf + sizeof(LaunchNode*);
				nTempIP = *(unsigned long *)pBuf;
				pBuf = pBuf + sizeof(unsigned long);
				nTempPort = *(int *)pBuf;
				pBuf = pBuf + sizeof(int);
				if (nTempIP == nLocalIP && nTempPort == nLocalPort)
				{
					//printf("launch command received: '%s'\n", pBuf);fflush(stdout);
					LaunchMPDProcessArg *pArg = new LaunchMPDProcessArg;
					pArg->nIP = nLocalIP;
					pArg->nPort = nLocalPort;
					pArg->nSrcIP = Command.hCmd.nSrcIP;
					pArg->nSrcPort = Command.hCmd.nSrcPort;
					pArg->pszCommand = new char[strlen(pBuf)+1];
					pArg->pNode = pLaunchNode;
					strcpy(pArg->pszCommand, pBuf);
					//printf("launching '%s'\n", pArg->pszCommand);fflush(stdout);
					DWORD dwThreadID;
					CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LaunchMPDProcess, pArg, 0, &dwThreadID));
				}
				else
				{
					//printf("forwarding launch command: '%s'\n", pBuf);fflush(stdout);
					Command.nCommand = MPD_CMD_FORWARD;
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				break;
			case MPD_CMD_LAUNCH_RET:
				ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
				pBuf = Command.pCommandBuffer;
				nTempIP = *((unsigned long *)pBuf);
				pBuf = pBuf + sizeof(unsigned long);
				nTempPort = *((int *)pBuf);
				if (nTempIP == nLocalIP && nTempPort == nLocalPort)
				{
					pBuf = pBuf + sizeof(int);
					pLaunchNode = *((LaunchNode **)pBuf);
					pBuf = pBuf + sizeof(LaunchNode*);
					pLaunchNode->Set(*((DWORD*)pBuf));
				}
				else
				{
					Command.nCommand = MPD_CMD_FORWARD;
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				break;
			case MPD_CMD_LAUNCH_EXITCODE:
				ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
				pBuf = Command.pCommandBuffer;
				nTempIP = *((unsigned long *)pBuf);
				pBuf = pBuf + sizeof(unsigned long);
				nTempPort = *((int *)pBuf);
				if (nTempIP == nLocalIP && nTempPort == nLocalPort)
				{
					DWORD dwExitCode;
					int nGroup, nRank;
					pBuf = pBuf + sizeof(int);
					pLaunchNode = *((LaunchNode **)pBuf);
					pBuf = pBuf + sizeof(LaunchNode*);
					dwExitCode = *((DWORD *)pBuf);
					pBuf = pBuf + sizeof(DWORD);
					nGroup = *((int *)pBuf);
					pBuf = pBuf + sizeof(int);
					nRank = *((int *)pBuf);
					pLaunchNode->SetExit(nGroup, nRank, dwExitCode);
				}
				else
				{
					Command.nCommand = MPD_CMD_FORWARD;
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				break;
			case MPD_CMD_KILL:
				ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
				pBuf = Command.pCommandBuffer;
				nTempIP = *(unsigned long *)pBuf;
				pBuf = pBuf + sizeof(unsigned long);
				nTempPort = *(int *)pBuf;
				pBuf = pBuf + sizeof(int);
				n = *(int *)pBuf;
				if (nTempIP == nLocalIP && nTempPort == nLocalPort)
					KillMPDProcess(n);
				else
				{
					Command.nCommand = MPD_CMD_FORWARD;
					hCommand = InsertCommand(Command);
					WaitForCommand(hCommand);
				}
				break;
			case MPD_CMD_KILL_GROUP:
				ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
				pBuf = Command.pCommandBuffer;
				n = *(int *)pBuf;
				Command.nCommand = MPD_CMD_FORWARD;
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				KillMPDProcesses(n);
				break;
			default:
				printf("Unknown command: %d\n", (int)Command.hCmd.cCommand);fflush(stdout);
				ReceiveBlocking(sock, sock_event, Command.pCommandBuffer, Command.hCmd.nBufferLength, 0);
			}
		}
	}

	NT_closesocket(sock, sock_event);
}
