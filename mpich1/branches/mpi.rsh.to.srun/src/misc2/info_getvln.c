/* 
 *   $Id: info_getvln.c,v 1.8 2001/11/14 20:08:06 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_get_valuelen = PMPI_Info_get_valuelen
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_get_valuelen  MPI_Info_get_valuelen
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_get_valuelen as PMPI_Info_get_valuelen
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/*@
    MPI_Info_get_valuelen - Retrieves the length of the value associated with a key

Input Parameters:
+ info - info object (handle)
- key - key (string)

Output Parameters:
+ valuelen - length of value argument (integer)
- flag - true if key defined, false if not (boolean)

.N fortran
@*/
int MPI_Info_get_valuelen(MPI_Info info, char *key, int *valuelen, int *flag)
{
    MPI_Info curr;
    int mpi_errno;
    static char myname[] = "MPI_INFO_GET_VALUELEN";

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


    curr = info->next;
    *flag = 0;

    while (curr) {
	if (!strcmp(curr->key, key)) {
	    *valuelen = strlen(curr->value);
	    *flag = 1;
	    break;
	}
	curr = curr->next;
    }

    return MPI_SUCCESS;
}
