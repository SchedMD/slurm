/*
 *  $Id: attr_putval.c,v 1.11 2001/11/14 19:54:18 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Attr_put = PMPI_Attr_put
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Attr_put  MPI_Attr_put
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Attr_put as PMPI_Attr_put
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "attr.h" 

/*@

MPI_Attr_put - Stores attribute value associated with a key

Input Parameters:
+ comm - communicator to which attribute will be attached (handle) 
. keyval - key value, as returned by  'MPI_KEYVAL_CREATE' (integer) 
- attribute_val - attribute value 

Notes:
Values of the permanent attributes 'MPI_TAG_UB', 'MPI_HOST', 'MPI_IO', and
'MPI_WTIME_IS_GLOBAL' may not be changed. 

The type of the attribute value depends on whether C or Fortran is being used.
In C, an attribute value is a pointer ('void *'); in Fortran, it is a single 
integer (`not` a pointer, since Fortran has no pointers and there are systems 
for which a pointer does not fit in an integer (e.g., any > 32 bit address 
system that uses 64 bits for Fortran 'DOUBLE PRECISION').

If an attribute is already present, the delete function (specified when the
corresponding keyval was created) will be called.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_KEYVAL
.N MPI_ERR_PERM_KEY

.seealso MPI_Attr_get, MPI_Keyval_create, MPI_Attr_delete
@*/
int MPI_Attr_put ( MPI_Comm comm, int keyval, void *attr_value )
{
  MPIR_HBT_node *attr;
  MPIR_Attr_key *attr_key;
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_ptr;
  static char myname[] = "MPI_ATTR_PUT";

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  attr_key = MPIR_GET_KEYVAL_PTR( keyval );
  MPIR_TEST_MPI_KEYVAL(keyval,attr_key,comm_ptr,myname);

  /* Check for valid arguments */
  if ( ( (keyval == MPI_KEYVAL_INVALID) && (mpi_errno = MPI_ERR_OTHER) ) )
	return MPIR_ERROR( comm_ptr, mpi_errno, myname);

  if (comm == MPI_COMM_WORLD && attr_key->permanent) 
	return MPIR_ERROR( comm_ptr, 
	     MPIR_ERRCLASS_TO_CODE(MPI_ERR_ARG,MPIR_ERR_PERM_KEY),myname );

  MPIR_HBT_lookup(comm_ptr->attr_cache, keyval, &attr);
  if (attr == (MPIR_HBT_node *)0) {
	(void) MPIR_HBT_new_node ( attr_key, attr_value, &attr );
	(void) MPIR_HBT_insert ( comm_ptr->attr_cache, attr );
	/* Every update to the attr_key must be counted! */
	MPIR_REF_INCR(attr_key);
  }
  else {
      /* 
	 This is an unclear part of the standard.  Under MPI_KEYVAL_CREATE,
	 it is claimed that ONLY MPI_COMM_FREE and MPI_ATTR_DELETE
	 can cause the delete routine to be called.  Under 
	 MPI_ATTR_PUT, however, the delete routine IS called.
       */
	if ( attr_key->delete_fn.c_delete_fn ) {
#ifndef MPID_NO_FORTRAN
	    if (attr_key->FortranCalling) {
		MPI_Aint invall = (MPI_Aint)attr->value;
		MPI_Fint inval = (int)invall;
		MPI_Fint fcomm = MPI_Comm_c2f(comm);
		(void) (*attr_key->delete_fn.f77_delete_fn)(&fcomm, 
					   &keyval, &inval,
					   attr_key->extra_state, &mpi_errno );
		attr->value = (void *)(MPI_Aint)inval;
	    }
	    else
#endif
		mpi_errno = (*attr_key->delete_fn.c_delete_fn)(comm, keyval, 
					   attr->value,
					   attr_key->extra_state );
	    if (mpi_errno) 
		return MPIR_ERROR( comm_ptr, mpi_errno, myname);
	    }
	attr->value = attr_value;
  }
  /* The device may want to know about attributes */
  MPID_ATTR_SET(comm_ptr,keyval,attr_value);

  TR_POP;
  return (mpi_errno);
}

