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
extern FORTRAN_API void FORT_CALL MPI_FILE_WRITE_ALL_BEGIN( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak MPI_FILE_WRITE_ALL_BEGIN = PMPI_FILE_WRITE_ALL_BEGIN
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_write_all_begin__( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_write_all_begin__ = pmpi_file_write_all_begin__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_write_all_begin( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_write_all_begin = pmpi_file_write_all_begin
#else
extern FORTRAN_API void FORT_CALL mpi_file_write_all_begin_( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#pragma weak mpi_file_write_all_begin_ = pmpi_file_write_all_begin_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_WRITE_ALL_BEGIN MPI_FILE_WRITE_ALL_BEGIN
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_write_all_begin__ mpi_file_write_all_begin__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_write_all_begin mpi_file_write_all_begin
#else
#pragma _HP_SECONDARY_DEF pmpi_file_write_all_begin_ mpi_file_write_all_begin_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_WRITE_ALL_BEGIN as PMPI_FILE_WRITE_ALL_BEGIN
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_write_all_begin__ as pmpi_file_write_all_begin__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_write_all_begin as pmpi_file_write_all_begin
#else
#pragma _CRI duplicate mpi_file_write_all_begin_ as pmpi_file_write_all_begin_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_write_all_begin_ PMPI_FILE_WRITE_ALL_BEGIN
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_write_all_begin_ pmpi_file_write_all_begin__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_write_all_begin pmpi_file_write_all_begin_
#endif
#define mpi_file_write_all_begin_ pmpi_file_write_all_begin
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_write_all_begin_ pmpi_file_write_all_begin
#endif
#define mpi_file_write_all_begin_ pmpi_file_write_all_begin_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_write_all_begin_ MPI_FILE_WRITE_ALL_BEGIN
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_write_all_begin_ mpi_file_write_all_begin__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_write_all_begin mpi_file_write_all_begin_
#endif
#define mpi_file_write_all_begin_ mpi_file_write_all_begin
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_write_all_begin_ mpi_file_write_all_begin
#endif
#endif
#endif

#if defined(MPIHP) || defined(MPILAM)
/* Prototype to keep compiler happy */
void mpi_file_write_all_begin_(MPI_Fint *fh,void *buf,MPI_Fint *count,
			       MPI_Fint *datatype, MPI_Fint *ierr );
void mpi_file_write_all_begin_(MPI_Fint *fh,void *buf,MPI_Fint *count,
                       MPI_Fint *datatype, MPI_Fint *ierr ){
    MPI_File fh_c;
    MPI_Datatype datatype_c;
    
    fh_c = MPI_File_f2c(*fh);
    datatype_c = MPI_Type_f2c(*datatype);

    *ierr = MPI_File_write_all_begin(fh_c,buf,*count,datatype_c);
}
#else
/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_write_all_begin_(MPI_Fint *fh,void *buf,MPI_Fint *count,
			       MPI_Fint *datatype, MPI_Fint *ierr );
FORTRAN_API void FORT_CALL mpi_file_write_all_begin_(MPI_Fint *fh,void *buf,MPI_Fint *count,
                       MPI_Fint *datatype, MPI_Fint *ierr ){
    MPI_File fh_c;
    
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_write_all_begin(fh_c,buf,*count,(MPI_Datatype)*datatype);
}
#endif
