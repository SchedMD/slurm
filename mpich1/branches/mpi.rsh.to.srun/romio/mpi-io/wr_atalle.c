/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_write_at_all_end = PMPI_File_write_at_all_end
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_write_at_all_end MPI_File_write_at_all_end
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_write_at_all_end as PMPI_File_write_at_all_end
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_write_at_all_end - Complete a split collective write using explict offset

Input Parameters:
. fh - file handle (handle)
. buf - initial address of buffer (choice)

Output Parameters:
. status - status object (Status)

.N fortran
@*/
int MPI_File_write_at_all_end(MPI_File mpi_fh, void *buf, MPI_Status *status)
{
    int error_code;
    static char myname[] = "MPI_FILE_WRITE_AT_ALL_END";

    error_code = MPIOI_File_write_all_end(mpi_fh, buf, myname, status);

    return error_code;
}
