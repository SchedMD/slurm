/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef CRYPT_H
#define CRYPT_H

#if defined(__cplusplus)
extern "C" {
#endif

char *crypt(const char *buf,const char *salt);

#if defined(__cplusplus)
}
#endif

#endif
