#ifndef SHMEMLOCKEDQUEUE_H
#define SHMEMLOCKEDQUEUE_H

#include "nt_global_cpp.h"

// With the environment variable MPICH_USE_POLLING undefined the remove functions wait on a kernel event handle
// With MPICH_USE_POLLING defined the remove functions poll a variable in shared memory

// Message states
#define SHMEM_Q_READ				0
#define SHMEM_Q_AVAIL_FOR_WRITE		SHMEM_Q_READ
#define SHMEM_Q_BEING_WRITTEN		1
#define SHMEM_Q_AVAIL_FOR_READ		2
#define SHMEM_Q_SHP_AVAIL_FOR_READ	3
#define SHMEM_Q_BEING_READ			4

// Helper macros
#define SHMEM_Q_HEAD_OFFSET		(((unsigned long *)m_pBase)[0])
#define SHMEM_Q_TAIL_OFFSET		(((unsigned long *)m_pBase)[1])
#define SHMEM_Q_HEAD_PTR		(ShmemLockedQueueHeader *)( (LPBYTE)m_pBase + SHMEM_Q_HEAD_OFFSET )
#define SHMEM_Q_TAIL_PTR		(ShmemLockedQueueHeader *)( (LPBYTE)m_pBase + SHMEM_Q_TAIL_OFFSET )

class ShmemLockedQueue
{
public:
	struct ShmemLockedQueueHeader
	{
		int tag, from, state;
		unsigned int length;
		unsigned long next_offset;
	};

	ShmemLockedQueue();
	~ShmemLockedQueue();

	bool Init(char *name, unsigned long size);
	bool Insert(unsigned char *buffer, unsigned int length, int tag, int from);
	bool InsertSHP(unsigned char *buffer, unsigned int length, int tag, int from, HANDLE hRemoteMutex, HANDLE hRemoteEvent, ShmemLockedQueue *pOtherQueue);
	bool RemoveNext(unsigned char *buffer, unsigned int *length, int *tag, int *from);
	bool RemoveNextInsert(MessageQueue *pMsgQueue, bool bBlocking = true);

	void SetProgressFunction(void (*ProgressPollFunction)());
private:
	HANDLE m_hMapping;
	LONG *m_plQMutex, *m_plQEmptyTrigger;
	HANDLE m_hMsgAvailableEvent;
	LONG *m_plMsgAvailableTrigger;
	bool m_bUseEvent;
	LPVOID m_pBottom, m_pBase, m_pEnd;
	unsigned long m_dwSize, m_dwMaxMsgSize;
	void (*m_pProgressPollFunction)();
};

#endif
