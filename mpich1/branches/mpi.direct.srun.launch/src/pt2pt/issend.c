/*
 *  $Id: issend.c,v 1.9 2001/11/14 20:09:59 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Issend = PMPI_Issend
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Issend  MPI_Issend
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Issend as PMPI_Issend
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
    MPI_Issend - Starts a nonblocking synchronous send

Input Parameters:
+ buf - initial address of send buffer (choice) 
. count - number of elements in send buffer (integer) 
. datatype - datatype of each send buffer element (handle) 
. dest - rank of destination (integer) 
. tag - message tag (integer) 
- comm - communicator (handle) 

Output Parameter:
. request - communication request (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_TAG
.N MPI_ERR_RANK
.N MPI_ERR_EXHAUSTED
@*/
int MPI_Issend( void *buf, int count, MPI_Datatype datatype, int dest, 
		int tag, MPI_Comm comm, MPI_Request *request )
{
    struct MPIR_COMMUNICATOR *comm_ptr;
    struct MPIR_DATATYPE     *dtype_ptr;
    MPIR_SHANDLE             *shandle;
    static char myname[] = "MPI_ISSEND";
    int mpi_errno = MPI_SUCCESS;

    TR_PUSH(myname);

    comm_ptr = MPIR_GET_COMM_PTR(comm);
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

    dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);
    MPIR_TEST_DTYPE(datatype,dtype_ptr,comm_ptr,myname);

#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_COUNT(count);
    MPIR_TEST_SEND_TAG(tag);
    MPIR_TEST_SEND_RANK(comm_ptr,dest);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

    MPIR_ALLOCFN(shandle,MPID_SendAlloc,
	       comm_ptr,MPI_ERR_EXHAUSTED,myname );
    *request = (MPI_Request)shandle;
    MPID_Request_init( shandle, MPIR_SEND );

    MPIR_REMEMBER_SEND( shandle, buf, count, datatype, dest, tag, comm_ptr);

    if (dest == MPI_PROC_NULL) {
	shandle->is_complete = 1;
	return MPI_SUCCESS;
    }
    /* This COULD test for the contiguous homogeneous case first .... */
    MPID_IssendDatatype( comm_ptr, buf, count, dtype_ptr, 
			 comm_ptr->local_rank, tag, 
			 comm_ptr->send_context, 
			 comm_ptr->lrank_to_grank[dest], 
			 *request, &mpi_errno );
    if (mpi_errno) return MPIR_ERROR( comm_ptr, mpi_errno, myname );
    TR_POP;
    return MPI_SUCCESS;
}
