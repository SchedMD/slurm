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
extern FORTRAN_API void FORT_CALL MPI_FILE_SEEK( MPI_Fint *, MPI_Offset *, MPI_Fint *, MPI_Fint * );
#pragma weak MPI_FILE_SEEK = PMPI_FILE_SEEK
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_seek__( MPI_Fint *, MPI_Offset *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_seek__ = pmpi_file_seek__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_seek( MPI_Fint *, MPI_Offset *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_seek = pmpi_file_seek
#else
extern FORTRAN_API void FORT_CALL mpi_file_seek_( MPI_Fint *, MPI_Offset *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_seek_ = pmpi_file_seek_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_SEEK MPI_FILE_SEEK
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_seek__ mpi_file_seek__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_seek mpi_file_seek
#else
#pragma _HP_SECONDARY_DEF pmpi_file_seek_ mpi_file_seek_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_SEEK as PMPI_FILE_SEEK
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_seek__ as pmpi_file_seek__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_seek as pmpi_file_seek
#else
#pragma _CRI duplicate mpi_file_seek_ as pmpi_file_seek_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_seek_ PMPI_FILE_SEEK
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_seek_ pmpi_file_seek__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_seek pmpi_file_seek_
#endif
#define mpi_file_seek_ pmpi_file_seek
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_seek_ pmpi_file_seek
#endif
#define mpi_file_seek_ pmpi_file_seek_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_seek_ MPI_FILE_SEEK
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_seek_ mpi_file_seek__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_seek mpi_file_seek_
#endif
#define mpi_file_seek_ mpi_file_seek
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_seek_ mpi_file_seek
#endif
#endif
#endif

/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_seek_(MPI_Fint *fh,MPI_Offset *offset,MPI_Fint *whence, MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpi_file_seek_(MPI_Fint *fh,MPI_Offset *offset,MPI_Fint *whence, MPI_Fint *ierr )
{
    MPI_File fh_c;
    
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_seek(fh_c,*offset,*whence);
}
