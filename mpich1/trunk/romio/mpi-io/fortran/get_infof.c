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
extern FORTRAN_API void FORT_CALL MPI_FILE_GET_INFO( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak MPI_FILE_GET_INFO = PMPI_FILE_GET_INFO
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_info__( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_get_info__ = pmpi_file_get_info__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_info( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_get_info = pmpi_file_get_info
#else
extern FORTRAN_API void FORT_CALL mpi_file_get_info_( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_get_info_ = pmpi_file_get_info_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_GET_INFO MPI_FILE_GET_INFO
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_info__ mpi_file_get_info__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_info mpi_file_get_info
#else
#pragma _HP_SECONDARY_DEF pmpi_file_get_info_ mpi_file_get_info_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_GET_INFO as PMPI_FILE_GET_INFO
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_info__ as pmpi_file_get_info__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_info as pmpi_file_get_info
#else
#pragma _CRI duplicate mpi_file_get_info_ as pmpi_file_get_info_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_get_info_ PMPI_FILE_GET_INFO
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_info_ pmpi_file_get_info__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_info pmpi_file_get_info_
#endif
#define mpi_file_get_info_ pmpi_file_get_info
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_info_ pmpi_file_get_info
#endif
#define mpi_file_get_info_ pmpi_file_get_info_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_get_info_ MPI_FILE_GET_INFO
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_info_ mpi_file_get_info__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_info mpi_file_get_info_
#endif
#define mpi_file_get_info_ mpi_file_get_info
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_info_ mpi_file_get_info
#endif
#endif
#endif

/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_get_info_(MPI_Fint *fh, MPI_Fint *info_used, MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpi_file_get_info_(MPI_Fint *fh, MPI_Fint *info_used, MPI_Fint *ierr )
{
    MPI_File fh_c;
    MPI_Info info_used_c;
    
    fh_c = MPI_File_f2c(*fh);

    *ierr = MPI_File_get_info(fh_c, &info_used_c);
    *info_used = MPI_Info_c2f(info_used_c);
}
