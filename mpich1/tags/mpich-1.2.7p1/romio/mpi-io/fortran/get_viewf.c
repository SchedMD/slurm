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
extern FORTRAN_API void FORT_CALL MPI_FILE_GET_VIEW( MPI_Fint *, MPI_Offset*, MPI_Fint*, MPI_Fint*, char * FORT_MIXED_LEN_DECL, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak MPI_FILE_GET_VIEW = PMPI_FILE_GET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_view__( MPI_Fint *, MPI_Offset*, MPI_Fint*, MPI_Fint*, char * FORT_MIXED_LEN_DECL, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak mpi_file_get_view__ = pmpi_file_get_view__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_get_view( MPI_Fint *, MPI_Offset*, MPI_Fint*, MPI_Fint*, char * FORT_MIXED_LEN_DECL, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak mpi_file_get_view = pmpi_file_get_view
#else
extern FORTRAN_API void FORT_CALL mpi_file_get_view_( MPI_Fint *, MPI_Offset*, MPI_Fint*, MPI_Fint*, char * FORT_MIXED_LEN_DECL, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak mpi_file_get_view_ = pmpi_file_get_view_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_GET_VIEW MPI_FILE_GET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_view__ mpi_file_get_view__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_view mpi_file_get_view
#else
#pragma _HP_SECONDARY_DEF pmpi_file_get_view_ mpi_file_get_view_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_GET_VIEW as PMPI_FILE_GET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_view__ as pmpi_file_get_view__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_get_view as pmpi_file_get_view
#else
#pragma _CRI duplicate mpi_file_get_view_ as pmpi_file_get_view_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_get_view_ PMPI_FILE_GET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_view_ pmpi_file_get_view__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_view pmpi_file_get_view_
#endif
#define mpi_file_get_view_ pmpi_file_get_view
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_get_view_ pmpi_file_get_view
#endif
#define mpi_file_get_view_ pmpi_file_get_view_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_get_view_ MPI_FILE_GET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_get_view_ mpi_file_get_view__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_view mpi_file_get_view_
#endif
#define mpi_file_get_view_ mpi_file_get_view
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_get_view_ mpi_file_get_view
#endif
#endif
#endif

#if defined(MPIHP) || defined(MPILAM)
/* Prototype to keep compiler happy */
void mpi_file_get_view_(MPI_Fint *fh,MPI_Offset *disp,MPI_Fint *etype,
		MPI_Fint *filetype,char *datarep, int *ierr, int str_len );

void mpi_file_get_view_(MPI_Fint *fh,MPI_Offset *disp,MPI_Fint *etype,
   MPI_Fint *filetype,char *datarep, int *ierr, int str_len )
{
    MPI_File fh_c;
    MPI_Datatype etype_c, filetype_c;
    int i, tmpreplen;
    char *tmprep;

    if (datarep <= (char *) 0) {
        FPRINTF(stderr, "MPI_File_get_view: datarep is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    tmprep = (char *) ADIOI_Malloc((MPI_MAX_DATAREP_STRING+1) * sizeof(char));
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_get_view(fh_c, disp, &etype_c, &filetype_c, tmprep);

    tmpreplen = strlen(tmprep);
    if (tmpreplen <= str_len) {
        ADIOI_Strncpy(datarep, tmprep, tmpreplen);

        /* blank pad the remaining space */
        for (i=tmpreplen; i<str_len; i++) datarep[i] = ' ';
    }
    else {
        /* not enough space */
        ADIOI_Strncpy(datarep, tmprep, str_len);
        /* this should be flagged as an error. */
        *ierr = MPI_ERR_UNKNOWN;
    }
    
    *etype = MPI_Type_c2f(etype_c);
    *filetype = MPI_Type_c2f(filetype_c);
    ADIOI_Free(tmprep);
}

#else

#ifdef _UNICOS
void mpi_file_get_view_(MPI_Fint *fh,MPI_Offset *disp,MPI_Fint *etype,
   MPI_Fint *filetype, _fcd datarep_fcd, int *ierr)
{
    char *datarep = _fcdtocp(datarep_fcd);
    int str_len = _fcdlen(datarep_fcd);
#else
/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_get_view_( MPI_Fint *fh, MPI_Offset *disp, MPI_Fint *etype, MPI_Fint *filetype, char *datarep FORT_MIXED_LEN_DECL, MPI_Fint *ierr FORT_END_LEN_DECL );

FORTRAN_API void FORT_CALL mpi_file_get_view_( MPI_Fint *fh, MPI_Offset *disp, MPI_Fint *etype, MPI_Fint *filetype, char *datarep FORT_MIXED_LEN(str_len), MPI_Fint *ierr FORT_END_LEN(str_len) )
{
#endif
    MPI_File fh_c;
    int i, tmpreplen;
    char *tmprep;

/* Initialize the string to all blanks */
    if (datarep <= (char *) 0) {
        FPRINTF(stderr, "MPI_File_get_view: datarep is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    tmprep = (char *) ADIOI_Malloc((MPI_MAX_DATAREP_STRING+1) * sizeof(char));
    fh_c = MPI_File_f2c(*fh);
    *ierr = MPI_File_get_view(fh_c, disp, etype, filetype, tmprep);

    tmpreplen = strlen(tmprep);
    if (tmpreplen <= str_len) {
        ADIOI_Strncpy(datarep, tmprep, tmpreplen);

        /* blank pad the remaining space */
        for (i=tmpreplen; i<str_len; i++) datarep[i] = ' ';
    }
    else {
        /* not enough space */
        ADIOI_Strncpy(datarep, tmprep, str_len);
        /* this should be flagged as an error. */
        *ierr = MPI_ERR_UNKNOWN;
    }

    ADIOI_Free(tmprep);
}
#endif
