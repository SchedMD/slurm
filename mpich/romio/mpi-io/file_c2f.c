/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_c2f = PMPI_File_c2f
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_c2f MPI_File_c2f
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_c2f as PMPI_File_c2f
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif
#include "adio_extern.h"

/*@
    MPI_File_c2f - Translates a C file handle to a Fortran file handle

Input Parameters:
. fh - C file handle (handle)

Return Value:
  Fortran file handle (integer)
@*/
MPI_Fint MPI_File_c2f(MPI_File mpi_fh)
{
    return MPIO_File_c2f(mpi_fh);
}
