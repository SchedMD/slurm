/*
 *  $Id: sendutil.c,v 1.4 1999/08/20 02:27:36 ashton Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 *
 *	Written by James Cownie (BBN). 19 June 1996
 */

#include "mpiimpl.h"
#ifdef MPI_KEEP_SEND_QUEUE
#include "../util/queue.h"
#include "reqalloc.h"
#include "sbcnst.h"

/* These functions are only used in the ADI2 world, and then 
 * only if the code is running under the control of a debugger 
 * since they create additional cost in iXsend operations.
 */

static void * MPIR_sqels = 0;

/* The debugger looks at MPIR_Sendq. */
MPIR_SQUEUE MPIR_Sendq;

void MPIR_Sendq_init()
{
  MPIR_sqels          = MPID_SBinit( sizeof( MPIR_SQEL ), 100, 100 );  
  MPIR_Sendq.sq_head  = 0;
  MPIR_Sendq.sq_tailp = &MPIR_Sendq.sq_head;
  MPID_THREAD_DS_LOCK_INIT(&MPIR_Sendq);
}

void MPIR_Sendq_finalize()
{
  MPID_SBdestroy( MPIR_sqels );
  /* Should we free the lock ? No one else does ... 
   * MPID_THREAD_DSLOCK_FREE(&MPIR_Sendq)
   */
}

/*
   MPIR_Remember_send - Called to remember a non-blocking send 
                        operation so that the user can see the
			state of the program from a debugger.
*/

void MPIR_Remember_send( 
	MPIR_SHANDLE *sh, 
	void *buff, 
	int count, 
	MPI_Datatype datatype, 
	int target, 
	int tag, 
	struct MPIR_COMMUNICATOR *comm_ptr )
{
  struct MPIR_DATATYPE *dtype_ptr;

  /* Assume that the allocator maintains its own lock */

  MPIR_SQEL * sqe = (MPIR_SQEL *) MPID_SBalloc( MPIR_sqels );
  int contig_size = 0;

  sqe->db_shandle = sh;
  sqe->db_comm    = comm_ptr;
  sqe->db_target  = target;
  sqe->db_tag     = tag;
  sqe->db_data    = buff;
  sqe->db_next    = (MPIR_SQEL *)0;
  
  /* ***Assume that it's a flat datatype... *** */
  dtype_ptr   = MPIR_GET_DTYPE_PTR(datatype);
  contig_size = MPIR_GET_DTYPE_SIZE(datatype,dtype_ptr);

  sqe->db_byte_length = count * contig_size;

  /* Insert at the tail of the list */
  MPID_THREAD_DS_LOCK(&MPIR_Sendq)
  *MPIR_Sendq.sq_tailp = sqe;
  MPIR_Sendq.sq_tailp  = &(sqe->db_next);

  MPID_THREAD_DS_UNLOCK(&MPIR_Sendq)  
}


/*
   MPIR_Forget_send - Called when a non-blocking send 
                      operation has completed to remove
		      it from the list of pending ops.
@*/

void MPIR_Forget_send( 
	MPIR_SHANDLE *sh)
{
  /* Walk over the list looking for the one we need */
  MPIR_SQEL **sqep = &MPIR_Sendq.sq_head;
  MPID_THREAD_DS_LOCK(&MPIR_Sendq)

  for (; *sqep; sqep = &(*sqep)->db_next)
    {
      MPIR_SQEL *sqe = *sqep;

      if (sqe->db_shandle == sh)
	{ /* Got it */
	  if ((*sqep = sqe->db_next) == 0)
	    MPIR_Sendq.sq_tailp = sqep;	/* update tail if appropriate */

	  MPID_SBfree(MPIR_sqels, sqe);
	  MPID_THREAD_DS_UNLOCK(&MPIR_Sendq)  
	  return;
	}
    }
  MPID_THREAD_DS_UNLOCK(&MPIR_Sendq)  
}
#endif	/* KEEP_SEND_QUEUE */

