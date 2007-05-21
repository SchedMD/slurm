/* 
 *   $Id: type_get_cont.c,v 1.8 2001/11/14 19:57:22 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */
#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_get_contents = PMPI_Type_get_contents
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_get_contents  MPI_Type_get_contents
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_get_contents as PMPI_Type_get_contents
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
/* #include "cookie.h"
#include "datatype.h" 
#include "objtrace.h" */

/*@
    MPI_Type_get_contents - Retrieves the actual arguments used in the creation call for a datatype

Input Parameters:
+ datatype - datatype to access (handle)
. max_integers - number of elements in array_of_integers (non-negative integer)
. max_addresses - number of elements in array_of_addresses (non-negative integer)
. max_datatypes - number of elements in array_of_datatypes (non-negative integer)

Output Parameters:
. array_of_integers - contains integer arguments used in constructing datatype (array of integers)
. array_of_addresses - contains address arguments used in constructing datatype (array of integers)
- array_of_datatypes - contains datatype arguments used in constructing datatype (array of handles)

.N fortran
@*/
int MPI_Type_get_contents(
	MPI_Datatype datatype, 
	int max_integers, 
	int max_addresses, 
	int max_datatypes, 
	int *array_of_integers, 
	MPI_Aint *array_of_addresses, 
	MPI_Datatype *array_of_datatypes)
{
    int i;
    struct MPIR_DATATYPE *dtypeptr;
    static char myname[] = "MPI_TYPE_GET_CONTENTS";
    int mpi_errno;

    dtypeptr = MPIR_GET_DTYPE_PTR(datatype);

    switch (dtypeptr->dte_type) {
    case MPIR_CONTIG:
	array_of_integers[0] = dtypeptr->count;
	array_of_datatypes[0] = dtypeptr->old_type->self;
	MPIR_REF_INCR(dtypeptr->old_type);
	break;
    case MPIR_VECTOR:  /* In MPICH, vector is internally represented
			  as hvector. */
    case MPIR_HVECTOR:
	array_of_integers[0] = dtypeptr->count;
	array_of_integers[1] = dtypeptr->blocklen;
	array_of_addresses[0] = dtypeptr->stride;
	array_of_datatypes[0] = dtypeptr->old_type->self;
	MPIR_REF_INCR(dtypeptr->old_type);
	break;
    case MPIR_INDEXED: /* In MPICH, indexed is internally represented
			  as hindexed. */
    case MPIR_HINDEXED:
	array_of_integers[0] = dtypeptr->count;
	for (i=1; i<=dtypeptr->count; i++)
	    array_of_integers[i] = dtypeptr->blocklens[i-1];
	for (i=0; i<dtypeptr->count; i++)
	    array_of_addresses[i] = dtypeptr->indices[i];
	array_of_datatypes[0] = dtypeptr->old_type->self;
	MPIR_REF_INCR(dtypeptr->old_type);
	break;
    case MPIR_STRUCT:
	array_of_integers[0] = dtypeptr->count;
	for (i=1; i<=dtypeptr->count; i++)
	    array_of_integers[i] = dtypeptr->blocklens[i-1];
	for (i=0; i<dtypeptr->count; i++)
	    array_of_addresses[i] = dtypeptr->indices[i];
	for (i=0; i<dtypeptr->count; i++) {
	    array_of_datatypes[i] = dtypeptr->old_types[i]->self;
	    MPIR_REF_INCR(dtypeptr->old_types[i]);
	}
	break;
    default:  
	/* When we define datatype names, the argument should be the
	   name of this type */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_TYPE, MPIR_ERR_BASIC_TYPE, 
				     myname, (char *)0, (char *)0,
				     (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
    return MPI_SUCCESS;
}
