/*
 *  $Id: pack.c,v 1.12 2001/11/14 20:10:00 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Pack = PMPI_Pack
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Pack  MPI_Pack
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Pack as PMPI_Pack
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
    MPI_Pack - Packs a datatype into contiguous memory

Input Parameters:
+ inbuf - input buffer start (choice) 
. incount - number of input data items (integer) 
. datatype - datatype of each input data item (handle) 
. outcount - output buffer size, in bytes (integer) 
. position - current position in buffer, in bytes (integer) 
- comm - communicator for packed message (handle) 

Output Parameter:
. outbuf - output buffer start (choice) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_TYPE
.N MPI_ERR_COUNT
.N MPI_ERR_ARG

.seealso: MPI_Unpack, MPI_Pack_size

@*/
int MPI_Pack ( void *inbuf, int incount, MPI_Datatype datatype, 
	       void *outbuf, int outcount, int *position, MPI_Comm comm )
{
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_ptr;
  struct MPIR_DATATYPE     *dtype_ptr;
  static char myname[] = "MPI_PACK";

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);
  MPIR_TEST_DTYPE(datatype,dtype_ptr,comm_ptr,myname);

  /* NOT ENOUGH ERROR CHECKING AT PRESENT */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_ARG(position);
  MPIR_TEST_COUNT(incount);
  if (*position < 0) 
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_POSITION_NEG, 
		   myname, "Value of position must be nonnegative", 
			   "Value of position must be nonnegative (is %d)",
				   *position );
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );

  /***************************************************************
   ** Debbie Swider put these error checks in on 11/17/97 ********
   ***************************************************************/

  /*** Check to see that output buffer size is not < 0 ******/
  if (MPIR_TEST_OUTSIZE(comm,outcount)) {
     return MPIR_ERROR(comm_ptr,mpi_errno,myname);
  }

  /*** Check to see that output buffer size is not less than ***
       number of input data items ***/
  if (MPIR_TEST_OUT_LT_IN(comm,outcount,incount)) {
     return MPIR_ERROR(comm_ptr,mpi_errno,myname);
  }

  /*****************************************************************
   *****************************************************************/
     
  if (!dtype_ptr->committed) {
      return MPIR_ERROR( comm_ptr, 
	     MPIR_ERRCLASS_TO_CODE(MPI_ERR_TYPE,MPIR_ERR_UNCOMMITTED), myname );
  }
#endif

  /* Msgform is the form for ALL messages; we need to convert it into
     a Msgrep which may be different for each system.  Eventually, 
     Msgform should just be one of the Msgrep cases.
     In addition, this should probably not refer to XDR explicitly.

     Note that we pass the buffer to MPID_Pack in the same way that
     MPI_Pack gets it - buffer/position, not current buffer position.
     This is a change from MPICH 1.1.0 (needed by Nexus).
   */
  MPID_Pack( inbuf, incount, dtype_ptr, 
	     /*((char *)outbuf) + *position, outcount-*position, position,*/
	     outbuf,outcount,position,
	     comm_ptr, MPI_ANY_SOURCE, MPID_MSGREP_UNKNOWN, comm_ptr->msgform, 
/*	     (comm_ptr->msgform == MPID_MSGFORM_OK) ? MPID_MSG_OK : 
		  MPID_MSG_XDR, */ &mpi_errno );
  TR_POP;
  MPIR_RETURN(comm_ptr,mpi_errno,myname);
}
