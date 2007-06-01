/* 
 *   $Id: info_dup.c,v 1.11 2001/11/14 20:08:05 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_dup = PMPI_Info_dup
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_dup  MPI_Info_dup
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_dup as PMPI_Info_dup
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
    MPI_Info_dup - Returns a duplicate of the info object

Input Parameters:
. info - info object (handle)

Output Parameters:
. newinfo - duplicate of info object (handle)

.N fortran
@*/
int MPI_Info_dup(MPI_Info info, MPI_Info *newinfo)
{
    MPI_Info curr_old, curr_new;
    int mpi_errno;
    static char myname[] = "MPI_INFO_DUP";

    if ((info <= (MPI_Info) 0) || (info->cookie != MPIR_INFO_COOKIE)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO, MPIR_ERR_DEFAULT, myname, 
				     (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    *newinfo = (MPI_Info) MALLOC(sizeof(struct MPIR_Info));
    if (!*newinfo) {
	return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
    }
    curr_new = *newinfo;
    curr_new->cookie = MPIR_INFO_COOKIE;
    curr_new->key = 0;
    curr_new->value = 0;
    curr_new->next = 0;

    curr_old = info->next;
    while (curr_old) {
	curr_new->next = (MPI_Info) MALLOC(sizeof(struct MPIR_Info));
	if (!curr_new->next) {
	    return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
	}
	curr_new	 = curr_new->next;
	curr_new->cookie = 0;  /* cookie not set on purpose */
	curr_new->key	 = STRDUP(curr_old->key);
	curr_new->value	 = STRDUP(curr_old->value);
	curr_new->next	 = 0;
	
	curr_old	 = curr_old->next;
    }

    return MPI_SUCCESS;
}
