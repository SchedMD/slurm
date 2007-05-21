/*
 *  $Id: create_recv.c,v 1.9 2001/11/14 20:09:57 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Recv_init = PMPI_Recv_init
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Recv_init  MPI_Recv_init
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Recv_init as PMPI_Recv_init
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
    MPI_Recv_init - Builds a handle for a receive

Input Parameters:
+ buf - initial address of receive buffer (choice) 
. count - number of elements received (integer) 
. datatype - type of each element (handle) 
. source - rank of source or 'MPI_ANY_SOURCE' (integer) 
. tag - message tag or 'MPI_ANY_TAG' (integer) 
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

.seealso: MPI_Start, MPI_Request_free
@*/
int MPI_Recv_init( void *buf, int count, MPI_Datatype datatype, int source, 
		   int tag, MPI_Comm comm, MPI_Request *request )
{
    int         mpi_errno = MPI_SUCCESS;
    struct MPIR_DATATYPE *dtype_ptr;
    struct MPIR_COMMUNICATOR *comm_ptr;
    static char myname[] = "MPI_RECV_INIT";
    MPIR_PRHANDLE *rhandle;

    TR_PUSH(myname);

    comm_ptr = MPIR_GET_COMM_PTR(comm);
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
    
    dtype_ptr   = MPIR_GET_DTYPE_PTR(datatype);
    MPIR_TEST_DTYPE(datatype,dtype_ptr,comm_ptr,myname);

#ifndef MPIR_NO_ERROR_CHECKING
	MPIR_TEST_COUNT(count);
	MPIR_TEST_RECV_TAG(tag);
	MPIR_TEST_RECV_RANK(comm_ptr,source);
	if (mpi_errno)
	    return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

    MPIR_ALLOCFN(rhandle,MPID_PRecvAlloc,
	       comm_ptr,MPI_ERR_EXHAUSTED,myname );
    *request = (MPI_Request)rhandle;
    MPID_Request_init( &(rhandle->rhandle), MPIR_PERSISTENT_RECV );
    /* Save the information about the operation, being careful with
       ref-counted items */
    MPIR_REF_INCR(dtype_ptr);
    rhandle->perm_datatype = dtype_ptr;
    rhandle->perm_tag	   = tag;
    rhandle->perm_source   = source;
    rhandle->perm_count	   = count;
    rhandle->perm_buf	   = buf;
    MPIR_REF_INCR(comm_ptr);
    rhandle->perm_comm	   = comm_ptr;
    rhandle->active	   = 0;
    /* dest of MPI_PROC_NULL handled in start */

    TR_POP;
    return MPI_SUCCESS;
}
