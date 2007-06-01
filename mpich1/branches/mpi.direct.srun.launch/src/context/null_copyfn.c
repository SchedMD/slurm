/*
 *  $Id: null_copyfn.c,v 1.4 2001/04/20 19:38:31 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/*D
  
MPI_NULL_COPY_FN - A function to not copy attributes  

Notes:
See discussion of 'MPI_Keyval_create' for the use of this function.

D*/
int MPIR_null_copy_fn ( 
	MPI_Comm comm, 
	int keyval, 
	void *extra_state, 
	void *attr_in, 
	void *attr_out, 
	int *flag )
{
  /* No error checking at present */

  /* Set attr_out, the flag and return success */
  (*flag) = 0;
  return (MPI_SUCCESS);
}
