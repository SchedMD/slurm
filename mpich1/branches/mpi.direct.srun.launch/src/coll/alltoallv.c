/*
 *  $Id: alltoallv.c,v 1.9 2001/11/14 19:50:11 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Alltoallv = PMPI_Alltoallv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Alltoallv  MPI_Alltoallv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Alltoallv as PMPI_Alltoallv
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

MPI_Alltoallv - Sends data from all to all processes, with a displacement

Input Parameters:
+ sendbuf - starting address of send buffer (choice) 
. sendcounts - integer array equal to the group size 
specifying the number of elements to send to each processor 
. sdispls - integer array (of length group size). Entry 
 'j'  specifies the displacement (relative to sendbuf  from
which to take the outgoing data destined for process  'j'  
. sendtype - data type of send buffer elements (handle) 
. recvcounts - integer array equal to the group size 
specifying the maximum number of elements that can be received from
each processor 
. rdispls - integer array (of length group size). Entry 
 'i'  specifies the displacement (relative to recvbuf  at
which to place the incoming data from process  'i'  
. recvtype - data type of receive buffer elements (handle) 
- comm - communicator (handle) 

Output Parameter:
. recvbuf - address of receive buffer (choice) 

.N fortran

.N Errors
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_BUFFER
@*/
int MPI_Alltoallv ( 
	void *sendbuf, 
	int *sendcnts, 
	int *sdispls, 
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int *recvcnts, 
	int *rdispls, 
	MPI_Datatype recvtype, 
	MPI_Comm comm )
{
  int        mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *stype_ptr, *rtype_ptr;
  MPIR_ERROR_DECL;
  static char myname[] = "MPI_ALLTOALLV";

  TR_PUSH(myname);
  comm_ptr = MPIR_GET_COMM_PTR(comm);
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr, myname);

  stype_ptr = MPIR_GET_DTYPE_PTR(sendtype);
  MPIR_TEST_DTYPE(sendtype,stype_ptr,comm_ptr, myname );

  rtype_ptr = MPIR_GET_DTYPE_PTR(recvtype);
  MPIR_TEST_DTYPE(recvtype,rtype_ptr,comm_ptr, myname );

  /* Check for invalid arguments */
  MPIR_ERROR_PUSH(comm_ptr);
  mpi_errno = comm_ptr->collops->Alltoallv( sendbuf, sendcnts, sdispls, 
					    stype_ptr, 
					    recvbuf, recvcnts, rdispls, 
					    rtype_ptr, comm_ptr );
  MPIR_ERROR_POP(comm_ptr);
  TR_POP;
  MPIR_RETURN(comm_ptr,mpi_errno,myname);
}
