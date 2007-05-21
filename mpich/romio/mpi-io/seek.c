/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"
#include "adioi.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_seek = PMPI_File_seek
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_seek MPI_File_seek
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_seek as PMPI_File_seek
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_seek - Updates the individual file pointer

Input Parameters:
. fh - file handle (handle)
. offset - file offset (integer)
. whence - update mode (state)

.N fortran
@*/
int MPI_File_seek(MPI_File mpi_fh, MPI_Offset offset, int whence)
{
    int error_code;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_SEEK";
    MPI_Offset curr_offset, eof_offset;

#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILESEEK, TRDTBLOCK, fh, MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    MPIO_CHECK_NOT_SEQUENTIAL_MODE(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    switch(whence) {
    case MPI_SEEK_SET:
	/* --BEGIN ERROR HANDLING-- */
	if (offset < 0) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE, myname,
					      __LINE__, MPI_ERR_ARG,
					      "**iobadoffset", 0);
	    error_code = MPIO_Err_return_file(fh, error_code);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */
	break;
    case MPI_SEEK_CUR:
	/* find offset corr. to current location of file pointer */
	ADIOI_Get_position(fh, &curr_offset);
	offset += curr_offset;

	/* --BEGIN ERROR HANDLING-- */
	if (offset < 0) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE, myname,
					      __LINE__, MPI_ERR_ARG,
					      "**ionegoffset", 0);
	    error_code = MPIO_Err_return_file(fh, error_code);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */

	break;
    case MPI_SEEK_END:
	/* we can in many cases do seeks w/o a file actually opened, but not in
	 * the MPI_SEEK_END case */
	ADIOI_TEST_DEFERRED(fh, "MPI_File_seek", &error_code);

	/* find offset corr. to end of file */
	ADIOI_Get_eof_offset(fh, &eof_offset);
	offset += eof_offset;

	/* --BEGIN ERROR HANDLING-- */
	if (offset < 0) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE, myname,
					      __LINE__, MPI_ERR_ARG,
					      "**ionegoffset", 0);
	    error_code = MPIO_Err_return_file(fh, error_code);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */

	break;
    default:
	/* --BEGIN ERROR HANDLING-- */
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iobadwhence", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
	/* --END ERROR HANDLING-- */
    }

    ADIO_SeekIndividual(fh, offset, ADIO_SEEK_SET, &error_code);
    /* TODO: what do we do with this error? */

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, fh, MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */

    error_code = MPI_SUCCESS;

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();
    return error_code;
}
