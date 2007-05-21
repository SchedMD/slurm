/*
 *  $Id: chget.c,v 1.1.1.1 1997/09/17 20:39:19 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


//////// ??????? This is a dead file ?????? //////////
#include "mpid.h"

/* 
  This file contains the routines to handle transfering messages with 
  a "get"protocol.

  Some parts of this code reflect early attempts at a single copy model;
  this model will be handled in the future with a separate set of
  similar but not identical routines.

 */

/***************************************************************************/
/* Here are some definitions to simplify debugging                         */
/***************************************************************************/
#include "mpiddebug.h"
/***************************************************************************/

/***************************************************************************/
/* These are used to keep track of the number and kinds of messages that   */
/* are received                                                            */
/***************************************************************************/
#include "mpidstat.h"

/***************************************************************************/
/* Some operations are completed in several stages.  To ensure that a      */
/* process does not exit from MPID_End while requests are pending, we keep */
/* track of how many are outstanding                                      */
/***************************************************************************/
extern int MPID_n_pending;  /* Number of uncompleted split requests */

/***************************************************************************/
/* This is used to provide for a globally allocated message pkt in case
   we wish to preallocate or double buffer.  For example, the p4 device
   could use this to preallocate a message buffer; the Paragon could use
   this to use irecv's instead of recvs. 
 */
/***************************************************************************/
MPID_PKT_GALLOC


/*
   This code is called when a receive finds that the message has already 
   arrived and has been placed in the unexpected queue.  This code
   stores the information about the message (source, tag, length),
   copies the message into the receiver's buffer.

   dmpi_recv_handle is the API's receive handle that is to receive the
   data.

   dmpi_unexpected is the handle of the data found in the unexpected queue.

   If the message was long, it may not have all been delivered.  In that
   case, we ask for the rest of the message to be delivered.  

   There could really be an "eager" and "rendevous" version of this routine.
   See the respective routines for a description of their protocols.

   Currently, this code is IDENTICAL to the eager process_unexpected
 */

int MPID_CH_Save_unex_get( dmpi_recv_handle, pkt, from )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
int          from;
{
MPID_RHANDLE *mpid_recv_handle;

mpid_recv_handle		      = &dmpi_recv_handle->dev_rhandle;
dmpi_recv_handle->dev_rhandle.send_id = pkt->get_pkt.send_id;
dmpi_recv_handle->totallen	      = pkt->get_pkt.len;
mpid_recv_handle->mode		      = ?
mpid_recv_handle->send_id	      = pkt->get_pkt.sync_id;
return MPI_SUCCESS;
}

/* 
   See mpid/ch2/comments.txt for a description of the algorithm
 */
int MPID_CH_Do_get( dmpi_recv_handle, from, pkt )
MPIR_RHANDLE   *dmpi_recv_handle;
MPID_PKT_GET_T *pkt;
int            from;
{
int msglen, err;

msglen = pkt->len;
MPID_CHK_MSGLEN(dmpi_recv_handle,msglen,err)
dmpi_recv_handle->totallen = msglen;
pkt->recv_id = (MPID_Aint) dmpi_recv_handle;
err = MPID_CH_Do_get_to_mem( dmpi_recv_handle->dev_rhandle.start, from, pkt );

if (pkt->cur_offset >= pkt->len) {
    DMPI_mark_recv_completed(dmpi_recv_handle);
#ifdef MPID_DEBUG_ALL /* #DEBUG_START# */
    if (MPID_DebugFlag) {
	FPRINTF( MPID_DEBUG_FILE, 
		"[%d] Do Get completed read of data (tag = %d, left = %d)\n", 
		MPID_MyWorldRank, dmpi_recv_handle->tag,
		pkt->len - pkt->cur_offset );
	fflush( MPID_DEBUG_FILE );
	}
#endif                /* #DEBUG_END# */
    }
else
    dmpi_recv_handle->completer = MPID_CMPL_RECV_GET;
return err;
}

/* This should REUSE the packet passed in rather than allocating a new one.
   But we always want to use the "dynamic send" version. 
 */
int MPID_CH_Do_get_to_mem( address, from, pkt )
void           *address;
MPID_PKT_GET_T *pkt;
int            from;
{
MPID_PKT_SEND_DECL(MPID_PKT_GET_T,tpkt);

MEMCPY( address, pkt->address, pkt->len_avail );

pkt->cur_offset += pkt->len_avail;

#if !defined(MPID_PKT_GET_NEEDS_ACK)
if (pkt->len - pkt->cur_offset > 0) {
#endif

MPID_PKT_SEND_ALLOC(MPID_PKT_GET_T,tpkt,0);
MPID_PKT_SEND_ALLOC_TEST(tpkt,return MPI_ERR_EXHAUSTED)
MEMCPY( MPID_PKT_SEND_ADDR(tpkt), pkt, sizeof(MPID_PKT_GET_T) );
MPID_PKT_SEND_SET(tpkt,mode,MPID_PKT_DONE_GET);
MPID_SendControl( MPID_PKT_SEND_ADDR(tpkt), sizeof(MPID_PKT_GET_T), from );
MPID_PKT_SEND_FREE(tpkt);

#ifdef MPID_DEBUG_ALL /* #DEBUG_START# */
if (MPID_DebugFlag) {
    MPIR_RHANDLE *dmpi_recv_handle = (MPIR_RHANDLE *)(pkt->recv_id);
    FPRINTF( MPID_DEBUG_FILE, 
	     "[%d] Do Get mem completed read of data (tag = %d, left=%d)\n", 
	     MPID_MyWorldRank, dmpi_recv_handle->tag, 
	    pkt->len - pkt->cur_offset );
    fflush( MPID_DEBUG_FILE );
    }
#endif                /* #DEBUG_END# */

#if !defined(MPID_PKT_GET_NEEDS_ACK)
}
else {
    MPID_FreeGetAddress( pkt->address );
    }
#endif

/* NOTE IF WE SEND THE PACKET BACK, WE MUST NOT FREE IT!!! IN THE RECV CODE */
/* Note that if we are copying data into the shared area, we do not ever
   need the ack.  On the other hand, direct mapping REQUIRES the ack */

return MPI_SUCCESS;
}

/* 
  Handle the continuation of a get (partial data transmission) 
 */
int MPID_CH_Cont_get( pkt, from )
MPID_PKT_GET_T *pkt;
int            from;
{
MPIR_RHANDLE *dmpi_recv_handle;
int          err;
char         *address;

#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag) {
    FPRINTF( MPID_DEBUG_FILE,
	    "[%d]Cont-get from %d (tag %d) offset %d\n", 
	    MPID_MyWorldRank, from, pkt->tag, pkt->cur_offset );
    fflush( MPID_DEBUG_FILE );
    }
#endif                  /* #DEBUG_END# */

if (pkt->recv_id == 0) {
    fprintf( stderr, "Internal error! null recv id\n" );
    exit(1);
    }
dmpi_recv_handle = (MPIR_RHANDLE *)(pkt->recv_id);
/* 
   add more data.  Note that if this is an "unexpected" message and we
   are doing aggressive delivery, then we need to use the temp field, not
   the start field.  Check to see if start is null or not.

   One of start and temp must be null, or the code will become confused.
 */
if (dmpi_recv_handle->dev_rhandle.start && 
    dmpi_recv_handle->dev_rhandle.temp) {
    fprintf( stderr, 
	    "[%d] WARNING: Internal error; msgs have both start and temp\n",
	    MPID_MyWorldRank );
    }
address = (char *)(dmpi_recv_handle->dev_rhandle.start);
if (!address) {
    DEBUG_PRINT_MSG("R Cont-get for unexpected receive")
    address = (char *)dmpi_recv_handle->dev_rhandle.temp;
    if (!address) {
	fprintf( stderr, "Internal error! Null buffer for receive data\n" );
	exit(1);
	}
    }
err = MPID_CH_Do_get_to_mem( address + pkt->cur_offset, from, pkt );
if (pkt->cur_offset >= pkt->len) {
    DMPI_mark_recv_completed(dmpi_recv_handle);
    }
return err;
}

/* 
   Send-side operations
 */
int MPID_CH_post_send_long_get( dmpi_send_handle, mpid_send_handle, len ) 
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int len;
{
MPID_PKT_GET_T *pkt;
int              dest;
int              len_actual;

dest           = dmpi_send_handle->dest;
MPID_PKT_SEND_ALLOC(MPID_PKT_GET_T,pkt,0);
/* We depend on getting a packet */
/* 			dmpi_send_handle->dev_shandle.is_non_blocking); */
MPID_PKT_SEND_ALLOC_TEST(pkt,return MPI_ERR_EXHAUSTED)

pkt->mode	= MPID_PKT_DO_GET;
pkt->send_id	= (MPID_Aint) dmpi_send_handle;
pkt->recv_id	= 0;
pkt->context_id	= dmpi_send_handle->contextid;
pkt->lrank	= dmpi_send_handle->lrank;
pkt->tag	= dmpi_send_handle->tag;
ptk->len	= len;
len_actual	= len;
pkt->address	= 
	  MPID_SetupGetAddress( mpid_send_handle->start, &len_actual, dest ));
pkt->len_avail	= len_actual;
pkt->cur_offset	= 0;

DEBUG_PRINT_SEND_PKT("S Starting a send",pkt)
DEBUG_PRINT_LONG_MSG("S Sending extra-long message",pkt)
MPID_SENDCONTROL( mpid_send_handle, MPID_PKT_SEND_ADDR(pkt), 
		  sizeof(MPID_PKT_GET_T), dest );

/* Remember that we await a reply */
MPID_n_pending++;

MPID_PKT_SEND_FREE(pkt);
/* Message isn't completed until we receive the DONE_GET packet */
dmpi_send_handle->completer = MPID_CMPL_SEND_GET;

return MPI_SUCCESS;
}

/* 
  Handle the ack for a Send/GET.  Mark the send as completed, and 
  free the get memory.  This is used ONLY to process a packet of type
  MPID_PKT_DONE_GET.  Note that when we send a packet and expect a return
  of this type, we increment MPID_n_pending.  This allows us to make sure
  that we process all messages before exiting.  This is the ONLY routine that
  decrements MPID_n_pending.
 */
int MPID_CH_Done_get( pkt, from )
MPID_PKT_GET_T *pkt;
int            from;
{
MPIR_SHANDLE *dmpi_send_handle;

dmpi_send_handle = (MPIR_SHANDLE *)(pkt->send_id);
#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag) {
    FPRINTF( MPID_DEBUG_FILE,
	    "[%d]Done-get from %d (tag = %d, left = %d)\n", 
	    MPID_MyWorldRank, from, pkt->tag, pkt->len - pkt->cur_offset );
    fflush( MPID_DEBUG_FILE );
    }
#endif                  /* #DEBUG_END# */

if (pkt->cur_offset < pkt->len) {
    /* A partial transmission.  Send it back */
    int m;
    MPID_PKT_SEND_DECL(MPID_PKT_GET_T,tpkt);

    m = pkt->len_avail;
    if (pkt->cur_offset + m > pkt->len) m = pkt->len - pkt->cur_offset;

#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
    if (MPID_DebugFlag) {
	FPRINTF( MPID_DEBUG_FILE,
	    "[%d]Done-get returning %d bytes to %d\n", 
		MPID_MyWorldRank, m, from );
	fflush( MPID_DEBUG_FILE );
	}
#endif                  /* #DEBUG_END# */

    /* Now, get a new packet and send it back */
    /* SHOULD JUST RETURN THE PACKET THAT WE HAVE! */
    MPID_PKT_SEND_ALLOC(MPID_PKT_GET_T,tpkt,0);
    MPID_PKT_SEND_ALLOC_TEST(tpkt,return MPI_ERR_EXHAUSTED)
    MEMCPY( MPID_PKT_SEND_ADDR(tpkt), pkt, sizeof(MPID_PKT_GET_T) );
    MPID_PKT_SEND_SET(tpkt,len_avail,m);
    MPID_PKT_SEND_SET(tpkt,mode,MPID_PKT_CONT_GET);
    MEMCPY( MPID_PKT_SEND_GET(tpkt,address), 
	    ((char *)dmpi_send_handle->dev_shandle.start) + pkt->cur_offset, 
	    m );
    MPID_SendControl( MPID_PKT_SEND_ADDR(tpkt), sizeof(MPID_PKT_GET_T), from );
    MPID_PKT_SEND_FREE(tpkt);
    dmpi_send_handle->completer = MPID_CMPL_SEND_GET;
    }
else {
    /* Remember that we have finished this transaction */
    MPID_n_pending--;
    if (MPID_n_pending < 0) {
	fprintf( stdout, 
		"[%d] Internal error in processing messages; pending<0\n", 
		MPID_MyWorldRank );
	}
#if defined(MPID_PKT_GET_NEEDS_ACK)
    MPID_FreeGetAddress( pkt->address );
    pkt->address = 0;
#endif
    DMPI_mark_send_completed( dmpi_send_handle );
    }
return MPI_SUCCESS;
}

Cmpl_send_get is the generic "loop until completed"
