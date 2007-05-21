/* 
 *   $Id: info_delete.c,v 1.11 2001/11/14 20:08:04 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_delete = PMPI_Info_delete
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_delete  MPI_Info_delete
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_delete as PMPI_Info_delete
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

/*@
    MPI_Info_delete - Deletes a (key,value) pair from info

Input Parameters:
+ info - info object (handle)
- key - key (string)

.N fortran
@*/
int MPI_Info_delete(MPI_Info info, char *key)
{
    MPI_Info prev, curr;
    int done;
    static char myname[] = "MPI_INFO_DELETE";
    int mpi_errno;

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

    if (strlen(key) > MPI_MAX_INFO_KEY) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_KEY_TOOLONG,
				     myname, (char *)0, (char *)0, 
				     strlen(key), MPI_MAX_INFO_KEY );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (!strlen(key)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_KEY_EMPTY,
				     myname, (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    prev = info;
    curr = info->next;
    done = 0;

    while (curr) {
	if (!strcmp(curr->key, key)) {
	    FREE(curr->key);   
	    FREE(curr->value);
	    prev->next = curr->next;
	    FREE(curr);
	    done = 1;
	    break;
	}
	prev = curr;
	curr = curr->next;
    }

    if (!done) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_NOKEY, MPIR_ERR_DEFAULT, 
				     myname, (char *)0, (char *)0, key );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    return MPI_SUCCESS;
}
