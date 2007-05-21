/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_get_atomicity = PMPI_File_get_atomicity
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_get_atomicity MPI_File_get_atomicity
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_get_atomicity as PMPI_File_get_atomicity
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_get_atomicity - Returns the atomicity mode

Input Parameters:
. fh - file handle (handle)

Output Parameters:
. flag - true if atomic mode, false if nonatomic mode (logical)

.N fortran
@*/
int MPI_File_get_atomicity(MPI_File mpi_fh, int *flag)
{
    int error_code;
    ADIO_File fh;
    static char myname[] = "MPI_FILE_GET_ATOMICITY";
    
    MPID_CS_ENTER();
    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    *flag = fh->atomicity;

fn_exit:
    MPID_CS_EXIT();
    return MPI_SUCCESS;
}
