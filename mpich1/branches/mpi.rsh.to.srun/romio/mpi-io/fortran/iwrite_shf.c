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
extern FORTRAN_API void FORT_CALL MPI_FILE_IWRITE_SHARED( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint*, MPI_Fint * );
#pragma weak MPI_FILE_IWRITE_SHARED = PMPI_FILE_IWRITE_SHARED
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_iwrite_shared__( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint*, MPI_Fint * );
#pragma weak mpi_file_iwrite_shared__ = pmpi_file_iwrite_shared__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_iwrite_shared( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint*, MPI_Fint * );
#pragma weak mpi_file_iwrite_shared = pmpi_file_iwrite_shared
#else
extern FORTRAN_API void FORT_CALL mpi_file_iwrite_shared_( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint*, MPI_Fint * );
#pragma weak mpi_file_iwrite_shared_ = pmpi_file_iwrite_shared_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_IWRITE_SHARED MPI_FILE_IWRITE_SHARED
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_iwrite_shared__ mpi_file_iwrite_shared__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_iwrite_shared mpi_file_iwrite_shared
#else
#pragma _HP_SECONDARY_DEF pmpi_file_iwrite_shared_ mpi_file_iwrite_shared_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_IWRITE_SHARED as PMPI_FILE_IWRITE_SHARED
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_iwrite_shared__ as pmpi_file_iwrite_shared__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_iwrite_shared as pmpi_file_iwrite_shared
#else
#pragma _CRI duplicate mpi_file_iwrite_shared_ as pmpi_file_iwrite_shared_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_iwrite_shared_ PMPI_FILE_IWRITE_SHARED
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_iwrite_shared_ pmpi_file_iwrite_shared__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_iwrite_shared pmpi_file_iwrite_shared_
#endif
#define mpi_file_iwrite_shared_ pmpi_file_iwrite_shared
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_iwrite_shared_ pmpi_file_iwrite_shared
#endif
#define mpi_file_iwrite_shared_ pmpi_file_iwrite_shared_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_iwrite_shared_ MPI_FILE_IWRITE_SHARED
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_iwrite_shared_ mpi_file_iwrite_shared__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_iwrite_shared mpi_file_iwrite_shared_
#endif
#define mpi_file_iwrite_shared_ mpi_file_iwrite_shared
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_iwrite_shared_ mpi_file_iwrite_shared
#endif
#endif
#endif

#if defined(MPIHP) || defined(MPILAM)
/* Prototype to keep compiler happy */
void mpi_file_iwrite_shared_(MPI_Fint *fh,void *buf,int *count,
			     MPI_Fint *datatype,MPI_Fint *request, MPI_Fint *ierr );
void mpi_file_iwrite_shared_(MPI_Fint *fh,void *buf,int *count,
                    MPI_Fint *datatype,MPI_Fint *request, MPI_Fint *ierr )
{
    MPI_File fh_c;
    MPIO_Request req_c;
    MPI_Datatype datatype_c;
    
    fh_c = MPI_File_f2c(*fh);
    datatype_c = MPI_Type_f2c(*datatype);

    *ierr = MPI_File_iwrite_shared(fh_c,buf,*count,datatype_c,&req_c);
    *request = MPIO_Request_c2f(req_c);
}
#else
/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_iwrite_shared_(MPI_Fint *fh,void *buf,int *count,
			     MPI_Datatype *datatype,MPI_Fint *request, MPI_Fint *ierr ); 

FORTRAN_API void FORT_CALL mpi_file_iwrite_shared_(MPI_Fint *fh,void *buf,int *count,
                    MPI_Datatype *datatype,MPI_Fint *request, MPI_Fint *ierr )
{
    MPI_File fh_c;
    MPIO_Request req_c;
    
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_iwrite_shared(fh_c,buf,*count,*datatype,&req_c);
    *request = MPIO_Request_c2f(req_c);
}
#endif
