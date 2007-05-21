/*
 *  $Id: allgatherv.c,v 1.10 2001/11/14 19:50:10 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Allgatherv = PMPI_Allgatherv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Allgatherv  MPI_Allgatherv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Allgatherv as PMPI_Allgatherv
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

MPI_Allgatherv - Gathers data from all tasks and deliver it to all

Input Parameters:
+ sendbuf - starting address of send buffer (choice) 
. sendcount - number of elements in send buffer (integer) 
. sendtype - data type of send buffer elements (handle) 
. recvcounts - integer array (of length group size) 
containing the number of elements that are received from each process 
. displs - integer array (of length group size). Entry 
 'i'  specifies the displacement (relative to recvbuf ) at
which to place the incoming data from process  'i'  
. recvtype - data type of receive buffer elements (handle) 
- comm - communicator (handle) 

Output Parameter:
. recvbuf - address of receive buffer (choice) 

Notes:
 The MPI standard (1.0 and 1.1) says that 

 The jth block of data sent from 
 each proess is received by every process and placed in the jth block of the 
 buffer 'recvbuf'.  

 This is misleading; a better description is

 The block of data sent from the jth process is received by every
 process and placed in the jth block of the buffer 'recvbuf'.

 This text was suggested by Rajeev Thakur.

.N fortran

.N Errors
.N MPI_ERR_BUFFER
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
@*/
int MPI_Allgatherv ( void *sendbuf, int sendcount, MPI_Datatype sendtype, 
                     void *recvbuf, int *recvcounts, int *displs, 
		     MPI_Datatype recvtype, MPI_Comm comm )
{
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *stype_ptr, *rtype_ptr;
  MPIR_ERROR_DECL;
  static char myname[] = "MPI_ALLGATHERV";

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);

  stype_ptr = MPIR_GET_DTYPE_PTR(sendtype);

  rtype_ptr = MPIR_GET_DTYPE_PTR(recvtype);

  /* Check for invalid arguments */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr, myname );
  MPIR_TEST_DTYPE(sendtype,stype_ptr,comm_ptr, myname );
  MPIR_TEST_DTYPE(recvtype,rtype_ptr,comm_ptr, myname );
  MPIR_TEST_COUNT(sendcount);
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  /* We should really check for recvcounts[i] as well */
  /* also alltoallv, bcast, gather, gatherv, red_scat, reduce, 
     scan, scatter, scatterv */

  MPIR_ERROR_PUSH(comm_ptr)
  mpi_errno = comm_ptr->collops->Allgatherv( sendbuf, sendcount,  stype_ptr, 
					     recvbuf,  recvcounts, displs,   
					     rtype_ptr, comm_ptr );
  MPIR_ERROR_POP(comm_ptr);
  TR_POP;
  MPIR_RETURN(comm_ptr,mpi_errno,myname);
}
