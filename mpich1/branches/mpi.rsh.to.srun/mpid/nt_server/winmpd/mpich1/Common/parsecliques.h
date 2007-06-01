/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef PARSE_CLIQUES_H
#define PARSE_CLIQUES_H

#ifndef MALLOC
#define MALLOC malloc
#endif
#ifndef FREE
#define FREE free
#endif

int ParseCliques(char *pszCliques, int iproc, int nproc, int *count, int **members);

#endif
