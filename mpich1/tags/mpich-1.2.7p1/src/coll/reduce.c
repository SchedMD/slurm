/*
 *  $Id: reduce.c,v 1.9 2002/02/19 14:47:32 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Reduce = PMPI_Reduce
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Reduce  MPI_Reduce
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Reduce as PMPI_Reduce
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "coll.h"
#include "mpiops.h"

/*@

MPI_Reduce - Reduces values on all processes to a single value

Input Parameters:
+ sendbuf - address of send buffer (choice) 
. count - number of elements in send buffer (integer) 
. datatype - data type of elements of send buffer (handle) 
. op - reduce operation (handle) 
. root - rank of root process (integer) 
- comm - communicator (handle) 

Output Parameter:
. recvbuf - address of receive buffer (choice, 
significant only at 'root') 

Algorithm:
This implementation currently uses a simple tree algorithm.

.N fortran

.N collops

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_BUFFER
.N MPI_ERR_BUFFER_ALIAS
@*/
int MPI_Reduce ( void *sendbuf, void *recvbuf, int count, 
		 MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm )
{
    int mpi_errno = MPI_SUCCESS;
    struct MPIR_COMMUNICATOR *comm_ptr;
    struct MPIR_DATATYPE     *dtype_ptr;
    MPIR_ERROR_DECL;
    static char myname[] = "MPI_REDUCE";

    TR_PUSH(myname);

    comm_ptr = MPIR_GET_COMM_PTR(comm);

    dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);

    /* Check for invalid arguments */
#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
    MPIR_TEST_DTYPE(datatype,dtype_ptr,comm_ptr,myname);
    MPIR_TEST_ALIAS(sendbuf,recvbuf);
    MPIR_TEST_COUNT(count);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif
    MPIR_ERROR_PUSH(comm_ptr);
    mpi_errno = comm_ptr->collops->Reduce(sendbuf, recvbuf, count, dtype_ptr, 
					  op, root, comm_ptr );
    MPIR_ERROR_POP(comm_ptr);
    TR_POP;
    MPIR_RETURN(comm_ptr,mpi_errno,myname);
}
