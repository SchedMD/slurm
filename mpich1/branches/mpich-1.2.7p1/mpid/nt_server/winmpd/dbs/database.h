/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef DATABASE_H
#define DATABASE_H

#define DBS_SUCCESS          0
#define DBS_FAIL            -1
#define MAX_DBS_NAME_LEN     256
#define MAX_DBS_KEY_LEN      256
#define MAX_DBS_VALUE_LEN    1024

#if defined(__cplusplus)
extern "C" {
#endif

int dbs_init();
int dbs_finalize();
int dbs_create(char *name);
int dbs_create_name_in(char *name);
int dbs_destroy(char *name);
int dbs_get(char *name, char *key, char *value);
int dbs_put(char *name, char *key, char *value);
int dbs_delete(char *name, char *key);
int dbs_first(char *name, char *key, char *value);
int dbs_next(char *name, char *key, char *value);
int dbs_firstdb(char *name);
int dbs_nextdb(char *name);

#if defined(__cplusplus)
}
#endif

#endif
