/*
 *  $Id: bcast.c,v 1.9 2001/11/14 19:50:12 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Bcast = PMPI_Bcast
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Bcast  MPI_Bcast
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Bcast as PMPI_Bcast
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

/*@

MPI_Bcast - Broadcasts a message from the process with rank "root" to
            all other processes of the group. 

Input/output Parameters:
+ buffer - starting address of buffer (choice) 
. count - number of entries in buffer (integer) 
. datatype - data type of buffer (handle) 
. root - rank of broadcast root (integer) 
- comm - communicator (handle) 

Algorithm:  
If the underlying device does not take responsibility, this function
uses a tree-like algorithm to broadcast the message to blocks of
processes.  A linear algorithm is then used to broadcast the message
from the first process in a block to all other processes.
'MPIR_BCAST_BLOCK_SIZE' determines the size of blocks.  If this is set
to 1, then this function is equivalent to using a pure tree algorithm.
If it is set to the size of the group or greater, it is a pure linear
algorithm.  The value should be adjusted to determine the most
efficient value on different machines.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_BUFFER
.N MPI_ERR_ROOT
@*/
int MPI_Bcast ( void *buffer, int count, MPI_Datatype datatype, int root, 
		MPI_Comm comm )
{
    int mpi_errno = MPI_SUCCESS;
    struct MPIR_COMMUNICATOR *comm_ptr;
    struct MPIR_DATATYPE     *dtype_ptr;
    static char myname[] = "MPI_BCAST";
    MPIR_ERROR_DECL;

    TR_PUSH(myname)
    comm_ptr = MPIR_GET_COMM_PTR(comm);

    dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);

    /* Check for invalid arguments */
#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
    MPIR_TEST_DTYPE(datatype,dtype_ptr,comm_ptr,myname);
    /* If an intercomm broadcast, the root can also be MPI_ROOT or 
       MPI_PROC_NULL */
    if (root < MPI_ROOT || root >= comm_ptr->np) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ROOT, MPIR_ERR_DEFAULT, myname, 
				     (char *)0, (char *)0, root );
    }
    MPIR_TEST_COUNT(count);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

    /* See the overview in Collection Operations for why this is ok */
    if (count == 0) return MPI_SUCCESS;
    
    MPIR_ERROR_PUSH(comm_ptr);
    mpi_errno = comm_ptr->collops->Bcast(buffer, count, dtype_ptr, root, 
					 comm_ptr);
    MPIR_ERROR_POP(comm_ptr);
    TR_POP;
    MPIR_RETURN(comm_ptr,mpi_errno,myname);
}
