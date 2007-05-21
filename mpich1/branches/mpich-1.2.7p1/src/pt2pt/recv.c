/*
 *  $Id: recv.c,v 1.11 2004/02/24 15:09:05 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Recv = PMPI_Recv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Recv  MPI_Recv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Recv as PMPI_Recv
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
    MPI_Recv - Basic receive

Output Parameters:
+ buf - initial address of receive buffer (choice) 
- status - status object (Status) 

Input Parameters:
+ count - maximum number of elements in receive buffer (integer) 
. datatype - datatype of each receive buffer element (handle) 
. source - rank of source (integer) 
. tag - message tag (integer) 
- comm - communicator (handle) 

Notes:
The 'count' argument indicates the maximum length of a message; the actual 
number can be determined with 'MPI_Get_count'.  

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_TYPE
.N MPI_ERR_COUNT
.N MPI_ERR_TAG
.N MPI_ERR_RANK

@*/
int MPI_Recv( void *buf, int count, MPI_Datatype datatype, int source, 
	      int tag, MPI_Comm comm, MPI_Status *status )
{
    struct MPIR_COMMUNICATOR *comm_ptr;
    struct MPIR_DATATYPE     *dtype_ptr;
    static char myname[] = "MPI_RECV";
    int         mpi_errno = MPI_SUCCESS;
    /* 
       Because this is a very common routine, we show how it can be
       optimized to be run "inline"; In addition, this lets us exploit
       features in the ADI to simplify the execution of blocking receive
       calls.
     */
    if (source != MPI_PROC_NULL)
    {
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

	MPID_RecvDatatype( comm_ptr, buf, count, dtype_ptr, source, tag, 
			   comm_ptr->recv_context, status, &mpi_errno );
	MPIR_RETURN(comm_ptr, mpi_errno, myname );
    }
    else {
	if (status != MPI_STATUS_IGNORE) {
	    /* See MPI standard section 3.11 */
	    MPID_ZERO_STATUS_COUNT(status);
	    status->MPI_SOURCE     = MPI_PROC_NULL;
	    status->MPI_TAG        = MPI_ANY_TAG;
	}
    }
    return MPI_SUCCESS;
}

