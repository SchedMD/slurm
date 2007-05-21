/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_get = PMPI_Info_get
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_get MPI_Info_get
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_get as PMPI_Info_get
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_Info_get - Retrieves the value associated with a key

Input Parameters:
. info - info object (handle)
. key - key (string)
. valuelen - length of value argument (integer)

Output Parameters:
. value - value (string)
. flag - true if key defined, false if not (boolean)

.N fortran
@*/
int MPI_Info_get(MPI_Info info, char *key, int valuelen, char *value, int *flag)
{
    MPI_Info curr;

    if ((info <= (MPI_Info) 0) || (info->cookie != MPIR_INFO_COOKIE)) {
        FPRINTF(stderr, "MPI_Info_get: Invalid info object\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (key <= (char *) 0) {
	FPRINTF(stderr, "MPI_Info_get: key is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (strlen(key) > MPI_MAX_INFO_KEY) {
	FPRINTF(stderr, "MPI_Info_get: key is longer than MPI_MAX_INFO_KEY\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (!strlen(key)) {
	FPRINTF(stderr, "MPI_Info_get: key is a null string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (valuelen <= 0) {
	FPRINTF(stderr, "MPI_Info_get: Invalid valuelen argument\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (value <= (char *) 0) {
	FPRINTF(stderr, "MPI_Info_get: value is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    curr = info->next;
    *flag = 0;

    while (curr) {
	if (!strcmp(curr->key, key)) {
	    ADIOI_Strncpy(value, curr->value, valuelen);
	    value[valuelen] = '\0';
	    *flag = 1;
	    break;
	}
	curr = curr->next;
    }

    return MPI_SUCCESS;
}
