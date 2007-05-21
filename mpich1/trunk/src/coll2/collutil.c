/*
 *  $Id: collutil.c,v 1.3 2000/07/18 20:30:19 gropp Exp $
 *
 *  (C) 2000 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
/*
 * This file provides the following functions to convert messages into
 * a single, contiguous buffer.  These are still undergoing development.
 * This approach allows an implementation on a homogenous platform to 
 * avoid data copies for contiguous buffers, yet work correctly on both
 * heterogeneous platforms and non-contiguous data.  The assumption in this 
 * is that most messages will be sent with contiguous data.  
 * 
 * This isn't quite right because we'd really like to get partial buffers
 * if possible.  For that, we'll eventually add a max-allocate to GetBuffer,
 * and allow GetBuffer to be called multiple times, carrying a position 
 * indicator (for incremental conversion of a datatype).  We may also
 * want to request particular byte-ranges from the canonical representation,
 * rather than just a stream.
 */

void *MPIR_GetSendBuffer( void *buf, int count, MPI_Datatype dtype, 
			  MPI_Comm comm,
			  int *len, MPI_Datatype *out_type, int *new_buf )
{
    int not_hetero = 1;
    struct MPIR_DATATYPE *dtype_ptr;
    int size;
    void *outbuf;
#ifdef MPID_HAS_HETERO 
    {
	struct MPIR_COMMUNICATOR *comm_ptr = MPIR_GET_COMM_PTR(comm);
	/* See if we are heterogeneous in this communicator */
	if (comm_ptr->msgform != MPID_MSG_OK) not_hetero = 0;
    }
#endif    

    /* If we are contiguous and not heterogeneous, use the buffer */
    dtype_ptr = MPIR_GET_DTYPE_PTR(dtype);
    if (dtype_ptr->is_contig && not_hetero) {
	/* If type is contiguous, just return it */
	*len      = dtype_ptr->size * count;
	*out_type = MPI_BYTE;
	*new_buf  = 0;
	return buf;
    }
    
/* Otherwise, use pack, send as bytes */
    MPI_Pack_size( count, dtype, comm, &size );
    if (!(outbuf = (void *)MALLOC( size )) ) { 
	; /* error */
    }
    *new_buf  = 1;
    *len      = 0;
    MPI_Pack( buf, count, dtype, outbuf, size, len, comm );
    *out_type = MPI_PACKED;
    return outbuf;
}

/* Call for buffers that set new_buf */
void MPIR_FreeSendBuffer( void *buf, int len )
{
    FREE( (void *) buf );
}

/* Receiving is the reverse; we can't unpack until we have the data */

void *MPIR_GetRecvBuffer( void *buf, int count, MPI_Datatype dtype, 
			  MPI_Comm comm,
			  int *len, MPI_Datatype *out_type, int *new_buf )
{
    int not_hetero = 1;
    struct MPIR_DATATYPE *dtype_ptr;
    int size;
    void *outbuf;
#ifdef MPID_HAS_HETERO 
    {
	struct MPIR_COMMUNICATOR *comm_ptr = MPIR_GET_COMM_PTR(comm);
	/* See if we are heterogeneous in this communicator */
	if (comm_ptr->msgform != MPID_MSG_OK) not_hetero = 0;
    }
#endif    

    /* If we are contiguous and not heterogeneous, use the buffer */
    dtype_ptr = MPIR_GET_DTYPE_PTR(dtype);
    if (dtype_ptr->is_contig && not_hetero) {
	/* If type is contiguous, just return it */
	*len      = dtype_ptr->size * count;
	*out_type = MPI_BYTE;
	*new_buf  = 0;
	return buf;
    }
    
/* Otherwise, use pack, send as bytes */
    MPI_Pack_size( count, dtype, comm, &size );
    if (!(outbuf = (void *)MALLOC( size )) ) { 
	; /* error */
    }
    *new_buf  = 1;
    *len      = size;
    *out_type = MPI_PACKED;
    return outbuf;
}

/* Call for buffers that set new_buf */
void MPIR_FreeRecvBuffer( void *buf, int len )
{
    FREE( (void *) buf );
}

