/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPIO_Test = PMPIO_Test
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPIO_Test MPIO_Test
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPIO_Test as PMPIO_Test
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/* status object not filled currently */

/*@
    MPIO_Test - Test the completion of a nonblocking read or write
                
Input Parameters:
. request - request object (handle)

Output Parameters:
. flag - true if operation completed (logical)
. status - status object (Status)

.N fortran
@*/
#ifdef HAVE_MPI_GREQUEST
int MPIO_Test(MPIO_Request *request, int *flag, MPI_Status *status)
{
	return (MPI_Test(request, flag, status));
}
#else
int MPIO_Test(MPIO_Request *request, int *flag, MPI_Status *status)
{
    int error_code;
    static char myname[] = "MPIO_TEST";
#ifdef MPI_hpux
    int fl_xmpi;

    if (*request != MPIO_REQUEST_NULL) {
	HPMP_IO_WSTART(fl_xmpi, BLKMPIOTEST, TRDTSYSTEM, (*request)->fd);
    }
#endif /* MPI_hpux */

    MPID_CS_ENTER();

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
        *flag = ADIO_ReadDone(request, status, &error_code);
        break;
    case ADIOI_WRITE:
        *flag = ADIO_WriteDone(request, status, &error_code);
        break;
    }

#ifdef MPI_hpux
    HPMP_IO_WEND(fl_xmpi);
#endif /* MPI_hpux */

fn_exit:
    MPID_CS_EXIT();
    return error_code;
}
#endif
