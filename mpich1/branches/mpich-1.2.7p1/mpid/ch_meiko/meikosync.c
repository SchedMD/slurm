






/*
 *  $Id: meikosync.c,v 1.1.1.1 1997/09/17 20:40:43 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#ifndef lint
static char vc[] = "$Id: meikosync.c,v 1.1.1.1 1997/09/17 20:40:43 gropp Exp $";
#endif

#include "mpid.h"
#include "mpiddebug.h"

/*
   This file contains routines to keep track of synchronous send messages;
   they must be explicitly acknowledged.

   Heterogeneous systems use integer id's; homogeneous systems just use the
   address of the dmpi_send_handle.  Heterogeneous code is in chhetero.c .
 */

/* Define MPID_TEST_SYNC to test the code that uses the addresses of the
   dmpi handles */
/*  #define MPID_TEST_SYNC */

#if defined(MPID_TEST_SYNC) || !defined(MPID_HAS_HETERO)
/* We can use the address of the request as the sync-id */

MPID_Aint MPID_MEIKO_Get_Sync_Id( dmpi_handle, mpid_handle )
MPIR_SHANDLE *dmpi_handle;
MPID_SHANDLE *mpid_handle;
{
return (MPID_Aint) dmpi_handle;
}

int MPID_MEIKO_Lookup_SyncAck( sync_id, dmpi_send_handle, mpid_send_handle )
MPID_Aint    sync_id;
MPIR_SHANDLE **dmpi_send_handle;
MPID_SHANDLE **mpid_send_handle;
{
*dmpi_send_handle = (MPIR_SHANDLE *)sync_id;
*mpid_send_handle = &((*dmpi_send_handle)->dev_shandle);
return MPI_SUCCESS;
}

/* Process a synchronization acknowledgement */
int MPID_SyncAck( sync_id, from )
MPID_Aint   sync_id;
int         from;
{
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;

/* This is an acknowledgement of a synchronous send; look it up and
   mark as completed */
#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag) {
    printf( 
    "[%d]SYNC received sync ack message for mode=%x from %d (%s:%d)\n",  
	   MPID_MyWorldRank, sync_id, from, __FILE__, __LINE__ );
    fflush( stdout );
    }
#endif                  /* #DEBUG_END# */
MPID_MEIKO_Lookup_SyncAck( sync_id, &dmpi_send_handle, &mpid_send_handle );
if (!dmpi_send_handle) {
    fprintf( stderr, "Error in processing sync ack!\n" );
    return MPI_ERR_INTERN;
    }
DMPI_mark_send_completed(dmpi_send_handle);
return MPI_SUCCESS;
}

/* return an acknowledgment */
void MPID_SyncReturnAck( sync_id, from )
MPID_Aint sync_id;
int       from;
{
MPID_PKT_SEND_DECL(MPID_PKT_SYNC_ACK_T,pkt);

MPID_PKT_SEND_ALLOC(MPID_PKT_SYNC_ACK_T,pkt,0);
/* Need to generate an error return !!!! */
MPID_PKT_SEND_ALLOC_TEST(pkt,return)

MPID_PKT_SEND_SET(pkt,mode,MPID_PKT_SYNC_ACK);
MPID_PKT_SEND_SET(pkt,sync_id,sync_id);

DEBUG_PRINT_BASIC_SEND_PKT("SYNC Starting a send",pkt)
MPID_SendControl( MPID_PKT_SEND_ADDR(pkt), sizeof(MPID_PKT_SYNC_ACK_T), from );
}

/* Look through entire list for this API handle */
void MPID_Sync_discard( dmpi )
MPIR_SHANDLE *dmpi;
{
/* Do something to indicated cancelled. 

   Need to:
   Send a message to recepient saying that message is canceled
   Wait for ack (in case request for message already in transit)
      If request arrives, complete message
   Set field so that MPI_TEST_CANCELLED will give correct value

   A test program that uses MPI_ISSEND (and never receives it) should
   work with MPI_CANCEL and TEST_CANCELLED).
 */
}
#endif
