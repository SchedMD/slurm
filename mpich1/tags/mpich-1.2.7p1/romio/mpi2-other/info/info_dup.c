/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_dup = PMPI_Info_dup
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_dup MPI_Info_dup
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_dup as PMPI_Info_dup
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_Info_dup - Returns a duplicate of the info object

Input Parameters:
. info - info object (handle)

Output Parameters:
. newinfo - duplicate of info object (handle)

.N fortran
@*/
int MPI_Info_dup(MPI_Info info, MPI_Info *newinfo)
{
    MPI_Info curr_old, curr_new;

    if ((info <= (MPI_Info) 0) || (info->cookie != MPIR_INFO_COOKIE)) {
        FPRINTF(stderr, "MPI_Info_dup: Invalid info object\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    *newinfo = (MPI_Info) ADIOI_Malloc(sizeof(struct MPIR_Info));
    curr_new = *newinfo;
    curr_new->cookie = MPIR_INFO_COOKIE;
    curr_new->key = 0;
    curr_new->value = 0;
    curr_new->next = 0;

    curr_old = info->next;
    while (curr_old) {
	curr_new->next = (MPI_Info) ADIOI_Malloc(sizeof(struct MPIR_Info));
	curr_new = curr_new->next;
	curr_new->cookie = 0;  /* cookie not set on purpose */
	curr_new->key = ADIOI_Strdup(curr_old->key);
	curr_new->value = ADIOI_Strdup(curr_old->value);
	curr_new->next = 0;
	
	curr_old = curr_old->next;
    }

    return MPI_SUCCESS;
}
