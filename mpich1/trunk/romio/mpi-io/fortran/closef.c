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
extern FORTRAN_API void FORT_CALL MPI_FILE_CLOSE( MPI_File*, MPI_Fint * );
#pragma weak MPI_FILE_CLOSE = PMPI_FILE_CLOSE
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_close__( MPI_File*, MPI_Fint * );
#pragma weak mpi_file_close__ = pmpi_file_close__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_close( MPI_File*, MPI_Fint * );
#pragma weak mpi_file_close = pmpi_file_close
#else
extern FORTRAN_API void FORT_CALL mpi_file_close_( MPI_File*, MPI_Fint * );
#pragma weak mpi_file_close_ = pmpi_file_close_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_CLOSE MPI_FILE_CLOSE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_close__ mpi_file_close__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_close mpi_file_close
#else
#pragma _HP_SECONDARY_DEF pmpi_file_close_ mpi_file_close_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_CLOSE as PMPI_FILE_CLOSE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_close__ as pmpi_file_close__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_close as pmpi_file_close
#else
#pragma _CRI duplicate mpi_file_close_ as pmpi_file_close_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_close_ PMPI_FILE_CLOSE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_close_ pmpi_file_close__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_close pmpi_file_close_
#endif
#define mpi_file_close_ pmpi_file_close
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_close_ pmpi_file_close
#endif
#define mpi_file_close_ pmpi_file_close_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_close_ MPI_FILE_CLOSE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_close_ mpi_file_close__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_close mpi_file_close_
#endif
#define mpi_file_close_ mpi_file_close
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_close_ mpi_file_close
#endif
#endif
#endif

/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_close_(MPI_Fint *fh, MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpi_file_close_(MPI_Fint *fh, MPI_Fint *ierr )
{
    MPI_File fh_c;

    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_close(&fh_c);
    *fh = MPI_File_c2f(fh_c);
}
