#include "mpid.h"
#include "ShmemLockedQueue.h"
#include "nt_global_cpp.h"
#include <windows.h>
#include <stdio.h>
#include <time.h>
#include "Database.h"
#include "bnrfunctions.h"
#include <stdlib.h>
#include "mpdutil.h"

#define MPICH_MPD_TIMEOUT     30
#define MPICH_SHORT_TIMEOUT   15000
#define MPICH_MEDIUM_TIMEOUT  30000

// Global variables
int g_nLastRecvFrom = 0;
int g_nIproc = 0;
int g_nNproc = 0;
unsigned int g_nNicMask = 0;
unsigned int g_nNicNet = 0;
bool g_bMultinic = false;
char g_pszHostName[NT_HOSTNAME_LEN] = "";
char g_pszRootHostName[NT_HOSTNAME_LEN] = "";
int g_nRootPort = 0;
NT_ipvishm_ProcEntry *g_pProcTable = NULL;
MessageQueue g_MsgQueue;
char g_ErrMsg[1024];
bool g_bInNT_ipvishm_End = false;
LARGE_INTEGER g_nPerfFrequency;
#ifdef DEBUG_OUTPUT
bool g_bVerbose = false;
#endif
bool g_bMPIRunUsed = true;
bool g_bUseDatabase = false;
bool g_bUseBNR = false;
Database g_Database;
char g_pszJobID[100]="";
char g_pszMPDHost[NT_HOSTNAME_LEN];
char g_pszMPDPhrase[256];
char g_pszMPDId[20];
int g_nMPDPort;
bool g_bMPDFinalize = false;

extern "C" {
int MPID_NT_ipvishm_is_shm( int );
__declspec(dllexport) void GetMPICHVersion(char *str, int length);
}

// Function name	: GetMPICHVersion
// Description	    : 
// Return type		: void 
// Argument         : char *str
// Argument         : int length
__declspec(dllexport) void GetMPICHVersion(char *str, int length)
{
    //_snprintf(str, length, "%d.%d.%d %s", VERSION_RELEASE, VERSION_MAJOR, VERSION_MINOR, __DATE__);
    _snprintf(str, length, "%s %s", MPICH_VERSION, __DATE__);
}

// Function name	: PollShmemQueue
// Description	    : 
// Return type		: void 
void PollShmemAndViQueues()
{
	bool bSleep = true;
	if (g_pShmemQueue[g_nIproc]->RemoveNextInsert(&g_MsgQueue, false))
		bSleep = false;
	if (ViWorkerThread(0))
		bSleep = false;
	if (bSleep)
		Sleep(0);
}

// Function name	: MakeErrMsg
// Description	    : 
// Return type		: void 
// Argument         : int error
// Argument         : char *pFormat
// Argument         : ...
void MakeErrMsg(int error, char *pFormat, ...)
{
	char chMsg[1024];
	va_list pArg;
	
	va_start(pArg, pFormat);
	vsprintf(chMsg, pFormat, pArg);
	va_end(pArg);

	nt_error(chMsg, error);
}

// Function name	: ArgSqueeze
// Description	    : Remove all null arguments from an arg vector; update the number of arguments.
// Return type		: void 
// Argument         :  int *Argc
// Argument         : char **argv
void ArgSqueeze( int *Argc, char **argv )
{
    int argc, i, j;

    // Compress out the eliminated args
    argc = *Argc;
    j    = 0;
    i    = 0;
    while (j < argc) 
	{
		while (argv[j] == 0 && j < argc) 
			j++;
		if (j < argc) argv[i++] = argv[j++];
    }

    // Back off the last value if it is null
    if (!argv[i-1]) 
		i--;
    *Argc = i;
}

// Function name	: AbortInit
// Description	    : 
// Return type		: void 
// Argument         : int error
// Argument         : char *pFormat
// Argument         : ...
void AbortInit(int error, char *pFormat, ...)
{
	char chMsg[1024];
	va_list pArg;
	
	va_start(pArg, pFormat);
	vsprintf(chMsg, pFormat, pArg);
	va_end(pArg);

	nt_error(chMsg, error);
}

// Function name	: SetEnvironmentString
// Description	    : 
// Return type		: void 
// Argument         : char *pszEnv
void SetEnvironmentString(char *pszEnv)
{
	char name[MAX_PATH]="", value[MAX_PATH]="";
	char *pChar;

	pChar = name;
	while (*pszEnv != '\0')
	{
		if (*pszEnv == '=')
		{
			*pChar = '\0';
			pChar = value;
		}
		else
		if (*pszEnv == '|')
		{
			*pChar = '\0';
			pChar = name;
			SetEnvironmentVariable(name, value);
		}
		else
		{
			*pChar = *pszEnv;
			pChar++;
		}
		pszEnv++;
	}
	*pChar = '\0';
	SetEnvironmentVariable(name, value);
}

static unsigned int GetIP(char *pszIP)
{
    unsigned int nIP;
    unsigned int a,b,c,d;
    sscanf(pszIP, "%u.%u.%u.%u", &a, &b, &c, &d);
    //printf("mask: %u.%u.%u.%u\n", a, b, c, d);fflush(stdout);
    nIP = (d << 24) | (c << 16) | (b << 8) | a;
    return nIP;
}

static unsigned int GetMask(char *pszMask)
{
    unsigned int nMask = 0;
    if (strstr(pszMask, "."))
    {
	unsigned int a,b,c,d;
	sscanf(pszMask, "%u.%u.%u.%u", &a, &b, &c, &d);
	//printf("mask: %u.%u.%u.%u\n", a, b, c, d);fflush(stdout);
	nMask = (d << 24) | (c << 16) | (b << 8) | a;
    }
    else
    {
	int nBits = atoi(pszMask);
	for (int i=0; i<nBits; i++)
	{
	    nMask = nMask << 1;
	    nMask = nMask | 0x1;
	}
    }
    /*
    unsigned int a, b, c, d;
    a = ((unsigned char *)(&nMask))[0];
    b = ((unsigned char *)(&nMask))[1];
    c = ((unsigned char *)(&nMask))[2];
    d = ((unsigned char *)(&nMask))[3];
    printf("mask: %u.%u.%u.%u\n", a, b, c, d);fflush(stdout);
    */
    return nMask;
}

static int GetLocalIPs(unsigned int *pIP, int max)
{
    char hostname[100], **hlist;
    HOSTENT *h = NULL;
    int error;
    int n = 0;
    
    if (gethostname(hostname, 100) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	return 0;
    }
    
    h = gethostbyname(hostname);
    if (h == NULL)
    {
	error = WSAGetLastError();
	return 0;
    }
    
    hlist = h->h_addr_list;
    while (*hlist != NULL && n<max)
    {
	pIP[n] = *(unsigned int*)(*hlist);

	/*
	unsigned int a, b, c, d;
	a = ((unsigned char *)(&pIP[n]))[0];
	b = ((unsigned char *)(&pIP[n]))[1];
	c = ((unsigned char *)(&pIP[n]))[2];
	d = ((unsigned char *)(&pIP[n]))[3];
	printf("ip: %u.%u.%u.%u\n", a, b, c, d);fflush(stdout);
	*/

	hlist++;
	n++;
    }
    return n;
}

bool PutRootPortInMPDDatabase(char *str, int port, char *barrier_name)
{
	char dbname[100];
	char pszStr[256];
	SOCKET sock;
	DWORD length = 100;
	char *pszID;

	pszID = getenv("MPD_ID");
	if (pszID != NULL)
	    strcpy(g_pszMPDId, pszID);

	strcpy(pszStr, str);
	str = strtok(pszStr, ":");
	if (str == NULL)
	    return false;
	strcpy(dbname, str);
	str = strtok(NULL, ":");
	if (str == NULL)
	    return false;
	g_nMPDPort = atoi(str);
	str = strtok(NULL, ":");
	if (str == NULL)
	    return false;
	strcpy(g_pszMPDPhrase, str);
	str = strtok(NULL, ":");
	if (str != NULL)
	    strcpy(g_pszMPDHost, str);
	else
	    GetComputerName(g_pszMPDHost, &length);

	easy_socket_init();

	if (ConnectToMPD(g_pszMPDHost, g_nMPDPort, g_pszMPDPhrase, &sock))
	{
		printf("ERROR:PutRootPortInMPDDatabase: ConnectToMPD failed.\n");fflush(stdout);
		return false;
	}

	sprintf(pszStr, "dbput name=%s key=port value=%d", dbname, port);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
		printf("ERROR:PutRootPortInMPDDatabase: Unable to write '%s' to socket[%d]\n", pszStr, sock);fflush(stdout);
		easy_closesocket(sock);
		return false;
	}
	if (!ReadStringTimeout(sock, pszStr, MPICH_MPD_TIMEOUT))
	{
		printf("ERROR:PutRootPortInMPDDatabase: put failed: error %d\n", WSAGetLastError());fflush(stdout);
		easy_closesocket(sock);
		return false;
	}
	if (strnicmp(pszStr, "DBS_SUCCESS", 11) != 0)
	{
	    printf("ERROR:PutRootPortInMPDDatabase: putting the root port in the mpd database failed.\n%s", pszStr);fflush(stdout);
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    return false;
	}

	sprintf(pszStr, "barrier name=%s count=2", barrier_name);
	if (WriteString(sock, pszStr) == SOCKET_ERROR)
	{
		printf("ERROR:PutRootPortInMPDDatabase: Unable to write the barrier command: error %d\n", WSAGetLastError());fflush(stdout);
		easy_closesocket(sock);
		return false;
	}
	bool bBarrierContinue = true;
	while (bBarrierContinue)
	{
	    if (!ReadStringTimeout(sock, pszStr, MPICH_MPD_TIMEOUT*2))
	    {
		printf("ERROR:PutRootPortInMPDDatabase: Unable to read the result of the barrier command: error %d\n", WSAGetLastError());fflush(stdout);
		easy_closesocket(sock);
		return false;
	    }
	    if (strncmp(pszStr, "SUCCESS", 8))
	    {
		// If it is not 'SUCCESS' then
		if (strncmp(pszStr, "INFO", 4))
		{
		    // If it is not an 'INFO - ...' message then it is an error
		    printf("ERROR:PutRootPortInMPDDatabase: barrier failed:\n%s\n", pszStr);fflush(stdout);
		    easy_closesocket(sock);
		    return false;
		}
	    }
	    else
	    {
		bBarrierContinue = false;
	    }
	}

	WriteString(sock, "done");
	easy_closesocket(sock);

	return true;
}

bool ParseMPDString(char *str)
{
    char pszStr[1024];
    char *pszID;

    pszID = getenv("MPD_ID");
    if (pszID == NULL)
	return false;
    strcpy(g_pszMPDId, pszID);

    strcpy(pszStr, str);
    str = strtok(pszStr, ":");
    if (str == NULL)
	return false;
    strcpy(g_pszMPDHost, str);
    str = strtok(NULL, ":");
    if (str == NULL)
	return false;
    g_nMPDPort = atoi(str);
    str = strtok(NULL, ":");
    if (str == NULL)
	return false;
    strcpy(g_pszMPDPhrase, str);

    return true;
}

bool UpdateMPIFinalizedInMPD()
{
    SOCKET sock;
    char pszStr[256];

    if (ConnectToMPD(g_pszMPDHost, g_nMPDPort, g_pszMPDPhrase, &sock))
    {
	printf("ConnectToMPD(%s:%d) failed preventing process %d from signalling that it has reached MPI_Finalize\n", g_pszMPDHost, g_nMPDPort, g_nIproc);
	fflush(stdout);
	return false;
    }

    sprintf(pszStr, "setMPIFinalized %s", g_pszMPDId);
    if (WriteString(sock, pszStr) == SOCKET_ERROR)
    {
	printf("ERROR:UpdateMPIFinalized: Unable to write '%s' to socket[%d]\n", pszStr, sock);
	fflush(stdout);
	easy_closesocket(sock);
	return false;
    }
    if (!ReadStringTimeout(sock, pszStr, MPICH_MPD_TIMEOUT))
    {
	printf("ERROR:UpdateMPIFinalized: Unable to read the result of the setMPIFinalized command\n");
	fflush(stdout);
	easy_closesocket(sock);
	return false;
    }
    if (stricmp(pszStr, "SUCCESS") != 0)
    {
	if (g_nIproc != 0) // don't print the error if it is the root process because the root may not have been started by an mpd.
	{
	    printf("ERROR:UpdateMPIFinalized: setMPIFinalized failed.\n");
	    fflush(stdout);
	}
	WriteString(sock, "done");
	easy_closesocket(sock);
	return false;
    }

    WriteString(sock, "done");
    easy_closesocket(sock);

    return true;
}

// Function name	: MPID_NT_ipvishm_Init
// Description	    : Launches all the processes and sets up a mechanism by which
//                    any process can make a connection with any other process.
// Return type		: void 
// Argument         :  int *argc
// Argument         : char ***argv
void MPID_NT_ipvishm_Init( int *argc, char ***argv )
{
	char pszIproc[10]="", pszNproc[10]="", pszRootPort[10]="";
	char pszExtra[256]="", pszTemp[100];
	char pszDBSHost[100]="", pszDBSPort[10]="";
	bool bCommPortAvailable = true;
	WSADATA wsaData;
	int err, i;

	try{

#ifdef DEBUG_OUTPUT
	char pszVerbose[100];
	if (GetEnvironmentVariable("MPICH_VERBOSE", pszVerbose, 100))
		g_bVerbose = true;
#endif

	//Start the Winsock dll
	if ((err = WSAStartup( MAKEWORD( 2, 0 ), &wsaData )) != 0)
		AbortInit(err, "Winsock2 dll not initialized");

	// Attempt to use BNR
	g_bUseBNR = LoadBNRFunctions();

	if (g_bUseBNR)
	{
		BNR_Group parent_group, joint_group;
		if (BNR_Init() == BNR_FAIL)
		{
			g_bUseBNR = false;
		}
		else
		{
			if (BNR_Get_group(&g_myBNRgroup) == BNR_FAIL)
				AbortInit(1, "BNR_Get_group failed");
			if (BNR_Get_parent(&parent_group) == BNR_FAIL)
				AbortInit(1, "BNR_Get_parent failed");
			try	{
			if (BNR_Merge(g_myBNRgroup, parent_group, &joint_group) == BNR_FAIL)
				AbortInit(1, "BNR_Merge failed");
			}catch(...) { AbortInit(1, "Exception caught in BNR_Merge"); }
			if (BNR_Fence(joint_group) == BNR_FAIL)
				AbortInit(1, "BNR_Fence failed");
			MPID_MyWorldRank = -1;
			if (BNR_Get_rank(g_myBNRgroup, &MPID_MyWorldRank) == BNR_FAIL)
				AbortInit(1, "BNR_Get_rank failed");
			
			char pKey[100], pBuffer[4096] = "";
			sprintf(pKey, "env%d", MPID_MyWorldRank);
			if (BNR_Get(joint_group, pKey, pBuffer) == BNR_FAIL)
				AbortInit(1, "BNR_Get %s failed", pKey);
			SetEnvironmentString(pBuffer);
			
			if (BNR_Free_group(parent_group) == BNR_FAIL)
				AbortInit(1, "BNR_Free_group(parent_group) failed");
			if (BNR_Free_group(joint_group) == BNR_FAIL)
				AbortInit(1, "BNR_Free_group(joint_group) failed");
		}
	}

	// Save the local host name
	// For multihomed systems MPI_COMNIC can set the hostname to a specific nic
	// else the default is whatever gethostname returns
	g_pszHostName[0] = '\0';
	GetEnvironmentVariable("MPICH_COMNIC", g_pszHostName, NT_HOSTNAME_LEN);
	if (strlen(g_pszHostName) < 1)
	{
		if (gethostname(g_pszHostName, NT_HOSTNAME_LEN) == SOCKET_ERROR)
		{
			err = WSAGetLastError();
			if (err == WSAEFAULT)
				AbortInit(err, "Cannot handle hostnames longer than 100 characters");
			else
				AbortInit(err, "gethostname failed");
		}
		// Convert the host name to an ip string to make connection establishment more robust
		NT_Tcp_get_ip_string(g_pszHostName, g_pszHostName);
	}

	if (g_bUseBNR)
	{
		char pBuffer[100];
		if (MPID_MyWorldRank == 0)
			BNR_Put(g_myBNRgroup, "MPICH_ROOT", g_pszHostName, -1);
		BNR_Fence(g_myBNRgroup);
		BNR_Get(g_myBNRgroup, "MPICH_ROOT", pBuffer);
		SetEnvironmentVariable("MPICH_ROOT", pBuffer);

		// Remove this line later
		sprintf(pszIproc, "%d", MPID_MyWorldRank);
	}

	// Read in the variables passed in the environment
	if (GetEnvironmentVariable("MPICH_JOBID", g_pszJobID, 100) == 0)
		g_bMPIRunUsed = false;

	g_bUseDatabase = false;
	if (GetEnvironmentVariable("MPICH_DBS", pszTemp, 100))
	{
		char *token;
		token = strtok(pszTemp, ":");
		if (token != NULL)
			strcpy(pszDBSHost, token);
		token = strtok(NULL, " \n");
		if (token != NULL)
			strcpy(pszDBSPort, token);
		g_bUseDatabase = true;
	}
	else
	{
		if ( GetEnvironmentVariable("MPICH_DBS_HOST", pszDBSHost, 100) && 
			 GetEnvironmentVariable("MPICH_DBS_PORT", pszDBSPort, 10) )
		{
			g_bUseDatabase = true;
		}
	}

	if (g_bUseDatabase)
	{
		char pszEnv[1024];
		int length = 1024;
		g_Database.SetID(g_pszJobID);
		g_Database.Init();
		if (GetEnvironmentVariable("MPICH_IPROC", pszIproc, 10) == 0)
		{
			// If there is no iproc variable in the environment then get a 
			// generic environment from the dbs server.
			g_Database.Get("env", pszEnv, &length);
			SetEnvironmentString(pszEnv);
			GetEnvironmentVariable("MPICH_IPROC", pszIproc, 10);
		}
		else
		{
			if (GetEnvironmentVariable("MPICH_NPROC", pszNproc, 10) == 0)
			{
				// If there is an iproc but no nproc envrionment variable
				// then get the environment specific to this process from 
				// the dbs server.
				char pszEnvKey[100];
				sprintf(pszEnvKey, "env%d", atoi(pszIproc));
				g_Database.Get(pszEnvKey, pszEnv, &length);
				SetEnvironmentString(pszEnv);
			}
			// If there is an iproc and nproc environment variable then get 
			// nothing from the dbs server.
		}
	}
	else
	{
		if (GetEnvironmentVariable("MPICH_IPROC", pszIproc, 10) == 0)
		{
			// If an application is run without MPIRun then it is the first 
			// and only process
			strcpy(pszIproc, "0");
			g_bMPIRunUsed = false;
		}
		if (GetEnvironmentVariable("MPICH_ROOT", pszTemp, 100))
		{
			char *token;
			token = strtok(pszTemp, ":");
			if (token != NULL)
				strcpy(g_pszRootHostName, token);
			token = strtok(NULL, " \n");
			if (token != NULL)
				strcpy(pszRootPort, token);
		}
		else
		{
			if (GetEnvironmentVariable("MPICH_ROOTHOST", g_pszRootHostName, 100) == 0)
			{
				unsigned long size = 100;
				GetComputerName(g_pszRootHostName, &size);
				g_bMPIRunUsed = false;
			}
			if (GetEnvironmentVariable("MPICH_ROOTPORT", pszRootPort, 10) == 0)
			{
				strcpy(pszRootPort, "-1");
				g_bMPIRunUsed = false;
			}
		}
		g_nRootPort = atoi(pszRootPort);
		GetEnvironmentVariable("MPICH_EXTRA", pszExtra, 100);
	}
	if (GetEnvironmentVariable("MPICH_NPROC", pszNproc, 10) == 0)
	{
		// If an application is run without MPIRun then it is the only process
		strcpy(pszNproc, "1");
		g_bMPIRunUsed = false;
	}
	if (GetEnvironmentVariable("MPICH_NUMCOMMPORTS", pszTemp, 100))
		g_NumCommPortThreads = atoi(pszTemp);
	if (GetEnvironmentVariable("MPICH_NOCOMMPORT", pszTemp, 100))
		bCommPortAvailable = false;

	MPID_MyWorldRank = g_nIproc = atoi(pszIproc);
	MPID_MyWorldSize = g_nNproc = atoi(pszNproc);

	// Save the high performance counter frequency
	QueryPerformanceFrequency(&g_nPerfFrequency);

	if (g_nIproc == 0)
		ClearLog();
	if (g_nNproc < 1)
		AbortInit(1, "Invalid number of processes: %d", g_nNproc);

	g_pProcTable = new NT_ipvishm_ProcEntry[g_nNproc];
	if (g_pProcTable == NULL)
		AbortInit(1, "Unable to allocate memory for the proc table in MPID_Init");
	for (i=0; i<g_nNproc; i++)
	{
		g_pProcTable[i].exename[0] = '\0';
		g_pProcTable[i].host[0] = '\0';
		g_pProcTable[i].listen_port = 0;
		g_pProcTable[i].control_port = 0;
		g_pProcTable[i].pid = 0;
		g_pProcTable[i].sock = INVALID_SOCKET;
		g_pProcTable[i].sock_event = NULL;
		g_pProcTable[i].hConnectLock = NULL;
		g_pProcTable[i].hValidDataEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		g_pProcTable[i].shm = 0;
		g_pProcTable[i].via = 0;
		g_pProcTable[i].msg.ovl.hEvent = NULL;
		g_pProcTable[i].msg.state = NT_MSG_READING_TAG;
		g_pProcTable[i].multinic = FALSE;
		g_pProcTable[i].num_nics = 0;
	}

	g_pProcTable[g_nIproc].num_nics = GetLocalIPs(g_pProcTable[g_nIproc].nic_ip, MAX_NUM_NICS);
	if (g_pProcTable[g_nIproc].num_nics > 1)
	    g_pProcTable[g_nIproc].multinic = TRUE;
	char pszNetMask[50];
	if (GetEnvironmentVariable("MPICH_NETMASK", pszNetMask, 50))
	{
	    char *token = strtok(pszNetMask, "/");
	    if (token != NULL)
	    {
		token = strtok(NULL, "\n");
		if (token != NULL)
		{
		    g_nNicNet = GetIP(pszNetMask);
		    g_nNicMask = GetMask(token);
		    g_bMultinic = true;
		}
	    }
	}
	else
	{
	    g_nNicNet = 0;
	    g_nNicMask = 0;
	    g_bMultinic = false;
	}

	bool bFixedPortUsed = false;
	if (g_nRootPort > 0 && g_nIproc == 0)
	{
		g_pProcTable[0].control_port = g_nRootPort;
		// If a specific port was provided through the environment then
		// don't write the port out to a file.
		bFixedPortUsed = true; 
	}

	// The executable name is the full path to the executable
	HMODULE hModule = GetModuleHandle(NULL);
	if (!GetModuleFileName(hModule, 
			g_pProcTable[g_nIproc].exename, NT_EXENAME_LEN))
		strcpy(g_pProcTable[g_nIproc].exename, "unknown.exe");
	strcpy(g_pProcTable[g_nIproc].host, g_pszHostName);
	g_pProcTable[g_nIproc].pid = (long)GetCurrentProcessId();

	// If all the processes can reach each other through shared memory then there is
	// no need to create the socket completion port threads.
	int nNumShmQueues = GetShmemClique();
	if (nNumShmQueues == g_nNproc)
		bCommPortAvailable = false;

	/*
	// If all the processes can reach each other through VI's then there is
	// no need to create the socket completion port threads.
	GetViClique();
	if (everyonecantalkvi)
		bCommPortAvailable = false;
	//*/

	if (bCommPortAvailable) // If there is no completion port available (Win9x) then socket communication is not available
	{
		DWORD dwThreadID;
		HANDLE hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

		// Start the communication thread
		hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (hReadyEvent == NULL)
			AbortInit(1, "Unable to create an event in MPID_Init");
		for (i=0; i<NT_CREATE_THREAD_RETRIES; i++)
		{
		    g_hCommPortThread = CreateThread(
			NULL, 0, 
			(LPTHREAD_START_ROUTINE) CommPortThread,
			hReadyEvent,
			NT_THREAD_STACK_SIZE, &dwThreadID);
		    if (g_hCommPortThread != NULL)
			break;
		    Sleep(NT_CREATE_THREAD_SLEEP_TIME);
		}
		if (g_hCommPortThread == NULL)
			AbortInit(GetLastError(), "Unable to spawn CommPortThread");
		if (WaitForSingleObject(hReadyEvent, MPICH_SHORT_TIMEOUT) == WAIT_TIMEOUT)
			AbortInit(1, "Communication thread setup timed out");
		CloseHandle(hReadyEvent);
	}
	else
		g_hCommPortThread = NULL;

	if (g_bUseBNR)
	{
		char pszKey[100], pszValue[MAX_PATH];

		sprintf(pszKey, "ListenPort%d", g_nIproc);
		sprintf(pszValue, "%d", g_pProcTable[g_nIproc].listen_port);
		BNR_Put(g_myBNRgroup, pszKey, pszValue, -1);

		sprintf(pszKey, "ListenHost%d", g_nIproc);
		strcpy(pszValue, g_pProcTable[g_nIproc].host);
		BNR_Put(g_myBNRgroup, pszKey, pszValue, -1);

		sprintf(pszKey, "Executable%d", g_nIproc);
		strcpy(pszValue, g_pProcTable[g_nIproc].exename);
		BNR_Put(g_myBNRgroup, pszKey, pszValue, -1);

		sprintf(pszKey, "pid%d", g_nIproc);
		sprintf(pszValue, "%d", g_pProcTable[g_nIproc].pid);
		BNR_Put(g_myBNRgroup, pszKey, pszValue, -1);

		// Put anything for VI ???
	}
	else if (g_bUseDatabase)
	{
		char pszKey[100], pszValue[MAX_PATH];

		sprintf(pszKey, "ListenPort%d", g_nIproc);
		sprintf(pszValue, "%d", g_pProcTable[g_nIproc].listen_port);
		g_Database.Put(pszKey, pszValue, strlen(pszValue)+1);

		sprintf(pszKey, "ListenHost%d", g_nIproc);
		strcpy(pszValue, g_pProcTable[g_nIproc].host);
		g_Database.Put(pszKey, pszValue, strlen(pszValue)+1);

		sprintf(pszKey, "Executable%d", g_nIproc);
		strcpy(pszValue, g_pProcTable[g_nIproc].exename);
		g_Database.Put(pszKey, pszValue, strlen(pszValue)+1);

		sprintf(pszKey, "pid%d", g_nIproc);
		sprintf(pszValue, "%d", g_pProcTable[g_nIproc].pid);
		g_Database.Put(pszKey, pszValue, strlen(pszValue)+1);
		//g_Database.Put(pszKey, &g_pProcTable[g_nIproc].pid, sizeof(int));

		// Put anything for VI ???
	}
	else
	{
		DWORD dwThreadID;
		HANDLE hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

		// Start the control thread
		ResetEvent(hReadyEvent);
		for (i=0; i<NT_CREATE_THREAD_RETRIES; i++)
		{
		    g_hControlLoopThread = CreateThread(
			NULL, 0, 
			(LPTHREAD_START_ROUTINE) ControlLoopThread,
			hReadyEvent,
			NT_THREAD_STACK_SIZE, &dwThreadID);
		    if (g_hControlLoopThread != NULL)
			break;
		    Sleep(NT_CREATE_THREAD_SLEEP_TIME);
		}
		if (g_hControlLoopThread == NULL)
			AbortInit(GetLastError(), "Unable to spawn ControlLoopThread");
		if (WaitForSingleObject(hReadyEvent, MPICH_SHORT_TIMEOUT) == WAIT_TIMEOUT)
			AbortInit(1, "Control thread setup timed out");
		
		if (g_nIproc == 0)
		{
			ResetEvent(hReadyEvent);
			
			// Why do I use a global variable instead of just using 
			// g_pProcTable[0].control_port?
			g_nRootPort = g_pProcTable[0].control_port;
			
			if (g_bMPIRunUsed && !bFixedPortUsed)
			{
				if (strnicmp(pszExtra, "shm:", 4) == 0)
				{
					// Write the port number to the temporary memory mapped file 
					// described by pszExtra
					HANDLE hMapping;
					LONG *pMapping;
					
					SECURITY_ATTRIBUTES saAttr;
					saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
					saAttr.lpSecurityDescriptor = NULL;
					saAttr.bInheritHandle = FALSE;
					
					// Create a mapping from the page file
					hMapping = CreateFileMapping(
						INVALID_HANDLE_VALUE,
						&saAttr, //NULL,
						PAGE_READWRITE,
						0, sizeof(LONG),
						&pszExtra[4]);
					
					if (hMapping == NULL)
						AbortInit(GetLastError(), "Unable to create a memory mapping for inter-process communication");
					if (GetLastError() != ERROR_ALREADY_EXISTS)
						AbortInit(1, "MPIRun has not created the memory mapping to place the root port number in");
					
					// Map the file and save the pointer to the base of the mapped file
					pMapping = (LONG *)MapViewOfFile(
						hMapping,
						FILE_MAP_WRITE,
						0,0,
						sizeof(LONG));
					
					if (pMapping == NULL)
						AbortInit(GetLastError(), "Unable to memory map the view of the ipc file");
					
					// Write the listening port to the ipc shared memory file
					*pMapping = g_pProcTable[0].control_port;
					
					// Wait for the launcher to read the data before closing the mapping
					while (*pMapping != 0)
						Sleep(200);
					
					UnmapViewOfFile(pMapping);
					CloseHandle(hMapping);
				}
				else if (strnicmp(pszExtra, "mpd:", 4) == 0)
				{
					// use mpd to get the root port back to mpirun
					if (!PutRootPortInMPDDatabase(&pszExtra[4], g_pProcTable[0].control_port, g_pszJobID))
					    AbortInit(-1, "Unable to put the root listening port in the mpd database");
					g_bMPDFinalize = true;
				}
				else
				{
					// Write the port number to the temporary file 
					// described by pszExtra
					char str[100];
					DWORD num_written;
					HANDLE hFile = CreateFile(pszExtra, GENERIC_WRITE, 
						FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE)
					{
						int error = GetLastError();
						LogMsg("CreateFile failed: error %d, file '%s'\n", error, pszExtra);
						AbortInit(error, "CreateFile failed: %s", pszExtra);
					}
					sprintf(str, "%d\n", g_pProcTable[0].control_port);
					if (!WriteFile(hFile, str, strlen(str)+2, &num_written, NULL))
					{
						int error = GetLastError();
						LogMsg("WriteFile failed of the root control port, Error: %d", error);
						CloseHandle(hFile);
						AbortInit(error, "WriteFile(%s, root port) failed", pszExtra);
					}
					CloseHandle(hFile);
				}
			}
			
			SendInitDataToRoot();
			
			// Wait for g_hControlLoop(Client)Thread(s) to signal that all 
			// processes have connected. For now, this is set to INFINITE.  
			// What value would be appropriate to give the processes time to 
			// launch and connect back to the root?
			WaitForSingleObject(g_hEveryoneConnectedEvent, INFINITE);
			
		}
		else
		{
		    if (strnicmp(pszExtra, "mpd:", 4) == 0)
		    {
			if (!ParseMPDString(&pszExtra[4]))
			    AbortInit(-1, "Unable to parse the mpd host and port\n");
			easy_socket_init();
			g_bMPDFinalize = true;
		    }

			// Send the root process or the database server information so it
			// can inform other processes how to connect to this process
			SendInitDataToRoot();
		}
		CloseHandle(hReadyEvent);
	}

	pszTemp[0] = '\0';
	GetEnvironmentVariable("MPICH_SINGLETHREAD", pszTemp, 100);
	if (pszTemp[0] == '1')
	{
		SetEnvironmentVariable("MPICH_SHM_SINGLETHREAD", "1");
		SetEnvironmentVariable("MPICH_VI_SINGLETHREAD", "1");
	}

	// Initialize the shared memory stuff
	try {
	InitSMP();
	}catch(...)
	{
	    nt_error("exception thrown in InitSMP caught in Init", 1);
	}

	try{
	// Initialize the VIA stuff
	if (InitVI())
	{
		pszTemp[0] = '\0';
		pszTemp[1] = '\0';
		GetEnvironmentVariable("MPICH_SHM_SINGLETHREAD", pszTemp, 100);
		GetEnvironmentVariable("MPICH_VI_SINGLETHREAD", &pszTemp[1], 99);
		if (pszTemp[0] == '1' && pszTemp[1] == '1' && g_pShmemQueue)
			g_MsgQueue.SetProgressFunction(PollShmemAndViQueues);
	}

	}catch(...)
	{
		nt_error("exception thrown in InitVi caught in Init", 1);
	}

	}catch(...)
	{
		nt_error("Exception caught in Init.", 1);
	}
}

// Function name	: MPID_NT_ipvishm_End
// Description	    : Finishes any outstanding IO, closes all connections, and waits for 
//                    everyone else to finish?
// Return type		: void 
void MPID_NT_ipvishm_End()
{
	//DPRINTF(("MPID_NT_ipvishm_End() called.\n"));
	g_bInNT_ipvishm_End = true;

	if (g_bMPDFinalize)
	    UpdateMPIFinalizedInMPD();

	if (g_nNproc > 1)
	{
		if (g_bUseBNR)
		{
			char pKey[100], pValue[100];
			sprintf(pKey, "InDone%d", g_nIproc);
			BNR_Put(g_myBNRgroup, pKey, "yes", 0);

			BNR_Fence(g_myBNRgroup);
			if (g_nIproc == 0)
			{
				for (int i=0; i<g_nNproc; i++)
				{
					sprintf(pKey, "InDone%d", i);
					BNR_Get(g_myBNRgroup, pKey, pValue);
				}
				BNR_Put(g_myBNRgroup, "AllDone", "yes", -1);
			}
			BNR_Fence(g_myBNRgroup);
			BNR_Get(g_myBNRgroup, "AllDone", pValue);
		}
		else if (g_bUseDatabase)
		{
			///////////////////////////////////////////////////////////////////////
			// Use InDone and PassThroughDone keys to create a barrier here.
			// Then use the ThroughDone key to guarantee no more database accesses.
			///////////////////////////////////////////////////////////////////////
			char pValue[100];
			int length = 100;
			// Everyone puts an InDone message into the database
			g_Database.Put("InDone", (void*)"yes", 4, false);
			if (g_nIproc == 0)
			{
				// Process zero consumes all the InDone messages ...
				for (int i=0; i<g_nNproc; i++)
				{
					length = 100;
					g_Database.Get("InDone", pValue, &length);
				}
				// ... and then put a PassThroughDone message
				g_Database.Put("PassThroughDone", (void*)"yes", 4);
			}
			// Everyone waits for the PassThroughDone message from process zero ...
			length = 100;
			g_Database.Get("PassThroughDone", pValue, &length);
			// ... and then puts a ThroughDone message
			g_bViClosing = true;
			g_Database.Put("ThroughDone", (void*)"yes", 4, false);
			if (g_nIproc == 0)
			{
				// Process zero consumes all the ThroughDone messages
				for (int i=0; i<g_nNproc; i++)
				{
					length = 100;
					g_Database.Get("ThroughDone", pValue, &length);
				}
				// When all the ThroughDone messages have been consumed, we can
				// guarantee that there will be no more use of the database.
				// So, it is safe for process zero to delete the branch in the
				// database corresponding to this job.
				g_Database.Delete();
			}
		}
		else
		{
			// Signal that the current process is in End
			SendInDoneMsg();
			
			WaitForSingleObject(g_hOkToPassThroughDone, INFINITE);
			CloseHandle(g_hOkToPassThroughDone);
			
			if (g_nIproc == 0)
			{
				// Wait for everyone else to arrive here
				WaitForSingleObject(g_hAllInDoneEvent, INFINITE);
				CloseHandle(g_hAllInDoneEvent);
			}
			
			// Signal the control loop thread to stop
			SetEvent(g_hStopControlLoopEvent);
			if (g_hControlLoopThread != NULL)
			{
			    WaitForSingleObject(g_hControlLoopThread, MPICH_SHORT_TIMEOUT);
			    CloseHandle(g_hControlLoopThread);
			    g_hControlLoopThread = NULL;
			}
		}
		
		if (g_hCommPortThread != NULL)
		{
			// Signal the communication thread to stop
			DPRINTF(("process %d: MPID_NT_ipvishm_End signalling CommPortThread to exit.\n", g_nIproc));
			g_nCommPortCommand = NT_COMM_CMD_EXIT;
			SetEvent(g_hCommPortEvent);
			
			// Assuming there aren't any blocking calls pending
			// the CommThread should exit soon after signalling g_hCommEvent
			if (WaitForSingleObject(g_hCommPortThread, MPICH_SHORT_TIMEOUT) == WAIT_TIMEOUT)
			{
				//nt_error("wait for CommThread to exit timed out", 1);
				LogMsg(TEXT("wait for CommPortThread to exit in End timed out"));
				TerminateThread(g_hCommPortThread, 0);
			}
			
			// Close all the communication sockets
			for (int i=0; i<g_nNproc; i++)
			{
				if (g_pProcTable[i].sock_event != NULL)
				{
					// Close the socket
					NT_Tcp_closesocket(g_pProcTable[i].sock, g_pProcTable[i].sock_event);
					g_pProcTable[i].sock = INVALID_SOCKET;
					g_pProcTable[i].sock_event = NULL;
					CloseHandle(g_pProcTable[i].msg.ovl.hEvent);
				}
				CloseHandle(g_pProcTable[i].hValidDataEvent);
			}
		}
	}

	// Clean up the shared memory stuff
	EndSMP();

	// Clean up the VIA stuff
	EndVI();

	// Clean up the BNR interface
	if (g_bUseBNR)
		BNR_Finalize();

	// Free up allocated memory
	if (g_pProcTable != NULL)
	{
		delete g_pProcTable;
		g_pProcTable = NULL;
	}
	if (g_bMPDFinalize)
	    easy_socket_finalize();
	WSACleanup();
}

int MPID_NT_ipvishm_exitall(char *msg, int code)
{
    nt_error(msg, code);
    return 0;
}

int MPID_NT_ipvishm_is_shm( int rank )
{
    return g_pProcTable[rank].shm;
}

// Function name	: nt_tcp_shm_proc_info
// Description	    : fills hostname and exename with information for 
//                    the i'th process and returns the process id of 
//                    that process
// Return type		: int 
// Argument         : int i
// Argument         : char **hostname
// Argument         : char **exename
int nt_ipvishm_proc_info(int i, char **hostname, char **exename)
{
	//DPRINTF(("nt_ipvishm_proc_info called for process %d.\n", i));
	// Check bounds
	if ((i < 0) || (i >= g_nNproc))
		return -1;

	// Check to see whether the information needs to be retrieved
	if (g_pProcTable[i].pid == 0)
	{
		if (g_bUseBNR)
		{
			char pszKey[100];
			char pszTemp[100];
			sprintf(pszKey, "ListenHost%d", i);
			BNR_Get(g_myBNRgroup, pszKey, g_pProcTable[i].host);
			sprintf(pszKey, "Executable%d", i);
			BNR_Get(g_myBNRgroup, pszKey, g_pProcTable[i].exename);
			sprintf(pszKey, "pid%d", i);
			BNR_Get(g_myBNRgroup, pszKey, pszTemp);
			g_pProcTable[i].pid = atoi(pszTemp);
		}
		else if (g_bUseDatabase)
		{
			char pszKey[100];
			char pszTemp[100];
			int length = NT_HOSTNAME_LEN;
			sprintf(pszKey, "ListenHost%d", i);
			g_Database.Get(pszKey, g_pProcTable[i].host, &length);
			length = NT_EXENAME_LEN;
			sprintf(pszKey, "Executable%d", i);
			g_Database.Get(pszKey, g_pProcTable[i].exename, &length);
			length = 100;
			sprintf(pszKey, "pid%d", i);
			g_Database.Get(pszKey, pszTemp, &length);
			g_pProcTable[i].pid = atoi(pszTemp);
			//length = sizeof(int);
			//g_Database.Get(pszKey, &g_pProcTable[i].pid, &length);
		}
		else
			GetProcessInfo(i);
	}

	// Return a pointer to the information in the proc table
	// I assume the buffers will not be modified only read
	*hostname = g_pProcTable[i].host;
	*exename = g_pProcTable[i].exename;

	return g_pProcTable[i].pid;
}

// Function name	: nt_error
// Description	    : Prints an error message and exits
// Return type		: void 
// Argument         : char *string
// Argument         : int value
void nt_error(char *string, int value)
{
	char host[100] = "";
	DWORD len = 100;
	GetComputerName(host, &len);
	printf("Error %d, process %d, host %s:\n   %s\n", value, g_nIproc, host, string);fflush(stdout);
    
	// Signal the threads to stop and close their socket connections
	DPRINTF(("process %d: nt_error signalling CommunicationThread to exit.\n", g_nIproc);fflush(stdout));
	g_nCommPortCommand = NT_COMM_CMD_EXIT;
	SetEvent(g_hCommPortEvent);

	// Close all the communication sockets
	if (g_pProcTable != NULL)
	{
		for (int i=0; i<g_nNproc; i++)
		{
			if (g_pProcTable[i].sock_event != NULL)
			{
				NT_Tcp_closesocket(g_pProcTable[i].sock, g_pProcTable[i].sock_event);
				g_pProcTable[i].sock = INVALID_SOCKET;
				g_pProcTable[i].sock_event = NULL;
			}
		}
	}

	if (g_bUseBNR)
		BNR_Finalize();

	WSACleanup();
	ExitProcess(value);
}

void PrintWinSockError(int error)
{
	HLOCAL str;
	int num_bytes;
	num_bytes = FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_ALLOCATE_BUFFER,
		0,
		error,
		MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
		(LPTSTR) &str,
		0,0);
	if (strlen((const char*)str))
	    printf("%s", str);
	else
	    printf("\n");
	LocalFree(str);
}

// Function name	: nt_error_socket
// Description	    : Prints an error message and exits
// Return type		: void 
// Argument         : char *string
// Argument         : int value
void nt_error_socket(char *string, int value)
{
	printf("Error %d, process %d:\n   %s\n   ", value, g_nIproc, string);
	PrintWinSockError(value);
	fflush(stdout);
    
	// Signal the threads to stop and close their socket connections
	DPRINTF(("process %d: nt_error signalling CommunicationThread to exit.\n", g_nIproc);fflush(stdout));
	g_nCommPortCommand = NT_COMM_CMD_EXIT;
	SetEvent(g_hCommPortEvent);

	// Close all the communication sockets
	if (g_pProcTable != NULL)
	{
		for (int i=0; i<g_nNproc; i++)
		{
			if (g_pProcTable[i].sock_event != NULL)
			{
				NT_Tcp_closesocket(g_pProcTable[i].sock, g_pProcTable[i].sock_event);
				g_pProcTable[i].sock = INVALID_SOCKET;
				g_pProcTable[i].sock_event = NULL;
			}
		}
	}

	if (g_bUseBNR)
		BNR_Finalize();

	WSACleanup();
	ExitProcess(value);
}

// Function name	: NT_PIbsend
// Description	    : Sends the buffer to process to, establishing a connection if necessary
// Return type		: int 
// Argument         : int type
// Argument         : void *buffer
// Argument         : int length
// Argument         : int to
// Argument         : int datatype
int NT_PIbsend(int type, void *buffer, int length, int to, int datatype)
{
	DPRINTF(("NT_PIbsend called: %d to %d, tag: %d, length: %d\n", g_nIproc, to, type, length));

	// Handle the special case of sending to oneself
	if (to == g_nIproc)
	{
		MessageQueue::MsgQueueElement *pElement;
		void *pBuf = g_MsgQueue.GetBufferToFill(type, length, g_nIproc, &pElement);
		if (pBuf == NULL)
			nt_error("NT_PIbsend: MessageQueue.GetBuffer failed.", 1);
		memcpy(pBuf, buffer, length);
		if (!g_MsgQueue.SetElementEvent(pElement))
			nt_error("NT_PIbsend: MessageQueue.SetElementEvent failed", 1);
		return 0;
	}

	// Check bounds
	if (to < 0 || to >= g_nNproc)
		MakeErrMsg(1, "Send out of range: %d is not between 0 and %d", to, g_nNproc);

	if (g_pProcTable[to].shm)
	{
		NT_ShmSend(type, buffer, length, to);
	}
	else
	{
		if (g_pProcTable[to].via)
		{
			NT_ViSend(type, buffer, length, to);
		}
		else
		{
			if (g_pProcTable[to].sock == INVALID_SOCKET)
			{
				DPRINTF(("making a connection to %d\n", to));
				if (!ConnectTo(to))
					MakeErrMsg(1, "NT_PIbsend: Unable to connect to process %d", to);
			}

			/*
			if (SendBlocking(g_pProcTable[to].sock, (char*)&type, sizeof(int), 0) == SOCKET_ERROR)
				nt_error_socket("NT_PIbsend: send type failed.", WSAGetLastError());
			if (SendBlocking(g_pProcTable[to].sock, (char*)&length, sizeof(int), 0) == SOCKET_ERROR)
				nt_error_socket("NT_PIbsend: send length failed", WSAGetLastError());
			if (SendBlocking(g_pProcTable[to].sock, (char*)buffer, length, 0) == SOCKET_ERROR)
				nt_error_socket("NT_PIbsend: send buffer failed", WSAGetLastError());
			*/
			if (SendStreamBlocking(g_pProcTable[to].sock, (char*)buffer, length, type) == SOCKET_ERROR)
				nt_error_socket("NT_PIbsend: send msg failed.", WSAGetLastError());
		}
	}
	DPRINTF(("type: %d, length: %d sent to %d\n", type, length, to));
	return 0;
}

// Function name	: NT_PInsend
// Description	    : 
// Return type		: int 
// Argument         : int type
// Argument         : void *buffer
// Argument         : int length
// Argument         : int to
// Argument         : int datatype
// Argument         : int *pId
int NT_PInsend(int type, void *buffer, int length, int to, int datatype, int *pId)
{
	// Do a blocking send
	NT_PIbsend(type, buffer, length, to, datatype);
	// Set the handle to be finished
	pId[0] = 0;
	return 0;
}

// Function name	: NT_PIbrecv
// Description	    : 
// Return type		: int 
// Argument         : int type
// Argument         : void *buffer
// Argument         : int length
// Argument         : int datatype
int NT_PIbrecv(int type, void *buffer, int length, int datatype)
{
	/*
	DPRINTF(("NT_PIbrecv called: %d type: %d, length: %d\n", g_nIproc, type, length));
	
	if (!g_MsgQueue.FillThisBuffer(type, buffer, &length, &g_nLastRecvFrom))
	{
		if (length == -1)
			return MPI_ERR_COUNT;
		else
			nt_error("Recv:FillBuffer failed.\n", 1);
	}
	DPRINTF(("type: %d len: %d received from %d\n", type, length, g_nLastRecvFrom));
	return 0;
	/*/
	int pId[10];
	g_MsgQueue.PostBufferForFilling(type, buffer, length, pId);
	g_MsgQueue.Wait(pId);
	g_nLastRecvFrom = pId[3];
	return 0;
	//*/
}

// Function name	: NT_PInrecv
// Description	    : 
// Return type		: int 
// Argument         : int type
// Argument         : void *buffer
// Argument         : int length
// Argument         : int datatype
// Argument         : int *pId
int NT_PInrecv(int type, void *buffer, int length, int datatype, int *pId)
{
	DPRINTF(("NT_PInrecv called: %d type: %d, length: %d\n", g_nIproc, type, length));
	return (g_MsgQueue.PostBufferForFilling(type, buffer, length, pId)) ? 0 : 1;
}

// Function name	: NT_PIwait
// Description	    : 
// Return type		: int 
// Argument         : int *pId
int NT_PIwait(int *pId)
{
	if (pId == NULL)
		nt_error("wait called on invalid object", 1);
	if (pId[0] == 0)
		return 1;
	return (g_MsgQueue.Wait(pId)) ? 1 : 0;
}

// Function name	: NT_PInstatus
// Description	    : 
// Return type		: int 
// Argument         : int *pId
int NT_PInstatus(int *pId)
{
	if (pId[0] == 0)
		return 1;
	//return (g_MsgQueue.Test(pId)) ? 1 : 0;
	if (g_MsgQueue.Test(pId))
		return 1;
	Sleep(0);
	return 0;
}

// Function name	: NT_PInprobe
// Description	    : Returns true if a message is available with the tag 'type'
// Return type		: int 
// Argument         : int type
int NT_PInprobe(int type)
{
	//DPRINTF(("NT_PInprobe called.\n"));
	if (g_MsgQueue.Available(type, g_nLastRecvFrom))
	{
		return 1;
	}
	Sleep(0);
	return 0;
}

// Function name	: MPID_Wtime
// Description	    : 
// Return type		: void 
// Argument         : double *t
void MPID_Wtime(double *t)
{
	LARGE_INTEGER nLargeInt;

	QueryPerformanceCounter(&nLargeInt);
	*t = double(nLargeInt.QuadPart) / (double)g_nPerfFrequency.QuadPart;
}

// Function name	: MPID_Wtick
// Description	    : 
// Return type		: void 
// Argument         : double *t
void MPID_Wtick(double *t)
{
	*t = 1.0 / (double)g_nPerfFrequency.QuadPart;
}

// Function name	: NT_PIgimax
// Description	    : Does a global max operation. Used for setting up 
//                    heterogeneous environments.
//                    Needed when MPID_HAS_HETERO is defined.
//                    What it is supposed to do, I have no idea.
// Return type		: int 
// Argument         : void *val
// Argument         : int n
// Argument         : int work
// Argument         : int procset
#ifdef MPID_HAS_HETERO
int NT_PIgimax(void *val, int n, int work, int procset)
{
	DPRINTF(("NT_PIgimax called.\n"));
    return -1;
}
#endif
