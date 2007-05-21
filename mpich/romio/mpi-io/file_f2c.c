/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_f2c = PMPI_File_f2c
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_f2c MPI_File_f2c
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_f2c as PMPI_File_f2c
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif
#include "adio_extern.h"

/*@
    MPI_File_f2c - Translates a Fortran file handle to a C file handle

Input Parameters:
. fh - Fortran file handle (integer)

Return Value:
  C file handle (handle)
@*/
MPI_File MPI_File_f2c(MPI_Fint fh)
{
    return MPIO_File_f2c(fh);
}
