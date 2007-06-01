/*
 *  $Id: scatter.c,v 1.11 2004/04/05 13:56:51 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Scatter = PMPI_Scatter
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Scatter  MPI_Scatter
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Scatter as PMPI_Scatter
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

MPI_Scatter - Sends data from one task to all other tasks in a group

Input Parameters:
+ sendbuf - address of send buffer (choice, significant 
only at 'root') 
. sendcount - number of elements sent to each process 
(integer, significant only at 'root') 
. sendtype - data type of send buffer elements (significant only at 'root') 
(handle) 
. recvcount - number of elements in receive buffer (integer) 
. recvtype - data type of receive buffer elements (handle) 
. root - rank of sending process (integer) 
- comm - communicator (handle) 

Output Parameter:
. recvbuf - address of receive buffer (choice) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_BUFFER
@*/
int MPI_Scatter ( 
	void *sendbuf, 
	int sendcnt, 
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int recvcnt, 
	MPI_Datatype recvtype, 
	int root, 
	MPI_Comm comm )
{
  int        mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *stype_ptr;
  struct MPIR_DATATYPE     *rtype_ptr;
  static char myname[] = "MPI_SCATTER";
  MPIR_ERROR_DECL;  

  TR_PUSH(myname);
  comm_ptr = MPIR_GET_COMM_PTR(comm);
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  /* Significant only at root */
  if (root == comm_ptr->local_rank) {
      stype_ptr = MPIR_GET_DTYPE_PTR(sendtype);
      MPIR_TEST_DTYPE(sendtype,stype_ptr,comm_ptr,myname);
  }

  rtype_ptr = MPIR_GET_DTYPE_PTR(recvtype);
  MPIR_TEST_DTYPE(recvtype,rtype_ptr,comm_ptr,myname);

  MPIR_ERROR_PUSH(comm_ptr);
  mpi_errno = comm_ptr->collops->Scatter(sendbuf, sendcnt, stype_ptr, 
				recvbuf, recvcnt, rtype_ptr, 
				root, comm_ptr );
  MPIR_ERROR_POP(comm_ptr);
  TR_POP;
  MPIR_RETURN(comm_ptr,mpi_errno,myname);
}
