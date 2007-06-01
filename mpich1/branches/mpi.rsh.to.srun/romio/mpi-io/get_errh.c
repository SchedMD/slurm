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
#pragma weak MPI_File_get_errhandler = PMPI_File_get_errhandler
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_get_errhandler MPI_File_get_errhandler
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_get_errhandler as PMPI_File_get_errhandler
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_get_errhandler - Returns the error handler for a file

Input Parameters:
. fh - file handle (handle)

Output Parameters:
. errhandler - error handler (handle)

.N fortran
@*/
int MPI_File_get_errhandler(MPI_File mpi_fh, MPI_Errhandler *errhandler)
{
    int error_code = MPI_SUCCESS;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_GET_ERRHANDLER";

    MPID_CS_ENTER();

    if (mpi_fh == MPI_FILE_NULL) {
	*errhandler = ADIOI_DFLT_ERR_HANDLER;
    }
    else {
	fh = MPIO_File_resolve(mpi_fh);
	/* --BEGIN ERROR HANDLING-- */
	if ((fh <= (MPI_File) 0) || ((fh)->cookie != ADIOI_FILE_COOKIE))
	{
	    error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "**iobadfh", 0);
	    error_code = MPIO_Err_return_file(MPI_FILE_NULL, error_code);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */

	*errhandler = fh->err_handler;
    }

fn_exit:
    MPID_CS_EXIT();
    return MPI_SUCCESS;
}
