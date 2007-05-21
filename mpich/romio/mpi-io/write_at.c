/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_write_at = PMPI_File_write_at
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_write_at MPI_File_write_at
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_write_at as PMPI_File_write_at
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/* status object not filled currently */

/*@
    MPI_File_write_at - Write using explict offset

Input Parameters:
. fh - file handle (handle)
. offset - file offset (nonnegative integer)
. buf - initial address of buffer (choice)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. status - status object (Status)

.N fortran
@*/
int MPI_File_write_at(MPI_File mpi_fh, MPI_Offset offset, void *buf,
                      int count, MPI_Datatype datatype, 
                      MPI_Status *status)
{
    int error_code;
    static char myname[] = "MPI_FILE_WRITE_AT";
#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEWRITEAT, TRDTBLOCK, fh, datatype, count);
#endif /* MPI_hpux */

    /* MPIOI_File_write() defined in mpi-io/write.c */
    error_code = MPIOI_File_write(mpi_fh, offset, ADIO_EXPLICIT_OFFSET, buf,
				  count, datatype, myname, status);

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, fh, datatype, count);
#endif /* MPI_hpux */

    return error_code;
}
