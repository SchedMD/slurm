/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_seek_shared = PMPI_File_seek_shared
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_seek_shared MPI_File_seek_shared
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_seek_shared as PMPI_File_seek_shared
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_seek_shared - Updates the shared file pointer

Input Parameters:
. fh - file handle (handle)
. offset - file offset (integer)
. whence - update mode (state)

.N fortran
@*/
int MPI_File_seek_shared(MPI_File mpi_fh, MPI_Offset offset, int whence)
{
    int error_code=MPI_SUCCESS, tmp_whence, myrank;
    static char myname[] = "MPI_FILE_SEEK_SHARED";
    MPI_Offset curr_offset, eof_offset, tmp_offset;
    ADIO_File fh;

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    MPIO_CHECK_NOT_SEQUENTIAL_MODE(fh, myname, error_code);
    MPIO_CHECK_FS_SUPPORTS_SHARED(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    tmp_offset = offset;
    MPI_Bcast(&tmp_offset, 1, ADIO_OFFSET, 0, fh->comm);
    /* --BEGIN ERROR HANDLING-- */
    if (tmp_offset != offset)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**notsame", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    tmp_whence = whence;
    MPI_Bcast(&tmp_whence, 1, MPI_INT, 0, fh->comm);
    /* --BEGIN ERROR HANDLING-- */
    if (tmp_whence != whence)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iobadwhence", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    ADIOI_TEST_DEFERRED(fh, "MPI_File_seek_shared", &error_code);

    MPI_Comm_rank(fh->comm, &myrank);

    if (!myrank)
    {
	switch(whence)
	{
	case MPI_SEEK_SET:
	    /* --BEGIN ERROR HANDLING-- */
	    if (offset < 0)
	    {
		error_code = MPIO_Err_create_code(MPI_SUCCESS,
						  MPIR_ERR_RECOVERABLE,
						  myname, __LINE__,
						  MPI_ERR_ARG,
						  "**iobadoffset", 0);
		error_code = MPIO_Err_return_file(fh, error_code);
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	    break;
	case MPI_SEEK_CUR:
	    /* get current location of shared file pointer */
	    ADIO_Get_shared_fp(fh, 0, &curr_offset, &error_code);
	    /* --BEGIN ERROR HANDLING-- */
	    if (error_code != MPI_SUCCESS)
	    {
		error_code = MPIO_Err_create_code(MPI_SUCCESS,
						  MPIR_ERR_FATAL,
						  myname, __LINE__,
						  MPI_ERR_INTERN, 
						  "**iosharedfailed", 0);
		error_code = MPIO_Err_return_file(fh, error_code);
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	    offset += curr_offset;
	    /* --BEGIN ERROR HANDLING-- */
	    if (offset < 0)
	    {
		error_code = MPIO_Err_create_code(MPI_SUCCESS,
						  MPIR_ERR_RECOVERABLE,
						  myname, __LINE__,
						  MPI_ERR_ARG,
						  "**ionegoffset", 0);
		error_code = MPIO_Err_return_file(fh, error_code);
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	    break;
	case MPI_SEEK_END:
	    /* find offset corr. to end of file */
	    ADIOI_Get_eof_offset(fh, &eof_offset);
	    offset += eof_offset;
	    /* --BEGIN ERROR HANDLING-- */
	    if (offset < 0)
	    {
		error_code = MPIO_Err_create_code(MPI_SUCCESS,
						  MPIR_ERR_RECOVERABLE,
						  myname, __LINE__,
						  MPI_ERR_ARG,
						  "**ionegoffset", 0);
		error_code = MPIO_Err_return_file(fh, error_code);
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	    break;
	default:
	    /* --BEGIN ERROR HANDLING-- */
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "**iobadwhence", 0);
	    error_code = MPIO_Err_return_file(fh, error_code);
	    goto fn_exit;
	    /* --END ERROR HANDLING-- */
	}

	ADIO_Set_shared_fp(fh, offset, &error_code);
	/* --BEGIN ERROR HANDLING-- */
	if (error_code != MPI_SUCCESS)
	{
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_FATAL,
					      myname, __LINE__,
					      MPI_ERR_INTERN, 
					      "**iosharedfailed", 0);
	    error_code = MPIO_Err_return_file(fh, error_code);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */

    }

    /* FIXME: explain why the barrier is necessary */
    MPI_Barrier(fh->comm);

    error_code = MPI_SUCCESS;

fn_exit:
    MPID_CS_EXIT();
    MPIR_Nest_decr();

    return error_code;
}
