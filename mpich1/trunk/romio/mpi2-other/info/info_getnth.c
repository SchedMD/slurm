/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_get_nthkey = PMPI_Info_get_nthkey
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_get_nthkey MPI_Info_get_nthkey
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_get_nthkey as PMPI_Info_get_nthkey
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_Info_get_nthkey - Returns the nth defined key in info

Input Parameters:
. info - info object (handle)
. n - key number (integer)

Output Parameters:
. keys - key (string)

.N fortran
@*/
int MPI_Info_get_nthkey(MPI_Info info, int n, char *key)
{
    MPI_Info curr;
    int nkeys, i;

    if ((info <= (MPI_Info) 0) || (info->cookie != MPIR_INFO_COOKIE)) {
        FPRINTF(stderr, "MPI_Info_get_nthkey: Invalid info object\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (key <= (char *) 0) {
	FPRINTF(stderr, "MPI_Info_get: key is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    curr = info->next;
    nkeys = 0;
    while (curr) {
	curr = curr->next;
	nkeys++;
    }

    if ((n < 0) || (n >= nkeys)) {
        FPRINTF(stderr, "MPI_Info_get_nthkey: n is an invalid number\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    curr = info->next;
    i = 0;
    while (i < n) {
	curr = curr->next;
	i++;
    }
    ADIOI_Strncpy(key, curr->key, MPI_MAX_INFO_KEY);

    return MPI_SUCCESS;
}
