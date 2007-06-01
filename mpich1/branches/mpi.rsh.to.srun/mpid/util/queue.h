#ifndef _MPID_QUEUE
#define _MPID_QUEUE
/*
   This file contains the datastructures for managing the queues of 
   posted receives and of unexpected messages.  

   These are collected together because a common operation is

     search unexpected queue for matching message; if not found, add to
     posted receive queue.

   In a multi-threaded environment, this needs to be "thread-atomic"; thus we
   combine these together.  This also simplifies the implementation of
   this operation.

   Because of the importance of this queue, we've added explicit support
   for MPI.  This is indicated by the fields:
   context_id
   tag, tagmask
   lsrc, srcmask

   (note that the last two may depart if we split the queues by source).
   The mask fields allow us to replace

   itag == tag || tag == MPI_ANY_TAG

   with 

   (itag & tagmask) == tag

   saving us a compare and branch at the cost of a load and bitwise-and.
   Generalizations of the tagmask could provide users with additional
   functionality (for MPICH EXTENSIONS).

   We use a singly linked list, since we have always just searched the list at
   the time when we delete something, so don't need random deletion.  (This
   saves space in the QEL and time in maintaining unnecessary back pointers).
   The queue header has a pointer to the first element in the queue, and a
   pointer to the pointer to the last element (this avoids an emptiness test
   when inserting).  

   In a multithreaded environment, we may want to make the following elements
   *volatile*:
   struct _MPID_QEL *next
   MPID_QEL *first
   MPID_QEL **lastp;
 */

typedef struct _MPID_QEL {  /* queue elements */
    int  context_id, 
         tag, tagmask,
         lsrc, srcmask;
    struct _MPID_QEL *next; /* next queue element */
    MPIR_RHANDLE *ptr;      /* request for this entry */
} MPID_QEL;

typedef struct {
    MPID_QEL *first; 
    MPID_QEL **lastp;       /* pointer to pointer to last... */
    } MPID_QUEUE;

typedef struct {        /* header for queues of things like handles */
    MPID_THREAD_DS_LOCK_DECLARE   /* Used for controlling access by threads */
    MPID_QUEUE unexpected;
    MPID_QUEUE posted;
} MPID_QHDR;

extern MPID_QHDR MPID_recvs;

/*
 * Additional information which is kept on the send Q, but *only* for
 * debugging purposes. This allows a debugger (TotalView in particular) to
 * display the set of active non-blocking sends. However the process has to
 * maintain these data structures which adds code to the path for iXsend
 * and the completion routines. 
 * Normally this code is compiled in, but is only executed under control
 * of a flag, which means that the cost is one load, test and branch even
 * when not being debugged.
 * For extra optimisation even this can be removed under control of an
 * ifdef. (See sendq.h).
 *
 * Note that there is no pointer from the "real" shandle to the debug one
 * we just search the queue to find the one we need at the point when	
 * we're removing it.
 */
typedef struct _MPIR_SQEL MPIR_SQEL;
struct _MPIR_SQEL {
  MPIR_SHANDLE * db_shandle;		/* The real shandle */
  struct MPIR_COMMUNICATOR *db_comm;    /* The communicator */
  int		 db_target;		/* Who is it to     */
  int		 db_tag;		/* What tag was it sent with */
  void *	 db_data;		/* Where it came from */
  int		 db_byte_length;	/* How long is it */
  /* May also want the MPI data type and replication count... */
  struct _MPIR_SQEL *db_next;   	/* For the chain */
}; 

/* The structure used to hold the send queue */
typedef struct 
{
  MPID_THREAD_DS_LOCK_DECLARE   /* Used for controlling access by threads */
  MPIR_SQEL *sq_head;
  MPIR_SQEL **sq_tailp;	    	/* pointer to pointer to last... */
} MPIR_SQUEUE;

void MPID_Dump_queue (MPID_QHDR *);
void MPID_Dump_queues (void);
/* int  MPID_Enqueue (MPID_QUEUE *, int, int, int, MPIR_RHANDLE *); */
int  MPID_Dequeue (MPID_QUEUE *, MPIR_RHANDLE *);
/* int MPID_Search_posted_queue ( int, int, int, int, MPIR_RHANDLE **); */
int MPID_Search_unexpected_for_request( MPIR_SHANDLE *, 
					MPIR_RHANDLE **, int * );
int MPID_Search_unexpected_queue ( int, int, int, int, MPIR_RHANDLE **);
void MPID_Msg_arrived ( int, int, int, MPIR_RHANDLE **, int * );
void MPID_Search_unexpected_queue_and_post ( int, int, int, 
				       MPIR_RHANDLE *, MPIR_RHANDLE **);
void MPID_Free_unexpected ( MPIR_RHANDLE * );
void MPID_InitQueue (void);

#endif


