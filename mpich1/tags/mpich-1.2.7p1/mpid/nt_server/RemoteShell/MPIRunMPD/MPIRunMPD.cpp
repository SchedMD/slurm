#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include "GetOpt.h"
#include "Redirection.h"
#include "localonly.h"

struct HostNode
{
	TCHAR host[100];
	TCHAR exe[MAX_PATH];
	long nSMPProcs;
	HostNode *next;
};

struct LaunchNode
{
	unsigned long nIP;
	int nPort;
	char pszIPPort[100];
	char pszCmdLine[1024];
	char pszArgs[1024];
	char pszEnv[4096];
	char pszDir[MAX_PATH];
	LaunchNode *pNext;
};

HostNode *g_pHosts = NULL;
long g_nHosts = 1;
long g_nFirstSMPProcs = 1;
TCHAR g_pszExe[MAX_PATH] = _T(""), g_pszArgs[MAX_PATH] = _T(""), g_pszEnv[1024] = _T("");
TCHAR g_pszFirstHost[100] = _T("");
bool g_bNoMPI = false;

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

// Function name	: ParseLineIntoHostNode
// Description	    : 
// Return type		: HostNode* 
// Argument         : LPTSTR line
HostNode* ParseLineIntoHostNode(LPTSTR line)
{
	TCHAR buffer[1024];
	LPTSTR pChar, pChar2;
	HostNode *node = NULL;

	_tcscpy(buffer, line);
	pChar = buffer;

	// Advance over white space
	while (*pChar != _T('\0') && _istspace(*pChar))
		pChar++;
	if (*pChar == _T('#') || *pChar == _T('\0'))
		return NULL;

	// Trim trailing white space
	pChar2 = &buffer[_tcslen(buffer)-1];
	while (_istspace(*pChar2) && (pChar >= pChar))
	{
		*pChar2 = '\0';
		pChar2--;
	}
	
	// If there is anything left on the line, consider it a host name
	if (_tcslen(pChar) > 0)
	{
		node = new HostNode;
		node->nSMPProcs = 1;
		node->next = NULL;
		node->exe[0] = _T('\0');

		// Copy the host name
		pChar2 = node->host;
		while (*pChar != _T('\0') && !_istspace(*pChar))
		{
			*pChar2 = *pChar;
			pChar++;
			pChar2++;
		}
		*pChar2 = _T('\0');

		// Advance over white space
		while (*pChar != _T('\0') && _istspace(*pChar))
			pChar++;
		// Get the number of SMP processes
		if (*pChar != _T('\0'))
		{
			node->nSMPProcs = _ttoi(pChar);
			if (node->nSMPProcs < 1)
				node->nSMPProcs = 1;
		}
		// Advance over the number
		while (*pChar != _T('\0') && _istdigit(*pChar))
			pChar++;

		// Advance over white space
		while (*pChar != _T('\0') && _istspace(*pChar))
			pChar++;
		// Copy the executable
		if (*pChar != _T('\0'))
			_tcscpy(node->exe, pChar);
	}

	return node;
}

// Function name	: ParseConfigFile
// Description	    : 
// Return type		: void 
// Argument         : LPTSTR filename
void ParseConfigFile(LPTSTR filename)
{
	FILE *fin;
	TCHAR buffer[1024] = TEXT("");

	fin = _tfopen(filename, TEXT("r"));
	if (fin == NULL)
	{
		_tprintf(TEXT("Unable to open file: %s\n"), filename);
		ExitProcess(1);
	}

	while (_fgetts(buffer, 1024, fin))
	{
		// Check for the name of the executable
		if (_tcsnicmp(buffer, TEXT("exe "), 4) == 0)
		{
			TCHAR *pChar = &buffer[4];
			while (_istspace(*pChar))
				pChar++;
			_tcscpy(g_pszExe, pChar);
			pChar = &g_pszExe[_tcslen(g_pszExe)-1];
			while (_istspace(*pChar) && (pChar >= g_pszExe))
			{
				*pChar = '\0';
				pChar--;
			}
		}
		else
		// Check for program arguments
		if (_tcsnicmp(buffer, TEXT("args "), 5) == 0)
		{
			TCHAR *pChar = &buffer[5];
			while (_istspace(*pChar))
				pChar++;
			_tcscpy(g_pszArgs, pChar);
			pChar = &g_pszArgs[_tcslen(g_pszArgs)-1];
			while (_istspace(*pChar) && (pChar >= g_pszArgs))
			{
				*pChar = '\0';
				pChar--;
			}
		}
		else
		// Check for environment variables
		if (_tcsnicmp(buffer, TEXT("env "), 4) == 0)
		{
			TCHAR *pChar = &buffer[4];
			while (_istspace(*pChar))
				pChar++;
			_tcscpy(g_pszEnv, pChar);
			pChar = &g_pszEnv[_tcslen(g_pszEnv)-1];
			while (_istspace(*pChar) && (pChar >= g_pszEnv))
			{
				*pChar = '\0';
				pChar--;
			}
		}
		else
		// Check for hosts
		if (_tcsnicmp(buffer, TEXT("hosts"), 5) == 0)
		{
			g_nHosts = 0;
			g_pHosts = NULL;
			HostNode *node, dummy;
			dummy.next = NULL;
			node = &dummy;
			while (_fgetts(buffer, 1024, fin))
			{
				node->next = ParseLineIntoHostNode(buffer);
				if (node->next != NULL)
				{
					node = node->next;
					g_nHosts++;
				}
			}
			g_pHosts = dummy.next;

			return;
		}
	}
	fclose(fin);
}

void main(int argc, char *argv[])
{
	int error, i;
	DWORD dwNumWritten;
	char pszUserName[100], pszPipeName[MAX_PATH];
	DWORD length;
	char pBuffer[4096];
	int nGroupId;
	int nNproc = 1;
	bool bGetHosts = false;
	bool bUseNP = false;
	//char pszCmdLine[1024];
	WSADATA wsaData;
	int err;

	//TCHAR pszJobID[100];
	//TCHAR pszEnv[MAX_PATH] = TEXT("");
	TCHAR pszDir[MAX_PATH] = TEXT(".");

	// Start the Winsock dll.
	if ((err = WSAStartup( MAKEWORD( 2, 0 ), &wsaData )) != 0)
	{
		printf("Winsock2 dll not initialized, error: %d\n", err);
		return;
	}

	/*
	bGetHosts = !GetOpt(argc, argv, "-np", &nNproc);

	if (argc == 1)
	{
		printf("No command line specified\n");
		return;
	}
	//*/
	GetOpt(argc, argv, "-env", g_pszEnv);
	if (!GetOpt(argc, argv, "-dir", pszDir))
		GetCurrentDirectory(MAX_PATH, pszDir);

	SetCurrentDirectory(pszDir);
	
	DWORD dwType;
	if (GetBinaryType(argv[1], &dwType))
	{
		// The first argument is an executable so set things up to run one process
		g_nHosts = 1;
		TCHAR pszTempExe[MAX_PATH], *namepart;
		_tcscpy(g_pszExe, argv[1]);
		GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
		// Quote the executable in case there are spaces in the path
		_stprintf(g_pszExe, TEXT("\"%s\""), pszTempExe);
		g_pszArgs[0] = TEXT('\0');
		for (int i=2; i<argc; i++)
		{
			_tcscat(g_pszArgs, argv[i]);
			if (i < argc-1)
				_tcscat(g_pszArgs, TEXT(" "));
		}
		RunLocal(true);
		return;
	}
	else
	{
		if (GetOpt(argc, argv, "-np", &g_nHosts))
		{
			if (g_nHosts < 1)
			{
				printf("Error: must specify a number greater than 0 after the -np option\n");
				return;
			}
			if (argc < 2)
			{
				printf("Error: not enough arguments.\n");
				return;
			}
			_tcscpy(g_pszExe, argv[1]);
			g_pszArgs[0] = TEXT('\0');
			for (int i=2; i<argc; i++)
			{
				_tcscat(g_pszArgs, argv[i]);
				if (i < argc-1)
					_tcscat(g_pszArgs, TEXT(" "));
			}
			bUseNP = true;
		}
		else
		if (GetOpt(argc, argv, "-localonly", &g_nHosts))
		{
			bool bDoSMP = !GetOpt(argc, argv, "-tcp");
			if (g_nHosts < 1)
			{
				printf("Error: must specify a number greater than 0 after the -localonly option\n");
				return;
			}
			if (argc < 2)
			{
				printf("Error: not enough arguments.\n");
				return;
			}
			TCHAR pszTempExe[MAX_PATH], *namepart;
			_tcscpy(g_pszExe, argv[1]);
			GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
			// Quote the executable in case there are spaces in the path
			_stprintf(g_pszExe, TEXT("\"%s\""), pszTempExe);
			g_pszArgs[0] = TEXT('\0');
			for (int i=2; i<argc; i++)
			{
				_tcscat(g_pszArgs, argv[i]);
				if (i < argc-1)
					_tcscat(g_pszArgs, TEXT(" "));
			}
			RunLocal(bDoSMP);
			return;
		}
		else
		{
			ParseConfigFile(argv[1]);
			if ((_tcslen(g_pszArgs) > 0) && (argc > 2))
				_tcscat(g_pszArgs, TEXT(" "));
			for (int i=2; i<argc; i++)
			{
				_tcscat(g_pszArgs, argv[i]);
				if (i < argc-1)
					_tcscat(g_pszArgs, TEXT(" "));
			}
		}
	}

	TCHAR pszTempExe[MAX_PATH], *namepart;
	GetFullPathName(g_pszExe, MAX_PATH, pszTempExe, &namepart);
	// Quote the executable in case there are spaces in the path
	_stprintf(g_pszExe, TEXT("\"%s\""), pszTempExe);


	// Figure out how many processes to launch
	nNproc = 0;
	if (bUseNP)
		nNproc = g_nHosts;
	else
	{
		HostNode *n = g_pHosts;
		while (n)
		{
			nNproc += n->nSMPProcs;
			n = n->next;
		}
	}

	length = 100;
	if (GetUserName(pszUserName, &length))
		sprintf(pszPipeName, "\\\\.\\pipe\\mpd%s", pszUserName);
	else
		strcpy(pszPipeName, "\\\\.\\pipe\\mpdpipe");
	
	//printf("MPIRunMPD connecting to pipe '%s'\n", pszPipeName);
	HANDLE hPipe = CreateFile(
		pszPipeName,
		GENERIC_READ | GENERIC_WRITE,
		0, NULL,
		OPEN_EXISTING,
		0, NULL);
	
	if (hPipe != INVALID_HANDLE_VALUE)
	{
		HANDLE hOutputPipe;
		HANDLE hIOThread, hReadyEvent;
		
		strcat(pszPipeName, "out");
		hOutputPipe = CreateNamedPipe(
			pszPipeName,
			PIPE_ACCESS_DUPLEX | FILE_FLAG_WRITE_THROUGH,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_BYTE | PIPE_WAIT,
			PIPE_UNLIMITED_INSTANCES,
			0,0,0, 
			NULL
			);
		
		if (hOutputPipe == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			printf("Unable to create pipe: error %d on pipe '%s'\n", error, pszPipeName);
			CloseHandle(hPipe);
			ExitProcess(error);
		}
		
		WriteFile(hPipe, pszPipeName, strlen(pszPipeName)+1, &dwNumWritten, NULL);
		//printf("MPIRunMPD waiting for connection back on pipe '%s'\n", pszPipeName);
		if (ConnectNamedPipe(hOutputPipe, NULL))
		{
			strcpy(pBuffer, "create group\n");
			WriteFile(hPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
			GetString(hOutputPipe, pBuffer);
			nGroupId = atoi(pBuffer);

			//printf("group id acquired: %d\n", nGroupId);

			LaunchNode *pList = NULL, *p;

			if (bUseNP)
			{
				p = pList = new LaunchNode;
				pList->pNext = NULL;

				sprintf(pBuffer, "next %d\n", nNproc);
				WriteFile(hPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
				for (i=0; i<nNproc; i++)
				{
					GetString(hOutputPipe, pBuffer);
					//printf("host%d: %s\n", i, pBuffer);
					//strcpy(p->pszCmdLine, pszCmdLine);
					strcpy(p->pszCmdLine, g_pszExe);
					strcpy(p->pszArgs, g_pszArgs);
					strcpy(p->pszIPPort, pBuffer);
					strcpy(p->pszDir, ".");
					p->pszEnv[0] = '\0';
					p->nIP = 0;
					p->nPort = 0;
					p->pNext = NULL;
					if (i<nNproc-1)
					{
						p->pNext = new LaunchNode;
						p = p->pNext;
					}
				}
			}
			else
			{
				int iproc = 0;
				int nShmLow = 0, nShmHigh = 0;
				unsigned long nCurIP;
				int nCurPort;
				
				while (g_pHosts)
				{
					nShmLow = iproc;
					nShmHigh = iproc + g_pHosts->nSMPProcs - 1;

					sprintf(pBuffer, "find %s\n", g_pHosts->host);
					WriteFile(hPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
					GetString(hOutputPipe, pBuffer);
					nCurPort = atoi(pBuffer);
					NT_get_ip(g_pHosts->host, &nCurIP);

					for (int i=0; i<g_pHosts->nSMPProcs; i++)
					{
						if (pList == NULL)
						{
							pList = p = new LaunchNode;
							pList->pNext = NULL;
						}
						else
						{
							p->pNext = new LaunchNode;
							p = p->pNext;
							p->pNext = NULL;
						}
						if (strlen(g_pHosts->exe) > 0)
							strcpy(p->pszCmdLine, g_pHosts->exe);
						else
							strcpy(p->pszCmdLine, g_pszExe);
						strcpy(p->pszArgs, g_pszArgs);
						p->nIP = nCurIP;
						p->nPort = nCurPort;
						
						sprintf(p->pszEnv, 
							"MPICH_USE_MPD=1|MPICH_JOBID=mpi%d|MPICH_NPROC=%d|MPICH_IPROC=%d|MPICH_SHM_LOW=%d|MPICH_SHM_HIGH=%d", 
							nGroupId, nNproc, iproc, nShmLow, nShmHigh);
						
						if (strlen(g_pszEnv) > 0)
						{
							strcat(p->pszEnv, "|");
							strcat(p->pszEnv, g_pszEnv);
						}
						iproc++;
					}
					
					HostNode *n = g_pHosts;
					g_pHosts = g_pHosts->next;
					delete n;
				}
			}

			hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			DWORD dwThreadID;
			hIOThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RedirectIOLoopThread, hReadyEvent, 0, &dwThreadID);
			if (WaitForSingleObject(hReadyEvent, 5000) != WAIT_OBJECT_0)
			{
				printf("Wait for hReadyEvent failed, error %d\n", GetLastError());
				ExitProcess(1);
			}

			//printf("IO loop waiting on socket: %s:%d\n", g_pszIOListenHost, g_nIOListenPort);

			// launch processes
			g_nConnectionsLeft = nNproc * 2; // 1 for stdout and 1 for stderr
			p = pList;
			for (i=0; i<nNproc; i++)
			{
				if (i == 0)
					sprintf(pBuffer, "launch h'%s'c'%s'a'%s'g'%d'r'%d'0'%s:%d'1'%s:%d'2'%s:%d'\n", 
						p->pszIPPort, p->pszCmdLine, p->pszArgs, nGroupId, i, 
						g_pszIOListenHost, g_nIOListenPort, 
						g_pszIOListenHost, g_nIOListenPort, 
						g_pszIOListenHost, g_nIOListenPort);
				else
					sprintf(pBuffer, "launch h'%s'c'%s'a'%s'g'%d'r'%d'1'%s:%d'2'%s:%d'\n", 
						p->pszIPPort, p->pszCmdLine, p->pszArgs, nGroupId, i, 
						g_pszIOListenHost, g_nIOListenPort, 
						g_pszIOListenHost, g_nIOListenPort);
				WriteFile(hPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
				p = p->pNext;
				delete pList;
				pList = p;
			}

			strcpy(pBuffer, "done\n");
			WriteFile(hPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);

			CloseHandle(hPipe);
			CloseHandle(hOutputPipe);

			WaitForSingleObject(g_hNoMoreConnectionsEvent, INFINITE);
		}
		else
		{
			error = GetLastError();
			printf("unable to connect to client pipe: error %d\n", error);
			CloseHandle(hPipe);
			CloseHandle(hOutputPipe);
		}
	}
	WSACleanup();
}
