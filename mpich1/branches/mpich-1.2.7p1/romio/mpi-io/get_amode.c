/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_get_amode = PMPI_File_get_amode
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_get_amode MPI_File_get_amode
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_get_amode as PMPI_File_get_amode
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_get_amode - Returns the file access mode

Input Parameters:
. fh - file handle (handle)

Output Parameters:
. amode - access mode (integer)

.N fortran
@*/
int MPI_File_get_amode(MPI_File mpi_fh, int *amode)
{
    int error_code;
    static char myname[] = "MPI_FILE_GET_AMODE";
    ADIO_File fh;
    
    MPID_CS_ENTER();

    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    *amode = fh->access_mode;

fn_exit:
    MPID_CS_EXIT();
    return MPI_SUCCESS;
}
