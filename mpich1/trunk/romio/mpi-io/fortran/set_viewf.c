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
extern FORTRAN_API void FORT_CALL MPI_FILE_SET_VIEW( MPI_Fint *, MPI_Offset *, MPI_Fint *, MPI_Fint *, char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak MPI_FILE_SET_VIEW = PMPI_FILE_SET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_set_view__( MPI_Fint *, MPI_Offset *, MPI_Fint *, MPI_Fint *, char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak mpi_file_set_view__ = pmpi_file_set_view__
#elif !defined(FORTRANUNDERSCORE)
extern FORTRAN_API void FORT_CALL mpi_file_set_view( MPI_Fint *, MPI_Offset *, MPI_Fint *, MPI_Fint *, char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak mpi_file_set_view = pmpi_file_set_view
#else
extern FORTRAN_API void FORT_CALL mpi_file_set_view_( MPI_Fint *, MPI_Offset *, MPI_Fint *, MPI_Fint *, char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
#pragma weak mpi_file_set_view_ = pmpi_file_set_view_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_FILE_SET_VIEW MPI_FILE_SET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_set_view__ mpi_file_set_view__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_set_view mpi_file_set_view
#else
#pragma _HP_SECONDARY_DEF pmpi_file_set_view_ mpi_file_set_view_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_FILE_SET_VIEW as PMPI_FILE_SET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_file_set_view__ as pmpi_file_set_view__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_file_set_view as pmpi_file_set_view
#else
#pragma _CRI duplicate mpi_file_set_view_ as pmpi_file_set_view_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpi_file_set_view_ PMPI_FILE_SET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_set_view_ pmpi_file_set_view__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_set_view pmpi_file_set_view_
#endif
#define mpi_file_set_view_ pmpi_file_set_view
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_file_set_view_ pmpi_file_set_view
#endif
#define mpi_file_set_view_ pmpi_file_set_view_
#endif

#else

#ifdef FORTRANCAPS
#define mpi_file_set_view_ MPI_FILE_SET_VIEW
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_file_set_view_ mpi_file_set_view__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_set_view mpi_file_set_view_
#endif
#define mpi_file_set_view_ mpi_file_set_view
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_file_set_view_ mpi_file_set_view
#endif
#endif
#endif

#if defined(MPIHP) || defined(MPILAM)
/* Prototype to keep compiler happy */
void mpi_file_set_view_(MPI_Fint *fh,MPI_Offset *disp,MPI_Fint *etype,
   MPI_Fint *filetype,char *datarep,MPI_Fint *info, int *ierr,
			int str_len );

void mpi_file_set_view_(MPI_Fint *fh,MPI_Offset *disp,MPI_Fint *etype,
   MPI_Fint *filetype,char *datarep,MPI_Fint *info, int *ierr,
   int str_len )
{
    char *newstr;
    MPI_File fh_c;
    int i, real_len; 
    MPI_Datatype etype_c, filetype_c;
    MPI_Info info_c;
    
    etype_c = MPI_Type_f2c(*etype);
    filetype_c = MPI_Type_f2c(*filetype);
    info_c = MPI_Info_f2c(*info);

    /* strip trailing blanks in datarep */
    if (datarep <= (char *) 0) {
        FPRINTF(stderr, "MPI_File_set_view: datarep is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    for (i=str_len-1; i>=0; i--) if (datarep[i] != ' ') break;
    if (i < 0) {
	FPRINTF(stderr, "MPI_File_set_view: datarep is a blank string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    real_len = i + 1;

    newstr = (char *) ADIOI_Malloc((real_len+1)*sizeof(char));
    ADIOI_Strncpy(newstr, datarep, real_len);
    newstr[real_len] = '\0';
    
    fh_c = MPI_File_f2c(*fh);
 
    *ierr = MPI_File_set_view(fh_c,*disp,etype_c,filetype_c,newstr,info_c);

    ADIOI_Free(newstr);
}

#else

#ifdef _UNICOS
void mpi_file_set_view_(MPI_Fint *fh,MPI_Offset *disp,MPI_Datatype *etype,
   MPI_Datatype *filetype,_fcd datarep_fcd,MPI_Fint *info, int *ierr)
{
   char *datarep = _fcdtocp(datarep_fcd);
   int str_len = _fcdlen(datarep_fcd);
#else
/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpi_file_set_view_( MPI_Fint *fh, MPI_Offset *disp, MPI_Fint *etype, MPI_Fint *filetype, char *datarep FORT_MIXED_LEN_DECL, MPI_Fint *info, MPI_Fint *ierr FORT_END_LEN_DECL );

FORTRAN_API void FORT_CALL mpi_file_set_view_( MPI_Fint *fh, MPI_Offset *disp, MPI_Fint *etype, MPI_Fint *filetype, char *datarep FORT_MIXED_LEN(str_len), MPI_Fint *info, MPI_Fint *ierr FORT_END_LEN(str_len) )
{
#endif
    char *newstr;
    MPI_File fh_c;
    int i, real_len; 
    MPI_Info info_c;
    
    info_c = MPI_Info_f2c(*info);

    /* strip trailing blanks in datarep */
    if (datarep <= (char *) 0) {
        FPRINTF(stderr, "MPI_File_set_view: datarep is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    for (i=str_len-1; i>=0; i--) if (datarep[i] != ' ') break;
    if (i < 0) {
	FPRINTF(stderr, "MPI_File_set_view: datarep is a blank string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    real_len = i + 1;

    newstr = (char *) ADIOI_Malloc((real_len+1)*sizeof(char));
    ADIOI_Strncpy(newstr, datarep, real_len);
    newstr[real_len] = '\0';
    
    fh_c = MPI_File_f2c(*fh);
 
    *ierr = MPI_File_set_view(fh_c,*disp,*etype,*filetype,newstr,info_c);

    ADIOI_Free(newstr);
}
#endif
