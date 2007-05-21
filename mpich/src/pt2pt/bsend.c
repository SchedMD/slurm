/*
 *  $Id: bsend.c,v 1.11 2001/11/14 20:09:55 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Bsend = PMPI_Bsend
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Bsend  MPI_Bsend
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Bsend as PMPI_Bsend
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "reqalloc.h"

/*@
    MPI_Bsend - Basic send with user-specified buffering

Input Parameters:
+ buf - initial address of send buffer (choice) 
. count - number of elements in send buffer (nonnegative integer) 
. datatype - datatype of each send buffer element (handle) 
. dest - rank of destination (integer) 
. tag - message tag (integer) 
- comm - communicator (handle) 

Notes:
This send is provided as a convenience function; it allows the user to 
send messages without worring about where they are buffered (because the
user `must` have provided buffer space with 'MPI_Buffer_attach').  

In deciding how much buffer space to allocate, remember that the buffer space 
is not available for reuse by subsequent 'MPI_Bsend's unless you are certain 
that the message
has been received (not just that it should have been received).  For example,
this code does not allocate enough buffer space
.vb
    MPI_Buffer_attach( b, n*sizeof(double) + MPI_BSEND_OVERHEAD );
    for (i=0; i<m; i++) {
        MPI_Bsend( buf, n, MPI_DOUBLE, ... );
    }
.ve
because only enough buffer space is provided for a single send, and the
loop may start a second 'MPI_Bsend' before the first is done making use of the
buffer.  

In C, you can 
force the messages to be delivered by 
.vb
    MPI_Buffer_detach( &b, &n );
    MPI_Buffer_attach( b, n );
.ve
(The 'MPI_Buffer_detach' will not complete until all buffered messages are 
delivered.)

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_RANK
.N MPI_ERR_TAG

.seealso: MPI_Buffer_attach, MPI_Ibsend, MPI_Bsend_init
@*/
int MPI_Bsend( 
	void *buf, 
	int count, 
	MPI_Datatype datatype, 
	int dest, 
	int tag, 
	MPI_Comm comm )
{
    MPI_Request handle;
    MPI_Status  status;
    int         mpi_errno = MPI_SUCCESS;
    struct MPIR_COMMUNICATOR *comm_ptr;
    MPIR_ERROR_DECL;
    static char myname[] = "MPI_BSEND";

    TR_PUSH(myname);

    if (dest != MPI_PROC_NULL)
    {
        /* We should let Ibsend find the errors, but
	   we will soon add a special case for faster Bsend and we'll
	   need these tests then 
	 */

	comm_ptr = MPIR_GET_COMM_PTR(comm);
#ifndef MPIR_NO_ERROR_CHECKING
	MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

	MPIR_TEST_COUNT(count);
	MPIR_TEST_SEND_TAG(tag);
	MPIR_TEST_SEND_RANK(comm_ptr,dest);
	if (mpi_errno)
	    return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif
	/* 
	   ? BsendDatatype?
	   MPID_BsendContig( comm, buf, len, src_lrank, tag, context_id,
	   dest_grank, msgrep, &mpi_errno );
	   if (!mpi_errno) return MPI_SUCCESS;
	   if (mpi_errno != MPIR_ERR_MAY_BLOCK) 
	   return MPIR_ERROR( comm, mpi_errno, myname );
	 */
	MPIR_ERROR_PUSH(comm_ptr);
	/* We don't use MPIR_CALL_POP so that we can free the handle */
	handle = MPI_REQUEST_NULL;
	if ((mpi_errno = MPI_Ibsend( buf, count, datatype, dest, tag, comm, 
				  &handle ))) {
	    MPIR_ERROR_POP(comm_ptr);
	    if (handle != MPI_REQUEST_NULL) 
		MPID_SendFree( handle );
	    return MPIR_ERROR(comm_ptr,mpi_errno,myname);
	}

	/* This Wait only completes the transfer of data into the 
	   buffer area.  The test/wait in util/bsendutil.c completes
	   the actual transfer 
	 */
	MPIR_CALL_POP(MPI_Wait( &handle, &status ),comm_ptr,myname);
	MPIR_ERROR_POP(comm_ptr);
    }
    TR_POP;
    return mpi_errno;
}



