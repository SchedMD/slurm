/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2003 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include <string.h>
#include "mpioimpl.h"
#include "mpiu_greq.h"

#ifdef HAVE_WEAK_SYMBOLS
/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

int MPIU_Greq_query_fn(void *extra_state, MPI_Status *status)
{
    int foo;

    /* can't touch user's MPI_ERROR, so hold it for a moment */
    foo = status->MPI_ERROR;

    /* get the status from the blocking operation */
    memcpy(status, extra_state, sizeof(MPI_Status));

    /* restore MPI_ERROR to whatever it had when we got it */
    status->MPI_ERROR = foo;

    /* and let Test|Wait know we weren't canceled */
    MPIR_Nest_incr();
    MPI_Status_set_cancelled(status, 0);
    MPIR_Nest_decr();

    /* the MPI_Status structure is a convienent place to stash the return
    * code of the blocking operation */
    return ((MPI_Status*)extra_state)->MPI_ERROR;
}

int MPIU_Greq_free_fn(void *extra_state)
{
    ADIOI_Free(extra_state);
    return MPI_SUCCESS;
}
int MPIU_Greq_cancel_fn(void *extra_state, int complete)
{
    MPIU_UNREFERENCED_ARG(extra_state);
    MPIU_UNREFERENCED_ARG(complete);

    /* can't cancel */
    return MPI_SUCCESS;
}
