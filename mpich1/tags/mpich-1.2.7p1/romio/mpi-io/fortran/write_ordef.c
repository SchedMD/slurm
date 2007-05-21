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
extern FORTRAN_API void FORT_CALL MPI_FILE_WRITE_ORDERED_END( MPI_Fint *, void*, MPI_Status*, MPI_Fint * );
#pragma weak MPI_FILE_WRITE_ORDERED_END = PMPI_FILE_WRITE_ORDERED_END
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_write_ordered_end__( MPI_Fint *, void*, MPI_Status*, MPI_Fint * );
#pragma weak mpi_file_write_ordered_end__ = pmpi_file_write_ordered_end__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_write_ordered_end( MPI_Fint *, void*, MPI_Status*, MPI_Fint * );
#pragma weak mpi_file_write_ordered_end = pmpi_file_write_ordered_end
#else
extern FORTRAN_API void FORT_CALL mpi_file_write_ordered_end_( MPI_Fint *, void*, MPI_Status*, MPI_Fint * );
#pragma weak mpi_file_write_ordered_end_ = pmpi_file_write_ordered_end_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_WRITE_ORDERED_END MPI_FILE_WRITE_ORDERED_END
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_write_ordered_end__ mpi_file_write_ordered_end__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_write_ordered_end mpi_file_write_ordered_end
#else
#pragma _HP_SECONDARY_DEF pmpi_file_write_ordered_end_ mpi_file_write_ordered_end_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_WRITE_ORDERED_END as PMPI_FILE_WRITE_ORDERED_END
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_write_ordered_end__ as pmpi_file_write_ordered_end__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_write_ordered_end as pmpi_file_write_ordered_end
#else
#pragma _CRI duplicate mpi_file_write_ordered_end_ as pmpi_file_write_ordered_end_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_write_ordered_end_ PMPI_FILE_WRITE_ORDERED_END
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_write_ordered_end_ pmpi_file_write_ordered_end__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_write_ordered_end pmpi_file_write_ordered_end_
#endif
#define mpi_file_write_ordered_end_ pmpi_file_write_ordered_end
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_write_ordered_end_ pmpi_file_write_ordered_end
#endif
#define mpi_file_write_ordered_end_ pmpi_file_write_ordered_end_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_write_ordered_end_ MPI_FILE_WRITE_ORDERED_END
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_write_ordered_end_ mpi_file_write_ordered_end__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_write_ordered_end mpi_file_write_ordered_end_
#endif
#define mpi_file_write_ordered_end_ mpi_file_write_ordered_end
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_write_ordered_end_ mpi_file_write_ordered_end
#endif
#endif
#endif

/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_write_ordered_end_(MPI_Fint *fh,void *buf,MPI_Status *status, 
				 MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpi_file_write_ordered_end_(MPI_Fint *fh,void *buf,MPI_Status *status, MPI_Fint *ierr ){
    MPI_File fh_c;
    
    fh_c = MPI_File_f2c(*fh);

    *ierr = MPI_File_write_ordered_end(fh_c,buf,status);
}
