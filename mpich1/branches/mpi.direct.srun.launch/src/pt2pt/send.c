/*
 *  $Id: send.c,v 1.9 2001/11/14 20:10:02 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Send = PMPI_Send
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Send  MPI_Send
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Send as PMPI_Send
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/* 
   Because this is a very common routine, we show how it can be
   optimized to be run "inline"; In addition, this lets us exploit
   features in the ADI to simplify the execution of blocking send 
   calls.
 */

/*@
    MPI_Send - Performs a basic send

Input Parameters:
+ buf - initial address of send buffer (choice) 
. count - number of elements in send buffer (nonnegative integer) 
. datatype - datatype of each send buffer element (handle) 
. dest - rank of destination (integer) 
. tag - message tag (integer) 
- comm - communicator (handle) 

Notes:
This routine may block until the message is received.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_TAG
.N MPI_ERR_RANK

.seealso: MPI_Isend, MPI_Bsend
@*/
int MPI_Send( void *buf, int count, MPI_Datatype datatype, int dest, 
	      int tag, MPI_Comm comm )
{
    int          mpi_errno = MPI_SUCCESS;
    struct MPIR_COMMUNICATOR *comm_ptr;
    struct MPIR_DATATYPE *dtype_ptr;
    static char myname[] = "MPI_SEND";

    if (dest == MPI_PROC_NULL) 
	return mpi_errno;

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

    if (dest == MPI_PROC_NULL) return MPI_SUCCESS;

    /* This COULD test for the contiguous homogeneous case first .... */
    MPID_SendDatatype( comm_ptr, buf, count, dtype_ptr, comm_ptr->local_rank, 
		       tag, 
		       comm_ptr->send_context, comm_ptr->lrank_to_grank[dest], 
		       &mpi_errno );
    MPIR_RETURN(comm_ptr, mpi_errno, myname );
}


