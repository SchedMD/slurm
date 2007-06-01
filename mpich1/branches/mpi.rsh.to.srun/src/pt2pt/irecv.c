/*
 *  $Id: irecv.c,v 1.9 2001/11/14 20:09:58 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Irecv = PMPI_Irecv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Irecv  MPI_Irecv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Irecv as PMPI_Irecv
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
    MPI_Irecv - Begins a nonblocking receive

Input Parameters:
+ buf - initial address of receive buffer (choice) 
. count - number of elements in receive buffer (integer) 
. datatype - datatype of each receive buffer element (handle) 
. source - rank of source (integer) 
. tag - message tag (integer) 
- comm - communicator (handle) 

Output Parameter:
. request - communication request (handle) 

.N fortran
@*/
int MPI_Irecv( void *buf, int count, MPI_Datatype datatype, int source, 
	       int tag, MPI_Comm comm, MPI_Request *request )
{
    struct MPIR_COMMUNICATOR *comm_ptr;
    struct MPIR_DATATYPE *dtype_ptr;
    MPIR_RHANDLE *rhandle;
    static char myname[] = "MPI_IRECV";
    int mpi_errno = MPI_SUCCESS;

    comm_ptr = MPIR_GET_COMM_PTR(comm);
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

    dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);
    MPIR_TEST_DTYPE(datatype,dtype_ptr,comm_ptr,myname);

#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_COUNT(count);
    MPIR_TEST_RECV_TAG(tag);
    MPIR_TEST_RECV_RANK(comm_ptr,source);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

    MPIR_ALLOCFN(rhandle,MPID_RecvAlloc,comm_ptr,
	       MPI_ERR_EXHAUSTED,myname);
    MPID_Request_init( rhandle, MPIR_RECV );
    *request = (MPI_Request) rhandle;

    if (source == MPI_PROC_NULL) {
	rhandle->s.MPI_TAG    = MPI_ANY_TAG;
	rhandle->s.MPI_SOURCE = MPI_PROC_NULL;
	rhandle->s.count      = 0;
	rhandle->is_complete  = 1;
	return MPI_SUCCESS;
    }
    MPID_IrecvDatatype( comm_ptr, buf, count, dtype_ptr, source, tag, 
			comm_ptr->recv_context, *request, &mpi_errno );
    if (mpi_errno) return MPIR_ERROR( comm_ptr, mpi_errno, myname );
    return MPI_SUCCESS;
}

