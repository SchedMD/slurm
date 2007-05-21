// MessageQueue.cpp: implementation of the MessageQueue class.
//
//////////////////////////////////////////////////////////////////////

#include "MessageQueue.h"
#include <stdio.h>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

// Function name	: MessageQueue::MessageQueue
// Description	    : 
// Return type		: 
MessageQueue::MessageQueue()
{
	int i;
	m_pHead = new MessageQueue::InternalNode[MSGQ_INITIALNUMNODES];
	for (i=0; i<MSGQ_INITIALNUMNODES; i++)
    {
		m_pHead[i].available = TRUE;
        m_pHead[i].nextfree = &m_pHead[i+1];
    }
    m_pHead[MSGQ_INITIALNUMNODES-1].nextfree = NULL;
    m_pNextAvailable = &m_pHead[1];
    m_pAllocList = NULL;

	// Insert a node for 0 because I know it is the most common tag
	m_pHead->available = FALSE;
	m_pHead->link.tag = 0;
	m_pHead->link.list = NULL;
	m_pHead->link.posted = NULL;
	m_pHead->link.next = NULL;

	m_num_elements = MSGQ_ELEMENT_BLOCK;
	m_num_available = MSGQ_ELEMENT_BLOCK;
	m_cur_index = 0;
	m_pool_size = 1;
	char pszTemp[100];
	if (GetEnvironmentVariable("MPICH_USE_POLLING", pszTemp, 100))
		m_bUseEvent = false;
	else
		m_bUseEvent = true;

	m_pPool = new MsgQueueElement*;
	m_pPool[0] = new MsgQueueElement[MSGQ_ELEMENT_BLOCK];

	for (i=0; i<MSGQ_ELEMENT_BLOCK; i++)
	{
		InitElement(&m_pPool[0][i]);
	}

	// Why is the following function protected in the header file by (_WIN32_WINNT >= 0x0500)
	// when the help files claim that they are implemented in NT 4?
	//InitializeCriticalSectionAndSpinCount(&m_CriticalSection, 4000);
	InitializeCriticalSection(&m_CriticalSection);

	m_pProgressPollFunction = NULL;

	m_nGCCount = 0;
	m_nGCMax = 10;
}

// Function name	: MessageQueue::~MessageQueue
// Description	    : 
// Return type		: 
MessageQueue::~MessageQueue()
{
	int i;
	for (i=0; i<m_num_elements; i++)
		CloseElement(&m_pPool[i/MSGQ_ELEMENT_BLOCK][i%MSGQ_ELEMENT_BLOCK]);
	m_num_elements = 0;
	m_num_available = 0;
	for (i=0; i<m_pool_size; i++)
		delete m_pPool[i];
	delete m_pPool;
	DeleteCriticalSection(&m_CriticalSection);
	delete m_pHead;
	m_pHead = NULL;
    AllocatedNode *pAllocNode;
    while (m_pAllocList)
    {
        delete m_pAllocList->pBuffer;
        pAllocNode = m_pAllocList;
        m_pAllocList = m_pAllocList->pNext;
        delete pAllocNode;
    }
}

// Function name	: MessageQueue::allocElement
// Description	    : 
// Return type		: MessageQueue::MsgQueueElement* 
MessageQueue::MsgQueueElement* MessageQueue::allocElement()
{
	// Check to see if the allocated array needs to be expanded
	if (m_num_available == 0)
	{
		int i;
		MessageQueue::MsgQueueElement **p = new MessageQueue::MsgQueueElement*[m_pool_size+1];
		for (i=0; i<m_pool_size; i++)
			p[i] = m_pPool[i];
		p[m_pool_size] = new MessageQueue::MsgQueueElement[MSGQ_ELEMENT_BLOCK];
		for (i=0; i<MSGQ_ELEMENT_BLOCK; i++)
		{
			InitElement(&p[m_pool_size][i]);
		}
		m_pool_size++;
		delete m_pPool;
		m_pPool = p;
		m_num_elements += MSGQ_ELEMENT_BLOCK;
		m_num_available = MSGQ_ELEMENT_BLOCK;
	}

	// Find the next un-used element
	while (m_pPool[m_cur_index/MSGQ_ELEMENT_BLOCK][m_cur_index%MSGQ_ELEMENT_BLOCK].bIn_use)
		++m_cur_index %= m_num_elements;

	// Mark it as used
	m_pPool[m_cur_index/MSGQ_ELEMENT_BLOCK][m_cur_index%MSGQ_ELEMENT_BLOCK].bIn_use = true;

	m_num_available--;

	// Return it
	return &m_pPool[m_cur_index/MSGQ_ELEMENT_BLOCK][m_cur_index%MSGQ_ELEMENT_BLOCK];
}

// Function name	: MessageQueue::freeElement
// Description	    : 
// Return type		: void 
// Argument         : MessageQueue::MsgQueueElement *element
void MessageQueue::freeElement(MessageQueue::MsgQueueElement *element)
{
	EnterCriticalSection(&m_CriticalSection);
	element->bIn_use = false;
	ResetElementEvent(element);
	m_num_available++;
	LeaveCriticalSection(&m_CriticalSection);
}

// Function name	: MessageQueue::AllocNode
// Description	    : 
// Return type		: MessageQueue::InternalNode * 
MessageQueue::InternalNode * MessageQueue::AllocNode()
{
	InternalNode *pNode;
	EnterCriticalSection(&m_CriticalSection);

	pNode = m_pNextAvailable;

	m_pNextAvailable = m_pNextAvailable->nextfree;
    if (m_pNextAvailable == NULL)
    {
        InternalNode *n = new InternalNode[MSGQ_INITIALNUMNODES];
        for (int i=0; i<MSGQ_INITIALNUMNODES; i++)
        {
            n[i].available = TRUE;
            n[i].nextfree = &n[i+1];
        }
        n[MSGQ_INITIALNUMNODES-1].nextfree = NULL;
        m_pNextAvailable = n;

        AllocatedNode *pANode = new AllocatedNode;
        pANode->pBuffer = n;
        pANode->pNext = m_pAllocList;
        m_pAllocList = pANode;
    }

	pNode->available = FALSE;

	LeaveCriticalSection(&m_CriticalSection);
	return pNode;
}

// Function name	: MessageQueue::FreeNode
// Description	    : 
// Return type		: void 
// Argument         : MessageQueue::InternalNode *pNode
void MessageQueue::FreeNode(MessageQueue::InternalNode *pNode)
{
	EnterCriticalSection(&m_CriticalSection);
	pNode->available = TRUE;
	pNode->nextfree = m_pNextAvailable;
	m_pNextAvailable = pNode;
	LeaveCriticalSection(&m_CriticalSection);
}

// Function name	: MessageQueue::GarbageCollect
// Description	    : 
// Return type		: void 
void MessageQueue::GarbageCollect()
{
    EnterCriticalSection(&m_CriticalSection);
    InternalNode *pNode, *pTrailer;

    // Leave the first node even if it is empty.  This node contains
    // tag 0 which is used for control messages.  Control messages
    // occur frequently.  It would be meaningless to remove this node
    // because it would just be re-allocated with the next message.
    // So start with the second node.
    pNode = m_pHead->link.next;
    pTrailer = m_pHead;
    while (pNode)
    {
	if ((pNode->link.posted == NULL) && (pNode->link.list == NULL))
	{
	    pTrailer->link.next = pNode->link.next;
	    pNode->available = TRUE;
	    pNode->nextfree = m_pNextAvailable;
	    m_pNextAvailable = pNode;
	}
	else
	{
	    pTrailer = pTrailer->link.next;
	}
	pNode = pNode->link.next;
    }

    m_nGCCount = 0;

    LeaveCriticalSection(&m_CriticalSection);
}

// Function name	: MessageQueue::FindNode
// Description	    : 
// Return type		: void 
// Argument         : MessageQueue::InternalNode *&node
void MessageQueue::FindNode(MessageQueue::InternalNode *&node)
{
    if (node == NULL)
    {
	node = AllocNode();
	node->link.list = AllocNode();
	find_buffer = node->link.list->list.buffer = new char[find_length];
	node->link.list->list.length = find_length;
	node->link.list->list.from = find_from;
	find_pElement = node->link.list->list.element = allocElement();
	node->link.list->list.next = NULL;
	node->link.posted = NULL;
	node->link.next = NULL;
	node->link.tag = find_tag;
	m_nGCCount++;
	return;
    }
    
    InternalNode *pNode = node;
    while (true)
    {
	if (find_tag == pNode->link.tag)
	{
	    if (pNode->link.posted != NULL)
	    {
		find_buffer = pNode->link.posted->list.buffer;
		if (find_length > pNode->link.posted->list.length)
		{
		    printf("MessageQueue:FindNode:Error - find_length: %d, pNode->link.posted->list.length: %d\n", find_length, pNode->link.posted->list.length);fflush(stdout);
		    pNode->link.posted->list.length = -1;
		}
		else
		    pNode->link.posted->list.length = find_length;
		pNode->link.posted->list.from = find_from;
		find_pElement = pNode->link.posted->list.element;
		pNode->link.posted = pNode->link.posted->list.next;
		return;
	    }
	    InternalNode *list = pNode->link.list;
	    if (list == NULL)
	    {
		pNode->link.list = AllocNode();
		find_buffer = pNode->link.list->list.buffer = new char[find_length];
		pNode->link.list->list.length = find_length;
		pNode->link.list->list.from = find_from;
		find_pElement = pNode->link.list->list.element = allocElement();
		pNode->link.list->list.next = NULL;
		return;
	    }
	    while (list->list.next != NULL)
		list = list->list.next;
	    list->list.next = AllocNode();
	    list = list->list.next;
	    find_buffer = list->list.buffer = new char[find_length];
	    list->list.length = find_length;
	    list->list.from = find_from;
	    find_pElement = list->list.element = allocElement();
	    list->list.next = NULL;
	    return;
	}
	
	if (pNode->link.next == NULL)
	{
	    pNode->link.next = AllocNode();
	    pNode->link.next->link.list = AllocNode();
	    find_buffer = pNode->link.next->link.list->list.buffer = new char[find_length];
	    pNode->link.next->link.list->list.length = find_length;
	    pNode->link.next->link.list->list.from = find_from;
	    find_pElement = pNode->link.next->link.list->list.element = allocElement();
	    pNode->link.next->link.list->list.next = NULL;
	    pNode->link.next->link.posted = NULL;
	    pNode->link.next->link.next = NULL;
	    pNode->link.next->link.tag = find_tag;
	    m_nGCCount++;
	    return;
	}
	pNode = pNode->link.next;
    }
}

// Function name	: MessageQueue::GetBufferToFill
// Description	    : 
// Return type		: void* 
// Argument         : int tag
// Argument         : int length
// Argument         : int from
// Argument         : MessageQueue::MsgQueueElement **ppElement
void* MessageQueue::GetBufferToFill(int tag, int length, int from, MessageQueue::MsgQueueElement **ppElement)
{
	void *ret_val;
	EnterCriticalSection(&m_CriticalSection);
	find_tag = tag;
	find_length = length;
	find_from = from;
	FindNode(m_pHead);
	*ppElement = find_pElement;
	ret_val = find_buffer;
	LeaveCriticalSection(&m_CriticalSection);
	if (m_nGCCount > m_nGCMax)
	    GarbageCollect();
	return ret_val;
}

// Function name	: MessageQueue::FillFindNode
// Description	    : 
// Return type		: void 
// Argument         : MessageQueue::InternalNode *&node
void MessageQueue::FillFindNode(MessageQueue::InternalNode *&node)
{
    if (node == NULL)
    {
	node = AllocNode();
	node->link.posted = AllocNode();
	node->link.posted->list.buffer = find_buffer;
	node->link.posted->list.length = find_length;
	node->link.posted->list.from = -1;
	find_pElement = node->link.posted->list.element = allocElement();
	node->link.posted->list.next = NULL;
	node->link.list = NULL;
	node->link.next = NULL;
	node->link.tag = find_tag;
	fillfind_node = node->link.posted;
	m_nGCCount++;
	return;
    }
    
    InternalNode *pNode = node;
    while (true)
    {
	if (find_tag == pNode->link.tag)
	{
	    if (pNode->link.list != NULL)
	    {
		find_length = pNode->link.list->list.length;
		fillfind_node = pNode->link.list;
		pNode->link.list = pNode->link.list->list.next;
		return;
	    }
	    InternalNode *posted = pNode->link.posted;
	    if (posted == NULL)
	    {
		pNode->link.posted = AllocNode();
		pNode->link.posted->list.buffer = find_buffer;
		pNode->link.posted->list.length = find_length;
		pNode->link.posted->list.from = -1;
		find_pElement = pNode->link.posted->list.element = allocElement();
		fillfind_node = pNode->link.posted;
		pNode->link.posted->list.next = NULL;
		return;
	    }
	    while (posted->list.next != NULL)
		posted = posted->list.next;
	    posted->list.next = AllocNode();
	    posted = posted->list.next;
	    posted->list.next = NULL;
	    posted->list.buffer = find_buffer;
	    posted->list.length = find_length;
	    posted->list.from = -1;
	    find_pElement = posted->list.element = allocElement();
	    fillfind_node = posted;
	    return;
	}
	
	if (pNode->link.next == NULL)
	{
	    pNode->link.next = AllocNode();
	    pNode->link.next->link.posted = AllocNode();
	    pNode->link.next->link.posted->list.buffer = find_buffer;
	    pNode->link.next->link.posted->list.length = find_length;
	    pNode->link.next->link.posted->list.from = -1;
	    find_pElement = pNode->link.next->link.posted->list.element = allocElement();
	    pNode->link.next->link.posted->list.next = NULL;
	    pNode->link.next->link.list = NULL;
	    pNode->link.next->link.next = NULL;
	    pNode->link.next->link.tag = find_tag;
	    fillfind_node = pNode->link.next->link.posted;
	    m_nGCCount++;
	    return;
	}
	pNode = pNode->link.next;
    }
}

// Function name	: MessageQueue::FillThisBuffer
// Description	    : 
// Return type		: bool 
// Argument         : int tag
// Argument         : void *buffer
// Argument         : int *length
// Argument         : int *from
bool MessageQueue::FillThisBuffer(int tag, void *buffer, int *length, int *from)
{
	bool bDeleteNeeded = true;
	InternalNode *node;
	int ret_length;

	EnterCriticalSection(&m_CriticalSection);

	// Find the node which contains the data
	// If the node doesn't exist then an empty one is created
	find_tag = tag;
	find_buffer = buffer;
	find_length = *length;
	FillFindNode(m_pHead);

	node = fillfind_node;
	ret_length = find_length;
	
	LeaveCriticalSection(&m_CriticalSection);
	if (m_nGCCount > m_nGCMax)
	    GarbageCollect();

	if (m_pProgressPollFunction)
	{
		// Poll the progress function until the message is received.
		while (!TestElementEvent(node->list.element))
			m_pProgressPollFunction();
	}
	else
	{
		// Wait for the buffer to be filled by another thread.
		WaitForElementEvent(node->list.element);
	}

	// After the buffer has been filled then the from field is valid
	*from = node->list.from;
	ret_length = node->list.length;
	if (node->list.buffer == buffer)
	{
		if (ret_length == -1)
		{
			printf("MessageQueue:FillThisBuffer:Error - length == -1\n");
			return false;
		}
		bDeleteNeeded = false;
	}
	else
	{
		if (ret_length > *length)
		{
			//printf("\nmessage[tag %d len %d] too big for buffer of length %d\n", tag, ret_length, *length);
			return false;
		}
		memcpy(buffer, node->list.buffer, ret_length);
	}
	*length = ret_length;

	// Return the element to the pool
	freeElement(node->list.element);

	// Delete the buffer if it was allocated
	if (bDeleteNeeded)
		delete node->list.buffer;

	FreeNode(node);

	return true;
}

// Function name	: MessageQueue::PostBufferForFilling
// Description	    : 
//  An asyncronous handle is an array of four integers (thus int *pID).
//  I use the first as a pointer to an InternalNode (only valid on 32 bit machines).
//  The second is a void pointer to the user buffer.
//  The third is the length - valid on both input and output.
//  The fourth stores who the message was from.
// Return type		: bool 
// Argument         : int tag
// Argument         : void *buffer
// Argument         : int length
// Argument         : int *pID
bool MessageQueue::PostBufferForFilling(int tag, void *buffer, int length, int *pID)
{
	EnterCriticalSection(&m_CriticalSection);

	// Find the node which contains the data
	// If the node doesn't exist then an empty one is created
	find_tag = tag;
	find_buffer = buffer;
	find_length = length;
	FillFindNode(m_pHead);

	pID[0] = (int)fillfind_node;
	pID[1] = (int)buffer;
	pID[2] = find_length <= length ? find_length : -1;
	pID[3] = -1;
	if (pID[2] == -1)
	{
		printf("MessageQueue:PostBufferForFilling:Buffer too short - %d < %d", length, find_length);
		fflush(stdout);
	}

	LeaveCriticalSection(&m_CriticalSection);
	if (m_nGCCount > m_nGCMax)
	    GarbageCollect();

	return (pID[2] != -1);
}

// Function name	: MessageQueue::Wait
// Description	    : 
// Return type		: bool 
// Argument         : int *pID
bool MessageQueue::Wait(int *pID)
{
	InternalNode *node = (InternalNode *)(pID[0]);

	if (node == NULL)
		return true;

	//if ( (void*)(pID[0]) < m_pHead || (void*)(pID[0]) >= m_pEnd)	{		printf("Wait:Invalid node pointer\n");fflush(stdout);	}

	if (m_pProgressPollFunction)
	{
		// Poll the progress function until the message is received.
		while (!TestElementEvent(node->list.element))
			m_pProgressPollFunction();
	}
	else
	{
		// Wait for the message to be processed by another thread.
		WaitForElementEvent(node->list.element);
	}

	// After the buffer has been filled then the from field is valid
	pID[3] = node->list.from;
	
	if (node->list.buffer == (void*)(pID[1]))
	{
		if (pID[2] == -1)
			return false;
		pID[2] = node->list.length;
	}
	else
	{
		if (node->list.length > pID[2])
			return false;
		memcpy((void*)(pID[1]), node->list.buffer, node->list.length);
		//memcpy((void*)(pID[1]), node->list.buffer, pID[2]);
	}

	// Return the element to the pool
	freeElement(node->list.element);

	// Delete the buffer if it was allocated
	if (node->list.buffer != (void*)(pID[1]))
		delete node->list.buffer;

	FreeNode(node);

	pID[0] = 0;

	return true;
}

// Function name	: MessageQueue::Test
// Description	    : 
// Return type		: bool 
// Argument         : int *pID
bool MessageQueue::Test(int *pID)
{
	if (pID[0])
	{
		if (!TestElementEvent(((InternalNode *)(pID[0]))->list.element))
		{
			if (m_pProgressPollFunction)
				m_pProgressPollFunction();
			return false;
		}
		/*
		if (!m_bUseEvent)
		{
			if (((InternalNode *)(pID[0]))->list.element->cTrigger == 0)
				return false;
		}
		else
		{
			if (WaitForSingleObject(((InternalNode *)(pID[0]))->list.element->hEvent, 0) != WAIT_OBJECT_0)
				return false;
		}
		//*/

		InternalNode *node = (InternalNode *)(pID[0]);
		
		// After the buffer has been filled then the from field is valid
		pID[3] = node->list.from;
		
		if (node->list.buffer == (void*)(pID[1]))
		{
			if (pID[2] == -1)
				return false;
			pID[2] = node->list.length;
		}
		else
		{
			if (node->list.length > pID[2])
				return false;
			memcpy((void*)(pID[1]), node->list.buffer, node->list.length);
		}
		
		// Return the element to the pool
		freeElement(node->list.element);
		
		// Delete the buffer if it was allocated
		if (node->list.buffer != (void*)(pID[1]))
			delete node->list.buffer;
		FreeNode(node);
		
		pID[0] = 0;
	}
	return true;
}

// Function name	: MessageQueue::FindAvailable
// Description	    : 
// Return type		: bool 
// Argument         : MessageQueue::InternalNode *node
// Argument         : int tag
// Argument         : int &from
bool MessageQueue::FindAvailable(MessageQueue::InternalNode *node, int tag, int &from)
{
	if (node == NULL)
		return false;

	if (tag == node->link.tag)
	{
		if (node->link.list == NULL)
			return false;
		from = node->link.list->list.from;
		return true;
	}

	return FindAvailable(node->link.next, tag, from);
}

// Function name	: MessageQueue::Available
// Description	    : 
// Return type		: bool 
// Argument         : int tag
// Argument         : int &from
bool MessageQueue::Available(int tag, int &from)
{
	bool ret_val;
	EnterCriticalSection(&m_CriticalSection);
	ret_val = FindAvailable(m_pHead, tag, from);
	LeaveCriticalSection(&m_CriticalSection);
	if (m_pProgressPollFunction)
		m_pProgressPollFunction();
	return ret_val;
}

// Function name	: MessageQueue::SetProgressFunction
// Description	    : 
// Return type		: void 
// Argument         : void (*ProgressPollFunction
void MessageQueue::SetProgressFunction(void (*ProgressPollFunction)())
{
	m_pProgressPollFunction = ProgressPollFunction;
}
