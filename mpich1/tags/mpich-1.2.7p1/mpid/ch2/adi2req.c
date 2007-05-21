/*
 *
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"
#include "reqalloc.h"
#include "sendq.h"	/* For MPIR_FORGET_SEND */
#include "mpipt2pt.h"

/***************************************************************************
 *
 * Multi-protocol, Multi-device support for 2nd generation ADI.
 * This file handles freed but not completed requests
 *
 ***************************************************************************/

void MPID_Request_free( MPI_Request request)
{
    MPI_Request rq = request ; /* MPID_devset->req_pending; */
    int         mpi_errno = MPI_SUCCESS;
    
    switch (rq->handle_type) {
    case MPIR_SEND:
	if (MPID_SendIcomplete( rq, &mpi_errno )) {
	    MPIR_FORGET_SEND( &rq->shandle );
	    MPID_SendFree( rq );
	    /* MPID_devset->req_pending = 0;*/
	    rq = 0;
	}
	break;
    case MPIR_RECV:
	if (MPID_RecvIcomplete( rq, (MPI_Status *)0, &mpi_errno )) {
	    MPID_RecvFree( rq );
	    /* MPID_devset->req_pending = 0; */
	    rq = 0;
	}
	break;
    case MPIR_PERSISTENT_SEND:
	/* This isn't complete yet */
	if (rq->persistent_shandle.active &&
	    !rq->persistent_shandle.shandle.is_complete) {
	    if (MPID_SendIcomplete( rq, &mpi_errno )) {
		rq->persistent_shandle.active = 0;
	    }
	}
	if (rq->persistent_shandle.active) {
	    MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPI internal", 
			"Unimplemented operation - persistent send free" );
	}
	else {
	    /* Must also free references to perm objects */
	    MPI_Comm ctmp = rq->persistent_shandle.perm_comm->self;
	    MPIR_Type_free( &rq->persistent_shandle.perm_datatype );
	    MPI_Comm_free( &ctmp );
	    MPID_PSendFree( rq );
	}
	break;
    case MPIR_PERSISTENT_RECV:
	/* This isn't complete yet */
	if (rq->persistent_rhandle.active && 
	    !rq->persistent_rhandle.rhandle.is_complete) {
	    if (MPID_RecvIcomplete( rq, (MPI_Status *)0, &mpi_errno )) {
		rq->persistent_rhandle.active = 0;
	    }
	}
	if (rq->persistent_rhandle.active) {
	    MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPI internal", 
			"Unimplemented operation - persistent recv free" );
	}
	else {
	    MPI_Comm ctmp = rq->persistent_rhandle.perm_comm->self;
	    MPIR_Type_free( &rq->persistent_rhandle.perm_datatype );
	    MPI_Comm_free( &ctmp );
	    MPID_PRecvFree( rq );
	}
	break;
    }

    MPID_DeviceCheck( MPID_NOTBLOCKING );
    /* 
     * If we couldn't complete it, decrement it's reference count
     * and forget about it.  This requires that the device detect
     * orphaned requests when they do complete, and process them
     * independent of any wait/test.
     */
    /*if (MPID_devset->req_pending) {*/
    if (rq) {
	rq->chandle.ref_count--;
/*	PRINTF( "Setting ref count to %d for %x\n", 
		rq->chandle.ref_count, (long)rq ); */
	/* MPID_devset->req_pending = 0; */
    }
}
