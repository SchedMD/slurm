/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2003 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpioimpl.h"

#ifndef _MPIU_GREQUEST_H
#define _MPIU_GREQUEST_H

int MPIU_Greq_query_fn(void *extra_state, MPI_Status *status);
int MPIU_Greq_free_fn(void *extra_state);
int MPIU_Greq_cancel_fn(void *extra_state, int complete);

#endif
