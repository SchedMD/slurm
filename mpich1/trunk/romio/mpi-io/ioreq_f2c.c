/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPIO_Request_f2c = PMPIO_Request_f2c
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPIO_Request_f2c MPIO_Request_f2c
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPIO_Request_f2c as PMPIO_Request_f2c
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif
#include "adio_extern.h"

/*@
    MPIO_Request_f2c - Translates a Fortran I/O-request handle to 
                       a C I/O-request handle

Input Parameters:
. request - Fortran I/O-request handle (integer)

Return Value:
  C I/O-request handle (handle)
@*/
#ifdef HAVE_MPI_GREQUEST
MPIO_Request MPIO_Request_f2c(MPI_Fint request) {
    return((MPIO_Request) request);
}
#else
MPIO_Request MPIO_Request_f2c(MPI_Fint request)
{
    int error_code;
    static char myname[] = "MPIO_REQUEST_F2C";

#ifndef INT_LT_POINTER
    return (MPIO_Request) request;
#else

    MPID_CS_ENTER();
    
    /* --BEGIN ERROR HANDLING-- */
    if (!request) {
	    error_code = MPIO_REQUEST_NULL;
	    goto fn_exit;
    }
    if ((request < 0) || (request > ADIOI_Reqtable_ptr)) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_REQUEST,
					  "**request", 0);
	error_code = MPIO_Err_return_file(MPI_FILE_NULL, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

fn_exit:
    MPID_CS_EXIT();
    return ADIOI_Reqtable[request];
#endif
}
#endif
