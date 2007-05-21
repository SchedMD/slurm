/* 
 *   $Id: info_set.c,v 1.12 2001/11/14 20:08:07 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_set = PMPI_Info_set
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_set  MPI_Info_set
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_set as PMPI_Info_set
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "mpimem.h"

#ifdef HAVE_STRING_H
/* For strdup */
#include <string.h> 
#ifndef HAVE_STRDUP
static char *strdup(const char *s1)
{
    char *s2;
    if ( (s2=malloc(strlen(s1)+1)) == NULL ) return NULL;
    return (strcpy(s2,s1));
}
#endif
#endif

/*@
    MPI_Info_set - Adds a (key,value) pair to info

Input Parameters:
+ info - info object (handle)
. key - key (string)
- value - value (string)

.N fortran
@*/
int MPI_Info_set(MPI_Info info, char *key, char *value)
{
    MPI_Info prev, curr;
    int mpi_errno;
    static char myname[] = "MPI_INFO_SET";

    if ((info <= (MPI_Info) 0) || (info->cookie != MPIR_INFO_COOKIE)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO, MPIR_ERR_DEFAULT, myname, 
				     (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (!key) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_DEFAULT, 
				     myname, (char *)0, (char *)0);
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (!value) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_INFO_VAL_INVALID,
				     myname, (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (strlen(key) > MPI_MAX_INFO_KEY) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_KEY_TOOLONG,
				     myname, (char *)0, (char *)0,strlen(key), 
				     MPI_MAX_INFO_KEY );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (strlen(value) > MPI_MAX_INFO_VAL) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_VALUE, 
				     MPIR_ERR_INFO_VALUE_TOOLONG, myname,
				     "Value is longer than MPI_MAX_INFO_VAL",
		     "Value of length %d is longer than MPI_MAX_INFO_VAL = %d",
				     strlen(value), MPI_MAX_INFO_VAL );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (!strlen(key)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_KEY_EMPTY,
				     myname, (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (!strlen(value)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_VALUE, 
				     MPIR_ERR_INFO_VALUE_NULL,
				     myname, (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    prev = info;
    curr = info->next;

    while (curr) {
	if (!strcmp(curr->key, key)) {
	    FREE(curr->value);  
	    curr->value = STRDUP(value);
	    break;
	}
	prev = curr;
	curr = curr->next;
    }

    if (!curr) {
	prev->next   = (MPI_Info) MALLOC(sizeof(struct MPIR_Info));
	curr	     = prev->next;
	curr->cookie = 0;  /* cookie not set on purpose */
	curr->key    = STRDUP(key);
	curr->value  = STRDUP(value);
	curr->next   = 0;
    }

    return MPI_SUCCESS;
}
