#include "TerminalClientThread.h"
#include "Command.h"
#include <stdio.h>
#include "global.h"
#include "LaunchNode.h"

// Function name	: PrintHelpToString
// Description	    : 
// Return type		: void 
// Argument         : char *pBuffer
void PrintHelpToString(char *pBuffer)
{
	sprintf(pBuffer, "\n");
	strcat(pBuffer, "Commands:\n");
	strcat(pBuffer, "RING MANAGEMENT:\n");
	strcat(pBuffer, " set host:port #spawns - Sets the number of processes this host can\n");
	strcat(pBuffer, "                         support. Usually equal to 1 or the number of CPUs.\n");
	strcat(pBuffer, " enable host:port      - Allows process launching on 'host:port'\n");
	strcat(pBuffer, " disable host:port     - Disables process launching on 'host:port'\n");
	strcat(pBuffer, " exit                  - Kills the ring\n");
	strcat(pBuffer, " quit                  - same as exit\n");
	strcat(pBuffer, " done                  - No further commands on this pipe connection\n");
	strcat(pBuffer, "\n");
	strcat(pBuffer, "DATABASE:\n");
	strcat(pBuffer, " id idString                - Sets the branch in the database.\n");
	strcat(pBuffer, "                              Must be called before any gets or puts.\n");
	strcat(pBuffer, " get keyString              - gets the value associated with keyString\n");
	strcat(pBuffer, " put keyString=valueString  - puts the value into the database\n");
	strcat(pBuffer, " putc keyString=valueString - consumable put: matches only 1 get call\n");
	strcat(pBuffer, " delete keyString           - deletes a key and all its values\n");
	strcat(pBuffer, " prune idString             - deletes a branch and all its key-value pairs\n");
	strcat(pBuffer, "\n");
	strcat(pBuffer, "INFORMATION:\n");
	strcat(pBuffer, " hosts          - Lists hosts in the ring\n");
	strcat(pBuffer, " ring           - prints the time to traverse the ring\n");
//	strcat(pBuffer, " print lists    - prints information on all the hosts\n" );
//	strcat(pBuffer, " print          - same as print lists\n");
	strcat(pBuffer, " print          - prints information on all the hosts\n" );
	strcat(pBuffer, " database       - prints the current state of the database\n" );
//	strcat(pBuffer, " lookup         - causes 'print' to do a lookup of the ip addresses\n");
//	strcat(pBuffer, " nolookup       - causes 'print' not to lookup the ip addresses (speedier)\n");
	strcat(pBuffer, " help           - prints this message\n");
	strcat(pBuffer, "\n");
	strcat(pBuffer, "PROCESS CREATION:\n");
	strcat(pBuffer, " launch h'host:port'c'command line'e'environment var=val|var=val...'\n");
	strcat(pBuffer, "        d'working directory'g'group id'r'group rank'0'stdinHost:port'\n");
	strcat(pBuffer, "        1'stdoutHost:port'2'stderrHost:port'\n");
	strcat(pBuffer, " kill host:port launchid\n");
	strcat(pBuffer, "                - kills the process launched on 'host:port' associated\n");
	strcat(pBuffer, "                  with the launchid returned by a previous 'launch' call\n");
	strcat(pBuffer, " gkill groupid  - kills all the processes launched with this groupid\n");
	strcat(pBuffer, " ps             - list the running processes on all the nodes\n");
	strcat(pBuffer, "JOB MANAGEMENT:\n");
	strcat(pBuffer, " create group   - returns a group id\n");
	strcat(pBuffer, " next n         - returns the next n host:port locations for launching\n");
	strcat(pBuffer, "\n");
}

// Function name	: GetString
// Description	    : 
// Return type		: int 
// Argument         : HANDLE hInput
// Argument         : char *pBuffer
int GetString(HANDLE hInput, char *pBuffer)
{
	DWORD dwNumRead;
	if (pBuffer == NULL)
		return -1;
	*pBuffer = '\n';

	// Ignore any leading CR/LF bytes
	while (*pBuffer == '\r' || *pBuffer == '\n')
	{
		if (!ReadFile(hInput, pBuffer, 1, &dwNumRead, NULL))
		{
			*pBuffer = '\0';
			return GetLastError();
		}
	}

	//printf("%c", pBuffer);fflush(stdout);
	// Read bytes until reaching a CR or LF
	do
	{
		pBuffer++;
		if (!ReadFile(hInput, pBuffer, 1, &dwNumRead, NULL))
		{
			*pBuffer = '\0';
			return GetLastError();
		}
		//printf("%c", pBuffer);fflush(stdout);
	} while (*pBuffer != '\r' && *pBuffer != '\n');

	// Should I check to see if there is another character?
	// Do I assume that the lines will be separated by two character or just one?  CR and LF
	// If there are two characters then maybe I should read the second one also.

	// NULL terminate the string
	*pBuffer = '\0';

	return 0;
}

// Function name	: GetNextHostsToBuffer
// Description	    : 
// Return type		: void 
// Argument         : char *pBuffer
// Argument         : int n
void GetNextHostsToBuffer(char *pBuffer, int n)
{
	MPDAvailableNode *pList, *pTemp;

	pBuffer[0] = '\n';
	pBuffer[1] = '\0';
	
	pList = g_List.GetNextAvailable(n);

	in_addr addr;
	char *pszIPString;
	while (pList)
	{
		addr.S_un.S_addr = pList->nIP;
		pszIPString = inet_ntoa(addr);
		sprintf(pBuffer, "%s:%d\n", pszIPString, pList->nPort);
		pBuffer = &pBuffer[strlen(pBuffer)];
		pTemp = pList;
		pList = pList->pNext;
		delete pTemp;
	}
}

#define CMD_BUFF_SIZE_PLUS_PADDING CMD_BUFF_SIZE+100

// Function name	: TerminalClientThread
// Description	    : 
// Return type		: void 
// Argument         : TerminalClientThreadArg *pArg
void TerminalClientThread(TerminalClientThreadArg *pArg)
{
	char buffer[CMD_BUFF_SIZE_PLUS_PADDING] = "";
	CommandData Command;
	MPD_CMD_HANDLE hCommand;
	bool bIdSet = false;
	int nRetVal;
	LARGE_INTEGER nFrequency, nStart, nFinish;
	char pszID[100];
	int nLength;
	HANDLE hInput, hOutput, hEndOutput;
	DWORD dwNumWritten;

	hInput = pArg->hInput;
	hOutput = pArg->hOutput;
	hEndOutput = pArg->hEndOutput;
	delete pArg;

	while (true)
	{
		if (nRetVal = GetString(hInput, buffer))
		{
			sprintf(buffer, "GetString failed: %d\n", nRetVal);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
			break;
		}
		if ((stricmp(buffer, "exit") == 0) || (stricmp(buffer, "quit") == 0))
		{
			Command.nCommand = MPD_CMD_DESTROY_RING;
			Command.hCmd.nBufferLength = 0;
			hCommand = InsertCommand(Command);
			WaitForCommand(hCommand);
			break;
		}
		else if (stricmp(buffer, "done") == 0)
		{
			break;
		}
		else if (strnicmp(buffer, "next ", 5) == 0)
		{
			GetNextHostsToBuffer(buffer, atoi(&buffer[5]));
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (strnicmp(buffer, "find ", 5) == 0)
		{
			unsigned long nIP;
			int nPort = -1;
			g_List.GetID(&buffer[5], &nIP, &nPort);
			sprintf(buffer, "%d\n", nPort);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (strnicmp(buffer, "launch ", 7) == 0)
		{
			char pBuffer[100];
			sprintf(pBuffer, "y'%d'", (int)hEndOutput);
			strcat(buffer, pBuffer);
			Command.nCommand = MPD_CMD_LAUNCH;
			strcpy(Command.pCommandBuffer, &buffer[7]);
			Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
			hCommand = InsertCommand(Command);
			nLength = 100;
			WaitForCommand(hCommand, buffer, &nLength);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (strnicmp(buffer, "launchid ", 9) == 0)
		{
			DWORD dwData = LaunchNode::GetLaunchNodeData(atoi(&buffer[9]), 2000);
			sprintf(buffer, "%d\n", dwData);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (strnicmp(buffer, "kill ", 5) == 0)
		{
			Command.nCommand = MPD_CMD_KILL;
			strcpy(Command.pCommandBuffer, &buffer[5]);
			Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
			hCommand = InsertCommand(Command);
			WaitForCommand(hCommand);
		}
		else if (strnicmp(buffer, "gkill ", 6) == 0)
		{
			Command.nCommand = MPD_CMD_KILL_GROUP;
			strcpy(Command.pCommandBuffer, &buffer[6]);
			Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
			hCommand = InsertCommand(Command);
			WaitForCommand(hCommand);
		}
		else if (stricmp(buffer, "create group") == 0)
		{
			// This section is not thread safe.
			if (g_nNextGroupID == -1 || g_nNextGroupID > g_nMaxGroupID)
			{
				// Get the current group id
				Command.nCommand = MPD_CMD_GET;
				strcpy(Command.pCommandBuffer, "global:currentID");
				Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
				hCommand = InsertCommand(Command);
				nLength = 100;
				WaitForCommand(hCommand, buffer, &nLength);

				// Save it and increment it by some ammount
				g_nNextGroupID = atoi(buffer);
				g_nMaxGroupID = g_nNextGroupID + 999;
	
				// Put the new group id back in the database
				Command.nCommand = MPD_CMD_PUTC;
				sprintf(Command.pCommandBuffer, "global:currentID=%d", g_nMaxGroupID+1);
				Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
			}
			sprintf(buffer, "%d\n", g_nNextGroupID);
			g_nNextGroupID++;
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (stricmp(buffer, "hosts") == 0)
		{
			Command.nCommand = MPD_CMD_HOSTS;
			Command.hCmd.nBufferLength = 0;
			hCommand = InsertCommand(Command);
			nLength = CMD_BUFF_SIZE_PLUS_PADDING;
			WaitForCommand(hCommand, buffer, &nLength);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (stricmp(buffer, "cpu") == 0)
		{
			Command.nCommand = MPD_CMD_CPUSAGE;
			Command.hCmd.nBufferLength = 0;
			hCommand = InsertCommand(Command);
			nLength = CMD_BUFF_SIZE_PLUS_PADDING;
			WaitForCommand(hCommand, buffer, &nLength);
			strcat(buffer, "\n");
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (stricmp(buffer, "ps") == 0)
		{
			Command.nCommand = MPD_CMD_PS;
			Command.hCmd.nBufferLength = 0;
			hCommand = InsertCommand(Command);
			nLength = CMD_BUFF_SIZE_PLUS_PADDING;
			WaitForCommand(hCommand, buffer, &nLength);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (strnicmp(buffer, "set ", 4) == 0)
		{
			Command.nCommand = MPD_CMD_ADD;
			strcpy(Command.pCommandBuffer, &buffer[4]);
			Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
			hCommand = InsertCommand(Command);
			WaitForCommand(hCommand);
		}
		else if (strnicmp(buffer, "enable ", 7) == 0)
		{
			Command.nCommand = MPD_CMD_ENABLE;
			strcpy(Command.pCommandBuffer, &buffer[7]);
			Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
			hCommand = InsertCommand(Command);
			WaitForCommand(hCommand);
		}
		else if (strnicmp(buffer, "disable ", 8) == 0)
		{
			Command.nCommand = MPD_CMD_DISABLE;
			strcpy(Command.pCommandBuffer, &buffer[8]);
			Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
			hCommand = InsertCommand(Command);
			WaitForCommand(hCommand);
		}
		else if (strnicmp(buffer, "id ", 3) == 0)
		{
			strcpy(pszID, &buffer[3]);
			bIdSet = true;
		}
		else if (strnicmp(buffer, "get ", 4) == 0)
		{
			if (bIdSet)
			{
				Command.nCommand = MPD_CMD_GET;
				sprintf(Command.pCommandBuffer, "%s:%s", pszID, &buffer[4]);
				Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
				hCommand = InsertCommand(Command);
				nLength = CMD_BUFF_SIZE_PLUS_PADDING;
				WaitForCommand(hCommand, buffer, &nLength);
			}
			else
				sprintf(buffer, "'id dbsID' must be called before get\n");
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
			strcpy(buffer, "\n");
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (strnicmp(buffer, "put ", 4) == 0)
		{
			if (bIdSet)
			{
				Command.nCommand = MPD_CMD_PUT;
				sprintf(Command.pCommandBuffer, "%s:%s", pszID, &buffer[4]);
				Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				sprintf(buffer, "put completed\n");
			}
			else
				sprintf(buffer, "'id dbsID' must be called before put\n");
			//WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (strnicmp(buffer, "putc ", 5) == 0)
		{
			if (bIdSet)
			{
				Command.nCommand = MPD_CMD_PUTC;
				sprintf(Command.pCommandBuffer, "%s:%s", pszID, &buffer[5]);
				Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				sprintf(buffer, "putc completed\n");
			}
			else
				sprintf(buffer, "'id dbsID' must be called before putc\n");
			//WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (strnicmp(buffer, "prune ", 6) == 0)
		{
			Command.nCommand = MPD_CMD_DELETE_ID;
			sprintf(Command.pCommandBuffer, "%s", &buffer[6]);
			Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
			hCommand = InsertCommand(Command);
			WaitForCommand(hCommand);
			sprintf(buffer, "prune completed\n");
			//WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (strnicmp(buffer, "delete ", 7) == 0)
		{
			if (bIdSet)
			{
				Command.nCommand = MPD_CMD_DELETE_KEY;
				sprintf(Command.pCommandBuffer, "%s:%s", pszID, &buffer[7]);
				Command.hCmd.nBufferLength = strlen(Command.pCommandBuffer) + 1;
				hCommand = InsertCommand(Command);
				WaitForCommand(hCommand);
				sprintf(buffer, "delete completed\n");
			}
			else
				sprintf(buffer, "'id dbsID' must be called before delete\n");
			//WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (stricmp(buffer, "ring") == 0)
		{
			QueryPerformanceFrequency(&nFrequency);
			QueryPerformanceCounter(&nStart);
			Command.nCommand = MPD_CMD_RUN_THE_RING;
			Command.hCmd.nBufferLength = 0;
			hCommand = InsertCommand(Command);
			WaitForCommand(hCommand);
			QueryPerformanceCounter(&nFinish);
			double s = double(nFinish.QuadPart - nStart.QuadPart) / (double)nFrequency.QuadPart;
			if (s < 0.000001)
				sprintf(buffer, "ring returned in %4.2f nano seconds\n", s * 1000000000);
			else if (s < 0.001)
				sprintf(buffer, "ring returned in %4.2f micro seconds\n", s * 1000000);
			else if (s < 1)
				sprintf(buffer, "ring returned in %4.2f milli seconds\n", s * 1000);
			else
				sprintf(buffer, "ring returned in %4.2f seconds\n", s);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (stricmp(buffer, "print lists") == 0)
		{
			// Make everyone print out their lists
			Command.nCommand = MPD_CMD_PRINT_LISTS;
			Command.hCmd.nBufferLength = 0;
			hCommand = InsertCommand(Command);
			WaitForCommand(hCommand);
			/*/
			// Print out the local list only
			g_List.PrintToString(buffer);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
			//*/
		}
		else if (stricmp(buffer, "print") == 0)
		{
			// Print out the local list only
			g_List.PrintToString(buffer);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else if (stricmp(buffer, "print database") == 0 || stricmp(buffer, "database") == 0)
		{
			Command.nCommand = MPD_CMD_PRINT_DATABASE;
			Command.hCmd.nBufferLength = 0;
			hCommand = InsertCommand(Command);
			nLength = CMD_BUFF_SIZE_PLUS_PADDING;
			WaitForCommand(hCommand, buffer, &nLength);
			if (nLength > 0)
				WriteFile(hOutput, buffer, nLength, &dwNumWritten, NULL);
		}
		else if (stricmp(buffer, "lookup") == 0)
			g_List.m_bLookupIP = true;
		else if (stricmp(buffer, "nolookup") == 0)
			g_List.m_bLookupIP = false;
		else if (stricmp(buffer, "help") == 0)
		{
			PrintHelpToString(buffer);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
		}
		else
		{
			WriteFile(hOutput, "unknown command: '", strlen("unknown command: '"), &dwNumWritten, NULL);
			WriteFile(hOutput, buffer, strlen(buffer), &dwNumWritten, NULL);
			WriteFile(hOutput, "'\n", strlen("'\n"), &dwNumWritten, NULL);
		}
	}
}
