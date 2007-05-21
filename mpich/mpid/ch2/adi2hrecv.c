/*
 *  $Id: adi2hrecv.c,v 1.3 2001/11/12 23:01:41 ashton Exp $
 *
 *  (C) 1995 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"
#include "reqalloc.h"
#include "../util/queue.h"

/***************************************************************************/
/*
 * Multi-protocol, Multi-device support for 2nd generation ADI.
 * This file has support for noncontiguous sends for systems that do not 
 * have native support for complex datatypes.
 */
/***************************************************************************/

void MPID_RecvDatatype( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	int src_lrank, 
	int tag, 
	int context_id, 
	MPI_Status *status, 
	int *error_code )
{
    MPIR_RHANDLE rhandle;
    MPI_Request  request = (MPI_Request)&rhandle;

    DEBUG_INIT_STRUCT(request,sizeof(rhandle));
    MPID_RecvInit( &rhandle );
    /* rhandle.finish = 0; gets set in IrecvDatatype */
    *error_code = 0;
    MPID_IrecvDatatype( comm_ptr, buf, count, dtype_ptr, src_lrank, tag, 
			context_id, request, error_code );
    if (!*error_code) {
	MPID_RecvComplete( request, status, error_code );
    }
}

void MPID_IrecvDatatype( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	int src_lrank, 
	int tag, 
	int context_id, 
	MPI_Request request, 
	int *error_code )
{
    MPIR_RHANDLE    *dmpi_unexpected, *rhandle = &request->rhandle;
    int             len;
    MPID_Msgrep_t   msgrep = MPID_MSGREP_RECEIVER;
    MPID_DO_HETERO(MPID_Msg_pack_t msgact = MPID_MSG_OK;)
    void            *mybuf;
    int             contig_size;
    MPID_DO_HETERO(int             src_grank);

    DEBUG_PRINT_ARGS("R starting IrecvDatatype");

    /* Just in case; make sure that finish is 0 */
    rhandle->finish = 0;

    /* See if this is really contiguous */
    contig_size = MPIR_GET_DTYPE_SIZE(datatype,dtype_ptr);

    MPID_DO_HETERO(src_grank = (src_lrank >= 0) ? 
		   comm_ptr->lrank_to_grank[src_lrank] : src_lrank);
    MPID_DO_HETERO(MPID_Msg_rep( comm_ptr, src_grank, dtype_ptr, 
				 &msgrep, &msgact ));
    if (contig_size > 0
	MPID_DO_HETERO(&& msgact == MPID_MSG_OK)) {
	/* Just drop through into the contiguous send routine 
	   For packed data, the representation format is that in the
	   communicator.
	 */
	len = contig_size * count;
	MPID_IrecvContig( comm_ptr, buf, len, src_lrank, tag, context_id, 
			  request, error_code );
	return;
    }

    /* 
       Follow the same steps as IrecvContig, buf after creating a 
       temporary buffer to hold the incoming data in.
       */
    
    MPID_UnpackMessageSetup( count, dtype_ptr, comm_ptr, src_lrank, msgrep,
			     (void **)&mybuf, &len, error_code );
    if (*error_code) return;
    /* setup the request */
    /* 
       At this time, we check to see if the message has already been received.
       Note that we cannot have any thread receiving a message while 
       checking the queues.   In case we do enqueue the message, we set
       the fields that will need to be valid BEFORE calling this routine
       (this is extra overhead ONLY in the case that the message was
       unexpected, which is already the higher-overhead case).
     */
    /* Here we need to set up a different, special buffer if NOT 
       contiguous/homogeneous.  We'll also need a special complete 
       function to unpack the data
     */
    rhandle->len	 = len;
    rhandle->buf	 = mybuf;
    rhandle->start       = buf;
    rhandle->count       = count;
    rhandle->datatype    = dtype_ptr;
    MPIR_REF_INCR(dtype_ptr);
    rhandle->is_complete = 0;
    rhandle->wait        = 0;
    rhandle->test        = 0;
    rhandle->finish      = MPID_UnpackMessageComplete;

    MPID_Search_unexpected_queue_and_post( src_lrank, tag, context_id,  
					   rhandle, &dmpi_unexpected );
    if (dmpi_unexpected) {
	DEBUG_PRINT_MSG("R Found in unexpected queue");
	DEBUG_TEST_FCN(dmpi_unexpected->push,"req->push");
	*error_code = (*dmpi_unexpected->push)( rhandle, dmpi_unexpected );
	DEBUG_PRINT_MSG("R Exiting IrecvDatatype");
	/* This may or may not complete the message */
	return;
    }

    /* If we got here, the message is not yet available */
    /*    MPID_DRAIN_INCOMING */

    DEBUG_PRINT_MSG("R Exiting IrecvDatatype")
}
