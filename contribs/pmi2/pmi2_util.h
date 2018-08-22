/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2007 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef PMI2UTIL_H_INCLUDED
#define PMI2UTIL_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>

/* maximum sizes for arrays */
#define PMI2_MAXLINE 1024
#define PMI2_IDSIZE    32

#define TRUE  1
#define FALSE 0

#ifdef DEBUG
    #define PMI2U_printf(x...) do {				\
	char logstr[1024];					\
	snprintf(logstr, 1024, x);      			\
	fprintf(stderr, "[%s (%d): %s] %s\n",			\
		__FILE__, __LINE__, __func__, logstr);		\
    } while (0)
#else
    #define PMI2U_printf(x...)
#endif

#define PMI2U_Assert(a_) do { \
    if (!(a_)) { \
        PMI2U_printf("ASSERT( %s )", #a_); \
    } \
} while (0)

#define PMI2U_ERR_POP(err) do { \
    pmi2_errno = err; \
    PMI2U_printf("err. %d", pmi2_errno); \
    goto fn_fail; \
} while (0)

#define PMI2U_ERR_SETANDJUMP(err, class, x...) do { \
    char errstr[PMI2_MAXLINE * 3]; \
    snprintf(errstr, sizeof(errstr), x);	\
    PMI2U_printf("err. %s", errstr);\
    pmi2_errno = class; \
    goto fn_fail; \
} while (0)

#define PMI2U_ERR_CHKANDJUMP(cond, err, class, x...) do { \
    if (cond) PMI2U_ERR_SETANDJUMP(err, class, x); \
} while (0)

#define PMI2U_CHKMEM_SETERR(rc_, nbytes_, name_) do { \
    PMI2U_printf("ERROR: memory allocation of %lu bytes failed for %s", \
            (long unsigned int) nbytes_, name_); \
    rc_ = PMI2_ERR_NOMEM; \
    goto fn_exit; \
} while(0)

/* Persistent memory that we may want to recover if something goes wrong */
#define PMI2U_CHKMEM_DECL(n_) \
    void* pmi2u_chkmem_stk_[n_] = {0}; \
    int pmi2u_chkmem_stk_sp_= 0; \
    const int pmi2u_chkmem_stk_sz_ = n_

#define PMI2U_CHKMEM_REAP() \
    while (pmi2u_chkmem_stk_sp_ > 0) { \
        free ((void*)( pmi2u_chkmem_stk_[--pmi2u_chkmem_stk_sp_] )); \
    }

#define PMI2U_CHKMEM_COMMIT() pmi2u_chkmem_stk_sp_ = 0

#define PMI2U_CHKMEM_MALLOC(pointer_,type_,nbytes_,rc_,name_) do { \
    pointer_ = (type_)malloc(nbytes_); \
    if (pointer_ && (pmi2u_chkmem_stk_sp_< pmi2u_chkmem_stk_sz_)) { \
        pmi2u_chkmem_stk_[pmi2u_chkmem_stk_sp_++] = pointer_; \
    } else { \
        PMI2U_CHKMEM_SETERR(rc_,nbytes_,name_); \
        goto fn_fail; \
    } \
} while(0)

#define PMI2U_CHKMEM_FREEALL() \
    while (pmi2u_chkmem_stk_sp_ > 0) { \
        free ((void*)( pmi2u_chkmem_stk_[--pmi2u_chkmem_stk_sp_] )); \
}

/* prototypes for PMIU routines */
void PMI2U_Set_rank( int PMI_rank );
void PMI2U_SetServer( void );
int  PMI2U_readline( int fd, char *buf, int max );
int  PMI2U_writeline( int fd, char *buf );
int  PMI2U_parse_keyvals( char *st );
void PMI2U_dump_keyvals( void );
char *PMI2U_getval( const char *keystr, char *valstr, int vallen );
void PMI2U_chgval( const char *keystr, char *valstr );
int MPIU_Strncpy(char *, const char *, size_t);

#endif /* PMI2UTIL_H_INCLUDED */
