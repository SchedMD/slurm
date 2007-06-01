/* 
 *   $Id: info_getnthf.c,v 1.6 2001/12/12 23:36:42 ashton Exp $    
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
#pragma weak MPI_INFO_GET_NTHKEY = PMPI_INFO_GET_NTHKEY
void MPI_INFO_GET_NTHKEY (MPI_Fint *, MPI_Fint *, char *, MPI_Fint *, MPI_Fint);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_info_get_nthkey__ = pmpi_info_get_nthkey__
void mpi_info_get_nthkey__ (MPI_Fint *, MPI_Fint *, char *, MPI_Fint *, MPI_Fint);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_info_get_nthkey = pmpi_info_get_nthkey
void mpi_info_get_nthkey (MPI_Fint *, MPI_Fint *, char *, MPI_Fint *, MPI_Fint);
#else
#pragma weak mpi_info_get_nthkey_ = pmpi_info_get_nthkey_
void mpi_info_get_nthkey_ (MPI_Fint *, MPI_Fint *, char *, MPI_Fint *, MPI_Fint);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_INFO_GET_NTHKEY  MPI_INFO_GET_NTHKEY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nthkey__  mpi_info_get_nthkey__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nthkey  mpi_info_get_nthkey
#else
#pragma _HP_SECONDARY_DEF pmpi_info_get_nthkey_  mpi_info_get_nthkey_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_INFO_GET_NTHKEY as PMPI_INFO_GET_NTHKEY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_info_get_nthkey__ as pmpi_info_get_nthkey__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_info_get_nthkey as pmpi_info_get_nthkey
#else
#pragma _CRI duplicate mpi_info_get_nthkey_ as pmpi_info_get_nthkey_
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
#define mpi_info_get_nthkey_ PMPI_INFO_GET_NTHKEY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_get_nthkey_ pmpi_info_get_nthkey__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_get_nthkey_ pmpi_info_get_nthkey
#else
#define mpi_info_get_nthkey_ pmpi_info_get_nthkey_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_info_get_nthkey_ MPI_INFO_GET_NTHKEY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_get_nthkey_ mpi_info_get_nthkey__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_get_nthkey_ mpi_info_get_nthkey
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
/*
FORTRAN_API void FORT_CALL mpi_info_get_nthkey_ (MPI_Fint *, MPI_Fint *, char *, 
				     MPI_Fint *, MPI_Fint);
*/
/* Definitions of Fortran Wrapper routines */
/*
FORTRAN_API void FORT_CALL mpi_info_get_nthkey_(MPI_Fint *info, MPI_Fint *n, char *key, 
			  MPI_Fint *__ierr, MPI_Fint keylen)
*/
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_info_get_nthkey_ (MPI_Fint *, MPI_Fint *, char * FORT_MIXED_LEN_DECL, 
				     MPI_Fint * FORT_END_LEN_DECL);

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_info_get_nthkey_(MPI_Fint *info, MPI_Fint *n, char *key FORT_MIXED_LEN(keylen), 
			  MPI_Fint *__ierr FORT_END_LEN(keylen))
{
    MPI_Info info_c;
    int i, tmpkeylen;
    char *tmpkey;
    int    mpi_errno;
    static char myname[] = "MPI_INFO_GET_NTHKEY";

    if (!key) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_DEFAULT, 
				     myname, (char *)0, (char *)0);
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }

    tmpkey = (char *) MALLOC((MPI_MAX_INFO_KEY+1) * sizeof(char));
    info_c = MPI_Info_f2c(*info);
    *__ierr = MPI_Info_get_nthkey(info_c, (int)*n, tmpkey);

    if (*__ierr != MPI_SUCCESS) return;
    tmpkeylen = strlen(tmpkey);

    if (tmpkeylen <= (int)keylen) {
	strncpy(key, tmpkey, tmpkeylen);

	/* blank pad the remaining space */
	for (i=tmpkeylen; i<(int)keylen; i++) key[i] = ' ';
    }
    else {
	/* not enough space */
	strncpy(key, tmpkey, (int)keylen);
	/* this should be flagged as an error. */
	*__ierr = MPI_ERR_UNKNOWN;
    }

    FREE(tmpkey);
}


