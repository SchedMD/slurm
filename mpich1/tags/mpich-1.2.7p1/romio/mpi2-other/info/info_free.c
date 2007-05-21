/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_free = PMPI_Info_free
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_free MPI_Info_free
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_free as PMPI_Info_free
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_Info_free - Frees an info object

Input Parameters:
. info - info object (handle)

.N fortran
@*/
int MPI_Info_free(MPI_Info *info)
{
    MPI_Info curr, next;

    if ((*info <= (MPI_Info) 0) || ((*info)->cookie != MPIR_INFO_COOKIE)) {
        FPRINTF(stderr, "MPI_Info_free: Invalid info object\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    curr = (*info)->next;
    ADIOI_Free(*info);
    *info = MPI_INFO_NULL;

    while (curr) {
	next = curr->next;
	ADIOI_Free(curr->key);
	ADIOI_Free(curr->value);
	ADIOI_Free(curr);
	curr = next;
    }

    return MPI_SUCCESS;
}
