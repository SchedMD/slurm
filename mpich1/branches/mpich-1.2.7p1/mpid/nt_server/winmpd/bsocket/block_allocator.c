/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "blockallocator.h"
#include "bsocket.h"
#include <stdlib.h>
#include <stdio.h>

struct BlockAllocator_struct
{
    void **pNextFree;
    void *(* alloc_fn)(size_t size);
    void (* free_fn)(void *p);
    struct BlockAllocator_struct *pNextAllocation;
    unsigned int nBlockSize;
    int nCount, nIncrementSize;
#ifdef WITH_ALLOCATOR_LOCKING
    MPIDU_Lock_t lock;
#endif
};

int g_nLockSpinCount = 100;

BlockAllocator BlockAllocInit(unsigned int blocksize, int count, int incrementsize, void *(* alloc_fn)(unsigned int size), void (* free_fn)(void *p))
{
    BlockAllocator p;
    void **ppVoid;
    int i;

    p = alloc_fn( sizeof(struct BlockAllocator_struct) + ((blocksize + sizeof(void**)) * count) );

    p->alloc_fn = alloc_fn;
    p->free_fn = free_fn;
    p->nIncrementSize = incrementsize;
    p->pNextAllocation = NULL;
    p->nCount = count;
    p->nBlockSize = blocksize;
    p->pNextFree = (void**)(p + 1);
#ifdef WITH_ALLOCATOR_LOCKING
    MPIDU_Init_lock(&p->lock);
#endif

    ppVoid = (void**)(p + 1);
    for (i=0; i<count-1; i++)
    {
	*ppVoid = (void*)((char*)ppVoid + sizeof(void**) + blocksize);
	ppVoid = *ppVoid;
    }
    *ppVoid = NULL;

    return p;
}

int BlockAllocFinalize(BlockAllocator *p)
{
    if (*p == NULL)
	return 0;
    BlockAllocFinalize(&(*p)->pNextAllocation);
    if ((*p)->free_fn != NULL)
	(*p)->free_fn(*p);
    *p = NULL;
    return 0;
}

void * BlockAlloc(BlockAllocator p)
{
    void *pVoid;
    
#ifdef WITH_ALLOCATOR_LOCKING
    MPIDU_Lock(&p->lock);
#endif

    pVoid = p->pNextFree + 1;
    
    if (*(p->pNextFree) == NULL)
    {
	BlockAllocator pIter = p;
	while (pIter->pNextAllocation != NULL)
	    pIter = pIter->pNextAllocation;
	pIter->pNextAllocation = BlockAllocInit(p->nBlockSize, p->nIncrementSize, p->nIncrementSize, p->alloc_fn, p->free_fn);
	p->pNextFree = pIter->pNextFree;
    }
    else
	p->pNextFree = *(p->pNextFree);

#ifdef WITH_ALLOCATOR_LOCKING
    MPIDU_Unlock(&p->lock);
#endif

    return pVoid;
}

int BlockFree(BlockAllocator p, void *pBlock)
{
#ifdef WITH_ALLOCATOR_LOCKING
    MPIDU_Lock(&p->lock);
#endif

    ((void**)pBlock)--;
    *((void**)pBlock) = p->pNextFree;
    p->pNextFree = pBlock;

#ifdef WITH_ALLOCATOR_LOCKING
    MPIDU_Unlock(&p->lock);
#endif

    return 0;
}
