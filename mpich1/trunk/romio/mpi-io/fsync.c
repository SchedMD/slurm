/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_sync = PMPI_File_sync
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_sync MPI_File_sync
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_sync as PMPI_File_sync
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_sync - Causes all previous writes to be transferred
                    to the storage device

Input Parameters:
. fh - file handle (handle)

.N fortran
@*/
int MPI_File_sync(MPI_File mpi_fh)
{
    int error_code;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_SYNC";
#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILESYNC, TRDTBLOCK, fh,
		  MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */
    MPID_CS_ENTER();
    MPIR_Nest_incr();

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

    ADIOI_TEST_DEFERRED(fh, "MPI_File_sync", &error_code);

    ADIO_Flush(fh, &error_code);

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, fh, MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */
 
fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();
    return error_code;
}
