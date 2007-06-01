/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_set_view = PMPI_File_set_view
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_set_view MPI_File_set_view
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_set_view as PMPI_File_set_view
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_set_view - Sets the file view

Input Parameters:
. fh - file handle (handle)
. disp - displacement (nonnegative integer)
. etype - elementary datatype (handle)
. filetype - filetype (handle)
. datarep - data representation (string)
. info - info object (handle)

.N fortran
@*/
int MPI_File_set_view(MPI_File mpi_fh, MPI_Offset disp, MPI_Datatype etype,
		      MPI_Datatype filetype, char *datarep, MPI_Info info)
{
    int filetype_size, etype_size, error_code;
    static char myname[] = "MPI_FILE_SET_VIEW";
    ADIO_Offset shared_fp, byte_off;
    ADIO_File fh;

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);

    if ((disp < 0) && (disp != MPI_DISPLACEMENT_CURRENT))
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG, 
					  "**iobaddisp", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }

    /* rudimentary checks for incorrect etype/filetype.*/
    if (etype == MPI_DATATYPE_NULL) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**ioetype", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }

    if (filetype == MPI_DATATYPE_NULL) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iofiletype", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }

    if ((fh->access_mode & MPI_MODE_SEQUENTIAL) &&
	(disp != MPI_DISPLACEMENT_CURRENT))
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG, 
					  "**iodispifseq", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }

    if ((disp == MPI_DISPLACEMENT_CURRENT) &&
	!(fh->access_mode & MPI_MODE_SEQUENTIAL))
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG, 
					  "**iodispifseq", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    MPI_Type_size(filetype, &filetype_size);
    MPI_Type_size(etype, &etype_size);

    /* --BEGIN ERROR HANDLING-- */
    if (filetype_size % etype_size != 0)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iofiletype", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }

    if (strcmp(datarep, "native") && strcmp(datarep, "NATIVE"))
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__,
					  MPI_ERR_UNSUPPORTED_DATAREP, 
					  "**unsupporteddatarep",0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    if (disp == MPI_DISPLACEMENT_CURRENT) {
	MPI_Barrier(fh->comm);
	ADIO_Get_shared_fp(fh, 0, &shared_fp, &error_code);
	/* TODO: check error code */

	MPI_Barrier(fh->comm); 
	ADIOI_Get_byte_offset(fh, shared_fp, &byte_off);
	/* TODO: check error code */

	disp = byte_off;
    }

    ADIO_Set_view(fh, disp, etype, filetype, info, &error_code);

    /* reset shared file pointer to zero */
    if ((fh->file_system != ADIO_PIOFS) &&
	(fh->file_system != ADIO_PVFS) &&
	(fh->file_system != ADIO_PVFS2) && 
        (fh->shared_fp_fd != ADIO_FILE_NULL))
    {
	/* only one process needs to set it to zero, but I don't want to 
	   create the shared-file-pointer file if shared file pointers have 
	   not been used so far. Therefore, every process that has already 
	   opened the shared-file-pointer file sets the shared file pointer 
	   to zero. If the file was not opened, the value is automatically 
	   zero. Note that shared file pointer is stored as no. of etypes
	   relative to the current view, whereas indiv. file pointer is
	   stored in bytes. */

	ADIO_Set_shared_fp(fh, 0, &error_code);
    }

    if ((fh->file_system != ADIO_PIOFS) &&
	(fh->file_system != ADIO_PVFS) &&
	(fh->file_system != ADIO_PVFS2 ))
    {
	MPI_Barrier(fh->comm); /* for above to work correctly */
    }

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();

    return error_code;
}
