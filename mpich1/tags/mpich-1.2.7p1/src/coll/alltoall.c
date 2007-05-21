/*
 *  $Id: alltoall.c,v 1.11 2001/11/14 19:50:11 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Alltoall = PMPI_Alltoall
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Alltoall MPI_Alltoall
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Alltoall as PMPI_Alltoall
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

MPI_Alltoall - Sends data from all to all processes

Input Parameters:
+ sendbuf - starting address of send buffer (choice) 
. sendcount - number of elements to send to each process (integer) 
. sendtype - data type of send buffer elements (handle) 
. recvcount - number of elements received from any process (integer) 
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
int MPI_Alltoall( void *sendbuf, int sendcount, MPI_Datatype sendtype, 
                  void *recvbuf, int recvcnt, MPI_Datatype recvtype, 
		  MPI_Comm comm )
{
  int          mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *stype_ptr, *rtype_ptr;
  MPIR_ERROR_DECL;
  static char myname[] = "MPI_ALLTOALL";

  TR_PUSH(myname);
  comm_ptr = MPIR_GET_COMM_PTR(comm);

  stype_ptr = MPIR_GET_DTYPE_PTR(sendtype);

  rtype_ptr = MPIR_GET_DTYPE_PTR(recvtype);
 
  /* Check for invalid arguments */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
  MPIR_TEST_DTYPE(sendtype,stype_ptr,comm_ptr, myname );
  MPIR_TEST_DTYPE(recvtype,rtype_ptr,comm_ptr, myname );
  MPIR_TEST_COUNT(sendcount);
  MPIR_TEST_COUNT(recvcnt);
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  MPIR_ERROR_PUSH(comm_ptr);
  mpi_errno = comm_ptr->collops->Alltoall(sendbuf, sendcount, stype_ptr, 
                  recvbuf, recvcnt, rtype_ptr, comm_ptr );
  MPIR_ERROR_POP(comm_ptr);
  TR_POP;
  MPIR_RETURN(comm_ptr,mpi_errno, myname);
}
