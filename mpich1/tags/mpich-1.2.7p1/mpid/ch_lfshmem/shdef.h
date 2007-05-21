/*
   Sending and receiving packets

   Packets are sent and received on connections.  In order to simultaneously
   provide a good fit with conventional message-passing systems and with 
   other more direct systems (e.g., sockets), I've defined a set of
   connection macros that are here translated into Chameleon message-passing
   calls or into other, data-channel transfers.  These are somewhat 
   complicated by the need to provide access to non-blocking operations

   This file is designed for use with the portable shared memory code from
   p2p.

   In addition, we provide a simple way to log the "channel" operations
   If MPID_TRACE_FILE is set, we write information on the operation (both
   start and end) to the given file.  In order to simplify the code, we
   use the macro MPID_TRACE_CODE(name,channel).  Other implementations
   of channel.h are encouraged to provide the trace calls; note that as macros,
   they can be completely removed at compile time for more 
   performance-critical systems.

 */
/* Do we need stdio here, or has it already been included? */
#include "p2p.h"

#if defined(MPI_cspp)
#include <sys/cnx_types.h>
#define MPID_MAX_NODES CNX_MAX_NODES
#define MPID_MAX_PROCS_PER_NODE CNX_MAX_CPUS_PER_NODE
#define MPID_MAX_PROCS MPID_MAX_NODES*MPID_MAX_PROCS_PER_NODE
#define MPID_MAX_SHMEM 16777216
#else
#define MPID_MAX_PROCS 32
#define MPID_MAX_SHMEM 4194304
#endif


#define MPID_SHMEM_MAX_PKTS (4*MPID_MAX_PROCS)

#if !defined(VOLATILE)
#if (HAS_VOLATILE || defined(__STDC__))
#define VOLATILE volatile
#else
#define VOLATILE
#endif
#endif

/*
   Notes on the shared data.

   Some of the data may be pointers to shared memory where the POINTERS
   should reside in local memory (for increased efficiency).

   In particularly, the structure MPID_SHMEM_globmem is allocated out of
   shared memory and contains various thinks like the locks.  However, we
   don't want to find the ADDRESS of a lock by looking through some 
   shared memory.  
   Note also that we want all changable data to be on separate cache lines.

   Thus, we want to replace 
     VOLATILE MPID_PKT_T *(a[MPID_MAX_PROCS]);
   by
     VOLATILE MPID_PKT_PTR_T (a[MPID_MAX_PROCS])
   where
      typedef union { MPID_PTK_T *pkt ; PAD } MPID_PKT_PTR_T ;
   where the PAD is char pad[sizeof-cachline].

   In addition, we want to put data that is guarded together into the
   same cache-line.  However, we may not want to put the locks on the same
   cache-line, since other processes may be attempting to acquire the
   locks.

   Note that there are two structures.  A Queue, for messages (required
   message ordering), and a Stack, for available message packets.

   Finally, note that while the array of message queues and stacks itself is
   in shared memory, their locations in the shared memory do not change (for
   example, the location of the queue data structure for process 12 does
   not move).  Because of that, we do not want to access the elements of
   these structures by first dereferencing MPID_shmem (the pointer to the
   general structure), rather, we want to keep a local COPY of the locations
   in MPID_shmem and use those instead.  It is true that on some systems,
   the cache architecture will do this for us, but my making this explicit,
   we avoid any performance surprises (at least about this).  The local
   copy is kept in MPID_lshmem;
 */

/* 
   For many systems, it is important to align data structures on 
   cache lines, and to insure that separate structures are in
   different cache lines.  Currently, the largest cache line that we've seen
   is 128 bytes, so we pick that as the default.
 */
#ifndef MPID_CACHE_LINE_SIZE
#define MPID_CACHE_LINE_SIZE 128
#define MPID_CACHE_LINE_LOG_SIZE 7
#endif

typedef struct {
    int          size;           /* Size of barrier */
    VOLATILE int phase;          /* Used to indicate the phase of this 
				    barrier; only process 0 can change */
    VOLATILE int cnt1, cnt2;     /* Used to perform counts */
    } MPID_SHMEM_Barrier_t;

/* This is a lock-free short queue.  It relies on using ordered writes to
   set a "full" bit in the packet */

/* Not used */
#ifndef MPID_LFQ_DEPTH
#define MPID_LFQ_DEPTH 4
#endif

/*
   This is the global area of memory; when this structure is allocated,
   we have the initial shared memory
 */
typedef struct {
    /* locks may need to be aligned, so keep at front (p2p_shmalloc provides
       16-byte alignment for each allocated block).       */
    p2p_lock_t globlock;

    VOLATILE MPID_PKT_T pool[MPID_MAX_PROCS][MPID_MAX_PROCS];  
    /* Preallocated pkts */

    /* We put globid last because it may otherwise upset the cache alignment
       of the arrays */
    VOLATILE int        globid;           /* Used to get my id in the world */
    MPID_SHMEM_Barrier_t barrier;         /* Used for barriers */
    } MPID_SHMEM_globmem;	

/* 
   It is the contents of pool/mypool that are volatile, 
   not the actual pointers.
 */
typedef struct {
    VOLATILE MPID_PKT_T *(pool[MPID_MAX_PROCS]);    /* For sending */
    VOLATILE MPID_PKT_T *mypool;   /* For receiving */
    } MPID_SHMEM_lglobmem;	

extern MPID_SHMEM_globmem  *MPID_shmem;
extern MPID_SHMEM_lglobmem  MPID_lshmem;
extern int                  MPID_myid;
extern int                  MPID_numids;

/*
 * We need macros to set/clear/read the ready fields
 */
#if MPID_USE_SX4_VOPS
/* The NEC SX-4 has a non-coherent memory system.  We use exclusively
   vector instructions to move data in and out of the shared packet areas;
   the vector instructions always bypass the cache.
 */
#define MPID_PKT_READY_SET(x)    syncvset1(x)
#define MPID_PKT_READY_CLR(x)    syncvset0(x)
#define MPID_PKT_READY_IS_SET(x) vtest1(x)
#define MPID_PKT_COPYIN( dest, src, len ) MEMCPY(dest,src,len)
/* vmemcpy( dest, src, len + ((len)&0x7)?(8 - (len)&0x7):0 ) */
/* copyout only needed if packet copied immediately from store */
#define MPID_PKT_COPYOUT()
#else
#define MPID_PKT_READY_SET(x)    *(x) = 1
#define MPID_PKT_READY_CLR(x)    *(x) = 0
#define MPID_PKT_READY_IS_SET(x) (*(x) == 1)
#define MPID_PKT_COPYIN( dest, src, len ) MEMCPY(dest,src,len)
#define MPID_PKT_COPYOUT()
/* #define MPID_PKT_READY_IS_CLR(x) (*(x) == 0) */
#endif
