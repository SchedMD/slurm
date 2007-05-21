/* 
 *   $Id: info_getnth.c,v 1.9 2003/03/02 16:03:43 gropp Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_get_nthkey = PMPI_Info_get_nthkey
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_get_nthkey  MPI_Info_get_nthkey
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_get_nthkey as PMPI_Info_get_nthkey
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
    MPI_Info_get_nthkey - Returns the nth defined key in info

Input Parameters:
+ info - info object (handle)
- n - key number (integer)

Output Parameters:
. keys - key (string).  The maximum number of characters is 'MPI_MAX_INFO_KEY'.

.N fortran
@*/
int MPI_Info_get_nthkey(MPI_Info info, int n, char *key)
{
    MPI_Info curr;
    int nkeys, i;
    int mpi_errno;
    static char myname[] = "MPI_INFO_GET_NTHKEY";

    if ((info <= (MPI_Info) 0) || (info->cookie != MPIR_INFO_COOKIE)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO, MPIR_ERR_DEFAULT, myname, 
				     (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (!key) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_DEFAULT, 
				     myname, (char *)0, (char *)0);
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    curr = info->next;
    nkeys = 0;
    while (curr) {
	curr = curr->next;
	nkeys++;
    }

    if ((n < 0) || (n >= nkeys)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_INFO_NKEY, myname,
				     "n is an invalid number",
				     "n = %d is an invalid number", n );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    curr = info->next;
    i = 0;
    while (i < n) {
	curr = curr->next;
	i++;
    }
    strcpy(key, curr->key);

    return MPI_SUCCESS;
}
