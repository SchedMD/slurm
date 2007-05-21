/*
 *  $Id: pack_size.c,v 1.13 2001/11/14 20:10:00 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Pack_size = PMPI_Pack_size
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Pack_size  MPI_Pack_size
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Pack_size as PMPI_Pack_size
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

extern struct MPIR_DATATYPE MPIR_I_DCOMPLEX;

/*@
    MPI_Pack_size - Returns the upper bound on the amount of space needed to
                    pack a message

Input Parameters:
+ incount - count argument to packing call (integer) 
. datatype - datatype argument to packing call (handle) 
- comm - communicator argument to packing call (handle) 

Output Parameter:
. size - upper bound on size of packed message, in bytes (integer) 

Notes:
The MPI standard document describes this in terms of 'MPI_Pack', but it 
applies to both 'MPI_Pack' and 'MPI_Unpack'.  That is, the value 'size' is 
the maximum that is needed by either 'MPI_Pack' or 'MPI_Unpack'.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_TYPE
.N MPI_ERR_ARG

@*/
int MPI_Pack_size ( int incount, MPI_Datatype datatype, MPI_Comm comm, 
		    int *size )
{
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *dtype_ptr;
  static char myname[] = "MPI_PACK_SIZE";

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);

  dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);
  
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
  MPIR_TEST_DTYPE(datatype,dtype_ptr,comm_ptr,myname);
  
  MPIR_TEST_ARG(size);
  MPIR_TEST_COUNT(incount);
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );

  /******************************************************************
   ***** Debbie Swider put this error check in on 11/17/97 **********
   ******************************************************************/

  /*** Check to see that datatype is committed ***/
  if (!dtype_ptr->committed) {
      return MPIR_ERROR(comm_ptr, 
            MPIR_ERRCLASS_TO_CODE(MPI_ERR_TYPE,MPIR_ERR_UNCOMMITTED), myname );
  }
#endif


  /* Msgform is the form for ALL messages; we need to convert it into
     a Msgrep which may be different for each system.  Eventually, 
     Msgform should just be one of the Msgrep cases.
     In addition, this should probably not refer to XDR explicitly.
   */
  MPID_Pack_size( incount, dtype_ptr, comm_ptr->msgform, 
/*		  (comm_ptr->msgform == MPID_MSGFORM_OK) ? MPID_MSG_OK : 
		  MPID_MSG_XDR, */ size );
  /* We add the largest size that we expect */
#ifndef MPID_NO_FORTRAN
  (*size) += MPIR_I_DCOMPLEX.size;
#else
  (*size) += sizeof(double);
#endif

  TR_POP;
  return MPI_SUCCESS;
}


