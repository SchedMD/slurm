/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_set_size = PMPI_File_set_size
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_set_size MPI_File_set_size
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_set_size as PMPI_File_set_size
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_set_size - Sets the file size

Input Parameters:
. fh - file handle (handle)
. size - size to truncate or expand file (nonnegative integer)

.N fortran
@*/
int MPI_File_set_size(MPI_File mpi_fh, MPI_Offset size)
{
    int error_code;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_SET_SIZE";
    MPI_Offset tmp_sz;

#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILESETSIZE, TRDTBLOCK, fh,
		  MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);

    if (size < 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iobadsize", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    tmp_sz = size;
    MPI_Bcast(&tmp_sz, 1, ADIO_OFFSET, 0, fh->comm);

    /* --BEGIN ERROR HANDLING-- */
    if (tmp_sz != size) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**notsame", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    ADIOI_TEST_DEFERRED(fh, "MPI_File_set_size", &error_code);

    ADIO_Resize(fh, size, &error_code);
    /* TODO: what to do with error code? */
    
#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, fh, MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();

    return error_code;
}
