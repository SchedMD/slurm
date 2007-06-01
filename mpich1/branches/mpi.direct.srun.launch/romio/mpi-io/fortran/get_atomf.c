/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "mpio.h"


#if defined(MPIO_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
extern FORTRAN_API void FORT_CALL MPI_FILE_GET_ATOMICITY( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak MPI_FILE_GET_ATOMICITY = PMPI_FILE_GET_ATOMICITY
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_atomicity__( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_get_atomicity__ = pmpi_file_get_atomicity__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_atomicity( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_get_atomicity = pmpi_file_get_atomicity
#else
extern FORTRAN_API void FORT_CALL mpi_file_get_atomicity_( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_get_atomicity_ = pmpi_file_get_atomicity_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_GET_ATOMICITY MPI_FILE_GET_ATOMICITY
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_atomicity__ mpi_file_get_atomicity__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_atomicity mpi_file_get_atomicity
#else
#pragma _HP_SECONDARY_DEF pmpi_file_get_atomicity_ mpi_file_get_atomicity_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_GET_ATOMICITY as PMPI_FILE_GET_ATOMICITY
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_atomicity__ as pmpi_file_get_atomicity__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_atomicity as pmpi_file_get_atomicity
#else
#pragma _CRI duplicate mpi_file_get_atomicity_ as pmpi_file_get_atomicity_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_get_atomicity_ PMPI_FILE_GET_ATOMICITY
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_atomicity_ pmpi_file_get_atomicity__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_atomicity pmpi_file_get_atomicity_
#endif
#define mpi_file_get_atomicity_ pmpi_file_get_atomicity
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_atomicity_ pmpi_file_get_atomicity
#endif
#define mpi_file_get_atomicity_ pmpi_file_get_atomicity_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_get_atomicity_ MPI_FILE_GET_ATOMICITY
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_atomicity_ mpi_file_get_atomicity__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_atomicity mpi_file_get_atomicity_
#endif
#define mpi_file_get_atomicity_ mpi_file_get_atomicity
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_atomicity_ mpi_file_get_atomicity
#endif
#endif
#endif

/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_get_atomicity_(MPI_Fint *fh, MPI_Fint *flag, MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpi_file_get_atomicity_(MPI_Fint *fh, MPI_Fint *flag, MPI_Fint *ierr )
{
    MPI_File fh_c;
    
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_get_atomicity(fh_c, flag);
}

