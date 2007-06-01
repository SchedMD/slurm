/* 
 *   $Id: info_deletef.c,v 1.6 2001/12/12 23:36:41 ashton Exp $    
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
#pragma weak MPI_INFO_DELETE = PMPI_INFO_DELETE
void MPI_INFO_DELETE (MPI_Fint *, char *, MPI_Fint *, MPI_Fint);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_info_delete__ = pmpi_info_delete__
void mpi_info_delete__ (MPI_Fint *, char *, MPI_Fint *, MPI_Fint);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_info_delete = pmpi_info_delete
void mpi_info_delete (MPI_Fint *, char *, MPI_Fint *, MPI_Fint);
#else
#pragma weak mpi_info_delete_ = pmpi_info_delete_
void mpi_info_delete_ (MPI_Fint *, char *, MPI_Fint *, MPI_Fint);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_INFO_DELETE  MPI_INFO_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_delete__  mpi_info_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_delete  mpi_info_delete
#else
#pragma _HP_SECONDARY_DEF pmpi_info_delete_  mpi_info_delete_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_INFO_DELETE as PMPI_INFO_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_info_delete__ as pmpi_info_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_info_delete as pmpi_info_delete
#else
#pragma _CRI duplicate mpi_info_delete_ as pmpi_info_delete_
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
#define mpi_info_delete_ PMPI_INFO_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_delete_ pmpi_info_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_delete_ pmpi_info_delete
#else
#define mpi_info_delete_ pmpi_info_delete_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_info_delete_ MPI_INFO_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_delete_ mpi_info_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_delete_ mpi_info_delete
#endif
#endif


/* Prototype to suppress warning about missing prototypes */
/*
FORTRAN_API void FORT_CALL mpi_info_delete_ (MPI_Fint *, char *, MPI_Fint *, MPI_Fint);
*/
/* Definitions of Fortran Wrapper routines */ 
/*
FORTRAN_API void FORT_CALL mpi_info_delete_(MPI_Fint *info, char *key, MPI_Fint *__ierr, 
		      MPI_Fint keylen)*/
/* Prototype to suppress warning about missing prototypes */
FORTRAN_API void FORT_CALL mpi_info_delete_ (MPI_Fint *, char * FORT_MIXED_LEN_DECL, MPI_Fint * FORT_END_LEN_DECL);

/* Definitions of Fortran Wrapper routines */ 
FORTRAN_API void FORT_CALL mpi_info_delete_(MPI_Fint *info, char *key FORT_MIXED_LEN(keylen), MPI_Fint *__ierr
		      FORT_END_LEN(keylen))
{
    MPI_Info info_c;
    char *newkey;
    int new_keylen, lead_blanks, i;
    static char myname[] = "MPI_INFO_DELETE";
    int mpi_errno;

    if (!key) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO_KEY, MPIR_ERR_DEFAULT, 
				     myname, (char *)0, (char *)0);
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
    if (!newkey) {
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
	return;
    }
    strncpy(newkey, key, new_keylen);
    newkey[new_keylen] = '\0';

    info_c = MPI_Info_f2c(*info);
    *__ierr = MPI_Info_delete(info_c, newkey);
    FREE(newkey);
}

