/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_close = PMPI_File_close
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_close MPI_File_close
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_close as PMPI_File_close
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_close - Closes a file

Input Parameters:
. fh - file handle (handle)

.N fortran
@*/
int MPI_File_close(MPI_File *mpi_fh)
{
    int error_code;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_CLOSE";
#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_WSTART(fl_xmpi, BLKMPIFILECLOSE, TRDTBLOCK, *fh);
#endif /* MPI_hpux */

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    fh = MPIO_File_resolve(*mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    if (((fh)->file_system != ADIO_PIOFS) &&
	((fh)->file_system != ADIO_PVFS) &&
	((fh)->file_system != ADIO_PVFS2) &&
	((fh)->file_system != ADIO_GRIDFTP))
    {
	ADIOI_Free((fh)->shared_fp_fname);
        /* need a barrier because the file containing the shared file
        pointer is opened with COMM_SELF. We don't want it to be
	deleted while others are still accessing it. */ 
        MPI_Barrier((fh)->comm);
	if ((fh)->shared_fp_fd != ADIO_FILE_NULL)
	    ADIO_Close((fh)->shared_fp_fd, &error_code);
    }

    ADIO_Close(fh, &error_code);
    MPIO_File_free(mpi_fh);

#ifdef MPI_hpux
    HPMP_IO_WEND(fl_xmpi);
#endif /* MPI_hpux */

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();
    return error_code;
}
