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
extern FORTRAN_API void FORT_CALL MPI_FILE_GET_POSITION( MPI_Fint *, MPI_Offset*, MPI_Fint * );
#pragma weak MPI_FILE_GET_POSITION = PMPI_FILE_GET_POSITION
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_position__( MPI_Fint *, MPI_Offset*, MPI_Fint * );
#pragma weak mpi_file_get_position__ = pmpi_file_get_position__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_position( MPI_Fint *, MPI_Offset*, MPI_Fint * );
#pragma weak mpi_file_get_position = pmpi_file_get_position
#else
extern FORTRAN_API void FORT_CALL mpi_file_get_position_( MPI_Fint *, MPI_Offset*, MPI_Fint * );
#pragma weak mpi_file_get_position_ = pmpi_file_get_position_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_GET_POSITION MPI_FILE_GET_POSITION
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_position__ mpi_file_get_position__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_position mpi_file_get_position
#else
#pragma _HP_SECONDARY_DEF pmpi_file_get_position_ mpi_file_get_position_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_GET_POSITION as PMPI_FILE_GET_POSITION
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_position__ as pmpi_file_get_position__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_position as pmpi_file_get_position
#else
#pragma _CRI duplicate mpi_file_get_position_ as pmpi_file_get_position_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_get_position_ PMPI_FILE_GET_POSITION
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_position_ pmpi_file_get_position__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_position pmpi_file_get_position_
#endif
#define mpi_file_get_position_ pmpi_file_get_position
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_position_ pmpi_file_get_position
#endif
#define mpi_file_get_position_ pmpi_file_get_position_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_get_position_ MPI_FILE_GET_POSITION
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_position_ mpi_file_get_position__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_position mpi_file_get_position_
#endif
#define mpi_file_get_position_ mpi_file_get_position
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_position_ mpi_file_get_position
#endif
#endif
#endif

/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_get_position_(MPI_Fint *fh, MPI_Offset *offset, MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpi_file_get_position_(MPI_Fint *fh, MPI_Offset *offset, MPI_Fint *ierr )
{
    MPI_File fh_c;
    
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_get_position(fh_c, offset);
}
