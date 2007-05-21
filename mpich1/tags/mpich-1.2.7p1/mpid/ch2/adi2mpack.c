/*
 *  $Id: adi2mpack.c,v 1.3 2001/11/12 23:05:09 ashton Exp $
 *
 *  (C) 1995 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
/* For MPIR_Type_free */
#include "mpipt2pt.h"

/* 
 * This file contains the routines to support noncontiguous and heterogeneous
 * datatypes by providing routines to pack and unpack messages 
 */

void MPID_PackMessage( 
	void *src, 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	int dest_grank, 
	MPID_Msgrep_t msgrep, 
	MPID_Msg_pack_t msgact, 
	void **mybuf, 
	int *mylen, 
	int *error_code )
{
    int position = 0;

    /* Allocate the buffer */
    MPID_Pack_size( count, dtype_ptr, msgact, mylen );

    if (*mylen > 0) {
	*mybuf = (void *)MALLOC( *mylen );
	if (!*mybuf) {
	    *error_code = MPI_ERR_INTERN;
	    return;
	}
	MPID_Pack( src, count, dtype_ptr, *mybuf, *mylen, &position, 
		   comm_ptr, dest_grank, msgrep, msgact, error_code );
	*mylen = position;
    }
    else {
	*mylen	    = 0;
	*error_code = 0;
    }
}

int MPID_PackMessageFree( 
	MPIR_SHANDLE *shandle)
{
    if (shandle->start) {
	FREE( shandle->start );
	shandle->start = 0;
    }
    shandle->finish = 0;
    return 0;
}

void MPID_UnpackMessageSetup( 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	int dest_grank, 
	MPID_Msgrep_t msgrep, 
	void **mybuf, 
	int *mylen, 
	int *error_code )
{
    /* Get "max" size for message */
    MPID_Pack_size( count, dtype_ptr, MPID_MSG_XDR, mylen );

    /* Allocate the buffer */
    if (*mylen) {
	*mybuf = (void *)MALLOC( *mylen );
	if (!*mybuf) {
	    *error_code = MPI_ERR_INTERN;
	    return;
	}
    } 
    else {
	*mybuf = 0;
	*error_code = 0;
    }
}

int MPID_UnpackMessageComplete( 
	MPIR_RHANDLE *rhandle)
{
    int in_position = 0, out_position = 0;

    DEBUG_PRINT_MSG("R Unpacking into user's buffer");
    /* Unpack from buf into user's buffer */

    MPID_Unpack( rhandle->buf, rhandle->s.count, rhandle->msgrep, 
		 &in_position, 
		 rhandle->start, rhandle->count, rhandle->datatype, 
		 &out_position, rhandle->comm, rhandle->s.MPI_SOURCE, 
		 &rhandle->s.MPI_ERROR );
    
    rhandle->s.count = out_position;

    /* Free the space */
    if (rhandle->buf) {
	FREE( rhandle->buf );
    }

    /* Decrement ref to datatype */
    MPIR_Type_free( &rhandle->datatype );
    rhandle->finish = 0;
    return 0;
}
