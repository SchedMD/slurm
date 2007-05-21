/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef BNR_H
#define BNR_H

#if defined(__cplusplus)
extern "C" {
#endif

#define BNR_MAX_KEY_LEN         256
#define BNR_MAX_VALUE_LEN       1024
#define BNR_MAX_DB_NAME_LENGTH  100
#define bool_t                  int
#define BNR_SUCCESS             0
#define BNR_FAIL                -1

int BNR_Init(int *spawned);
int BNR_Finalize();
int BNR_Get_size(int *size);
int BNR_Get_rank(int *rank);
int BNR_Barrier();

int BNR_KM_Get_my_name(char *dbname);
int BNR_KM_Get_name_length_max();
int BNR_KM_Create(char * dbname);
int BNR_KM_Destroy(char * dbname);
int BNR_KM_Put(char *dbname, char *key, char *value);
int BNR_KM_Commit(char *dbname);
int BNR_KM_Get(char *dbname, char *key, char *value);
int BNR_KM_Iter_first(char *dbname, char *key, char *value);
int BNR_KM_Iter_next(char *dbname, char *key, char *value);

int BNR_Spawn_multiple(int count, char **cmds, char ***argvs, 
		       int *maxprocs, void *info, int *errors, 
		       bool_t *same_domain, void *preput_info);

#if defined(__cplusplus)
}
#endif

#endif
