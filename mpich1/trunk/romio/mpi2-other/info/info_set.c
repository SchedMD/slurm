/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_set = PMPI_Info_set
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_set MPI_Info_set
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_set as PMPI_Info_set
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_Info_set - Adds a (key,value) pair to info

Input Parameters:
. info - info object (handle)
. key - key (string)
. value - value (string)

.N fortran
@*/
int MPI_Info_set(MPI_Info info, char *key, char *value)
{
    MPI_Info prev, curr;

    if ((info <= (MPI_Info) 0) || (info->cookie != MPIR_INFO_COOKIE)) {
        FPRINTF(stderr, "MPI_Info_set: Invalid info object\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (key <= (char *) 0) {
	FPRINTF(stderr, "MPI_Info_set: key is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (value <= (char *) 0) {
	FPRINTF(stderr, "MPI_Info_set: value is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (strlen(key) > MPI_MAX_INFO_KEY) {
	FPRINTF(stderr, "MPI_Info_set: key is longer than MPI_MAX_INFO_KEY\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (strlen(value) > MPI_MAX_INFO_VAL) {
	FPRINTF(stderr, "MPI_Info_set: value is longer than MPI_MAX_INFO_VAL\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (!strlen(key)) {
	FPRINTF(stderr, "MPI_Info_set: key is a null string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (!strlen(value)) {
	FPRINTF(stderr, "MPI_Info_set: value is a null string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    prev = info;
    curr = info->next;

    while (curr) {
	if (!strcmp(curr->key, key)) {
	    ADIOI_Free(curr->value); 
	    curr->value = ADIOI_Strdup(value);
	    break;
	}
	prev = curr;
	curr = curr->next;
    }

    if (!curr) {
	prev->next = (MPI_Info) ADIOI_Malloc(sizeof(struct MPIR_Info));
	curr = prev->next;
	curr->cookie = 0;  /* cookie not set on purpose */
	curr->key = ADIOI_Strdup(key);
	curr->value = ADIOI_Strdup(value);
	curr->next = 0;
    }

    return MPI_SUCCESS;
}
