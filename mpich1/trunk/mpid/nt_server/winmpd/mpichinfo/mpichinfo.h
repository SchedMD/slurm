/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef MPICH_INFO_H
#define MPICH_INFO_H

#define MPICH_INFO_COOKIE     0x12345678
#define MPICH_MAX_INFO_KEY    255
#define MPICH_MAX_INFO_VAL    1024
#define MPICH_INFO_NULL       ((MPICH_Info) 0)
#define MPICH_SUCCESS         0
#define MPICH_FAIL            -1

#define MALLOC malloc
#define FREE   free

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct MPICH_Info_struct {
    int cookie;
    char *key, *value;
    struct MPICH_Info_struct *next;
} * MPICH_Info;

int MPICH_Info_set(MPICH_Info info, char *key, char *value);
int MPICH_Info_get_valuelen(MPICH_Info info, char *key, int *valuelen, int *flag);
int MPICH_Info_get_nthkey(MPICH_Info info, int n, char *key);
int MPICH_Info_get_nkeys(MPICH_Info info, int *nkeys);
int MPICH_Info_get(MPICH_Info info, char *key, int valuelen, char *value, int *flag);
int MPICH_Info_free(MPICH_Info *info);
int MPICH_Info_dup(MPICH_Info info, MPICH_Info *newinfo);
int MPICH_Info_delete(MPICH_Info info, char *key);
int MPICH_Info_create(MPICH_Info *info);

#if defined(__cplusplus)
}
#endif

#endif
