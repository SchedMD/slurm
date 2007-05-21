#include "RightThread.h"
#include "Command.h"
#include "sockets.h"
#include <stdio.h>
#include "global.h"
#include "GetReturnThread.h"
#include "StringOpt.h"
#include "LaunchMPDProcess.h"
#include "LaunchNode.h"
#include "GetCPUsage.h"

// Function name	: RightThread
// Description	    : 
// Return type		: void 
// Argument         : LaunchMPDArg *pArg
void RightThread(LaunchMPDArg *pArg)
{
	SOCKET sock;
	WSAEVENT sock_event;
	char host[100] = "";
	int port = 0;
	char buffer[CMD_BUFF_SIZE];
	int error;
	CommandData *pCommand;
	char *pBuf;
	char pShortBuffer[100];
	unsigned long nLocalIP;
	int nLocalPort = 0;
	int nLocalSpawns;
	bool bDone = false;
	char *token;
	char pszID[256], *pszKey, *pszValue;
	int n;
	bool bPersistentPut = true;
	unsigned long nTempIP;
	int nTempPort;
	char pszLocalHost[100] = "";
	LaunchNode *pLaunchNode = NULL;

	gethostname(pszLocalHost, 100);

	if (error = NT_create_bind_socket(&sock, &sock_event, 0))
	{
		printf("RightThread: create and bind socket failed, error %d\n", error);
		ExitProcess(error);
	}

	if (pArg == NULL)
	{
		gets(host);
		gets(buffer);
		port = atoi(buffer);
	}
	else
	{
		WaitForSingleObject(pArg->pRight->hReadyEvent, INFINITE);
		strcpy(host, pArg->pRight->pszHost);
		port = pArg->pRight->nPort;
	}

	if (error = NT_connect(sock, host, port))
	{
		printf("RightThread: NT_connect failed for %s:%d, error %d\n", host, port, error);
		ExitProcess(error);
	}

	//printf("Connected to %s:%d\n", host, port);
	g_bRightConnected = true;

	while (nLocalPort == 0)
	{
		g_List.GetMyID(&nLocalIP, &nLocalPort, &nLocalSpawns);
		Sleep(200);
	}
	g_List.Add(nLocalIP, nLocalPort, nLocalSpawns);

	// Send an ADD message around the ring
	CommandData data;
	data.hCmd.cCommand = MPD_CMD_ADD;
	data.hCmd.nSrcIP = nLocalIP;
	data.hCmd.nSrcPort = nLocalPort;
	data.hCmd.pData = NULL;
	data.hCmd.nBufferLength = sizeof(unsigned long) + 2 * sizeof(int);
	pBuf = data.pCommandBuffer;
	*((unsigned long *)pBuf) = nLocalIP;
	pBuf += sizeof(unsigned long);
	*((int *)pBuf) = nLocalPort;
	pBuf += sizeof(int);
	*((int *)pBuf) = nLocalSpawns;
	SendBlocking(sock, (char*)&data.hCmd, sizeof(CommandHeader), 0);
	SendBlocking(sock, data.pCommandBuffer, data.hCmd.nBufferLength, 0);

	while (!bDone)
	{
		pCommand = GetNextCommand();

		// Do something with the command
		if (pCommand->nCommand == MPD_CMD_QUIT)
		{
			MarkCommandCompleted(pCommand);
			break;
		}

		switch (pCommand->nCommand)
		{
		case MPD_CMD_FORWARD:
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			if (pCommand->hCmd.nBufferLength > 0)
				SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_ADD:
			token = strtok(pCommand->pCommandBuffer, ":");
			pBuf = pCommand->pCommandBuffer;
			if (NT_get_ip(token, (unsigned long *)pBuf))
			{
				sprintf(pCommand->pCommandBuffer, "Unable to resolve hostname, error %d\n", WSAGetLastError()); // <----------
				pCommand->hCmd.nBufferLength = strlen(pCommand->pCommandBuffer)+1;
				pCommand->bSuccess = false;
				MarkCommandCompleted(pCommand);
				break;
			}
			nTempIP = *(unsigned long *)pBuf;
			pBuf += sizeof(unsigned long);
			token = strtok(NULL, " \t");
			*((int *)pBuf) = nTempPort = atoi(token);
			pBuf += sizeof(int);
			token = strtok(NULL, "\n");
			*((int *)pBuf) = n = (token == NULL) ? 1 : atoi(token);
			pCommand->hCmd.cCommand = MPD_CMD_ADD;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.pData = NULL;
			pCommand->hCmd.nBufferLength = sizeof(unsigned long) + 2 * sizeof(int);
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			g_List.Add(nTempIP, nTempPort, n);
			MarkCommandCompleted(pCommand);
			break;
		//case MPD_CMD_REMOVE:
			// Send a remove message after a node has crashed and the ring has been stitched.
		//	break;
		case MPD_CMD_INCREMENT:
			pCommand->hCmd.cCommand = MPD_CMD_INCREMENT;
			pBuf = pCommand->pCommandBuffer;
			*(unsigned long *)pBuf = nLocalIP;
			pBuf += sizeof(unsigned long);
			*(int *)pBuf = nLocalPort;
			pCommand->hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int);
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.pData = NULL;
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			g_List.Increment(nLocalIP, nLocalPort);
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_DECREMENT:
			pCommand->hCmd.cCommand = MPD_CMD_DECREMENT;
			pBuf = pCommand->pCommandBuffer;
			*(unsigned long *)pBuf = nLocalIP;
			pBuf += sizeof(unsigned long);
			*(int *)pBuf = nLocalPort;
			pCommand->hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int);
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.pData = NULL;
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			g_List.Decrement(nLocalIP, nLocalPort);
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_ENABLE:
			token = strtok(pCommand->pCommandBuffer, ":");
			pBuf = pCommand->pCommandBuffer;
			if (NT_get_ip(token, (unsigned long *)pBuf))
			{
				sprintf(pCommand->pCommandBuffer, "Unable to resolve hostname, error %d\n", WSAGetLastError());// <----------
				pCommand->hCmd.nBufferLength = strlen(pCommand->pCommandBuffer)+1;
				pCommand->bSuccess = false;
				MarkCommandCompleted(pCommand);
				break;
			}
			nTempIP = *(unsigned long *)pBuf;
			pBuf += sizeof(unsigned long);
			token = strtok(NULL, "\n");
			if (token != NULL)
				*((int *)pBuf) = nTempPort = atoi(token);
			else
				*((int *)pBuf) = nTempPort = -1;
			pCommand->hCmd.cCommand = MPD_CMD_ENABLE;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.pData = NULL;
			pCommand->hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int);
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			g_List.Enable(nTempIP, nTempPort);
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_DISABLE:
			token = strtok(pCommand->pCommandBuffer, ":");
			pBuf = pCommand->pCommandBuffer;
			if (NT_get_ip(token, (unsigned long *)pBuf))
			{
				sprintf(pCommand->pCommandBuffer, "Unable to resolve hostname, error %d\n", WSAGetLastError());// <----------
				pCommand->hCmd.nBufferLength = strlen(pCommand->pCommandBuffer)+1;
				pCommand->bSuccess = false;
				MarkCommandCompleted(pCommand);
				break;
			}
			nTempIP = *(unsigned long *)pBuf;
			pBuf += sizeof(unsigned long);
			token = strtok(NULL, "\n");
			if (token != NULL)
				*((int *)pBuf) = nTempPort = atoi(token);
			else
				*((int *)pBuf) = nTempPort = -1;
			pCommand->hCmd.cCommand = MPD_CMD_DISABLE;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.pData = NULL;
			pCommand->hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int);
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			g_List.Disable(nTempIP, nTempPort);
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_PUTC:
			bPersistentPut = false;
		case MPD_CMD_PUT:
			token = strtok(pCommand->pCommandBuffer, ":");
			if (token == NULL)
			{
				MarkCommandCompleted(pCommand);
				break;
			}
			strcpy(pszID, token);
			token = strtok(NULL, "=");
			if (token == NULL)
			{
				MarkCommandCompleted(pCommand);
				break;
			}
			pszKey = new char[strlen(token)+1];
			strcpy(pszKey, token);
			token = strtok(NULL, "\n");
			if (token == NULL)
			{
				delete pszKey;
				MarkCommandCompleted(pCommand);
				break;
			}
			n = strlen(token) + 1;
			pszValue = new char[n];
			strcpy(pszValue, token);
			if (g_bDatabaseIsLocal)
				g_Database.Put(pszID, pszKey, pszValue, n, bPersistentPut);
			else
			{
				pCommand->hCmd.nBufferLength = 3 * sizeof(int);
				pCommand->hCmd.cCommand = bPersistentPut ? MPD_CMD_PUT : MPD_CMD_PUTC;
				pCommand->hCmd.nSrcIP = nLocalIP;
				pCommand->hCmd.nSrcPort = nLocalPort;
				pBuf = pCommand->pCommandBuffer;
				n = strlen(pszID) + 1;
				*((int *)pBuf) = n;
				pBuf += sizeof(int);
				strcpy(pBuf, pszID);
				pBuf += n;
				pCommand->hCmd.nBufferLength += n;
				n = strlen(pszKey) + 1;
				*((int *)pBuf) = n;
				pBuf += sizeof(int);
				strcpy(pBuf, pszKey);
				pBuf += n;
				pCommand->hCmd.nBufferLength += n;
				n = strlen(pszValue) + 1;
				*((int *)pBuf) = n;
				pBuf += sizeof(int);
				strcpy(pBuf, pszValue);
				pBuf += n;
				pCommand->hCmd.nBufferLength += n;
				SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
				SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
				delete pszKey;
				delete pszValue;
			}
			bPersistentPut = true;
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_GET:
			token = strtok(pCommand->pCommandBuffer, ":");
			if (token == NULL)
			{
				MarkCommandCompleted(pCommand);
				break;
			}
			strcpy(pszID, token);
			token = strtok(NULL, "\n");
			if (token == NULL)
			{
				MarkCommandCompleted(pCommand);
				break;
			}
			pszKey = new char[strlen(token)+1];
			strcpy(pszKey, token);
			pCommand->hCmd.cCommand = MPD_CMD_GET;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			if (g_bDatabaseIsLocal)
			{
				GetReturnThreadArg *pArg = new GetReturnThreadArg;
				pArg->pCommand = pCommand;
				strcpy(pArg->pszDbsID, pszID);
				pArg->pszDbsKey = pszKey;

				DWORD dwThreadID;
				CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)GetThread, pArg, 0, &dwThreadID));
			}
			else
			{
				pCommand->hCmd.nBufferLength = 2 * sizeof(unsigned long) + 3 * sizeof(int);
				pBuf = pCommand->pCommandBuffer;
				*((unsigned long *)pBuf) = nLocalIP;
				pBuf += sizeof(unsigned long);
				*((int *)pBuf) = nLocalPort;
				pBuf += sizeof(int);
				*((unsigned long *)pBuf) = (unsigned long)pCommand; // nGetIdentifier is a pointer to the Command structure
				pBuf += sizeof(unsigned long);
				n = strlen(pszID) + 1;
				pCommand->hCmd.nBufferLength += n;
				*((int *)pBuf) = n;
				pBuf += sizeof(int);
				strcpy(pBuf, pszID);
				pBuf += n;
				n = strlen(pszKey) + 1;
				pCommand->hCmd.nBufferLength += n;
				*((int *)pBuf) = n;
				pBuf += sizeof(int);
				strcpy(pBuf, pszKey);

				SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
				SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
				delete pszKey;
			}
			break;
		case MPD_CMD_DELETE_ID:
			strcpy(pszID, pCommand->pCommandBuffer);
			if (g_bDatabaseIsLocal)
				g_Database.Delete(pszID);
			else
			{
				pCommand->hCmd.nBufferLength = sizeof(int);
				pCommand->hCmd.cCommand = MPD_CMD_DELETE_ID;
				pCommand->hCmd.nSrcIP = nLocalIP;
				pCommand->hCmd.nSrcPort = nLocalPort;
				pBuf = pCommand->pCommandBuffer;
				n = strlen(pszID) + 1;
				*((int *)pBuf) = n;
				pBuf += sizeof(int);
				strcpy(pBuf, pszID);
				pBuf += n;
				pCommand->hCmd.nBufferLength += n;
				SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
				SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			}
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_DELETE_KEY:
			token = strtok(pCommand->pCommandBuffer, ":");
			if (token == NULL)
			{
				MarkCommandCompleted(pCommand);
				break;
			}
			strcpy(pszID, token);
			token = strtok(NULL, "\n");
			if (token == NULL)
			{
				MarkCommandCompleted(pCommand);
				break;
			}
			pszKey = new char[strlen(token)+1];
			strcpy(pszKey, token);
			if (g_bDatabaseIsLocal)
				g_Database.Delete(pszID, pszKey);
			else
			{
				pCommand->hCmd.nBufferLength = 2 * sizeof(int);
				pCommand->hCmd.cCommand = MPD_CMD_DELETE_KEY;
				pCommand->hCmd.nSrcIP = nLocalIP;
				pCommand->hCmd.nSrcPort = nLocalPort;
				pBuf = pCommand->pCommandBuffer;
				n = strlen(pszID) + 1;
				*((int *)pBuf) = n;
				pBuf += sizeof(int);
				strcpy(pBuf, pszID);
				pBuf += n;
				pCommand->hCmd.nBufferLength += n;
				n = strlen(pszKey) + 1;
				*((int *)pBuf) = n;
				pBuf += sizeof(int);
				strcpy(pBuf, pszKey);
				pBuf += n;
				pCommand->hCmd.nBufferLength += n;
				SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
				SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			}
			delete pszKey;
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_QUIT:
			break;
		case MPD_CMD_DESTROY_RING:
			pCommand->hCmd.cCommand = MPD_CMD_DESTROY_RING;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.nBufferLength = 0;
			pCommand->hCmd.pData = NULL;
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			MarkCommandCompleted(pCommand);
			NT_closesocket(sock, sock_event);
			ExitThread(0);
			break;
		case MPD_CMD_HOSTS:
			pCommand->hCmd.cCommand = MPD_CMD_HOSTS;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			sprintf(pCommand->pCommandBuffer, "%s:%d\n", pszLocalHost, nLocalPort);
			pCommand->hCmd.nBufferLength = strlen(pCommand->pCommandBuffer);
			pCommand->hCmd.pData = pCommand;
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			break;
		case MPD_CMD_CPUSAGE:
			pCommand->hCmd.cCommand = MPD_CMD_CPUSAGE;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			sprintf(pCommand->pCommandBuffer, "%s:%d %d ", pszLocalHost, nLocalPort, GetCPUsage());
			pCommand->hCmd.nBufferLength = strlen(pCommand->pCommandBuffer);
			pCommand->hCmd.pData = pCommand;
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			break;
		case MPD_CMD_RUN_THE_RING:
			pCommand->hCmd.cCommand = MPD_CMD_RUN_THE_RING;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.pData = pCommand;
			pCommand->hCmd.nBufferLength = 0;
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			break;
		case MPD_CMD_PRINT_LIST:
			g_List.Print();
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_PRINT_LISTS:
			pCommand->hCmd.cCommand = MPD_CMD_PRINT_LISTS;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.pData = NULL;
			pCommand->hCmd.nBufferLength = 0;
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			g_List.Print();
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_PRINT_DATABASE:
			pCommand->hCmd.cCommand = MPD_CMD_PRINT_DATABASE;
			pCommand->hCmd.nBufferLength = 0;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.pData = pCommand;
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			break;
		case MPD_CMD_LAUNCH:
			GetStringOpt(pCommand->pCommandBuffer, 'h', buffer);
			token = strtok(buffer, ":");
			if (token != NULL)
			{
				NT_get_ip(token, &nTempIP);
				/*
				{
					char *pName;
					in_addr in;
					in.S_un.S_addr = nTempIP;
					pName = inet_ntoa(in);
					printf("creating launch command for: %s\n", pName);fflush(stdout);
				}
				//*/
				token = strtok(NULL, " \n");
				if (token != NULL)
				{
					nTempPort = atoi(token);

					LaunchNode *pNode = LaunchNode::AllocLaunchNode();
					GetStringOpt(pCommand->pCommandBuffer, 'y', buffer);
					pNode->InitData((HANDLE)atoi(buffer));
					if (nTempIP == nLocalIP && nTempPort == nLocalPort)
					{
						LaunchMPDProcessArg *pArg = new LaunchMPDProcessArg;
						pArg->hEndOutput = (HANDLE)atoi(buffer);
						pArg->nIP = nLocalIP;
						pArg->nPort = nLocalPort;
						pArg->nSrcIP = nLocalIP;
						pArg->nSrcPort = nLocalPort;
						pArg->pszCommand = new char[strlen(pCommand->pCommandBuffer)+1];
						pArg->pNode = pNode;
						strcpy(pArg->pszCommand, pCommand->pCommandBuffer);
						//printf("launching '%s'\n", pArg->pszCommand);fflush(stdout);
						DWORD dwThreadID;
						CloseHandle(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)LaunchMPDProcess, pArg, 0, &dwThreadID));
					}
					else
					{
						strcpy(buffer, pCommand->pCommandBuffer);
						pCommand->hCmd.cCommand = MPD_CMD_LAUNCH;
						pCommand->hCmd.nSrcIP = nLocalIP;
						pCommand->hCmd.nSrcPort = nLocalPort;
						pCommand->hCmd.pData = NULL;
						
						pBuf = pCommand->pCommandBuffer;
						*((LaunchNode **)pBuf) = pNode;
						pBuf = pBuf + sizeof(LaunchNode*);
						*((unsigned long *)pBuf) = nTempIP;
						pBuf = pBuf + sizeof(unsigned long);
						*((int *)pBuf) = nTempPort;
						pBuf += sizeof(int);
						strcpy(pBuf, buffer);
						
						pCommand->hCmd.nBufferLength = sizeof(LaunchNode*) + sizeof(unsigned long) + sizeof(int) + strlen(buffer)+1;
						
						SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
						SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
					}
					sprintf(pCommand->pCommandBuffer, "%d\n", pNode->GetID());
					pCommand->hCmd.nBufferLength = strlen(pCommand->pCommandBuffer) + 1;
				}
			}
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_LAUNCH_RET:
			pBuf = pCommand->pCommandBuffer;
			nTempIP = *((unsigned long *)pBuf);
			pBuf = pBuf + sizeof(unsigned long);
			nTempPort = *((int *)pBuf);
			pBuf = pBuf + sizeof(int);
			pLaunchNode = *((LaunchNode **)pBuf);
			pBuf = pBuf + sizeof(LaunchNode *);
			if (nTempIP == nLocalIP && nTempPort == nLocalPort)
			{
				pLaunchNode->Set(*((DWORD*)pBuf));
			}
			else
			{
				pCommand->hCmd.cCommand = MPD_CMD_LAUNCH_RET;
				pCommand->hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int) + sizeof(LaunchNode*) + sizeof(DWORD);
				pCommand->hCmd.nSrcIP = nLocalIP;
				pCommand->hCmd.nSrcPort = nLocalPort;
				pCommand->hCmd.pData = NULL;
				
				SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
				SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			}
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_LAUNCH_EXITCODE:
			pBuf = pCommand->pCommandBuffer;
			nTempIP = *((unsigned long *)pBuf);
			pBuf = pBuf + sizeof(unsigned long);
			nTempPort = *((int *)pBuf);
			pBuf = pBuf + sizeof(int);
			pLaunchNode = *((LaunchNode **)pBuf);
			pBuf = pBuf + sizeof(LaunchNode *);
			if (nTempIP == nLocalIP && nTempPort == nLocalPort)
			{
				DWORD dwExitCode;
				int nGroup, nRank;
				dwExitCode = *((DWORD *)pBuf);
				pBuf = pBuf + sizeof(DWORD);
				nGroup = *((int *)pBuf);
				pBuf = pBuf + sizeof(int);
				nRank = *((int *)pBuf);
				pLaunchNode->SetExit(nGroup, nRank, dwExitCode);
			}
			else
			{
				pCommand->hCmd.cCommand = MPD_CMD_LAUNCH_EXITCODE;
				pCommand->hCmd.nBufferLength = sizeof(unsigned long) + sizeof(int) + sizeof(LaunchNode*) + sizeof(DWORD) + sizeof(int) + sizeof(int);
				pCommand->hCmd.nSrcIP = nLocalIP;
				pCommand->hCmd.nSrcPort = nLocalPort;
				pCommand->hCmd.pData = NULL;

				SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
				SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			}
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_KILL:
			token = strtok(pCommand->pCommandBuffer, ":");
			if (token != NULL)
			{
				NT_get_ip(token, &nTempIP);
				/*
				{
					char *pName;
					in_addr in;
					in.S_un.S_addr = nTempIP;
					pName = inet_ntoa(in);
					printf("kill: %s:", pName);fflush(stdout);
				}
				//*/
				token = strtok(NULL, " \n");
				if (token != NULL)
				{
					nTempPort = atoi(token);
					//printf("%d", nTempPort);fflush(stdout);

					token = strtok(NULL, " \n");
					if (token != NULL)
					{
						n = atoi(token);
						//printf(" (%d)\n", n);fflush(stdout);
						if (nTempIP == nLocalIP && nTempPort == nLocalPort)
							KillMPDProcess(n);
						else
						{
							strcpy(buffer, pCommand->pCommandBuffer);
							pCommand->hCmd.cCommand = MPD_CMD_KILL;
							pCommand->hCmd.nSrcIP = nLocalIP;
							pCommand->hCmd.nSrcPort = nLocalPort;
							pCommand->hCmd.pData = NULL;
							
							pBuf = pCommand->pCommandBuffer;
							*((unsigned long *)pBuf) = nTempIP;
							pBuf = pBuf + sizeof(unsigned long);
							*((int *)pBuf) = nTempPort;
							pBuf += sizeof(int);
							*((int *)pBuf) = n;
						
							pCommand->hCmd.nBufferLength = sizeof(unsigned long) + 2 * sizeof(int);
						
							SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
							SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
						}
					}
				}
			}
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_KILL_GROUP:
			n = atoi(pCommand->pCommandBuffer);
			//printf("killing group %d\n", n);fflush(stdout);

			strcpy(buffer, pCommand->pCommandBuffer);
			pCommand->hCmd.cCommand = MPD_CMD_KILL_GROUP;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			pCommand->hCmd.pData = NULL;
							
			pBuf = pCommand->pCommandBuffer;
			*((int *)pBuf) = n;

			pCommand->hCmd.nBufferLength = sizeof(int);

			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);

			KillMPDProcesses(n);
			MarkCommandCompleted(pCommand);
			break;
		case MPD_CMD_PS:
			pCommand->hCmd.cCommand = MPD_CMD_PS;
			pCommand->hCmd.nSrcIP = nLocalIP;
			pCommand->hCmd.nSrcPort = nLocalPort;
			sprintf(pShortBuffer, "%s:%d", pszLocalHost, nLocalPort);
			PrintMPDProcessesToBuffer(pCommand->pCommandBuffer, pShortBuffer);
			pCommand->hCmd.nBufferLength = strlen(pCommand->pCommandBuffer);
			pCommand->hCmd.pData = pCommand;
			SendBlocking(sock, (char*)&pCommand->hCmd, sizeof(CommandHeader), 0);
			SendBlocking(sock, pCommand->pCommandBuffer, pCommand->hCmd.nBufferLength, 0);
			break;
		default:
			pCommand->bSuccess = false;
			sprintf(pCommand->pCommandBuffer, "Unknown command\n");
			pCommand->hCmd.nBufferLength = strlen(pCommand->pCommandBuffer)+1;
			MarkCommandCompleted(pCommand);
		}
	}

	NT_closesocket(sock, sock_event);
}
