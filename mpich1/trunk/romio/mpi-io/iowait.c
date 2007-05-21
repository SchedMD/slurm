/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPIO_Wait = PMPIO_Wait
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPIO_Wait MPIO_Wait
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPIO_Wait as PMPIO_Wait
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/* status object not filled currently */

/*@
    MPIO_Wait - Waits for the completion of a nonblocking read or write

Input Parameters:
. request - request object (handle)

Output Parameters:
. status - status object (Status)

.N fortran
@*/
#ifdef HAVE_MPI_GREQUEST
int MPIO_Wait(MPIO_Request *request, MPI_Status *status)
{
	return(MPI_Wait(request, status));
}
#else
int MPIO_Wait(MPIO_Request *request, MPI_Status *status)
{
    int error_code;
    static char myname[] = "MPIO_WAIT";

#ifdef MPI_hpux
    int fl_xmpi;

    if (*request != MPIO_REQUEST_NULL) {
	HPMP_IO_WSTART(fl_xmpi, BLKMPIOWAIT, TRDTBLOCK, (*request)->fd);
    }
#endif /* MPI_hpux */

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    if (*request == MPIO_REQUEST_NULL) {
	    error_code = MPI_SUCCESS;
	    goto fn_exit;
    }


    /* --BEGIN ERROR HANDLING-- */
    if ((*request < (MPIO_Request) 0) ||
	((*request)->cookie != ADIOI_REQ_COOKIE))
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_REQUEST,
					  "**request", 0);
	error_code = MPIO_Err_return_file(MPI_FILE_NULL, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    switch ((*request)->optype) {
    case ADIOI_READ:
        ADIO_ReadComplete(request, status, &error_code);
        break;
    case ADIOI_WRITE:
        ADIO_WriteComplete(request, status, &error_code);
        break;
    }

#ifdef MPI_hpux
    HPMP_IO_WEND(fl_xmpi);
#endif /* MPI_hpux */

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();
    return error_code;
}
#endif
