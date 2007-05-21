/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_preallocate = PMPI_File_preallocate
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_preallocate MPI_File_preallocate
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_preallocate as PMPI_File_preallocate
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_preallocate - Preallocates storage space for a file

Input Parameters:
. fh - file handle (handle)
. size - size to preallocate (nonnegative integer)

.N fortran
@*/
int MPI_File_preallocate(MPI_File mpi_fh, MPI_Offset size)
{
    ADIO_Fcntl_t *fcntl_struct;
    int error_code=0, mynod=0;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_PREALLOCATE";
    MPI_Offset tmp_sz;
#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEPREALLOCATE, TRDTBLOCK,
		  fh, MPI_DATATYPE_NULL, -1);
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

    tmp_sz = size;
    MPI_Bcast(&tmp_sz, 1, ADIO_OFFSET, 0, fh->comm);

    if (tmp_sz != size) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**notsame", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    if (size == 0) return MPI_SUCCESS;

    ADIOI_TEST_DEFERRED(fh, myname, &error_code);

    MPI_Comm_rank(fh->comm, &mynod);
    if (!mynod) {
	fcntl_struct = (ADIO_Fcntl_t *) ADIOI_Malloc(sizeof(ADIO_Fcntl_t));
	fcntl_struct->diskspace = size;
	ADIO_Fcntl(fh, ADIO_FCNTL_SET_DISKSPACE, fcntl_struct, &error_code);
	ADIOI_Free(fcntl_struct);
    }
    MPI_Barrier(fh->comm);
    
#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, fh, MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */


fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();

    /* TODO: bcast result? */
    if (!mynod) return error_code;
    else return MPI_SUCCESS;
}
