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
extern FORTRAN_API void FORT_CALL MPI_FILE_READ_ORDERED( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Status*, MPI_Fint * );
#pragma weak MPI_FILE_READ_ORDERED = PMPI_FILE_READ_ORDERED
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_read_ordered__( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Status*, MPI_Fint * );
#pragma weak mpi_file_read_ordered__ = pmpi_file_read_ordered__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_read_ordered( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Status*, MPI_Fint * );
#pragma weak mpi_file_read_ordered = pmpi_file_read_ordered
#else
extern FORTRAN_API void FORT_CALL mpi_file_read_ordered_( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Status*, MPI_Fint * );
#pragma weak mpi_file_read_ordered_ = pmpi_file_read_ordered_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_READ_ORDERED MPI_FILE_READ_ORDERED
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_read_ordered__ mpi_file_read_ordered__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_read_ordered mpi_file_read_ordered
#else
#pragma _HP_SECONDARY_DEF pmpi_file_read_ordered_ mpi_file_read_ordered_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_READ_ORDERED as PMPI_FILE_READ_ORDERED
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_read_ordered__ as pmpi_file_read_ordered__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_read_ordered as pmpi_file_read_ordered
#else
#pragma _CRI duplicate mpi_file_read_ordered_ as pmpi_file_read_ordered_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_read_ordered_ PMPI_FILE_READ_ORDERED
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_read_ordered_ pmpi_file_read_ordered__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_read_ordered pmpi_file_read_ordered_
#endif
#define mpi_file_read_ordered_ pmpi_file_read_ordered
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_read_ordered_ pmpi_file_read_ordered
#endif
#define mpi_file_read_ordered_ pmpi_file_read_ordered_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_read_ordered_ MPI_FILE_READ_ORDERED
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_read_ordered_ mpi_file_read_ordered__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_read_ordered mpi_file_read_ordered_
#endif
#define mpi_file_read_ordered_ mpi_file_read_ordered
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_read_ordered_ mpi_file_read_ordered
#endif
#endif
#endif

#if defined(MPIHP) || defined(MPILAM)
/* Prototype to keep compiler happy */
void mpi_file_read_ordered_(MPI_Fint *fh,void *buf,MPI_Fint *count,
		    MPI_Fint *datatype,MPI_Status *status, MPI_Fint *ierr );

void mpi_file_read_ordered_(MPI_Fint *fh,void *buf,MPI_Fint *count,
                      MPI_Fint *datatype,MPI_Status *status, MPI_Fint *ierr )
{
    MPI_File fh_c;
    MPI_Datatype datatype_c;
    
    fh_c = MPI_File_f2c(*fh);
    datatype_c = MPI_Type_f2c(*datatype);

    *ierr = MPI_File_read_ordered(fh_c,buf,*count,datatype_c,status);
}
#else
/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_read_ordered_(MPI_Fint *fh,void *buf,MPI_Fint *count,
		    MPI_Fint *datatype,MPI_Status *status, MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpi_file_read_ordered_(MPI_Fint *fh,void *buf,MPI_Fint *count,
                      MPI_Fint *datatype,MPI_Status *status, MPI_Fint *ierr ){
    MPI_File fh_c;
    
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_read_ordered(fh_c,buf,*count,(MPI_Datatype)*datatype,status);
}
#endif
