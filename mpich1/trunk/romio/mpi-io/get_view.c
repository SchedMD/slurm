/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_get_view = PMPI_File_get_view
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_get_view MPI_File_get_view
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_get_view as PMPI_File_get_view
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif
#ifdef MPISGI
#include "mpisgi2.h"
#endif

/*@
    MPI_File_get_view - Returns the file view

Input Parameters:
. fh - file handle (handle)

Output Parameters:
. disp - displacement (nonnegative integer)
. etype - elementary datatype (handle)
. filetype - filetype (handle)
. datarep - data representation (string)

.N fortran
@*/
int MPI_File_get_view(MPI_File mpi_fh,
		      MPI_Offset *disp,
		      MPI_Datatype *etype,
		      MPI_Datatype *filetype,
		      char *datarep)
{
    int error_code;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_GET_VIEW";
    int i, j, k, combiner;
    MPI_Datatype copy_etype, copy_filetype;


    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);

    if (datarep <= (char *) 0)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG, 
					  "**iodatarepnomem", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    *disp = fh->disp;
    ADIOI_Strncpy(datarep, "native", MPI_MAX_DATAREP_STRING);

    MPI_Type_get_envelope(fh->etype, &i, &j, &k, &combiner);
    if (combiner == MPI_COMBINER_NAMED) *etype = fh->etype;
    else {
        MPIR_Nest_incr();
        MPI_Type_contiguous(1, fh->etype, &copy_etype);
        MPIR_Nest_decr();

        MPIR_Nest_incr();
        MPI_Type_commit(&copy_etype);
        MPIR_Nest_decr();
        *etype = copy_etype;
    }
    MPI_Type_get_envelope(fh->filetype, &i, &j, &k, &combiner);
    if (combiner == MPI_COMBINER_NAMED) *filetype = fh->filetype;
    else {
        MPI_Type_contiguous(1, fh->filetype, &copy_filetype);

        MPI_Type_commit(&copy_filetype);
        *filetype = copy_filetype;
    }

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();

    return MPI_SUCCESS;
}
