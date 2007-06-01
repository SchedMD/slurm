/* 
 *   $Id: info_getnks.c,v 1.7 2001/11/14 20:08:06 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_get_nkeys = PMPI_Info_get_nkeys
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_get_nkeys  MPI_Info_get_nkeys
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_get_nkeys as PMPI_Info_get_nkeys
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
    MPI_Info_get_nkeys - Returns the number of currently defined keys in info

Input Parameters:
. info - info object (handle)

Output Parameters:
. nkeys - number of defined keys (integer)

.N fortran
@*/
int MPI_Info_get_nkeys(MPI_Info info, int *nkeys)
{
    MPI_Info curr;
    int mpi_errno;
    static char myname[] = "MPI_INFO_GET_NKEYS";

    if ((info <= (MPI_Info) 0) || (info->cookie != MPIR_INFO_COOKIE)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO, MPIR_ERR_DEFAULT, myname, 
				     (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    curr = info->next;
    *nkeys = 0;

    while (curr) {
	curr = curr->next;
	(*nkeys)++;
    }

    return MPI_SUCCESS;
}
