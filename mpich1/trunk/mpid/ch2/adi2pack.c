/*
 *
 *
 *  (C) 1995 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include "mpid.h"
#include "mpidmpi.h"
#ifdef HAVE_WINDOWS_H
#include "mpiddev.h"
#endif

/* 
 * This file contains the interface to the Pack/Unpack routines.
 * It includes not only the ADI Pack/Unpack routines, but the
 * service routines used to implement noncontiguous operations
 *
 * For now, these use the "old" routines in src/dmpi.
 */

/* Here is a prototype for the Error message routine.  See mpiimpl.h for 
   the full version */
#if defined(USE_STDARG) && !defined(USE_OLDSTYLE_STDARG)
int MPIR_Err_setmsg( int, int, const char *, const char *, const char *, ... );
#else
int MPIR_Err_setmsg();
#endif

/*
 * Compute the msgrep and msgform for a message to OR FROM a particular
 * partner.  The partner is the GLOBAL RANK in COMM_WORLD, not the relative
 * rank in the communicator.
 */
void MPID_Msg_rep( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	int partner, 
	struct MPIR_DATATYPE *dtype_ptr, 
	MPID_Msgrep_t *msgrep, 
	MPID_Msg_pack_t *msgact )
{
#ifndef MPID_HAS_HETERO
	*msgrep = MPID_MSGREP_RECEIVER;
	*msgact = MPID_MSG_OK;
#else
    /* Check for homogeneous communicator */
    if (comm_ptr->msgform == MPID_MSG_OK) {
	*msgrep = MPID_MSGREP_RECEIVER;
	*msgact = MPID_MSG_OK;
	return;
    }

    /* Packed data is a special case (data already in correct form) */
    if (dtype_ptr->dte_type == MPIR_PACKED) {
	switch (comm_ptr->msgform) {
	case MPID_MSG_OK: *msgrep = MPID_MSGREP_RECEIVER; break;
	case MPID_MSG_SWAP: *msgrep = MPID_MSGREP_SENDER; break;
	case MPID_MSG_XDR: *msgrep = MPID_MSGREP_XDR; break;
	}
	*msgact = MPID_MSG_OK;
	return;
    }

    /* Check for collective or point-to-point */
    if (partner != MPI_ANY_SOURCE) {
	if (MPID_procinfo[MPID_MyWorldRank].byte_order == MPID_H_XDR ||
	    MPID_procinfo[partner].byte_order == MPID_H_XDR) {
	    *msgrep = MPID_MSGREP_XDR;
	    *msgact = MPID_MSG_XDR;
	}
	else if (MPID_procinfo[MPID_MyWorldRank].byte_order != 
		 MPID_procinfo[partner].byte_order) {
	    *msgrep = MPID_MSGREP_RECEIVER;
	    *msgact = MPID_MSG_SWAP;
	}
	else {
	    *msgrep = MPID_MSGREP_RECEIVER;
	    *msgact = MPID_MSG_OK;
	}
    }
    else {
	switch (comm_ptr->msgform) {
	case MPID_MSG_OK: *msgrep = MPID_MSGREP_RECEIVER; break;
	case MPID_MSG_SWAP: *msgrep = MPID_MSGREP_SENDER; break;
	case MPID_MSG_XDR: *msgrep = MPID_MSGREP_XDR; break;
	}
	*msgact = (MPID_Msg_pack_t)((comm_ptr->msgform != MPID_MSG_OK) ? 
	    MPID_MSG_XDR : MPID_MSG_OK);
    }
#endif
}

void MPID_Msg_act( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	int partner, 
	struct MPIR_DATATYPE *dtype_ptr, 
	MPID_Msgrep_t msgrep, 
	MPID_Msg_pack_t *msgact )
{
    int mpi_errno;
    switch (msgrep) {
    case MPID_MSGREP_RECEIVER:
	*msgact = MPID_MSG_OK;
	break;
    case MPID_MSGREP_XDR:
	*msgact = MPID_MSG_XDR;
       break;
    case MPID_MSGREP_SENDER:
	/* Could check here for byte swap */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_MSGREP_SENDER,
				     (char *)0, (char *)0,
		     "Error in packing data: sender format not implemented!" );
	(void) MPIR_ERROR(MPIR_COMM_WORLD,mpi_errno, (char *)0);
	*msgact = MPID_MSG_OK;
	break;	
    default:
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_MSGREP_UNKNOWN,
				     (char *)0, 
    "Error in packing data: Unknown internal message format",
    "Error in packing data: Unknown internal message format %d", (int)msgrep );
	(void) MPIR_ERROR(MPIR_COMM_WORLD,mpi_errno, (char *)0);
	
    }
}

void MPID_Pack_size( 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	MPID_Msg_pack_t msgact, 
	int *size )
{
    int contig_size;

#if defined(MPID_HAS_HETERO) && defined(HAS_XDR)
    /* Only XDR has a different length */
    if (msgact == MPID_MSG_XDR) {
	*size = MPID_Mem_XDR_Len( dtype_ptr, count );
    }
    else
#endif
    {
	contig_size = MPIR_GET_DTYPE_SIZE(datatype,dtype_ptr);
	if (contig_size > 0) 
	    *size = contig_size * count;
	else {
	    /* Our pack routine is "tight" */
	    *size = dtype_ptr->size * count;
	}
    }
}

/*
 * The meaning of the arguments here is very similar to MPI_Pack.
 * In particular, the destination buffer is specified as
 * (dest/position) with total size maxcount bytes.  The next byte
 * to write is offset by position into dest.  This is a change from
 * MPICH 1.1.0 .
 *
 * Note also that msgrep is ignored.
 */
void MPID_Pack( 
	void *src, 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	void *dest, 
	int maxcount, 
	int *position, 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	int partner, 
	MPID_Msgrep_t msgrep, 
	MPID_Msg_pack_t msgact, 
	int *error_code )
{
    int (*packcontig) (unsigned char *, unsigned char *, 
				 struct MPIR_DATATYPE *, 
				 int, void * ) = 0;
    void *packctx = 0;
    int  outlen;
    int  err;
#if defined(MPID_HAS_HETERO) && defined(HAS_XDR)
    XDR xdr_ctx;    
#endif

    /* Update the dest address.  This relies on char * being bytes. */
    if (*position) {
	dest = (void *)( (char *)dest + *position );
	maxcount -= *position;
    }
    /* 
       dest and maxcount now specify the REMAINING space in the buffer.
       Position is left unchanged because this code increments it 
       relatively (i.e., it adds the number of bytes written to dest).
     */
#ifdef MPID_HAS_HETERO
    switch (msgact) {
    case MPID_MSG_XDR:
#ifdef HAS_XDR
	MPID_Mem_XDR_Init( dest, maxcount, XDR_ENCODE, &xdr_ctx );
	packctx = (void *)&xdr_ctx;
	packcontig = MPID_Type_XDR_encode;
#else
	*error_code = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_MSGREP_UNKNOWN,
				       (char *)0, (char *)0,
		     "Error in packing data: xdr format not implemented!" );
#endif
	break;
    case MPID_MSG_SWAP:
	packcontig = MPID_Type_swap_copy;
	break;
    case MPID_MSG_OK:
	/* use default routines */
	break;
    }
#endif
    outlen = 0;
    err = MPIR_Pack2( src, count, maxcount, dtype_ptr, packcontig, packctx, 
			      dest, &outlen, position );
    if (err) *error_code = err;

    /* If the CHANGE IN position is > maxcount, then an error has occurred.
       We need to include maxcount in the call to MPIR_Pack2. */
#if defined(MPID_HAS_HETERO) && defined(HAS_XDR)
    if (packcontig == MPID_Type_XDR_encode)
	MPID_Mem_XDR_Free( &xdr_ctx );
#endif    
}

void MPID_Unpack( 
	void *src, 
	int maxcount, 
	MPID_Msgrep_t msgrep, 
	int *in_position, 
	void *dest, 
	int count, 
	struct MPIR_DATATYPE *dtype_ptr, 
	int *out_position,
	struct MPIR_COMMUNICATOR *comm_ptr, 
	int partner, 
	int *error_code )
{
    int act_len = 0;
    int dest_len = 0;
    int err;

    err = MPIR_Unpack( comm_ptr, (char *)src + *in_position, 
		       maxcount - *in_position, count,  dtype_ptr, 
		       msgrep, dest, &act_len, &dest_len );
    /* Be careful not to set MPI_SUCCESS over another error code */
    if (err) *error_code = err;
    *in_position += act_len;
    *out_position += dest_len;
}

