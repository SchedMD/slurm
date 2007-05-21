/* 
 *   $Id: type_get_env.c,v 1.7 2001/11/14 19:57:22 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */
#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_get_envelope = PMPI_Type_get_envelope
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_get_envelope  MPI_Type_get_envelope
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_get_envelope as PMPI_Type_get_envelope
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
MPI_Type_get_envelope - Returns information on the number and type of input arguments used in the call that created datatype

Input Parameters:
. datatype - datatype to access (handle)

Output Parameters:
+ num_integers - number of input integers used in the call constructing combiner (nonnegative integer)
. num_addresses - number of input addresses used in the call constructing combiner (nonnegative integer)
. num_datatypes - number of input datatypes used in the call constructing combiner (nonnegative integer)
- combiner - combiner (state)

.N fortran
@*/
int MPI_Type_get_envelope(
	MPI_Datatype datatype, 
	int *num_integers, 
	int *num_addresses, 
	int *num_datatypes, 
	int *combiner)
{
    struct MPIR_DATATYPE *dtypeptr;

    dtypeptr = MPIR_GET_DTYPE_PTR(datatype);

    switch (dtypeptr->dte_type) {
    case MPIR_CONTIG:
	*num_integers = 1;
	*num_addresses = 0;
	*num_datatypes = 1;
	*combiner = MPI_COMBINER_CONTIGUOUS;
	break;
    case MPIR_VECTOR:
	*combiner = MPI_COMBINER_VECTOR;
	*num_integers = 3;
	*num_addresses = 0;
	*num_datatypes = 1;
	break;
    case MPIR_HVECTOR:
	*combiner = MPI_COMBINER_HVECTOR;
	*num_integers = 2;
	*num_addresses = 1;
	*num_datatypes = 1;
	break;
    case MPIR_INDEXED:
	*combiner = MPI_COMBINER_INDEXED;
	*num_integers = 1 + 2*dtypeptr->count;
	*num_addresses = 0;
	*num_datatypes = 1;
	break;
    case MPIR_HINDEXED:
	*combiner = MPI_COMBINER_HINDEXED;
	*num_integers = 1 + dtypeptr->count;
	*num_addresses = dtypeptr->count;
	*num_datatypes = 1;
	break;
    case MPIR_STRUCT:
	*combiner = MPI_COMBINER_STRUCT;
	*num_integers = 1 + dtypeptr->count;
	*num_addresses = dtypeptr->count;
	*num_datatypes = dtypeptr->count;
	break;
    default:  
	*combiner = MPI_COMBINER_NAMED;
	*num_integers = 0;
	*num_addresses = 0;
	*num_datatypes = 0;
	break;
    }

    return MPI_SUCCESS;
}
