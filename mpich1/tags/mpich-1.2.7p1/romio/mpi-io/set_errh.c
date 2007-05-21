/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"
#include "adio_extern.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_set_errhandler = PMPI_File_set_errhandler
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_set_errhandler MPI_File_set_errhandler
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_set_errhandler as PMPI_File_set_errhandler
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_set_errhandler - Sets the error handler for a file

Input Parameters:
. fh - file handle (handle)
. errhandler - error handler (handle)

.N fortran
@*/
int MPI_File_set_errhandler(MPI_File mpi_fh, MPI_Errhandler errhandler)
{
    int error_code = MPI_SUCCESS;
    static char myname[] = "MPI_FILE_SET_ERRHANDLER";
    ADIO_File fh;

    MPID_CS_ENTER();

    if (mpi_fh == MPI_FILE_NULL) {
	ADIOI_DFLT_ERR_HANDLER = errhandler;
    }
    else {
	fh = MPIO_File_resolve(mpi_fh);

	/* --BEGIN ERROR HANDLING-- */
	MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
	/* --END ERROR HANDLING-- */

	if ((errhandler != MPI_ERRORS_RETURN) &&
	    (errhandler != MPI_ERRORS_ARE_FATAL))
	{
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__,
					      MPI_ERR_UNSUPPORTED_OPERATION,
					      "**fileopunsupported",
					      0);
	    error_code = MPIO_Err_return_file(fh, error_code);
	    goto fn_exit;
	}

	fh->err_handler = errhandler;
    }

fn_exit:
    MPID_CS_EXIT();
    return error_code;
}
