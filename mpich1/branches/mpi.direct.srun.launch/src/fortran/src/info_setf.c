/* 
 *   $Id: info_setf.c,v 1.7 2001/12/12 23:36:43 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef STDC_HEADERS
#include <string.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_INFO_SET = PMPI_INFO_SET
void MPI_INFO_SET (MPI_Fint *, char *, char *, MPI_Fint *, MPI_Fint, MPI_Fint);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_info_set__ = pmpi_info_set__
void mpi_info_set__ (MPI_Fint *, char *, char *, MPI_Fint *, MPI_Fint, MPI_Fint);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_info_set = pmpi_info_set
void mpi_info_set (MPI_Fint *, char *, char *, MPI_Fint *, MPI_Fint, MPI_Fint);
#else
#pragma weak mpi_info_set_ = pmpi_info_set_
void mpi_info_set_ (MPI_Fint *, char *, char *, MPI_Fint *, MPI_Fint, MPI_Fint);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_INFO_SET  MPI_INFO_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_set__  mpi_info_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_set  mpi_info_set
#else
#pragma _HP_SECONDARY_DEF pmpi_info_set_  mpi_info_set_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_INFO_SET as PMPI_INFO_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_info_set__ as pmpi_info_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_info_set as pmpi_info_set
#else
#pragma _CRI duplicate mpi_info_set_ as pmpi_info_set_
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
#define mpi_info_set_ PMPI_INFO_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_set_ pmpi_info_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_set_ pmpi_info_set
#else
#define mpi_info_set_ pmpi_info_set_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_info_set_ MPI_INFO_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_set_ mpi_info_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_set_ mpi_info_set
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
/*
FORTRAN_API void FORT_CALL mpi_info_set_ (MPI_Fint *, char *, char *, MPI_Fint *,
			      MPI_Fint, MPI_Fint);
*/
/* Definitions of Fortran Wrapper routines */
/*
FORTRAN_API void FORT_CALL mpi_info_set_(MPI_Fint *info, char *key, char *value, MPI_Fint *__ierr, 
                   MPI_Fint keylen, MPI_Fint vallen)
*/
FORTRAN_API void FORT_CALL mpi_info_set_ (MPI_Fint *, char * FORT_MIXED_LEN_DECL, char * FORT_MIXED_LEN_DECL, MPI_Fint *
			      FORT_END_LEN_DECL FORT_END_LEN_DECL);

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_info_set_(MPI_Fint *info, char *key FORT_MIXED_LEN(keylen), char *value FORT_MIXED_LEN(vallen), MPI_Fint *__ierr 
                   FORT_END_LEN(keylen) FORT_END_LEN(vallen))
{
    MPI_Info info_c;
    char *newkey, *newvalue;
    int new_keylen, new_vallen, lead_blanks, i;
    static char myname[] = "MPI_INFO_SET";
    int mpi_errno;

    if (!key) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_DEFAULT, 
				     myname, (char *)0, (char *)0);
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }
    if (!value) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_INFO_VAL_INVALID,
				     myname, (char *)0, (char *)0 );
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
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
    strncpy(newkey, key, new_keylen);
    newkey[new_keylen] = '\0';


    /* strip leading and trailing blanks in value */
    lead_blanks = 0;
    for (i=0; i<(int)vallen; i++) 
	if (value[i] == ' ') lead_blanks++;
	else break;

    for (i=(int)vallen-1; i>=0; i--) if (value[i] != ' ') break;
    if (i < 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_VALUE, 
				     MPIR_ERR_INFO_VALUE_NULL,
				     myname, (char *)0, (char *)0 );
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }
    new_vallen = i + 1 - lead_blanks;
    value += lead_blanks;

    newvalue = (char *) MALLOC((new_vallen+1)*sizeof(char));
    strncpy(newvalue, value, new_vallen);
    newvalue[new_vallen] = '\0';

 
    info_c = MPI_Info_f2c(*info);
    *__ierr = MPI_Info_set(info_c, newkey, newvalue);
    FREE(newkey);
    FREE(newvalue);
}
