/*
 *  $Id: statusf2c.c,v 1.3 2001/11/14 20:06:44 ashton Exp $
 *
 *  (C) 1997 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpi_fortimpl.h"

extern void *MPIR_F_STATUS_IGNORE ;
extern void *MPIR_F_STATUSES_IGNORE;

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Status_f2c = PMPI_Status_f2c
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Status_f2c  MPI_Status_f2c
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Status_f2c as PMPI_Status_f2c
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
  MPI_Status_f2c - Convert a Fortran status to a C status

Input Parameters:
. f_status - Status value in Fortran (Integer)

Output Parameter:
. c_status - Status value in C (Status)
  
.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
@*/
int MPI_Status_f2c( MPI_Fint *f_status, MPI_Status *c_status )
{
    int i;
    int *c_status_arr = (int *)c_status;
    void *l_f_status = (void *)f_status;

    if  (l_f_status == MPIR_F_STATUS_IGNORE ||
	 l_f_status == MPIR_F_STATUSES_IGNORE) {
	return MPIR_ERROR( MPIR_COMM_WORLD, 
		MPIR_ERRCLASS_TO_CODE(MPI_ERR_ARG,MPIR_ERR_STATUS_IGNORE),
			   "MPI_STATUS_F2C" );
    }

    /* Copy Fortran to C values */
    for (i=0; i<MPI_STATUS_SIZE; i++)
	c_status_arr[i] = (int)f_status[i];
	
    return MPI_SUCCESS;
}
