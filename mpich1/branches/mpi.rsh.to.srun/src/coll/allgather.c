/*
 *  $Id: allgather.c,v 1.8 2004/10/01 13:27:24 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Allgather = PMPI_Allgather
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Allgather  MPI_Allgather
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Allgather as PMPI_Allgather
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

MPI_Allgather - Gathers data from all tasks and distribute it to all tasks

Input Parameters:
+ sendbuf - starting address of send buffer (choice) 
. sendcount - number of elements in send buffer (integer) 
. sendtype - data type of send buffer elements (handle) 
. recvcount - number of elements received from any process (integer) 
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
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_BUFFER
@*/
int MPI_Allgather ( void *sendbuf, int sendcount, MPI_Datatype sendtype,
                    void *recvbuf, int recvcount, MPI_Datatype recvtype, 
		    MPI_Comm comm )
{
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *stype_ptr, *rtype_ptr;
  MPIR_ERROR_DECL;
  static char myname[] = "MPI_ALLGATHER";

  TR_PUSH(myname);
  comm_ptr = MPIR_GET_COMM_PTR(comm);

  stype_ptr = MPIR_GET_DTYPE_PTR(sendtype);

  rtype_ptr = MPIR_GET_DTYPE_PTR(recvtype);

  /* Check for invalid arguments */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr, myname);
  MPIR_TEST_DTYPE(sendtype,stype_ptr,comm_ptr, myname );
  MPIR_TEST_DTYPE(recvtype,rtype_ptr,comm_ptr, myname );
  MPIR_TEST_COUNT(sendcount);
  MPIR_TEST_COUNT(recvcount);
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  MPIR_ERROR_PUSH(comm_ptr);
  mpi_errno = comm_ptr->collops->Allgather( sendbuf, sendcount, stype_ptr,
					    recvbuf, recvcount, rtype_ptr, 
					    comm_ptr );
  MPIR_ERROR_POP(comm_ptr);
  TR_POP;
  MPIR_RETURN(comm_ptr,mpi_errno,myname);
}


