/* 
 *   $Id: info_getf.c,v 1.5 2001/12/12 23:36:42 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if defined(STDC_HEADERS) || defined(HAVE_STRING_H)
#include <string.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_INFO_GET = PMPI_INFO_GET
void MPI_INFO_GET (MPI_Fint *, char *, MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint, MPI_Fint);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_info_get__ = pmpi_info_get__
void mpi_info_get__ (MPI_Fint *, char *, MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint, MPI_Fint);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_info_get = pmpi_info_get
void mpi_info_get (MPI_Fint *, char *, MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint, MPI_Fint);
#else
#pragma weak mpi_info_get_ = pmpi_info_get_
void mpi_info_get_ (MPI_Fint *, char *, MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint, MPI_Fint);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_INFO_GET  MPI_INFO_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get__  mpi_info_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get  mpi_info_get
#else
#pragma _HP_SECONDARY_DEF pmpi_info_get_  mpi_info_get_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_INFO_GET as PMPI_INFO_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_info_get__ as pmpi_info_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_info_get as pmpi_info_get
#else
#pragma _CRI duplicate mpi_info_get_ as pmpi_info_get_
#endif

/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

#ifdef F77_NAME_UPPER
#define mpi_info_get_ PMPI_INFO_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_get_ pmpi_info_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_get_ pmpi_info_get
#else
#define mpi_info_get_ pmpi_info_get_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_info_get_ MPI_INFO_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_get_ mpi_info_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_get_ mpi_info_get
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
/*
FORTRAN_API void FORT_CALL mpi_info_get_ (MPI_Fint *, char *, MPI_Fint *, char *,
			      MPI_Fint *, MPI_Fint *, MPI_Fint, MPI_Fint);
*/
/* Definitions of Fortran Wrapper routines */
/*
FORTRAN_API void FORT_CALL mpi_info_get_(MPI_Fint *info, char *key, MPI_Fint *valuelen, char *value, 
        MPI_Fint *flag, MPI_Fint *__ierr, MPI_Fint keylen, MPI_Fint valspace)
*/
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_info_get_ (MPI_Fint *, char * FORT_MIXED_LEN_DECL, MPI_Fint *, char * FORT_MIXED_LEN_DECL,
			      MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL FORT_END_LEN_DECL);

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_info_get_(MPI_Fint *info, char *key FORT_MIXED_LEN(keylen), MPI_Fint *valuelen, char *value FORT_MIXED_LEN(valspace), 
        MPI_Fint *flag, MPI_Fint *__ierr FORT_END_LEN(keylen) FORT_END_LEN(valspace))
{
    MPI_Info info_c;
    char *newkey, *tmpvalue;
    int new_keylen, lead_blanks, i, tmpvaluelen;
    int lflag;
    int mpi_errno;
    static char myname[] = "MPI_INFO_GET";

    if (!key) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_DEFAULT, 
				     myname, (char *)0, (char *)0);
	*__ierr =  MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }

    /* strip leading and trailing blanks in key */
    lead_blanks = 0;
    for (i=0; i<(int)keylen; i++) 
        if (key[i] == ' ') lead_blanks++;
        else break;

    for (i=(int)keylen-1; i>=0; i--) if (key[i] != ' ') break;
    if (i < 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_KEY_EMPTY,
				     myname, (char *)0, (char *)0 );
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }
    new_keylen = i + 1 - lead_blanks;
    key += lead_blanks;

    newkey = (char *) MALLOC((new_keylen+1)*sizeof(char));
    if (!newkey) {
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
	return;
    }
    strncpy(newkey, key, new_keylen);
    newkey[new_keylen] = '\0';

    if (!value) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_INFO_VAL_INVALID,
				     myname, 
				     "Value is an invalid address", (char *)0);
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }
    if (*valuelen <= 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_INFO_VALLEN, 
				     myname, 
				     (char *)0, (char *)0, (int)*valuelen );
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }
    if ((int)*valuelen > (int)valspace) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_INFO_VALSIZE, 
				     myname, 
 "valuelen is greater than the amount of space available in value",
 "valuelen = %d is greater than the amount of space available in value = %d",
				     (int)*valuelen, (int)valspace );
        *__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }
    
    tmpvalue = (char *) MALLOC(((int)*valuelen + 1)*sizeof(char));

    info_c = MPI_Info_f2c(*info);
    *__ierr = MPI_Info_get(info_c, newkey, (int)*valuelen, tmpvalue, &lflag);

    if (lflag) {
	tmpvaluelen = strlen(tmpvalue);
	strncpy(value, tmpvalue, tmpvaluelen);
	/* blank pad the remaining space */
	for (i=tmpvaluelen; i<(int)valspace; i++) value[i] = ' ';
    }
    *flag = MPIR_TO_FLOG(lflag);
    FREE(newkey);
    FREE(tmpvalue);
}
