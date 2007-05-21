/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_read_at_all_begin = PMPI_File_read_at_all_begin
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_read_at_all_begin MPI_File_read_at_all_begin
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_read_at_all_begin as PMPI_File_read_at_all_begin
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_read_at_all_begin - Begin a split collective read using explict offset

Input Parameters:
. fh - file handle (handle)
. offset - file offset (nonnegative integer)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)

.N fortran
@*/
int MPI_File_read_at_all_begin(MPI_File mpi_fh, MPI_Offset offset, void *buf,
			       int count, MPI_Datatype datatype)
{
    int error_code;
    static char myname[] = "MPI_FILE_READ_AT_ALL_BEGIN";

    error_code = MPIOI_File_read_all_begin(mpi_fh, offset,
					   ADIO_EXPLICIT_OFFSET,
					   buf, count, datatype, myname);

    return error_code;
}
