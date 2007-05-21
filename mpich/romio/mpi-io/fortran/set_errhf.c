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
extern FORTRAN_API void FORT_CALL MPI_FILE_SET_ERRHANDLER( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak MPI_FILE_SET_ERRHANDLER = PMPI_FILE_SET_ERRHANDLER
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_set_errhandler__( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_set_errhandler__ = pmpi_file_set_errhandler__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_set_errhandler( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_set_errhandler = pmpi_file_set_errhandler
#else
extern FORTRAN_API void FORT_CALL mpi_file_set_errhandler_( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_set_errhandler_ = pmpi_file_set_errhandler_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_SET_ERRHANDLER MPI_FILE_SET_ERRHANDLER
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_set_errhandler__ mpi_file_set_errhandler__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_set_errhandler mpi_file_set_errhandler
#else
#pragma _HP_SECONDARY_DEF pmpi_file_set_errhandler_ mpi_file_set_errhandler_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_SET_ERRHANDLER as PMPI_FILE_SET_ERRHANDLER
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_set_errhandler__ as pmpi_file_set_errhandler__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_set_errhandler as pmpi_file_set_errhandler
#else
#pragma _CRI duplicate mpi_file_set_errhandler_ as pmpi_file_set_errhandler_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_set_errhandler_ PMPI_FILE_SET_ERRHANDLER
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_set_errhandler_ pmpi_file_set_errhandler__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_set_errhandler pmpi_file_set_errhandler_
#endif
#define mpi_file_set_errhandler_ pmpi_file_set_errhandler
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_set_errhandler_ pmpi_file_set_errhandler
#endif
#define mpi_file_set_errhandler_ pmpi_file_set_errhandler_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_set_errhandler_ MPI_FILE_SET_ERRHANDLER
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_set_errhandler_ mpi_file_set_errhandler__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_set_errhandler mpi_file_set_errhandler_
#endif
#define mpi_file_set_errhandler_ mpi_file_set_errhandler
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_set_errhandler_ mpi_file_set_errhandler
#endif
#endif
#endif

/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_set_errhandler_(MPI_Fint *fh, MPI_Fint *err_handler, MPI_Fint *ierr);

FORTRAN_API void FORT_CALL mpi_file_set_errhandler_(MPI_Fint *fh, MPI_Fint *err_handler, MPI_Fint *ierr)
{
    MPI_File fh_c;
    MPI_Errhandler err_handler_c;
    
    fh_c = MPI_File_f2c(*fh);
    err_handler_c = MPI_Errhandler_f2c(*err_handler);

    *ierr = MPI_File_set_errhandler(fh_c,err_handler_c);
}

