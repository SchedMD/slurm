/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_get_size = PMPI_File_get_size
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_get_size MPI_File_get_size
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_get_size as PMPI_File_get_size
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_get_size - Returns the file size

Input Parameters:
. fh - file handle (handle)

Output Parameters:
. size - size of the file in bytes (nonnegative integer)

.N fortran
@*/
int MPI_File_get_size(MPI_File mpi_fh, MPI_Offset *size)
{
    int error_code;
    ADIO_File fh;
    ADIO_Fcntl_t *fcntl_struct;
    static char myname[] = "MPI_FILE_GET_SIZE";
#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEGETSIZE, TRDTBLOCK, fh,
		  MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    ADIOI_TEST_DEFERRED(fh, myname, &error_code);

    fcntl_struct = (ADIO_Fcntl_t *) ADIOI_Malloc(sizeof(ADIO_Fcntl_t));
    ADIO_Fcntl(fh, ADIO_FCNTL_GET_FSIZE, fcntl_struct, &error_code);
    *size = fcntl_struct->fsize;
    ADIOI_Free(fcntl_struct);

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, fh, MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();

    return error_code;
}
