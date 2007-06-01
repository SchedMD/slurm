/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_write_all_begin = PMPI_File_write_all_begin
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_write_all_begin MPI_File_write_all_begin
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_write_all_begin as PMPI_File_write_all_begin
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_write_all_begin - Begin a split collective write using
    individual file pointer

Input Parameters:
. fh - file handle (handle)
. buf - initial address of buffer (choice)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

.N fortran
@*/
int MPI_File_write_all_begin(MPI_File mpi_fh, void *buf, int count, 
			     MPI_Datatype datatype)
{
    int error_code;
    static char myname[] = "MPI_FILE_WRITE_ALL_BEGIN";

    error_code = MPIOI_File_write_all_begin(mpi_fh, (MPI_Offset) 0,
					    ADIO_INDIVIDUAL, buf, count,
					    datatype, myname);

    return error_code;
}

/* prevent multiple definitions of this routine */
#ifdef MPIO_BUILD_PROFILING
int MPIOI_File_write_all_begin(MPI_File mpi_fh,
			       MPI_Offset offset,
			       int file_ptr_type,
			       void *buf,
			       int count,
			       MPI_Datatype datatype,
			       char *myname)
{
    int error_code, datatype_size;
    ADIO_File fh;

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    MPIO_CHECK_COUNT(fh, count, myname, error_code);
    MPIO_CHECK_DATATYPE(fh, datatype, myname, error_code);
    MPIO_CHECK_NOT_SEQUENTIAL_MODE(fh, myname, error_code);

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET && offset < 0)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iobadoffset", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }

    if (fh->split_coll_count)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_IO, 
					  "**iosplitcoll", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    fh->split_coll_count = 1;

    MPI_Type_size(datatype, &datatype_size);
    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_INTEGRAL_ETYPE(fh, count, datatype_size, myname, error_code);
    /* --END ERROR HANDLING-- */

    fh->split_datatype = datatype;
    ADIO_WriteStridedColl(fh, buf, count, datatype, file_ptr_type,
			  offset, &fh->split_status, &error_code);
fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();

    return error_code;
}
#endif
