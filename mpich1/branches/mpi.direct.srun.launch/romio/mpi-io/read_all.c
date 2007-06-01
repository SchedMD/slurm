/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_read_all = PMPI_File_read_all
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_read_all MPI_File_read_all
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_read_all as PMPI_File_read_all
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/* status object not filled currently */

/*@
    MPI_File_read_all - Collective read using individual file pointer

Input Parameters:
. fh - file handle (handle)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)
. status - status object (Status)

.N fortran
@*/
int MPI_File_read_all(MPI_File mpi_fh, void *buf, int count, 
                      MPI_Datatype datatype, MPI_Status *status)
{
    int error_code;
    static char myname[] = "MPI_FILE_READ_ALL";
#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEREADALL, TRDTBLOCK, fh, datatype, count);
#endif /* MPI_hpux */

    error_code = MPIOI_File_read_all(mpi_fh, (MPI_Offset) 0,
				     ADIO_INDIVIDUAL, buf,
				     count, datatype, myname, status);

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, fh, datatype, count);
#endif /* MPI_hpux */

    return error_code;
}

/* Note: MPIOI_File_read_all also used by MPI_File_read_at_all */
/* prevent multiple definitions of this routine */
#ifdef MPIO_BUILD_PROFILING
int MPIOI_File_read_all(MPI_File mpi_fh,
			MPI_Offset offset,
			int file_ptr_type,
			void *buf,
			int count,
			MPI_Datatype datatype,
			char *myname,
			MPI_Status *status)
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

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET && offset < 0)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iobadoffset", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    MPI_Type_size(datatype, &datatype_size);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_INTEGRAL_ETYPE(fh, count, datatype_size, myname, error_code);
    MPIO_CHECK_READABLE(fh, myname, error_code);
    MPIO_CHECK_NOT_SEQUENTIAL_MODE(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    ADIO_ReadStridedColl(fh, buf, count, datatype, file_ptr_type,
			 offset, status, &error_code);
fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT()

    return error_code;
}
#endif
