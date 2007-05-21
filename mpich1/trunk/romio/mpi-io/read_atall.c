/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_read_at_all = PMPI_File_read_at_all
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_read_at_all MPI_File_read_at_all
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_read_at_all as PMPI_File_read_at_all
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/* status object not filled currently */

/*@
    MPI_File_read_at_all - Collective read using explict offset

Input Parameters:
. fh - file handle (handle)
. offset - file offset (nonnegative integer)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)
. status - status object (Status)

.N fortran
@*/
int MPI_File_read_at_all(MPI_File mpi_fh, MPI_Offset offset, void *buf,
                         int count, MPI_Datatype datatype, 
                         MPI_Status *status)
{
    int error_code;
    static char myname[] = "MPI_FILE_IREAD_AT";
#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEREADATALL, TRDTBLOCK, mpi_fh, datatype,
		  count);
#endif /* MPI_hpux */

    error_code = MPIOI_File_read_all(mpi_fh, offset, ADIO_EXPLICIT_OFFSET, buf,
				     count, datatype, myname, status);

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, mpi_fh, datatype, count);
#endif /* MPI_hpux */

    return error_code;
}
