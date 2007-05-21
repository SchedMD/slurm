/*
 *  $Id: scatterv.c,v 1.14 2002/02/21 16:07:41 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Scatterv = PMPI_Scatterv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Scatterv  MPI_Scatterv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Scatterv as PMPI_Scatterv
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

MPI_Scatterv - Scatters a buffer in parts to all tasks in a group

Input Parameters:
+ sendbuf - address of send buffer (choice, significant only at 'root') 
. sendcounts - integer array (of length group size) 
specifying the number of elements to send to each processor  
. displs - integer array (of length group size). Entry 
 'i'  specifies the displacement (relative to sendbuf  from
which to take the outgoing data to process  'i' 
. sendtype - data type of send buffer elements (handle) 
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
int MPI_Scatterv ( 
	void *sendbuf, 
	int *sendcnts, 
	int *displs, 
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int recvcnt,  
	MPI_Datatype recvtype, 
	int root, 
	MPI_Comm comm )
{
  int        mpi_errno = MPI_SUCCESS;
  int        rank;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *stype_ptr = 0, *rtype_ptr;
  MPIR_ERROR_DECL;
  static char myname[] = "MPI_SCATTERV";

  TR_PUSH(myname);
  comm_ptr = MPIR_GET_COMM_PTR(comm);
  rtype_ptr = MPIR_GET_DTYPE_PTR(recvtype);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr, myname);
  MPIR_TEST_COUNT(recvcnt);
  MPIR_TEST_DTYPE(recvtype,rtype_ptr,comm_ptr, myname );
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  /* Significant only at root */
  (void) MPIR_Comm_rank ( comm_ptr, &rank );
  if (rank == root) {
      stype_ptr = MPIR_GET_DTYPE_PTR(sendtype);
#ifndef MPIR_NO_ERROR_CHECKING
      MPIR_TEST_DTYPE(sendtype,stype_ptr,comm_ptr, myname );
      if (mpi_errno)
	  return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif
  }
  
  MPIR_ERROR_PUSH(comm_ptr);
  mpi_errno = comm_ptr->collops->Scatterv( sendbuf, sendcnts, displs, 
					   stype_ptr, 
					   recvbuf, recvcnt,  rtype_ptr, 
					   root, comm_ptr );
  MPIR_ERROR_POP(comm_ptr);
  TR_POP;
  MPIR_RETURN(comm_ptr,mpi_errno,myname);
}
