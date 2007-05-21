/*
 *  $Id: adi2ssend.c,v 1.2 2001/11/12 23:09:30 ashton Exp $
 *
 *  (C) 1995 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"

/***************************************************************************/
/*
 * Multi-protocol, Multi-device support for 2nd generation ADI.
 * We start with support for blocking, contiguous sends.
 * Note the 'msgrep' field; this gives a hook for heterogeneous systems
 * which can be ignored on homogeneous systems.
 * 
 * For the synchronous send, we always use a Rendezvous send.
 */
/***************************************************************************/

void MPID_SsendContig( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int len, 
	int src_lrank, 
	int tag, 
	int context_id, 
	int dest_grank, 
	MPID_Msgrep_t msgrep, 
	int *error_code )
{
    MPID_Device *dev = MPID_devset->dev[dest_grank];

    /* The one error test that makes sense here */
    if (buf == 0 && len > 0) {
	*error_code = MPI_ERR_BUFFER;
	return;
    }
    *error_code = (*(dev->rndv->send))( buf, len, src_lrank, tag, context_id, 
					dest_grank, msgrep );
}

void MPID_IssendContig( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int len, 
	int src_lrank, 
	int tag, 
	int context_id, 
	int dest_grank, 
	MPID_Msgrep_t msgrep, 
	MPI_Request request, 
	int *error_code )
{
    MPID_Device *dev = MPID_devset->dev[dest_grank];

    /* The one error test that makes sense here */
    if (buf == 0 && len > 0) {
	*error_code = MPI_ERR_BUFFER;
	return;
    }
    *error_code = (*(dev->rndv->isend))( buf, len, src_lrank, tag, context_id, 
					 dest_grank, msgrep, 
					 (MPIR_SHANDLE *)request );
}
