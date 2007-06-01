/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef BLOCKALLOCATOR_H
#define BLOCKALLOCATOR_H

#ifdef WITH_ALLOCATOR_LOCKING

extern int g_nLockSpinCount;

typedef volatile long MPIDU_Lock_t;

#include <errno.h>
#ifdef HAVE_WINDOWS_H
#include <winsock2.h>
#include <windows.h>
#endif

static inline void MPIDU_Init_lock( MPIDU_Lock_t *lock )
{
    *(lock) = 0;
}

static inline void MPIDU_Lock( MPIDU_Lock_t *lock )
{
    int i;
    for (;;)
    {
        for (i=0; i<g_nLockSpinCount; i++)
        {
            if (*lock == 0)
            {
#ifdef HAVE_INTERLOCKEDEXCHANGE
                if (InterlockedExchange((LPLONG)lock, 1) == 0)
                {
                    /*printf("lock %x\n", lock);fflush(stdout);*/
                    MPID_PROFILE_OUT(MPIDU_BUSY_LOCK);
                    return;
                }
#elif defined(HAVE_COMPARE_AND_SWAP)
                if (compare_and_swap(lock, 0, 1) == 1)
                {
                    MPID_PROFILE_OUT(MPIDU_BUSY_LOCK);
                    return;
                }
#else
#error Atomic memory operation needed to implement busy locks
#endif
            }
        }
        MPIDU_Yield();
    }
}

static inline void MPIDU_Unlock( MPIDU_Lock_t *lock )
{
    *(lock) = 0;
}

static inline void MPIDU_Busy_wait( MPIDU_Lock_t *lock )
{
    int i;
    for (;;)
    {
        for (i=0; i<g_nLockSpinCount; i++)
            if (!*lock)
            {
                return;
            }
        MPIDU_Yield();
    }
}

static inline void MPIDU_Free_lock( MPIDU_Lock_t *lock )
{
}

/*@
   MPIDU_Compare_swap - 

   Parameters:
+  void **dest
.  void *new_val
.  void *compare_val
.  MPIDU_Lock_t *lock
-  void **original_val

   Notes:
@*/
static inline int MPIDU_Compare_swap( void **dest, void *new_val, void *compare_val,            
                        MPIDU_Lock_t *lock, void **original_val )
{
    /* dest = pointer to value to be checked (address size)
       new_val = value to set dest to if *dest == compare_val
       original_val = value of dest prior to this operation */

#ifdef HAVE_NT_LOCKS
    /* *original_val = (void*)InterlockedCompareExchange(dest, new_val, compare_val); */
    *original_val = InterlockedCompareExchangePointer(dest, new_val, compare_val);
#elif defined(HAVE_COMPARE_AND_SWAP)
    if (compare_and_swap((volatile long *)dest, (long)compare_val, (long)new_val))
        *original_val = new_val;
#else
#error Locking functions not defined
#endif

    return 0;
}
#endif

typedef struct BlockAllocator_struct * BlockAllocator;

BlockAllocator BlockAllocInit(unsigned int blocksize, int count, int incrementsize, void *(* alloc_fn)(unsigned int size), void (* free_fn)(void *p));
int BlockAllocFinalize(BlockAllocator *p);
void * BlockAlloc(BlockAllocator p);
int BlockFree(BlockAllocator p, void *pBlock);

#endif
