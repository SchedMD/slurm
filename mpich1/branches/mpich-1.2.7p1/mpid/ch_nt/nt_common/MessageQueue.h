// MessageQueue.h: interface for the MessageQueue class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_MESSAGEQUEUE_H__BF8A68B9_19C3_11D3_95D2_009027106653__INCLUDED_)
#define AFX_MESSAGEQUEUE_H__BF8A68B9_19C3_11D3_95D2_009027106653__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "mpidefs.h"
#include <winsock2.h>
#include <windows.h>

// With the environment variable MPICH_USE_POLLING undefined, WaitForElementEvent waits on a kernel event handle.
// With MPICH_USE_POLLING defined, WaitForElementEvent polls a variable in shared memory.

#define MSGQ_ELEMENT_BLOCK		250
#define MSGQ_INITIALNUMNODES	5000

class MessageQueue  
{
public:
	struct MsgQueueElement
	{
		HANDLE hEvent;
		BYTE cTrigger;
		bool bIn_use;
	};

	MessageQueue();
	virtual ~MessageQueue();

	// INSERTING INTO QUEUE
	// A user wishing to add a message to the queue first calls GetBufferToFill to get
	// a buffer and an element pointer.  Then after filling the buffer calls SetElementEvent 
	// to signal that the message buffer has been filled.
	void* GetBufferToFill(int tag, int length, int from, MsgQueueElement **ppElement);

	// Call SetElementEvent after filling the buffer returned by GetBufferToFill
	inline bool SetElementEvent(MsgQueueElement *pElement);

	// REMOVING FROM QUEUE
	// A user wishing to remove a message from the queue can do it in the following two ways:
	// 1) Blocking
	//    Call FillThisBuffer.  When it returns, the buffer is filled and the length and from
	//    values are set accordingly.
	bool FillThisBuffer(int tag, void *buffer, int *length, int *from);
	// 2) Non-blocking
	//    a) Call PostBufferForFilling to supply a buffer for a message to be inserted into.
	bool PostBufferForFilling(int tag, void *buffer, int length, int *pID);
	//    b) Then call Wait or Test with the same pID that was passed to PostBufferForFilling.
	//       Wait blocks until the buffer has been filled, only returning false if there is an error.
	//       Test returns immediately: true = finished, false = not-finished.
	bool Wait(int *pID);
	bool Test(int *pID);

	// TESTING QUEUE
	// Available returns true if there is a message in the queue with the specified tag otherwise
	// it returns false.  You get better performance if you do a FillThisBuffer or PostBufferForFilling 
	// followed by a Wait or Test.  Available doesn't return true until there is a message in the queue.
	// This guarantees that an intermediate copy of the message will be made to save it in the queue
	// thereby decreasing performance.
	bool Available(int tag, int &from);

	void SetProgressFunction(void (*ProgressPollFunction)());
private:
	inline bool ResetElementEvent(MsgQueueElement *pElement);
	inline bool WaitForElementEvent(MsgQueueElement *pElement);
	inline bool TestElementEvent(MsgQueueElement *pElement);
	inline void InitElement(MsgQueueElement *pElement);
	inline void CloseElement(MsgQueueElement *pElement);
	struct InternalNode
	{
		int available;
		union 
		{
			struct LIST
			{
				int length, from;
				void *buffer;
				MsgQueueElement *element;
				InternalNode *next;
			} list;
			struct LINK
			{
				int tag;
				InternalNode *list, *posted, *next;
			} link;
            InternalNode *nextfree;
		};
	};
    struct AllocatedNode
    {
        void *pBuffer;
        AllocatedNode *pNext;
    };

	// Buffer management variables
	CRITICAL_SECTION m_CriticalSection;
	InternalNode *m_pHead, *m_pNextAvailable;
    AllocatedNode *m_pAllocList;

	// Event management variables
	int m_num_elements;
	int m_num_available;
	int m_cur_index;
	int m_pool_size;
	MsgQueueElement **m_pPool;
	bool m_bUseEvent;

	// Event management functions
	MsgQueueElement* allocElement();
	void freeElement(MsgQueueElement *element);

	// global variables used for FindNode
	int find_tag, find_length, find_from;
	void *find_buffer;
	MsgQueueElement *find_pElement;

	void FindNode(InternalNode *&node);
	bool FindAvailable(InternalNode *node, int tag, int &from);

	InternalNode *fillfind_node;
	void FillFindNode(InternalNode *&node);

	InternalNode *AllocNode();
	void FreeNode(InternalNode *pNode);
	int m_nGCCount, m_nGCMax;
	void GarbageCollect();
public:
	void (*m_pProgressPollFunction)();
};

// Function name	: MessageQueue::SetElementEvent
// Description	    : 
// Return type		: bool 
// Argument         : MessageQueue::MsgQueueElement *pElement
inline bool MessageQueue::SetElementEvent(MessageQueue::MsgQueueElement *pElement)
{
	if (m_bUseEvent)
	{
		if (!SetEvent(pElement->hEvent))
			return false;
		Sleep(0);
		return true;
	}
	pElement->cTrigger = 1;
	Sleep(0);
	return true;
}

// Function name	: MessageQueue::ResetElementEvent
// Description	    : 
// Return type		: bool 
// Argument         : MessageQueue::MsgQueueElement *pElement
inline bool MessageQueue::ResetElementEvent(MessageQueue::MsgQueueElement *pElement)
{
	if (m_bUseEvent)
	{
		if (!ResetEvent(pElement->hEvent))
			return false;
		return true;
	}
	pElement->cTrigger = 0;
	return true;
}

// Function name	: MessageQueue::WaitForElementEvent
// Description	    : 
// Return type		: bool 
// Argument         : MessageQueue::MsgQueueElement *pElement
inline bool MessageQueue::WaitForElementEvent(MessageQueue::MsgQueueElement *pElement)
{
	if (m_bUseEvent)
		return (WaitForSingleObject(pElement->hEvent, INFINITE) == WAIT_OBJECT_0);
	while (pElement->cTrigger == 0)
		Sleep(0);
	return true;
}

inline bool MessageQueue::TestElementEvent(MsgQueueElement *pElement)
{
	if (!m_bUseEvent)
		return (pElement->cTrigger != 0);
	return (WaitForSingleObject(pElement->hEvent, 0) == WAIT_OBJECT_0);
}

// Function name	: MessageQueue::InitElement
// Description	    : 
// Return type		: void 
// Argument         : MessageQueue::MsgQueueElement *pElement
inline void MessageQueue::InitElement(MessageQueue::MsgQueueElement *pElement)
{
	SECURITY_ATTRIBUTES p;
	p.bInheritHandle = FALSE;
	p.lpSecurityDescriptor = NULL;
	p.nLength = sizeof(SECURITY_ATTRIBUTES);
	pElement->hEvent = CreateEvent(&p, TRUE, FALSE, NULL);
	pElement->cTrigger = 0;
	pElement->bIn_use = false;
}

// Function name	: MessageQueue::CloseElement
// Description	    : 
// Return type		: void 
// Argument         : MessageQueue::MsgQueueElement *pElement
inline void MessageQueue::CloseElement(MessageQueue::MsgQueueElement *pElement)
{
	CloseHandle(pElement->hEvent);
	pElement->cTrigger = 0;
	pElement->bIn_use = false;
}

#endif // !defined(AFX_MESSAGEQUEUE_H__BF8A68B9_19C3_11D3_95D2_009027106653__INCLUDED_)
