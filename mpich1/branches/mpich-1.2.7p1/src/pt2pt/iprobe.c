/*
 *  $Id: iprobe.c,v 1.11 2004/12/07 16:39:19 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Iprobe = PMPI_Iprobe
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Iprobe  MPI_Iprobe
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Iprobe as PMPI_Iprobe
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
    MPI_Iprobe - Nonblocking test for a message

Input Parameters:
+ source - source rank, or  'MPI_ANY_SOURCE' (integer) 
. tag - tag value or  'MPI_ANY_TAG' (integer) 
- comm - communicator (handle) 

Output Parameter:
+ flag - (logical) 
- status - status object (Status) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_TAG
.N MPI_ERR_RANK

@*/
int MPI_Iprobe( int source, int tag, MPI_Comm comm, int *flag, 
		MPI_Status *status )
{
    int mpi_errno = MPI_SUCCESS;
    struct MPIR_COMMUNICATOR *comm_ptr;
    static char myname[] = "MPI_IPROBE";

    TR_PUSH(myname);

    comm_ptr = MPIR_GET_COMM_PTR(comm);

#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
    MPIR_TEST_RECV_TAG(tag);
    MPIR_TEST_RECV_RANK(comm_ptr,source);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

    if (source == MPI_PROC_NULL) {
	if (status) {
	    status->MPI_SOURCE = MPI_PROC_NULL;
	    status->MPI_TAG	   = MPI_ANY_TAG;
	    MPID_ZERO_STATUS_COUNT(status);
	}
	return MPI_SUCCESS;
	}
    MPID_Iprobe( comm_ptr, tag, comm_ptr->recv_context, source, flag, 
		 &mpi_errno, status );
    TR_POP;
    MPIR_RETURN( comm_ptr, mpi_errno, myname );
}
