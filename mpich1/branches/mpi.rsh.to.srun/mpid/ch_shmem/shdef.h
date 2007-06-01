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
#ifdef PROCESSOR_COUNT
#if PROCESSOR_COUNT > 256
#define MPID_MAX_PROCS PROCESSOR_COUNT
#define MPID_MAX_SHMEM 4194304*(PROCESSOR_COUNT/8)
#else
#define MPID_MAX_PROCS 256
#define MPID_MAX_SHMEM 4194304
#endif /* PROCESSOR_COUNT > 256 */
#else
#define MPID_MAX_PROCS 256
#define MPID_MAX_SHMEM 4194304
#endif /* PROCESSOR_COUNT */
#endif /* MPI_cspp */


#define MPID_SHMEM_MAX_PKTS (4*MPID_MAX_PROCS)

#if !defined(VOLATILE)
#if (HAS_VOLATILE || defined(__STDC__))
#define VOLATILE volatile
#else
#define VOLATILE
#define volatile
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
/* 
   The shared datastructures.  These are padded to be on different
   cachelines.  The queue is arranged so that the head and tail pointers
   are on the same cacheline .
   It might be useful to put the locks for the datastructure in the same
   structure.  Then again, if one process is doing while(!head), this
   could slow down another process that is trying to lock the queue or
   stack.

   Note that it is the head/tail pointers that are volatile, not the 
   contents of the pointers (e.g., we want mpid_pkt_t * volatile, not
   mpid_pkt_t volatile * volatile)
 */
typedef struct {
    MPID_PKT_T * VOLATILE head;
    MPID_PKT_T * VOLATILE tail;
    char                pad[MPID_CACHE_LINE_SIZE - 2 * sizeof(MPID_PKT_T *)];
    } MPID_SHMEM_Queue;

typedef struct {
    MPID_PKT_T * VOLATILE head;
    char                pad[MPID_CACHE_LINE_SIZE - 1 * sizeof(MPID_PKT_T *)];
    } MPID_SHMEM_Stack;

typedef struct {
    int          size;           /* Size of barrier */
    VOLATILE int phase;          /* Used to indicate the phase of this 
				    barrier; only process 0 can change */
    VOLATILE int cnt1, cnt2;     /* Used to perform counts */
    } MPID_SHMEM_Barrier_t;

/*
   This is the global area of memory; when this structure is allocated,
   we have the initial shared memory
 */
typedef struct {
    /* locks may need to be aligned, so keep at front (p2p_shmalloc provides
       16-byte alignment for each allocated block).       */
    p2p_lock_t availlock[MPID_MAX_PROCS];    /* locks on avail list */
    p2p_lock_t incominglock[MPID_MAX_PROCS]; /* locks on incoming list */
    p2p_lock_t globlock;
    MPID_SHMEM_Queue    incoming[MPID_MAX_PROCS];     /* Incoming messages */
    MPID_SHMEM_Stack    avail[MPID_MAX_PROCS];        /* Avail pkts */

    MPID_PKT_T          pool[MPID_SHMEM_MAX_PKTS];    /* Preallocated pkts */

    /* We put globid last because it may otherwise upset the cache alignment
       of the arrays */
#if defined(MPI_cspp)
    p2p_lock_t globid_lock[MPID_MAX_NODES];
    VOLATILE int globid[MPID_MAX_NODES];	/* Used for world id */
#else
    VOLATILE int        globid;           /* Used to get my id in the world */
#endif
    MPID_SHMEM_Barrier_t barrier;         /* Used for barriers */
    } MPID_SHMEM_globmem;	

/* 
   This is a LOCAL copy of the ADDRESSES of objects in MPID_shmem.
   We can use
    MPID_lshmem.incomingPtr[src]->head
   instead of
    MPID_shmem->incoming[src].head
   The advantage of this is that the dereference (->) is not done on the
   same address (MPID_shmem) that all other processors must also use.
 */
typedef struct {
    /* locks may need to be aligned, so keep at front (p2p_shmalloc provides
       16-byte alignment for each allocated block).       */
    p2p_lock_t *availlockPtr[MPID_MAX_PROCS];    /* locks on avail list */
    p2p_lock_t *incominglockPtr[MPID_MAX_PROCS]; /* locks on incoming list */
    MPID_SHMEM_Queue    *incomingPtr[MPID_MAX_PROCS];  /* Incoming messages */
    MPID_SHMEM_Stack    *availPtr[MPID_MAX_PROCS];     /* Avail pkts */
#ifdef FOO
    MPID_SHMEM_Queue    *my_incoming;                  /* My incoming queue */
    MPID_SHMEM_Stack    *my_avail;                     /* My avail stack */
#endif
    } MPID_SHMEM_lglobmem;	

extern MPID_SHMEM_globmem  *MPID_shmem;
extern MPID_SHMEM_lglobmem  MPID_lshmem;
extern int                  MPID_myid;
extern int                  MPID_numids;
extern MPID_PKT_T           *MPID_local;      /* Local pointer to arrived
						 packets; it is only
						 accessed by the owner */
extern MPID_PKT_T * VOLATILE *MPID_incoming;   /* pointer to my incoming 
						 queue HEAD (really?) */

#ifdef FOO
#define PI_NO_NSEND
#define PI_NO_NRECV
#define MPID_MyWorldRank \
    MPID_myid
#define MPID_WorldSize \
    MPID_numids

#define MPID_RecvAnyControl( pkt, size, from ) \
    { MPID_TRACE_CODE("BRecvAny",-1);\
      MPID_SHMEM_ReadControl( pkt, size, from );\
      MPID_TRACE_CODE_PKT("ERecvAny",*(from),\
			  MPID_PKT_RECV_GET(*(pkt),head.mode));}
#define MPID_RecvFromChannel( buf, size, channel ) \
    { MPID_TRACE_CODE("BRecvFrom",channel);\
      fprintf( stderr, "message too big and truncated!\n" );\
      MPID_TRACE_CODE("ERecvFrom",channel);}
#define MPID_ControlMsgAvail( ) \
    (MPID_local || *MPID_incoming)
#define MPID_SendControl( pkt, size, channel ) \
    { MPID_TRACE_CODE_PKT("BSendControl",channel,\
			  MPID_PKT_SEND_GET((MPID_PKT_T*)(pkt),head.mode));\
      MPID_SHMEM_SendControl( pkt, size, channel );\
      MPID_TRACE_CODE("ESendControl",channel);}
#define MPID_SendChannel( buf, size, channel ) \
    { MPID_TRACE_CODE("BSend",channel);\
	fprintf( stderr, "message too big (%d) and truncated!\n", size  );\
      MPID_TRACE_CODE("ESend",channel);}
#define MPID_SendControlBlock(pkt,size,channel) \
      MPID_SendControl(pkt,size,channel)
#define MPID_SENDCONTROL(mpid_send_handle,pkt,len,dest) \
MPID_SendControl( pkt, len, dest );

/* 
   Non-blocking versions (NOT required, but if PI_NO_NRECV and PI_NO_NSEND
   are NOT defined, they must be provided)
 */
#define MPID_IRecvFromChannel( buf, size, channel, id ) 
#define MPID_WRecvFromChannel( buf, size, channel, id ) 
#define MPID_RecvStatus( id ) 

/* Note that these use the tag based on the SOURCE, not the channel
   See MPID_SendChannel */
#define MPID_ISendChannel( buf, size, channel, id ) 
#define MPID_WSendChannel( buf, size, channel, id ) 

/*
   We also need an abstraction for out-of-band operations.  These could
   use transient channels or some other operation.  This is essentially for
   performing remote memory operations without local intervention; the need
   to determine completion of the operation requires some sort of handle.
 */
/* And here they are. Rather than call them transient channels, we define
   "transfers", which are split operations.  Both receivers and senders
   may create a transfer.

   Note that the message-passing version of this uses the 'ready-receiver'
   version of the operations.
 */
#define MPID_CreateSendTransfer( buf, size, partner, id ) 
#define MPID_CreateRecvTransfer( buf, size, partner, id ) 

#define MPID_StartRecvTransfer( buf, size, partner, id, rid ) 
#define MPID_EndRecvTransfer( buf, size, partner, id, rid ) 
#define MPID_TestRecvTransfer( rid ) 

#define MPID_StartSendTransfer( buf, size, partner, id, sid ) 
#define MPID_EndSendTransfer( buf, size, partner, id, sid ) 
#define MPID_TestSendTransfer( sid ) 

/* Miscellaneous definitions */
#define MALLOC(a) malloc((unsigned)(a))
#define FREE(a)   free(a)
#define SYexitall(a,b) p2p_error(a,b)

#endif
