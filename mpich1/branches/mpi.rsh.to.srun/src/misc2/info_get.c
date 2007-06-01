/* 
 *   $Id: info_get.c,v 1.9 2001/11/14 20:08:05 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_get = PMPI_Info_get
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_get  MPI_Info_get
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_get as PMPI_Info_get
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#if defined(STDC_HEADERS) || defined(HAVE_STRING_H)
#include <string.h>
#endif

/*@
    MPI_Info_get - Retrieves the value associated with a key

Input Parameters:
+ info - info object (handle)
. key - key (string)
- valuelen - length of value argument (integer)

Output Parameters:
+ value - value (string)
- flag - true if key defined, false if not (boolean)

.N fortran
@*/
int MPI_Info_get(MPI_Info info, char *key, int valuelen, char *value, int *flag)
{
    MPI_Info curr;
    int mpi_errno;
    static char myname[] = "MPI_INFO_GET";

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
				     myname, (char *)0, (char *)0,strlen(key), 
				     MPI_MAX_INFO_KEY );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (!strlen(key)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_KEY_EMPTY,
				     myname, (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (valuelen <= 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_INFO_VALLEN, 
				     myname, (char *)0, (char *)0, valuelen );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    if (!value) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_INFO_VAL_INVALID,
				     myname, 
				     "Value is an invalid address", (char *)0);
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    curr = info->next;
    *flag = 0;

    while (curr) {
	if (!strcmp(curr->key, key)) {
	    strncpy(value, curr->value, valuelen);
	    value[valuelen] = '\0';
	    *flag = 1;
	    break;
	}
	curr = curr->next;
    }

    return MPI_SUCCESS;
}
