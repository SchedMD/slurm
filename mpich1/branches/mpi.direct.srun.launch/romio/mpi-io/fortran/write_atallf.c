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
extern FORTRAN_API void FORT_CALL MPI_FILE_WRITE_AT_ALL( MPI_Fint *, MPI_Offset *, void*, MPI_Fint *, MPI_Fint *, MPI_Status*, MPI_Fint * );
#pragma weak MPI_FILE_WRITE_AT_ALL = PMPI_FILE_WRITE_AT_ALL
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_write_at_all__( MPI_Fint *, MPI_Offset *, void*, MPI_Fint *, MPI_Fint *, MPI_Status*, MPI_Fint * );
#pragma weak mpi_file_write_at_all__ = pmpi_file_write_at_all__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_write_at_all( MPI_Fint *, MPI_Offset *, void*, MPI_Fint *, MPI_Fint *, MPI_Status*, MPI_Fint * );
#pragma weak mpi_file_write_at_all = pmpi_file_write_at_all
#else
extern FORTRAN_API void FORT_CALL mpi_file_write_at_all_( MPI_Fint *, MPI_Offset *, void*, MPI_Fint *, MPI_Fint *, MPI_Status*, MPI_Fint * );
#pragma weak mpi_file_write_at_all_ = pmpi_file_write_at_all_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_WRITE_AT_ALL MPI_FILE_WRITE_AT_ALL
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_write_at_all__ mpi_file_write_at_all__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_write_at_all mpi_file_write_at_all
#else
#pragma _HP_SECONDARY_DEF pmpi_file_write_at_all_ mpi_file_write_at_all_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_WRITE_AT_ALL as PMPI_FILE_WRITE_AT_ALL
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_write_at_all__ as pmpi_file_write_at_all__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_write_at_all as pmpi_file_write_at_all
#else
#pragma _CRI duplicate mpi_file_write_at_all_ as pmpi_file_write_at_all_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_write_at_all_ PMPI_FILE_WRITE_AT_ALL
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_write_at_all_ pmpi_file_write_at_all__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_write_at_all pmpi_file_write_at_all_
#endif
#define mpi_file_write_at_all_ pmpi_file_write_at_all
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_write_at_all_ pmpi_file_write_at_all
#endif
#define mpi_file_write_at_all_ pmpi_file_write_at_all_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_write_at_all_ MPI_FILE_WRITE_AT_ALL
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_write_at_all_ mpi_file_write_at_all__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_write_at_all mpi_file_write_at_all_
#endif
#define mpi_file_write_at_all_ mpi_file_write_at_all
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_write_at_all_ mpi_file_write_at_all
#endif
#endif
#endif

#if defined(MPIHP) || defined(MPILAM)
/* Prototype to keep compiler happy */
void mpi_file_write_at_all_(MPI_Fint *fh,MPI_Offset *offset,void *buf,
                          MPI_Fint *count,MPI_Fint *datatype,
			    MPI_Status *status, MPI_Fint *ierr );

void mpi_file_write_at_all_(MPI_Fint *fh,MPI_Offset *offset,void *buf,
                          MPI_Fint *count,MPI_Fint *datatype,
                          MPI_Status *status, MPI_Fint *ierr )
{
    MPI_File fh_c;
    MPI_Datatype datatype_c;
    
    fh_c = MPI_File_f2c(*fh);
    datatype_c = MPI_Type_f2c(*datatype);

    *ierr = MPI_File_write_at_all(fh_c,*offset,buf,*count,datatype_c,status);
}
#else
/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_write_at_all_(MPI_Fint *fh,MPI_Offset *offset,void *buf,
                          MPI_Fint *count,MPI_Fint *datatype,
			    MPI_Status *status, MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpi_file_write_at_all_(MPI_Fint *fh,MPI_Offset *offset,void *buf,
                          MPI_Fint *count,MPI_Fint *datatype,
                          MPI_Status *status, MPI_Fint *ierr )
{
    MPI_File fh_c;
    
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_write_at_all(fh_c,*offset,buf,*count,*datatype,status);
}
#endif
