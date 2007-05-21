#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "reqalloc.h"

/*
   Nonblocking, eager shared-memory send/recv.
 */

/*
 * Hubert Ritzdorf has reported the following bug:

    As I have already reported at July 18th, there is a bug in
    ''shmemneager.c''.

    If an eager send has to be performed,

    MPID_SetupGetAddress( buf, &len, dest );

    is called to get an address in the shared memory. This works as long as
    ``p2p_shmalloc'' can allocate a shared memory region of size ``len''.
    If this is not possible, it allocates a smaller size. This size is
    returned in ``len''. But routine ''MPID_SHMEM_Eagern_isend''
    doesn't control this length and copies only the corresponding number
    number of bytes into the shared memory. And ``MPID_SHMEM_Eagerb_recv''
    tries to copy out the original length out of the shared memory.

  Hubert provided a fix, but I prefer to use one that synchronizes with
  ch_shmem (which also probably has this bug).  His fix is 

    a) I added a file ``flow.h'' in the directory ``mpid/ch_lfshmem''.
----------------- flow.h -------------------------------------
/ * Allocate shared memory if possible; otherwise use rendezvous * /

extern void  *MPID_Eager_address;

#define MPID_FLOW_MEM_OK(size,partner) \
(MPID_Eager_address = p2p_shmalloc (size))
------------------ end of flow.h -----------------------------

    b) I added the following 2 lines in ``lfshmempriv.c''

/ * Pointer used to store the address for eager sends * /
void               *MPID_Eager_address;

    c) In file ``shmemneager.c'', I add the statement

.    #include "flow.h"

    and replaced the statement

    pkt->address = MPID_SetupGetAddress( buf, &len, dest );

    by

    pkt->address = MPID_Eager_address;

 He also reports the problem with p2p.c not having MPID_myid == 0 be the 
 father process.  I believe that this is fixed now.

 */
/* Prototype definitions */
int MPID_SHMEM_Eagern_send ( void *, int, int, int, int, int, MPID_Msgrep_t );
int MPID_SHMEM_Eagern_isend ( void *, int, int, int, int, int, 
					MPID_Msgrep_t, MPIR_SHANDLE * );
int MPID_SHMEM_Eagern_cancel_send ( MPIR_SHANDLE * );
int MPID_SHMEM_Eagern_wait_send ( MPIR_SHANDLE * );
int MPID_SHMEM_Eagern_test_send ( MPIR_SHANDLE * );
int MPID_SHMEM_Eagern_save ( MPIR_RHANDLE *, int, void * );
int MPID_SHMEM_Eagern_unxrecv_start ( MPIR_RHANDLE *, void * );
void MPID_SHMEM_Eagern_delete ( MPID_Protocol * );

/* 
 * Blocking operations come from chbeager.c
 */
extern int MPID_SHMEM_Eagerb_send ( void *, int, int, int, int, 
					   int, MPID_Msgrep_t );
extern int MPID_SHMEM_Eagerb_recv ( MPIR_RHANDLE *, int, void * );
extern int MPID_SHMEM_Eagerb_irecv ( MPIR_RHANDLE *, int, void * );
extern int MPID_SHMEM_Eagerb_save ( MPIR_RHANDLE *, int, void * );
extern int MPID_SHMEM_Eagerb_unxrecv_start ( MPIR_RHANDLE *, void * );

/*
 * Definitions of the actual functions
 */

int MPID_SHMEM_Eagern_isend( buf, len, src_lrank, tag, context_id, dest,
			 msgrep, shandle )
void          *buf;
int           len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
MPIR_SHANDLE *shandle;
{
    int                       pkt_len, in_len;
    MPID_PKT_SEND_ADDRESS_T   *pkt, spkt;
    
    pkt = &spkt;

    pkt->mode	    = MPID_PKT_SEND_ADDRESS;
    pkt->context_id = context_id;
    pkt->lrank	    = src_lrank;
    pkt->tag	    = tag;
    pkt->len	    = len;
	
    DEBUG_PRINT_SEND_PKT("S Sending extra-long message",pkt);

    /* Place in shared memory */
    in_len = len;
    pkt->address = MPID_SetupGetAddress( buf, &len, dest );
    if (in_len != len) {
	MPID_FreeGetAddress( pkt->address );
	return MPID_SHMEM_Rndvn_isend( buf, in_len, src_lrank, tag, context_id,
				       dest, msgrep, shandle );
    }
    MEMCPY( pkt->address, buf, len );

    /* Send as packet only */
    MPID_SHMEM_SendControl( pkt, sizeof(MPID_PKT_SEND_ADDRESS_T), dest );

    shandle->wait	 = 0;
    shandle->test	 = 0;
    shandle->is_complete = 1;

    return MPI_SUCCESS;
}

int MPID_SHMEM_Eagern_send( buf, len, src_lrank, tag, context_id, dest,
			    msgrep )
void          *buf;
int           len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
{
    MPIR_SHANDLE shandle;

    DEBUG_INIT_STRUCT(&shandle,sizeof(shandle));
    MPIR_SET_COOKIE((&shandle),MPIR_REQUEST_COOKIE)
    MPID_SendInit( &shandle );	
    shandle.finish = 0;  /* Just in case (e.g., Eagern_isend -> Rndvn_isend) */
    MPID_SHMEM_Eagern_isend( buf, len, src_lrank, tag, context_id, dest,
			     msgrep, &shandle );
    /* Note that isend is (probably) complete */
    if (!shandle.is_complete) {
	DEBUG_TEST_FCN(shandle.wait,"req->wait");
	shandle.wait( &shandle );
    }
    return MPI_SUCCESS;
}

int MPID_SHMEM_Eagern_cancel_send( shandle )
MPIR_SHANDLE *shandle;
{
    return 0;
}

int MPID_SHMEM_Eagern_test_send( shandle )
MPIR_SHANDLE *shandle;
{
    /* Test for completion */
    return MPI_SUCCESS;
}

int MPID_SHMEM_Eagern_wait_send( shandle )
MPIR_SHANDLE *shandle;
{
    return MPI_SUCCESS;
}

/*
 * This is the routine called when a packet of type MPID_PKT_SEND_ADDRESS is
 * seen.  It receives the data as shown (final interface not set yet)
 */
int MPID_SHMEM_Eagern_recv( rhandle, from, in_pkt )
MPIR_RHANDLE *rhandle;
int          from;
void         *in_pkt;
{
    MPID_PKT_SEND_ADDRESS_T   *pkt = (MPID_PKT_SEND_ADDRESS_T *)in_pkt;
    int    msglen, err = MPI_SUCCESS;

    msglen = pkt->len;

    /* Check for truncation */
    MPID_CHK_MSGLEN(rhandle,msglen,err)
    /* Note that if we truncate, We really must receive the message in two 
       parts; the part that we can store, and the part that we discard.
       This case is not yet handled. */
    rhandle->s.count	 = msglen;
    rhandle->s.MPI_ERROR = err;
    /* source/tag? */
    MEMCPY( rhandle->buf, pkt->address, msglen );
    MPID_FreeGetAddress( pkt->address );
    if (rhandle->finish) {
	(rhandle->finish)( rhandle );
    }
    MPID_PKT_READY_CLR(&(pkt->ready));
    rhandle->is_complete = 1;
    
    return err;
}

/* This routine is called when a message arrives and was expected */
int MPID_SHMEM_Eagern_irecv( rhandle, from, in_pkt )
MPIR_RHANDLE *rhandle;
int          from;
void         *in_pkt;
{
    MPID_PKT_SEND_ADDRESS_T *pkt = (MPID_PKT_SEND_ADDRESS_T *)in_pkt;
    int    msglen, err = MPI_SUCCESS;

    msglen = pkt->len;

    /* Check for truncation */
    MPID_CHK_MSGLEN(rhandle,msglen,err)
    /* Note that if we truncate, We really must receive the message in two 
       parts; the part that we can store, and the part that we discard.
       This case is not yet handled. */
    rhandle->s.count	  = msglen;
    rhandle->s.MPI_TAG	  = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR  = err;
    MEMCPY( rhandle->buf, pkt->address, msglen );
    MPID_FreeGetAddress( pkt->address );
    if (rhandle->finish)
	(rhandle->finish)( rhandle );
    rhandle->wait	 = 0;
    rhandle->test	 = 0;
    rhandle->push	 = 0;
    rhandle->is_complete = 1;

    MPID_PKT_READY_CLR(&(pkt->ready));
    
    return err;
}

/* Save an unexpected message in rhandle */
int MPID_SHMEM_Eagern_save( rhandle, from, in_pkt )
MPIR_RHANDLE *rhandle;
int          from;
void         *in_pkt;
{
    MPID_PKT_SEND_ADDRESS_T *pkt = (MPID_PKT_SEND_ADDRESS_T *)in_pkt;

    rhandle->s.MPI_TAG	  = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR  = 0;
    rhandle->s.count      = pkt->len;
    rhandle->is_complete  = 0;
    /* Save the address */
    rhandle->start        = pkt->address;
    MPID_PKT_READY_CLR(&(pkt->ready));
    rhandle->push = MPID_SHMEM_Eagern_unxrecv_start;
    return 0;
}
/* 
 * This routine is called when it is time to receive an unexpected
 * message
 */
int MPID_SHMEM_Eagern_unxrecv_start( rhandle, in_runex )
MPIR_RHANDLE *rhandle;
void         *in_runex;
{
    MPIR_RHANDLE *runex = (MPIR_RHANDLE *)in_runex;
    int          msglen, err = 0;

    msglen = runex->s.count;
    MPID_CHK_MSGLEN(rhandle,msglen,err);
    /* Copy the data from the local area and free that area */
    if (runex->s.count > 0) {
	MEMCPY( rhandle->buf, runex->start, msglen );
	MPID_FreeGetAddress( runex->start );
    }
    rhandle->s		 = runex->s;
    MPID_RecvFree( runex );
    rhandle->wait	 = 0;
    rhandle->test	 = 0;
    rhandle->push	 = 0;
    rhandle->is_complete = 1;
    if (rhandle->finish) 
	(rhandle->finish)( rhandle );

    return err;
}

void MPID_SHMEM_Eagern_delete( p )
MPID_Protocol *p;
{
    FREE( p );
}

MPID_Protocol *MPID_SHMEM_Eagern_setup()
{
    MPID_Protocol *p;

    p = (MPID_Protocol *) MALLOC( sizeof(MPID_Protocol) );
    if (!p) return 0;
    p->send	   = MPID_SHMEM_Eagern_send;
    p->recv	   = MPID_SHMEM_Eagern_recv;
    p->isend	   = MPID_SHMEM_Eagern_isend;
    p->wait_send   = 0;
    p->push_send   = 0;
    p->cancel_send = MPID_SHMEM_Eagern_cancel_send;
    p->irecv	   = MPID_SHMEM_Eagern_irecv;
    p->wait_recv   = 0;
    p->push_recv   = 0;
    p->cancel_recv = 0;
    p->do_ack      = 0;
    p->unex        = MPID_SHMEM_Eagern_save;
    p->delete      = MPID_SHMEM_Eagern_delete;

    return p;
}
