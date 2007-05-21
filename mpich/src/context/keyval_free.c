/*
 *  $Id: keyval_free.c,v 1.9 2002/06/12 22:24:30 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Keyval_free = PMPI_Keyval_free
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Keyval_free  MPI_Keyval_free
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Keyval_free as PMPI_Keyval_free
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "mpimem.h"
#include "attr.h"

/*@

MPI_Keyval_free - Frees attribute key for communicator cache attribute

Input Parameter:
. keyval - Frees the integer key value (integer) 

Note:
Key values are global (they can be used with any and all communicators)

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
.N MPI_ERR_PERM_KEY

.seealso: MPI_Keyval_create
@*/
int MPI_Keyval_free ( int *keyval )
{
  int mpi_errno = MPI_SUCCESS;
  MPIR_Attr_key *attr_key;
  static char myname[] = "MPI_KEYVAL_FREE";

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_ARG(keyval);

  if (*keyval == MPI_KEYVAL_INVALID) {
      /* Can't free an invalid keyval */
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_KEYVAL, myname, 
				   (char *)0, (char *)0 );
  }
  if (mpi_errno) 
      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
#endif
  attr_key = MPIR_GET_KEYVAL_PTR( *keyval );

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_KEYVAL(*keyval,attr_key,MPIR_COMM_WORLD,myname);
  if (mpi_errno) 
      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
  if ( (attr_key->permanent == 1) && (MPIR_Has_been_initialized == 1) ){
      mpi_errno = MPIR_ERRCLASS_TO_CODE(MPI_ERR_ARG,MPIR_ERR_PERM_KEY);
  }
  if (mpi_errno) 
      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
#endif

  if (attr_key->ref_count <= 1) {
      MPIR_CLR_COOKIE(attr_key);
      FREE ( attr_key );
      MPIR_RmPointer( *keyval );
  }
  else {
      MPIR_REF_DECR(attr_key);
#ifdef FOO
      /* Debugging only */
      if (MPIR_Has_been_initialized != 1) 
	  PRINTF( "attr_key count is %d\n", attr_key->ref_count );
#endif
  }
  (*keyval) = MPI_KEYVAL_INVALID;
  
  return (MPI_SUCCESS);
}
