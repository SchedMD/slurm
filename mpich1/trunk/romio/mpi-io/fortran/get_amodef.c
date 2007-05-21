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
extern FORTRAN_API void FORT_CALL MPI_FILE_GET_AMODE( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak MPI_FILE_GET_AMODE = PMPI_FILE_GET_AMODE
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_amode__( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_get_amode__ = pmpi_file_get_amode__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_amode( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_get_amode = pmpi_file_get_amode
#else
extern FORTRAN_API void FORT_CALL mpi_file_get_amode_( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_get_amode_ = pmpi_file_get_amode_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_GET_AMODE MPI_FILE_GET_AMODE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_amode__ mpi_file_get_amode__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_amode mpi_file_get_amode
#else
#pragma _HP_SECONDARY_DEF pmpi_file_get_amode_ mpi_file_get_amode_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_GET_AMODE as PMPI_FILE_GET_AMODE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_amode__ as pmpi_file_get_amode__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_amode as pmpi_file_get_amode
#else
#pragma _CRI duplicate mpi_file_get_amode_ as pmpi_file_get_amode_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_get_amode_ PMPI_FILE_GET_AMODE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_amode_ pmpi_file_get_amode__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_amode pmpi_file_get_amode_
#endif
#define mpi_file_get_amode_ pmpi_file_get_amode
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_amode_ pmpi_file_get_amode
#endif
#define mpi_file_get_amode_ pmpi_file_get_amode_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_get_amode_ MPI_FILE_GET_AMODE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_amode_ mpi_file_get_amode__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_amode mpi_file_get_amode_
#endif
#define mpi_file_get_amode_ mpi_file_get_amode
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_amode_ mpi_file_get_amode
#endif
#endif
#endif

/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_get_amode_(MPI_Fint *fh, MPI_Fint *amode, MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpi_file_get_amode_(MPI_Fint *fh, MPI_Fint *amode, MPI_Fint *ierr )
{
    MPI_File fh_c;
    
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_get_amode(fh_c, amode);
}
