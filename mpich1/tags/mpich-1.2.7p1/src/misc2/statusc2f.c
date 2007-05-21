/*
 *  $Id: statusc2f.c,v 1.5 1999/08/30 15:47:51 swider Exp $
 *
 *  (C) 1997 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Status_c2f = PMPI_Status_c2f
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Status_c2f  MPI_Status_c2f
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Status_c2f as PMPI_Status_c2f
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
  MPI_Status_c2f - Convert a C status to a Fortran status

Input Parameters:
. c_status - Status value in C (Status)

Output Parameter:
. f_status - Status value in Fortran (Integer)
  
.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
@*/
int MPI_Status_c2f( MPI_Status *c_status, MPI_Fint *f_status )
{
    int i;
    int *c_status_arr = (int *)c_status;

    if (c_status == MPI_STATUS_IGNORE ||
	c_status == MPI_STATUSES_IGNORE) {
	return MPIR_ERROR( MPIR_COMM_WORLD, 
		MPIR_ERRCLASS_TO_CODE(MPI_ERR_ARG,MPIR_ERR_STATUS_IGNORE),
			   "MPI_STATUS_C2F" );
    }

    /* Copy C to Fortran */
    for (i=0; i<MPI_STATUS_SIZE; i++)
	f_status[i] = (MPI_Fint)c_status_arr[i];
	
    return MPI_SUCCESS;
}
