/*
 *  $Id: rsend_init.c,v 1.9 2001/11/14 20:10:01 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Rsend_init = PMPI_Rsend_init
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Rsend_init  MPI_Rsend_init
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Rsend_init as PMPI_Rsend_init
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
    MPI_Rsend_init - Builds a handle for a ready send

Input Parameters:
+ buf - initial address of send buffer (choice) 
. count - number of elements sent (integer) 
. datatype - type of each element (handle) 
. dest - rank of destination (integer) 
. tag - message tag (integer) 
- comm - communicator (handle) 

Output Parameter:
. request - communication request (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_RANK
.N MPI_ERR_TAG
.N MPI_ERR_COMM
.N MPI_ERR_EXHAUSTED

.seealso: MPI_Start, MPI_Request_free, MPI_Send_init
@*/
int MPI_Rsend_init( void *buf, int count, MPI_Datatype datatype, int dest, 
		    int tag, MPI_Comm comm, MPI_Request *request )
{
    int mpi_errno = MPI_SUCCESS;
    struct MPIR_DATATYPE *dtype_ptr;
    struct MPIR_COMMUNICATOR *comm_ptr;
    static char myname[] = "MPI_RSEND_INIT";
    MPIR_PSHANDLE *shandle;

    TR_PUSH(myname);

    comm_ptr = MPIR_GET_COMM_PTR(comm);
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_COUNT(count);
    MPIR_TEST_SEND_TAG(tag);
    MPIR_TEST_SEND_RANK(comm_ptr,dest);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

    /* This is IDENTICAL to the create_send code except for the send
       function */
    MPIR_ALLOCFN(shandle,MPID_PSendAlloc,
	       comm_ptr,MPI_ERR_EXHAUSTED, "MPI_RSEND_INIT" );
    *request = (MPI_Request)shandle;
    MPID_Request_init( &(shandle->shandle), MPIR_PERSISTENT_SEND );
    /* Save the information about the operation, being careful with
       ref-counted items */
    dtype_ptr   = MPIR_GET_DTYPE_PTR(datatype);
    MPIR_TEST_DTYPE(datatype,dtype_ptr,comm_ptr,myname);
    MPIR_REF_INCR(dtype_ptr);
    shandle->perm_datatype = dtype_ptr;
    shandle->perm_tag	   = tag;
    shandle->perm_dest	   = dest;
    shandle->perm_count	   = count;
    shandle->perm_buf	   = buf;
    MPIR_REF_INCR(comm_ptr);
    shandle->perm_comm	   = comm_ptr;
    shandle->active	   = 0;
    shandle->send          = MPID_IrsendDatatype;
    /* dest of MPI_PROC_NULL handled in start */

    TR_POP;
    return MPI_SUCCESS;
}
