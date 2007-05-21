/*
 *  $Id: unpack.c,v 1.10 2001/11/14 20:10:10 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Unpack = PMPI_Unpack
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Unpack  MPI_Unpack
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Unpack as PMPI_Unpack
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/*@
    MPI_Unpack - Unpack a datatype into contiguous memory

Input Parameters:
+ inbuf - input buffer start (choice) 
. insize - size of input buffer, in bytes (integer) 
. position - current position in bytes (integer) 
. outcount - number of items to be unpacked (integer) 
. datatype - datatype of each output data item (handle) 
- comm - communicator for packed message (handle) 

Output Parameter:
. outbuf - output buffer start (choice) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_ARG

.seealso: MPI_Pack, MPI_Pack_size
@*/
int MPI_Unpack ( void *inbuf, int insize, int *position, 
		 void *outbuf, int outcount, MPI_Datatype datatype, 
		 MPI_Comm comm )
{
  int mpi_errno = MPI_SUCCESS;
  int out_pos;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *dtype_ptr;
  static char myname[] = "MPI_UNPACK";

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);

  dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
  MPIR_TEST_DTYPE(datatype,dtype_ptr,comm_ptr,myname);

  /* NOT ENOUGH ERROR CHECKING AT PRESENT */
  MPIR_TEST_ARG(position);
  MPIR_TEST_COUNT(insize);
  if (*position < 0) 
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_DEFAULT, myname, 
				   (char *)0, (char *)0,
				   *position );
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );

  /******************************************************************
   ****** This error check was put in by Debbie Swider on 11/17/97 **
   ******************************************************************/
  
  /*** check to see that number of items to be unpacked is not < 0 ***/
  if (MPIR_TEST_OUTCOUNT(comm,outcount)) {
     return MPIR_ERROR(comm_ptr,mpi_errno,myname);
  }
 
  if (!dtype_ptr->committed) {
      return MPIR_ERROR( comm_ptr, 
           MPIR_ERRCLASS_TO_CODE(MPI_ERR_TYPE,MPIR_ERR_UNCOMMITTED), myname );
  }
#endif

  /* The data WAS received with MPI_PACKED format, and so was SENT with
     the format of the communicator */
  /* We need to compute the PACKED msgrep from the comm msgFORM. */
  out_pos = 0;
  MPID_Unpack( inbuf, insize, MPID_Msgrep_from_comm( comm_ptr ), position,
	       outbuf, outcount, dtype_ptr, &out_pos, 
	       comm_ptr, MPI_ANY_SOURCE, &mpi_errno );
  TR_POP;
  MPIR_RETURN(comm_ptr,mpi_errno,myname);
}
