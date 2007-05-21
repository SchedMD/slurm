/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#ifdef _UNICOS
#include <fortran.h>
#endif
#include "adio.h"
#include "mpio.h"


#if defined(MPIO_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
extern FORTRAN_API void FORT_CALL MPI_FILE_DELETE( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak MPI_FILE_DELETE = PMPI_FILE_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_delete__( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak mpi_file_delete__ = pmpi_file_delete__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_delete( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak mpi_file_delete = pmpi_file_delete
#else
extern FORTRAN_API void FORT_CALL mpi_file_delete_( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak mpi_file_delete_ = pmpi_file_delete_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_DELETE MPI_FILE_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_delete__ mpi_file_delete__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_delete mpi_file_delete
#else
#pragma _HP_SECONDARY_DEF pmpi_file_delete_ mpi_file_delete_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_DELETE as PMPI_FILE_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_delete__ as pmpi_file_delete__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_delete as pmpi_file_delete
#else
#pragma _CRI duplicate mpi_file_delete_ as pmpi_file_delete_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_delete_ PMPI_FILE_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_delete_ pmpi_file_delete__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_delete pmpi_file_delete_
#endif
#define mpi_file_delete_ pmpi_file_delete
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_delete_ pmpi_file_delete
#endif
#define mpi_file_delete_ pmpi_file_delete_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_delete_ MPI_FILE_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_delete_ mpi_file_delete__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_delete mpi_file_delete_
#endif
#define mpi_file_delete_ mpi_file_delete
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_delete_ mpi_file_delete
#endif
#endif
#endif

/* Prototype to keep compiler happy */
/*
FORTRAN_API void FORT_CALL mpi_file_delete_(char *filename, MPI_Fint *info, int *ierr, int str_len);

#ifdef _UNICOS
void mpi_file_delete_(_fcd filename_fcd, MPI_Fint *info, int *ierr)
{
    char *filename = _fcdtocp(filename_fcd);
    int str_len = _fcdlen(filename_fcd);
#else
FORTRAN_API void FORT_CALL mpi_file_delete_(char *filename, MPI_Fint *info, int *ierr, int str_len)
*/
/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_delete_(char *filename FORT_MIXED_LEN_DECL, MPI_Fint *info, MPI_Fint *ierr FORT_END_LEN_DECL);

#ifdef _UNICOS
void mpi_file_delete_(_fcd filename_fcd, MPI_Fint *info, MPI_Fint *ierr)
{
    char *filename = _fcdtocp(filename_fcd);
    int str_len = _fcdlen(filename_fcd);
#else
FORTRAN_API void FORT_CALL mpi_file_delete_(char *filename FORT_MIXED_LEN(str_len), MPI_Fint *info, MPI_Fint *ierr FORT_END_LEN(str_len))
{
#endif
    char *newfname;
    int real_len, i;
    MPI_Info info_c;

    info_c = MPI_Info_f2c(*info);

    /* strip trailing blanks */
    if (filename <= (char *) 0) {
        FPRINTF(stderr, "MPI_File_delete: filename is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    for (i=str_len-1; i>=0; i--) if (filename[i] != ' ') break;
    if (i < 0) {
        FPRINTF(stderr, "MPI_File_delete: filename is a blank string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    real_len = i + 1;

    newfname = (char *) ADIOI_Malloc((real_len+1)*sizeof(char));
    ADIOI_Strncpy(newfname, filename, real_len);
    newfname[real_len] = '\0';

    *ierr = MPI_File_delete(newfname, info_c);

    ADIOI_Free(newfname);
}
