/*
 *  $Id: adi2hssend.c,v 1.3 2001/11/12 23:02:44 ashton Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"

/***************************************************************************/
/*
 * Multi-protocol, Multi-device support for 2nd generation ADI.
 * This file has support for noncontiguous sends for systems that do not 
 * have native support for complex datatypes.
 */
/***************************************************************************/

void MPID_SsendDatatype( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	int src_lrank, 
	int tag, 
	int context_id, 
	int dest_grank, 
	int *error_code )
{
    int             len, contig_size;
    void            *mybuf;
    MPID_Msgrep_t   msgrep = MPID_MSGREP_RECEIVER;
    MPID_Msg_pack_t msgact = MPID_MSG_OK;

    /*
     * Alogrithm:
     * First, see if we can just send the data (contiguous or, for
     * heterogeneous, packed).
     * Otherwise, 
     * Create a local buffer, use SendContig, and then free the buffer.
     */

    contig_size = MPIR_GET_DTYPE_SIZE(datatype,dtype_ptr);
    MPID_DO_HETERO(MPID_Msg_rep( comm_ptr, dest_grank, dtype_ptr, 
				 &msgrep, &msgact ));
    
    if (contig_size > 0
	MPID_DO_HETERO(&& msgact == MPID_MSG_OK)) {
	/* Just drop through into the contiguous send routine 
	   For packed data, the representation format is that in the
	   communicator.
	 */
	len = contig_size * count;
	MPID_SsendContig( comm_ptr, buf, len, src_lrank, tag, context_id, 
		 dest_grank, msgrep, error_code );
	return;
    }

    mybuf = 0;
    MPID_PackMessage( buf, count, dtype_ptr, comm_ptr, dest_grank, 
		      msgrep, msgact, (void **)&mybuf, &len, error_code );
    if (*error_code) return;

    MPID_SsendContig( comm_ptr, mybuf, len, src_lrank, tag, context_id, 
		     dest_grank, msgrep, error_code );
    if (mybuf) {
	FREE( mybuf );
    }
}

/*
 * Noncontiguous datatype issend
 * This is a simple implementation.  Note that in the rendezvous case, the
 * "pack" could be deferred until the "ok to send" message arrives.  To
 * implement this, the individual "send" routines would have to know how to
 * handle general datatypes.  We'll leave that for later.
 */
void MPID_IssendDatatype( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	int src_lrank, 
	int tag, 
	int context_id, 
	int dest_grank, 
	MPI_Request request, 
	int *error_code )
{
    int             len, contig_size;
    char            *mybuf;
    MPID_Msgrep_t   msgrep = MPID_MSGREP_RECEIVER;
    MPID_Msg_pack_t msgact = MPID_MSG_OK;

    /*
     * Alogrithm:
     * First, see if we can just send the data (contiguous or, for
     * heterogeneous, packed).
     * Otherwise, 
     * Create a local buffer, use SendContig, and then free the buffer.
     */

    MPID_DO_HETERO(MPID_Msg_rep( comm_ptr, dest_grank, dtype_ptr, 
				 &msgrep, &msgact ));
    contig_size = MPIR_GET_DTYPE_SIZE(datatype,dtype_ptr);
    if (contig_size > 0 
	MPID_DO_HETERO(&& msgact == MPID_MSG_OK)) {
	/* Just drop through into the contiguous send routine 
	   For packed data, the representation format is that in the
	   communicator.
	 */
	len = contig_size * count;
	MPID_IssendContig( comm_ptr, buf, len, src_lrank, tag, context_id, 
			  dest_grank, msgrep, request, error_code );
	return;
    }

    mybuf = 0;
    MPID_PackMessage( buf, count, dtype_ptr, comm_ptr, dest_grank, 
		      msgrep, msgact, (void **)&mybuf, &len, error_code );
    if (*error_code) return;

    MPID_IssendContig( comm_ptr, mybuf, len, src_lrank, tag, context_id, 
		      dest_grank, msgrep, request, error_code );
    if (request->shandle.is_complete) {
	if (mybuf) { FREE( mybuf ); }
	}
    else {
	request->shandle.start  = mybuf;
	request->shandle.finish = MPID_PackMessageFree;
    }

    /* Note that, from the users perspective, the message is now complete
       (!) since the data is out of the input buffer (!) */
}
