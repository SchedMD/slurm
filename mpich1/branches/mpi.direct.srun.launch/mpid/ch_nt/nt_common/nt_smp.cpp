#include "nt_global_cpp.h"
#include "ShmemLockedQueue.h"
#include "parsecliques.h"
#include <stdlib.h>

// Shared memory stuff
int g_ShmemQSize = 1024*1024;
//ShmemQueue **g_pShmemQueue = NULL;
ShmemLockedQueue **g_pShmemQueue = NULL;
int g_nMaxShmSendSize = 1024*15;
HANDLE g_hShmRecvThread = NULL;
int g_nNumShemQueues = 0;

// Shared process stuff
HANDLE *g_hShpMutex = NULL, *g_hShpSendCompleteEvent = NULL;
HANDLE *g_hProcesses = NULL;

// Function name	: ShmRecvThread
// Description	    : 
// Return type		: void 
// Argument         : ShmemQueue *pShmemQueue
void ShmRecvThread(ShmemLockedQueue *pShmemLockedQueue)
{
	while (true)
	{
		if (!pShmemLockedQueue->RemoveNextInsert(&g_MsgQueue))
			ExitThread(0);
	}	
}

// Function name	: PollShmemQueue
// Description	    : 
// Return type		: void 
void PollShmemQueue()
{
	//*
	if (!g_pShmemQueue[g_nIproc]->RemoveNextInsert(&g_MsgQueue, false))
		Sleep(0);
	/*/
	for (int i=0; i<10; i++)
	{
		if (g_pShmemQueue[g_nIproc]->RemoveNextInsert(&g_MsgQueue, false))
			return;
	}
	Sleep(0);
	//*/
}

// Function name	: GetShmemClique
// Description	    : Determine which processes this process can reach through shared memory
// Return type		: void 
int GetShmemClique()
{
	int nSMPLow, nSMPHigh;
	int nCount = 0;
	int i;
	char pszTemp[100];

	for (i=0; i<g_nNproc; i++)
	    g_pProcTable[i].shm = 0;

	try{
	if (GetEnvironmentVariable("MPICH_SHM_CLICKS", pszTemp, 100) || GetEnvironmentVariable("MPICH_SHM_CLIQUES", pszTemp, 100))
	{
		int *pMembers = NULL;
		if (ParseCliques(pszTemp, g_nIproc, g_nNproc, &nCount, &pMembers))
		{
			nt_error("Unable to parse the SHM cliques", 1);
			return 0;
		}

		for (i=0; i<nCount; i++)
		{
			if ( (pMembers[i] >= 0) && (pMembers[i] < g_nNproc) )
			{
				//printf("rank %d reachable by shared memory\n", pMembers[i]);fflush(stdout);
				g_pProcTable[pMembers[i]].shm = 1;
			}
		}
		if (pMembers != NULL)
			delete pMembers;
	}
	else
	{
		char pszSMPLow[10]="", pszSMPHigh[10]="";

		if (GetEnvironmentVariable("MPICH_SHM_LOW", pszSMPLow, 10))
			nSMPLow = atoi(pszSMPLow);
		else
			nSMPLow = g_nIproc;
		if (GetEnvironmentVariable("MPICH_SHM_HIGH", pszSMPHigh, 10))
			nSMPHigh = atoi(pszSMPHigh);
		else
			nSMPHigh = g_nIproc;
		for (i=nSMPLow; i<=nSMPHigh; i++)
		{
			if ( i >= 0 && i < g_nNproc )
				g_pProcTable[i].shm = 1;
		}
		nCount = (nSMPHigh - nSMPLow) + 1;
	}
	}catch(...)
	{
	    nt_error("exception caught in GetShmemClique\n", 1);
	    return 0;
	}
	return nCount;
}

// Function name	: InitSMP
// Description	    : 
// Return type		: void 
void InitSMP()
{
	int i;
	char nameBuffer[256], pszTemp[100], pszSMPLow[10]="", pszSMPHigh[10]="";
#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
	PSECURITY_DESCRIPTOR pSD;
	SECURITY_ATTRIBUTES sattr;
#endif

	g_nNumShemQueues = GetShmemClique();

	if (g_nNumShemQueues < 2)
		return;

#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
	// Initialize a security descriptor.
	pSD = (PSECURITY_DESCRIPTOR) LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	if (pSD == NULL)
	{
	    nt_error("InitSMP: LocalAlloc failed", GetLastError());
	    return;
	}
	
	if (!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION))
	{
	    nt_error("InitSMP: InitializeSecurityDescriptor failed", GetLastError());
	    LocalFree((HLOCAL) pSD);
	    return;
	}
	
	// Add a NULL descriptor ACL to the security descriptor.
	if (!SetSecurityDescriptorDacl(
	    pSD, 
	    TRUE,     // specifying a descriptor ACL
	    (PACL) NULL,
	    FALSE))
	{
	    nt_error("InitSMP: SetSecurityDescriptorDacl failed", GetLastError());
	    LocalFree((HLOCAL) pSD);
	    return;
	}

	sattr.bInheritHandle = TRUE;
	sattr.lpSecurityDescriptor = pSD;
	sattr.nLength = sizeof(SECURITY_ATTRIBUTES);

	// make my process accessable to other processes
	// This doesn't work
	//SetKernelObjectSecurity(GetCurrentProcess(), DACL_SECURITY_INFORMATION, &sattr);
#endif

	// Initialize shared memory stuff
	if (GetEnvironmentVariable("MPICH_MAXSHMMSG", pszTemp, 100))
	{
		g_nMaxShmSendSize = atoi(pszTemp);
		if (g_nMaxShmSendSize < 0)
			g_nMaxShmSendSize = 0;
	}
	if (GetEnvironmentVariable("MPICH_SHMQSIZE", pszTemp, 100))
	{
		g_ShmemQSize = atoi(pszTemp);
		if (g_ShmemQSize < g_nMaxShmSendSize)
			g_ShmemQSize = g_nMaxShmSendSize;
	}

	g_pShmemQueue = new ShmemLockedQueue*[g_nNproc];
	for (i=0; i<g_nNproc; i++)
		g_pShmemQueue[i] = NULL;
	for (i=0; i<g_nNproc; i++)
	{
		if (g_pProcTable[i].shm == 1)
		{
		    /*printf("initializing shmem queue %d\n", i);fflush(stdout);*/
		    g_pShmemQueue[i] = new ShmemLockedQueue;
		    sprintf(nameBuffer, "%s.shm%d", g_pszJobID, i);
		    if (!g_pShmemQueue[i]->Init(nameBuffer, g_ShmemQSize))
			nt_error("unable to initialize ShmemQueue", i);
		}
	}

	// Initialize shared process stuff
	g_hShpMutex = new HANDLE[g_nNproc];
	g_hShpSendCompleteEvent = new HANDLE[g_nNproc];
	g_hProcesses = new HANDLE[g_nNproc];

	// Create all the named events and mutexes
	for (i=0; i<g_nNproc; i++)
	{
		if (g_pProcTable[i].shm == 1)
		{
		    char pBuffer[100];
		    sprintf(pBuffer, "%s.shp%dMutex", g_pszJobID, i);
		    g_hShpMutex[i] = CreateMutex(
#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
			&sattr,
#else
			NULL,
#endif
			FALSE, pBuffer);
		    if (g_hShpMutex[i] == NULL)
			MakeErrMsg(GetLastError(), "InitSMP: CreateMutex failed for g_hShmMutex[%d]", i);
		    sprintf(pBuffer, "%s.shp%dSendComplete", g_pszJobID, i);
		    g_hShpSendCompleteEvent[i] = CreateEvent(
#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
			&sattr,
#else
			NULL,
#endif
			TRUE, FALSE, pBuffer);
		    if (g_hShpSendCompleteEvent[i] == NULL)
			MakeErrMsg(GetLastError(), "InitSMP: CreateEvent failed for g_hShpSendCompleteEvent[%d]", i);
		}
	}

	unsigned long pid = GetCurrentProcessId();
	// Send my information to the other processes
	for (i=0; i<g_nNproc; i++)
	{
		if (i != g_nIproc && g_pProcTable[i].shm == 1)
		{
			if (!g_pShmemQueue[i]->Insert((unsigned char *)&pid, sizeof(unsigned long), 0, g_nIproc))
				nt_error("InitSMP: Unable to send pid info to remote process", i);
		}
	}
	// Get the information about the other processes
	for (i=0; i<g_nNproc; i++)
	{
	    if (i != g_nIproc && g_pProcTable[i].shm == 1)
	    {
		int tag, from;
		unsigned int length = sizeof(unsigned long);
		if (!g_pShmemQueue[g_nIproc]->RemoveNext((unsigned char *)&pid, &length, &tag, &from))
			nt_error("InitSMP: Unable to receive pid information from remote processes", 0);
		/*printf("received pid %d from shmem queue %d\n", pid, from);fflush(stdout);*/
		g_hProcesses[from] = OpenProcess(STANDARD_RIGHTS_REQUIRED | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
	    }
	}

	pszTemp[0] = '\0';
	GetEnvironmentVariable("MPICH_SHM_SINGLETHREAD", pszTemp, 100);

	if (pszTemp[0] == '1')
	{
		// Set the poll function so the shmem device will run single threaded.
		g_MsgQueue.SetProgressFunction(PollShmemQueue);
		//g_pShmemQueue[g_nIproc]->SetProgressFunction(PollShmemQueue);
	}
	else
	{
		// Start the shared memory receive thread
		DWORD dwThreadID;
		for (i=0; i<NT_CREATE_THREAD_RETRIES; i++)
		{
		    g_hShmRecvThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ShmRecvThread, g_pShmemQueue[g_nIproc], NT_THREAD_STACK_SIZE, &dwThreadID);
		    if (g_hShmRecvThread != NULL)
			break;
		    Sleep(NT_CREATE_THREAD_SLEEP_TIME);
		}
		if (g_hShmRecvThread == NULL)
			nt_error("InitSMP: Unable to create ShmRecvThread", 0);
	}
#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
	if(pSD != NULL)
		LocalFree((HLOCAL) pSD);
#endif
}

// Function name	: EndSMP
// Description	    : 
// Return type		: void 
void EndSMP()
{
	int i;

	if (g_nNumShemQueues < 2)
		return;

	if (g_hShmRecvThread != NULL)
	{
		// Signal the shm thread to exit
		g_pShmemQueue[g_nIproc]->Insert(NULL, 0, 0, -1);
		WaitForSingleObject(g_hShmRecvThread, 5000);
		CloseHandle(g_hShmRecvThread);
		g_hShmRecvThread = NULL;
	}

	// Delete the shared memory queues
	for (i=0; i<g_nNproc; i++)
	{
	    if (g_pProcTable[i].shm == 1)
		delete g_pShmemQueue[i];
	}
	delete g_pShmemQueue;

	// Delete all the shared process stuff

	// Delete all the named events and mutexes
	for (i=0; i<g_nNproc; i++)
	{
	    if (g_pProcTable[i].shm == 1)
	    {
		CloseHandle(g_hShpMutex[i]);
		CloseHandle(g_hShpSendCompleteEvent[i]);
		CloseHandle(g_hProcesses[i]);
	    }
	}
	delete g_hShpMutex;
	delete g_hShpSendCompleteEvent;
	delete g_hProcesses;
	g_hShpMutex = NULL;
	g_hShpSendCompleteEvent = NULL;
	g_hProcesses = NULL;
}

// Function name	: NT_ShmSend
// Description	    : 
// Return type		: void 
// Argument         : int type
// Argument         : void *buffer
// Argument         : int length
// Argument         : int to
void NT_ShmSend(int type, void *buffer, int length, int to)
{
	// do a short send
	if (length < g_nMaxShmSendSize)
	{
		// Shared memory send
		if (!g_pShmemQueue[to]->Insert((unsigned char *)buffer, length, type, g_nIproc))
		{
			nt_error("shared memory send failed", to);
		}
		return;
	}

	// do a shared process send
	if (g_hProcesses[to] != NULL)
	{
		// Shared process send
		if (!g_pShmemQueue[to]->InsertSHP((unsigned char *)buffer, length, type, g_nIproc, g_hShpMutex[to], g_hShpSendCompleteEvent[to], g_pShmemQueue[g_nIproc]))
		{
			nt_error("shared process send failed", to);
		}
		return;
	}

	// Shared memory send
	if (!g_pShmemQueue[to]->Insert((unsigned char *)buffer, length, type, g_nIproc))
	{
		nt_error("shared memory send failed", to);
	}
}
/*
void NT_ShmSend(int type, void *buffer, int length, int to)
{
	if (length > g_nMaxShmSendSize && g_hProcesses[to] != NULL)
	{
		// Shared process send
		if (!g_pShmemQueue[to]->InsertSHP((unsigned char *)buffer, length, type, g_nIproc, g_hShpMutex[to], g_hShpSendCompleteEvent[to], g_pShmemQueue[g_nIproc]))
		{
			nt_error("shared process send failed", to);
		}
	}
	else
	{
		// Shared memory send
		if (!g_pShmemQueue[to]->Insert((unsigned char *)buffer, length, type, g_nIproc))
		{
			nt_error("shared memory send failed", to);
		}
	}
}
*/

/*
void NT_ShmSend(int type, void *buffer, int length, int to)
{
	int len;
	// do a short send
	if (length < g_nMaxShmSendSize)
	{
		// Shared memory send
		if (!g_pShmemQueue[to]->Insert((unsigned char *)buffer, length, type, g_nIproc))
		{
			nt_error("shared memory send failed", to);
		}
		return;
	}

	// do a shared process send
	if (g_hProcesses[to] != NULL)
	{
		// Shared process send
		if (!g_pShmemQueue[to]->InsertSHP((unsigned char *)buffer, length, type, g_nIproc, g_hShpMutex[to], g_hShpSendCompleteEvent[to], g_pShmemQueue[g_nIproc]))
		{
			nt_error("shared process send failed", to);
		}
		return;
	}

	// stream the send through shared memory
	do
	{
		len = min(length, g_nMaxShmSendSize);
		if (!g_pShmemQueue[to]->Insert((unsigned char *)buffer, len, type, g_nIproc))
		{
			nt_error("shared memory send failed", to);
		}
		length -= len;
		buffer = (char *)buffer + len;
	} while (length);
}
*/
