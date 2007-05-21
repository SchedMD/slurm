#include "ShmemLockedQueue.h"
#include <stdio.h>
#include "lock.h"

#define SHM_Q_INITIALIZED 0x12345678

// Function name	: ShmemLockedQueue::ShmemLockedQueue
// Description	    : 
// Return type		: 
ShmemLockedQueue::ShmemLockedQueue()
{
	m_dwMaxMsgSize = 0;
	m_dwSize = 0;
	m_hMapping = NULL;
	m_hMsgAvailableEvent = NULL;
	m_pBase = NULL;
	m_pBottom = NULL;
	m_pEnd = NULL;
	m_plMsgAvailableTrigger = NULL;
	m_plQEmptyTrigger = NULL;
	m_plQMutex = NULL;
	m_pProgressPollFunction = NULL;

	char pszTemp[100];
	if (GetEnvironmentVariable("MPICH_USE_POLLING", pszTemp, 100))
		m_bUseEvent = false;
	else
		m_bUseEvent = true;
}

// Function name	: ShmemLockedQueue::~ShmemLockedQueue
// Description	    : 
// Return type		: 
ShmemLockedQueue::~ShmemLockedQueue()
{
	if (m_hMapping != NULL)
	{
		if (m_pBottom != NULL)
			//UnmapViewOfFile(m_pBottom);
			UnmapViewOfFile((void*)(((LONG*)m_pBottom) - 1)); // back up over the initialized field to the true beginning
		CloseHandle(m_hMapping);
	}
	if (m_hMsgAvailableEvent != NULL)
		CloseHandle(m_hMsgAvailableEvent);
	m_hMsgAvailableEvent = NULL;
}

// Function name	: ShmemLockedQueue::Init
// Description	    : 
// Return type		: bool 
// Argument         : char *name
// Argument         : unsigned long size
bool ShmemLockedQueue::Init(char *name, unsigned long size)
{
	bool bFirst = true;
	HANDLE hInitEvent = NULL;
	LONG *pInitialized;
#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
	PSECURITY_DESCRIPTOR pSD;
	SECURITY_ATTRIBUTES sattr;
#endif

	m_dwMaxMsgSize = size;

	size = size + sizeof(ShmemLockedQueueHeader) + 6*sizeof(LONG);

#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
	// Initialize a security descriptor.
	pSD = (PSECURITY_DESCRIPTOR) LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	if (pSD == NULL)
	{
	    printf("ShmemLockedQueue::Init: LocalAlloc failed, error %d\n", GetLastError());
	    return false;
	}
	
	if (!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION))
	{
	    printf("ShmemeLockedQueue::Init: InitializeSecurityDescriptor failed, error %d\n", GetLastError());
	    LocalFree((HLOCAL) pSD);
	    return false;
	}
	
	// Add a NULL descriptor ACL to the security descriptor.
	if (!SetSecurityDescriptorDacl(
	    pSD, 
	    TRUE,     // specifying a descriptor ACL
	    (PACL) NULL,
	    FALSE))
	{
	    printf("ShmemeLockeQueue::Init: SetSecurityDescriptorDacl failed, error %d\n", GetLastError());
	    LocalFree((HLOCAL) pSD);
	    return false;
	}

	sattr.bInheritHandle = TRUE;
	sattr.lpSecurityDescriptor = pSD;
	sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
#endif

	// Create a mapping from the page file
	m_hMapping = CreateFileMapping(
		INVALID_HANDLE_VALUE,
#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
		&sattr,
#else
		NULL,
#endif
		PAGE_READWRITE,
		0, size,
		name);

	// If the mapping already exists then we are attaching to it, 
	// not creating it
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		bFirst = false;

	if (m_hMapping == NULL)
	{
		printf("CreateFileMapping(%s) failed, error %d\n", name, GetLastError());fflush(stdout);
#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
		if(pSD != NULL)
			LocalFree((HLOCAL) pSD);
#endif
		return false;
	}

	// Map the file and save the pointer to the base of the mapped file
	m_pBottom = MapViewOfFile(
		m_hMapping,
		FILE_MAP_WRITE,
		0,0,
		size);

	if (m_pBottom == NULL)
	{
		printf("MapViewOfFile failed, error %d\n", GetLastError());fflush(stdout);
		CloseHandle(m_hMapping);
		m_hMapping = NULL;
#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
		if(pSD != NULL)
			LocalFree((HLOCAL) pSD);
#endif
		return false;
	}

	/* NEW Jan 9, 2003
	 Added an initialized field before the m_pBottom pointer
	 This means that m_pBottom must be moved back in the finalize function before it can be released
	 */
	pInitialized = (LONG*)m_pBottom;
	m_pBottom = (void*)(((LONG*)m_pBottom) + 1);

	m_plQMutex = (LONG*)m_pBottom;
	m_plQEmptyTrigger = &((LONG*)m_pBottom)[1];
	m_plMsgAvailableTrigger = &((LONG*)m_pBottom)[2];

	m_pEnd = (LPBYTE)m_pBottom + size - sizeof(LONG);
	m_pBase = (LPBYTE)m_pBottom + 3*sizeof(LONG);
	m_dwSize = size - sizeof(LONG);

	// If this process is creating the mapping, 
	// then set up the head and tail pointers
	if (bFirst)
	{
		((unsigned long*)m_pBase)[0] = 0;
		((unsigned long*)m_pBase)[1] = 2*sizeof(unsigned long);
		*m_plQMutex = 0;
		*m_plQEmptyTrigger = 0;
		*m_plMsgAvailableTrigger = 0;
	}

	// Create or Attach to the synchronization handles for this queue
	char pszEventName[200];

	sprintf(pszEventName, "%s.event", name);

	m_hMsgAvailableEvent = CreateEvent(
#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
		&sattr,
#else
		NULL,
#endif
		TRUE, FALSE, pszEventName);
	if (m_hMsgAvailableEvent == NULL)
	{
		printf("CreateEvent(%s) failed, error %d\n", pszEventName, GetLastError());fflush(stdout);
		CloseHandle(m_hMapping);
		CloseHandle(m_plQMutex);
		m_hMapping = NULL;
		m_plQMutex = NULL;
		return false;
	}

#ifdef SHARE_MEMORY_ACCROSS_WINWORKSTATIONS
	if(pSD != NULL)
		LocalFree((HLOCAL) pSD);
#endif

	if (bFirst)
	{
	    // mark the queue as initialized
	    *pInitialized = SHM_Q_INITIALIZED;
	}
	else
	{
	    // wait until the queue is initialized
	    int retry = 100;
	    while (*pInitialized != SHM_Q_INITIALIZED && retry)
	    {
		Sleep(200);
		retry--;
	    }
	    if (*pInitialized != SHM_Q_INITIALIZED)
	    {
		printf("timed out waiting for the shmem queue to be initialized\n");fflush(stdout);
		return false;
	    }
	}

	return true;
}

// Function name	: ShmemLockedQueue::Insert
// Description	    : 
// Return type		: bool 
// Argument         : unsigned char *buffer
// Argument         : unsigned int length
// Argument         : int tag
// Argument         : int from
bool ShmemLockedQueue::Insert(
	unsigned char *buffer, unsigned int length, int tag, int from)
{
	ShmemLockedQueueHeader *pMessage;

	lock(m_plQMutex);

	if (length > m_dwMaxMsgSize)
	{
		unlock(m_plQMutex);
		return false;
	}

	// Wait for a contiguous block large enough to hold the data 
	while ( ( SHMEM_Q_TAIL_PTR >= m_pEnd ) || 
			( (unsigned long)m_pEnd - 
			  (unsigned long)SHMEM_Q_TAIL_PTR - 
			   sizeof(ShmemLockedQueueHeader) < length) )
	{
		unlock(m_plQMutex);
		if (m_pProgressPollFunction)
		{
			while (!test(m_plQEmptyTrigger))
				m_pProgressPollFunction();
		}
		else
			wait(m_plQEmptyTrigger);
		lock(m_plQMutex);
	}

	// Read the tail pointer
	pMessage = SHMEM_Q_TAIL_PTR;
	// If the head offset is 0, set it to the tail
	if ( SHMEM_Q_HEAD_OFFSET == 0 )
		SHMEM_Q_HEAD_OFFSET = SHMEM_Q_TAIL_OFFSET;

	// Set the state and advance the tail offset
	pMessage->state = SHMEM_Q_BEING_WRITTEN;
	// Advance the tail offset
	SHMEM_Q_TAIL_OFFSET = 
		(unsigned long)(
			(unsigned long)pMessage + 
			sizeof(ShmemLockedQueueHeader) + 
			length - (unsigned long)m_pBase);
	unlock(m_plQMutex);

	// Write the header
	pMessage->tag = tag;
	pMessage->from = from;
	pMessage->length = length;
	pMessage->next_offset = (sizeof(ShmemLockedQueueHeader) + length);
	
	// Copy the data
	memcpy((LPBYTE)pMessage + sizeof(ShmemLockedQueueHeader), buffer, length);

	lock(m_plQMutex);
	// Signal data has arrived and release the mutex
	pMessage->state = SHMEM_Q_AVAIL_FOR_READ;
	if (m_bUseEvent)
		SetEvent(m_hMsgAvailableEvent);
	else
		setevent(m_plMsgAvailableTrigger);
	resetevent(m_plQEmptyTrigger);
	unlock(m_plQMutex);

	return true;
}

// NOTE: the SHP functions only work if this class is compiled with the 
// functions from nt_smp.cpp. Maybe in the future I will weave the SHP stuff
// into this class.

// Function name	: ShmemLockedQueue::InsertSHP
// Description	    : This function acquires the mutex of the remote queue, 
//                    inserts the local buffer and length into the remote shmem
//                    queue, and waits for the remote event to be signalled.  
//                    The remote process removes the message from its shmem 
//                    queue, uses the information to read the buffer from this
//                    process, and then signals the event meaning that the 
//                    buffer has been read and the user is free to touch the 
//                    buffer again.
// Return type		: bool 
// Argument         : unsigned char *buffer
// Argument         : unsigned int length
// Argument         : int tag
// Argument         : int from
#include "nt_global_cpp.h"
struct shpData
{
	void *address;
	int length;
};
bool ShmemLockedQueue::InsertSHP(
	unsigned char *buffer, unsigned int length, int tag, int from, 
	HANDLE hRemoteMutex, HANDLE hRemoteEvent, ShmemLockedQueue *pOtherQueue)
{
	ShmemLockedQueueHeader *pMessage;
	shpData data;
	data.address = buffer;
	data.length = length;
	
	WaitForSingleObject(hRemoteMutex, INFINITE);

	lock(m_plQMutex);

	// Wait for a contiguous block large enough to hold the data 
	while ( ( SHMEM_Q_TAIL_PTR >= m_pEnd ) || 
			( (unsigned long)m_pEnd - 
			  (unsigned long)SHMEM_Q_TAIL_PTR - 
			   sizeof(ShmemLockedQueueHeader) < sizeof(shpData) ) )
	{
		unlock(m_plQMutex);
		if (m_pProgressPollFunction)
		{
			while (!test(m_plQEmptyTrigger))
				m_pProgressPollFunction();
		}
		else
			wait(m_plQEmptyTrigger);
		lock(m_plQMutex);
	}

	// Read the tail pointer
	pMessage = SHMEM_Q_TAIL_PTR;
	// If the head offset is 0, set it to the tail
	if ( SHMEM_Q_HEAD_OFFSET == 0 )
		SHMEM_Q_HEAD_OFFSET = SHMEM_Q_TAIL_OFFSET;

	// Set the state and advance the tail offset
	pMessage->state = SHMEM_Q_BEING_WRITTEN;
	// Advance the tail offset
	SHMEM_Q_TAIL_OFFSET = (unsigned long)(
		(unsigned long)pMessage + 
		sizeof(ShmemLockedQueueHeader) + 
		sizeof(shpData) - (unsigned long)m_pBase);
	unlock(m_plQMutex);

	// Write the header
	pMessage->tag = tag;
	pMessage->from = from;
	pMessage->length = sizeof(shpData);
	pMessage->next_offset = (sizeof(ShmemLockedQueueHeader) + sizeof(shpData));
	
	// Copy the data
	memcpy((LPBYTE)pMessage + sizeof(ShmemLockedQueueHeader), 
		&data, sizeof(shpData));

	lock(m_plQMutex);
	// Signal data has arrived and release the mutex
	pMessage->state = SHMEM_Q_SHP_AVAIL_FOR_READ;
	if (m_bUseEvent)
		SetEvent(m_hMsgAvailableEvent);
	else
		setevent(m_plMsgAvailableTrigger);
	resetevent(m_plQEmptyTrigger);
	unlock(m_plQMutex);

	if (g_MsgQueue.m_pProgressPollFunction)
	{
		while (WaitForSingleObject(hRemoteEvent, 0) != WAIT_OBJECT_0)
			g_MsgQueue.m_pProgressPollFunction();
	}
	else if (pOtherQueue->m_pProgressPollFunction)
	{
		while (WaitForSingleObject(hRemoteEvent, 0) != WAIT_OBJECT_0)
			pOtherQueue->m_pProgressPollFunction();
			//pOtherQueue->RemoveNextInsert(&g_MsgQueue, false);
	}
	else
		WaitForSingleObject(hRemoteEvent, INFINITE);

	ResetEvent(hRemoteEvent);
	ReleaseMutex(hRemoteMutex);
	return true;
}

// Function name	: ShmemLockedQueue::RemoveNext
// Description	    : 
// Return type		: bool 
// Argument         : unsigned char *buffer
// Argument         : unsigned int *length
// Argument         : int *tag
// Argument         : int *from
bool ShmemLockedQueue::RemoveNext(
	unsigned char *buffer, unsigned int *length, int *tag, int *from)
{
	ShmemLockedQueueHeader *pTail, *pHead, *pMessage;

	// Get the next available entry in the queue
	while(true)
	{
		// Get the queue mutex
		lock(m_plQMutex);
		
		// Wait for the queue to not be empty
		while (SHMEM_Q_HEAD_OFFSET == 0)
		{
			unlock(m_plQMutex);
			if (m_bUseEvent)
			{
				if (WaitForSingleObject(m_hMsgAvailableEvent, INFINITE) 
						!= WAIT_OBJECT_0)
				{
					printf("ShmemLockedQueue:RemoveNext:Wait for MsgAvailableEvent on an empty queue failed, error %d\n", GetLastError());fflush(stdout);
					return false;
				}
			}
			else
				wait(m_plMsgAvailableTrigger);
			lock(m_plQMutex);
		}
		
		// Search the queue for the next entry not being read by another thread
		pMessage = SHMEM_Q_HEAD_PTR;
		pTail = SHMEM_Q_TAIL_PTR;
		
		while ((pMessage->state == SHMEM_Q_BEING_READ) && (pMessage < pTail))
			pMessage = (ShmemLockedQueueHeader*)
				((LPBYTE)pMessage + pMessage->next_offset);
		
		// If we haven't reached the tail and the element we are on is not 
		// currently being written, then successfully break out of this loop
		if ( (pMessage < pTail) && (pMessage->state != SHMEM_Q_BEING_WRITTEN) )
			break;

		// All messages are being read or the next message in order is 
		// not ready. I need to reset MsgAvailableEvent, wait for it to be 
		// signalled and then start over
		if (m_bUseEvent)
		{
			ResetEvent(m_hMsgAvailableEvent);
			unlock(m_plQMutex);
			if (WaitForSingleObject(m_hMsgAvailableEvent, INFINITE) 
					!= WAIT_OBJECT_0)
			{
				printf("ShmemLockedQueue:RemoveNext:Wait for MsgAvailableEvent failed, error %d\n", GetLastError());fflush(stdout);
				return false;
			}
		}
		else
		{
			resetevent(m_plMsgAvailableTrigger);
			unlock(m_plQMutex);
			wait(m_plMsgAvailableTrigger);
		}
	}

	// Check that the buffer provided is large enough to hold the data
	if (pMessage->length > *length)
	{
		printf("ShmemLockedQueue:RemoveNext:shmem message length %d > %d user buffer length\n", pMessage->length, *length);
		unlock(m_plQMutex);
		return false;
	}

	// Mark the message as being read
	pMessage->state = SHMEM_Q_BEING_READ;
	unlock(m_plQMutex);

	// Read the data from the message
	*tag = pMessage->tag;
	*from = pMessage->from;
	*length = pMessage->length;
	memcpy(buffer, 
		(LPBYTE)pMessage + sizeof(ShmemLockedQueueHeader), pMessage->length);

	lock(m_plQMutex);
	// Mark the message as having been read
	pMessage->state = SHMEM_Q_READ;

	// Update the head and tail pointers of the queue
	pHead = SHMEM_Q_HEAD_PTR;
	pTail = SHMEM_Q_TAIL_PTR;
		
	// Advance the head pointer over all the read messages
	while ( (pHead < pTail) && (pHead->state == SHMEM_Q_READ) )
		pHead = (ShmemLockedQueueHeader*)((LPBYTE)pHead + pHead->next_offset);

	if (pHead >= pTail)
	{
		// When the head catches up to the tail, 
		// the queue is empty so reset the pointers and signal the queue empty
		SHMEM_Q_HEAD_OFFSET = 0;
		SHMEM_Q_TAIL_OFFSET = 2*sizeof(unsigned long);
		if (m_bUseEvent)
			ResetEvent(m_hMsgAvailableEvent);
		else
			resetevent(m_plMsgAvailableTrigger);
		setevent(m_plQEmptyTrigger);
	}
	else
		SHMEM_Q_HEAD_OFFSET = (unsigned long)((LPBYTE)pHead - (LPBYTE)m_pBase);

	unlock(m_plQMutex);

	return true;
}

// Function name	: ShmemLockedQueue::RemoveNextInsert
// Description	    : 
// Return type		: bool 
// Argument         : MessageQueue *pMsgQueue
bool ShmemLockedQueue::RemoveNextInsert(MessageQueue *pMsgQueue, bool bBlocking)
{
	ShmemLockedQueueHeader *pTail, *pHead, *pMessage;

	if (bBlocking)
	{
		// Get the next available entry in the queue
		while(true)
		{
			// Get the queue mutex
			lock(m_plQMutex);
			
			// Wait for the queue to not be empty
			while (SHMEM_Q_HEAD_OFFSET == 0)
			{
				unlock(m_plQMutex);
				if (m_bUseEvent)
				{
					if (WaitForSingleObject(m_hMsgAvailableEvent, INFINITE) 
						!= WAIT_OBJECT_0)
					{
						printf("ShmemLockedQueue:RemoveNextInsert:Wait for MsgAvailableEvent on an empty queue failed, error %d\n", GetLastError());fflush(stdout);
						return false;
					}
				}
				else
					wait(m_plMsgAvailableTrigger);
				lock(m_plQMutex);
			}
			
			// Search the queue for the next available entry
			pMessage = SHMEM_Q_HEAD_PTR;
			pTail = SHMEM_Q_TAIL_PTR;
			
			while ((pMessage->state == SHMEM_Q_BEING_READ) && (pMessage < pTail))
				pMessage = (ShmemLockedQueueHeader*)
				((LPBYTE)pMessage + pMessage->next_offset);
			
			// If we haven't reached the tail and the element we are on is not 
			// currently being written, then successfully break out of this loop
			if ( (pMessage < pTail) && (pMessage->state != SHMEM_Q_BEING_WRITTEN) )
				break;
			
			// All messages are being read or the next message in order is not 
			// ready. I need to reset MsgAvailableEvent, wait for it to be 
			// signalled and then start over
			if (m_bUseEvent)
			{
				ResetEvent(m_hMsgAvailableEvent);
				unlock(m_plQMutex);
				if (WaitForSingleObject(m_hMsgAvailableEvent, INFINITE) 
					!= WAIT_OBJECT_0)
				{
					printf("ShmemLockedQueue:RemoveNextInsert:Wait for MsgAvailableEvent failed, error %d\n", GetLastError());fflush(stdout);
					return false;
				}
			}
			else
			{
				resetevent(m_plMsgAvailableTrigger);
				unlock(m_plQMutex);
				wait(m_plMsgAvailableTrigger);
			}
		}
	}
	else
	{
		// Try to get the next available entry in the queue

		// Get the queue mutex
		lock(m_plQMutex);
			
		// Wait for the queue to not be empty
		if (SHMEM_Q_HEAD_OFFSET == 0)
		{
			unlock(m_plQMutex);
			return false;
		}
			
		// Search the queue for the next available entry
		pMessage = SHMEM_Q_HEAD_PTR;
		pTail = SHMEM_Q_TAIL_PTR;
			
		while ((pMessage->state == SHMEM_Q_BEING_READ) && (pMessage < pTail))
			pMessage = (ShmemLockedQueueHeader*)
						((LPBYTE)pMessage + pMessage->next_offset);
			
		// If we haven't reached the tail and the element we are on is not 
		// currently being written, then successfully break out of this loop
		if ( (pMessage >= pTail) || (pMessage->state == SHMEM_Q_BEING_WRITTEN) )
		{
			// All messages are being read or the next message in order is not 
			// ready. I need to reset MsgAvailableEvent, wait for it to be 
			// signalled and then start over
			if (m_bUseEvent)
			{
				ResetEvent(m_hMsgAvailableEvent);
				unlock(m_plQMutex);
				return false;
			}
			else
			{
				resetevent(m_plMsgAvailableTrigger);
				unlock(m_plQMutex);
				return false;
			}
		}
	}

	MessageQueue::MsgQueueElement *pElement;
	if (pMessage->from == -1)
	{
		unlock(m_plQMutex);
		return false;
	}
	if (pMessage->state == SHMEM_Q_SHP_AVAIL_FOR_READ)
	{
		shpData data;

		memcpy(&data, (LPBYTE)pMessage + sizeof(ShmemLockedQueueHeader), 
			sizeof(shpData));

		void *pLocal = 
			g_MsgQueue.GetBufferToFill(pMessage->tag, data.length, 
										pMessage->from, &pElement);
		if (!ReadProcessMemory(	
				g_hProcesses[pMessage->from], 
				data.address, pLocal, data.length , NULL))
			//nt_error("Unable to read remote memory", pMessage->from);
			MakeErrMsg(GetLastError(), "Unable to read remote memory in process %d", pMessage->from);
		SetEvent(g_hShpSendCompleteEvent[g_nIproc]);
		g_MsgQueue.SetElementEvent(pElement);
	}
	else
	{
		void *pBuffer = pMsgQueue->GetBufferToFill(
			pMessage->tag, pMessage->length, pMessage->from, &pElement);
		
		// Mark the message as being read
		pMessage->state = SHMEM_Q_BEING_READ;
		unlock(m_plQMutex);
		
		// Read the data from the message
		memcpy(pBuffer, (LPBYTE)pMessage + sizeof(ShmemLockedQueueHeader), 
				pMessage->length);
		pMsgQueue->SetElementEvent(pElement);
		
		lock(m_plQMutex);
	}
	// Mark the message as having been read
	pMessage->state = SHMEM_Q_READ;

	// Update the head and tail pointers of the queue
	pHead = SHMEM_Q_HEAD_PTR;
	pTail = SHMEM_Q_TAIL_PTR;
		
	// Advance the head pointer over all the read messages
	while ( (pHead < pTail) && (pHead->state == SHMEM_Q_READ) )
		pHead = (ShmemLockedQueueHeader*)((LPBYTE)pHead + pHead->next_offset);

	if (pHead >= pTail)
	{
		// When the head catches up to the tail, the queue is empty so reset 
		// the pointers and signal the queue empty
		SHMEM_Q_HEAD_OFFSET = 0;
		SHMEM_Q_TAIL_OFFSET = 2*sizeof(unsigned long);
		if (m_bUseEvent)
			ResetEvent(m_hMsgAvailableEvent);
		else
			resetevent(m_plMsgAvailableTrigger);
		setevent(m_plQEmptyTrigger);
	}
	else
		SHMEM_Q_HEAD_OFFSET = (unsigned long)((LPBYTE)pHead - (LPBYTE)m_pBase);

	unlock(m_plQMutex);

	return true;
}

// Function name	: ShmemLockedQueue::SetProgressFunction
// Description	    : 
// Return type		: void 
// Argument         : void (*ProgressPollFunction
void ShmemLockedQueue::SetProgressFunction(void (*ProgressPollFunction)())
{
	m_pProgressPollFunction = ProgressPollFunction;
	//printf("ShmemLockedQueue::SetProgressFunction called\n");fflush(stdout);
}
