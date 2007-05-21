/*
 *  $Id: ssend.c,v 1.10 2001/11/14 20:10:03 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Ssend = PMPI_Ssend
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Ssend  MPI_Ssend
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Ssend as PMPI_Ssend
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/*@
    MPI_Ssend - Basic synchronous send

Input Parameters:
+ buf - initial address of send buffer (choice) 
. count - number of elements in send buffer (nonnegative integer) 
. datatype - datatype of each send buffer element (handle) 
. dest - rank of destination (integer) 
. tag - message tag (integer) 
- comm - communicator (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_TAG
.N MPI_ERR_RANK
@*/
int MPI_Ssend( void *buf, int count, MPI_Datatype datatype, 
	       int dest, int tag, MPI_Comm comm )
{
    int         mpi_errno = MPI_SUCCESS;
    MPI_Request handle;
    MPI_Status  status;
    MPIR_ERROR_DECL;
    struct MPIR_COMMUNICATOR *comm_ptr;
    static char myname[] = "MPI_SSEND";
    
    if (dest != MPI_PROC_NULL)
    {
	comm_ptr = MPIR_GET_COMM_PTR(comm);
	MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,"MPI_SSEND");

	MPIR_ERROR_PUSH(comm_ptr);
	MPIR_CALL_POP(MPI_Issend( buf, count, datatype, dest, tag, comm, 
			        &handle ),comm_ptr,myname);

	MPIR_CALL_POP(MPI_Wait( &handle, &status ),comm_ptr,myname);
	MPIR_ERROR_POP(comm_ptr);
    }
    return mpi_errno;
}
