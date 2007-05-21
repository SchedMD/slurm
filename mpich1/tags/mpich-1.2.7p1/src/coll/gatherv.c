/*
 *  $Id: gatherv.c,v 1.13 2002/02/19 14:47:20 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Gatherv = PMPI_Gatherv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Gatherv  MPI_Gatherv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Gatherv as PMPI_Gatherv
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

MPI_Gatherv - Gathers into specified locations from all processes in a group

Input Parameters:
+ sendbuf - starting address of send buffer (choice) 
. sendcount - number of elements in send buffer (integer) 
. sendtype - data type of send buffer elements (handle) 
. recvcounts - integer array (of length group size) 
containing the number of elements that are received from each process
(significant only at 'root') 
. displs - integer array (of length group size). Entry 
 'i'  specifies the displacement relative to recvbuf  at
which to place the incoming data from process  'i'  (significant only
at root) 
. recvtype - data type of recv buffer elements 
(significant only at 'root') (handle) 
. root - rank of receiving process (integer) 
- comm - communicator (handle) 

Output Parameter:
. recvbuf - address of receive buffer (choice, significant only at 'root') 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_TYPE
.N MPI_ERR_BUFFER
@*/
int MPI_Gatherv ( void *sendbuf, int sendcnt, MPI_Datatype sendtype, 
                  void *recvbuf, int *recvcnts, int *displs, 
		  MPI_Datatype recvtype, 
                  int root, MPI_Comm comm )
{
  int        mpi_errno = MPI_SUCCESS;
  int        rank;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *stype_ptr, *rtype_ptr = 0;
  MPIR_ERROR_DECL;
  static char myname[] = "MPI_GATHERV";

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);

  stype_ptr = MPIR_GET_DTYPE_PTR(sendtype);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr, myname);
  MPIR_TEST_COUNT(sendcnt);
  MPIR_TEST_DTYPE(sendtype,stype_ptr,comm_ptr, myname );
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  /* rtype is significant only at root */
  (void) MPIR_Comm_rank ( comm_ptr, &rank );
  if (rank == root) {
      rtype_ptr = MPIR_GET_DTYPE_PTR(recvtype);
#ifndef MPIR_NO_ERROR_CHECKING
      MPIR_TEST_DTYPE(recvtype,rtype_ptr,comm_ptr, myname );
      if (mpi_errno)
          return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif
  }

  MPIR_ERROR_PUSH(comm_ptr);
  mpi_errno = comm_ptr->collops->Gatherv( sendbuf, sendcnt,  stype_ptr, 
                  recvbuf, recvcnts, displs, rtype_ptr, 
                  root, comm_ptr );
  MPIR_ERROR_POP(comm_ptr);
  TR_POP;
  MPIR_RETURN(comm_ptr,mpi_errno,myname);
}
