/*
 *  $Id: opcreate.c,v 1.10 2001/11/14 19:50:12 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Op_create = PMPI_Op_create
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Op_create  MPI_Op_create
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Op_create as PMPI_Op_create
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
#include "mpiops.h"

/*@
  MPI_Op_create - Creates a user-defined combination function handle

Input Parameters:
+ function - user defined function (function) 
- commute -  true if commutative;  false otherwise. 

Output Parameter:
. op - operation (handle) 

Notes on the user function:
 The calling list for the user function type is
.vb
 typedef void (MPI_User_function) ( void * a, 
               void * b, int * len, MPI_Datatype * ); 
.ve
 where the operation is 'b[i] = a[i] op b[i]', for 'i=0,...,len-1'.  A pointer
 to the datatype given to the MPI collective computation routine (i.e., 
 'MPI_Reduce', 'MPI_Allreduce', 'MPI_Scan', or 'MPI_Reduce_scatter') is also
 passed to the user-specified routine.

.N fortran

.N collops

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_EXHAUSTED

.seealso: MPI_Op_free
@*/
int MPI_Op_create( 
	MPI_User_function *function, 
	int commute, 
	MPI_Op *op )
{
    struct MPIR_OP *new;
    MPIR_ALLOC(new,NEW( struct MPIR_OP ),MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
	       "MPI_OP_CREATE");
    MPIR_SET_COOKIE(new,MPIR_OP_COOKIE)
    new->commute   = commute;
    new->op	   = function;
    new->permanent = 0;
    *op = (MPI_Op)MPIR_FromPointer( new );
    return (MPI_SUCCESS);
}
