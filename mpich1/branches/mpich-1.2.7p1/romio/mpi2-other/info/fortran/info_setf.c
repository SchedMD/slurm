/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpio.h"
#include "adio.h"


#if defined(MPIO_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)
#ifdef FORTRANCAPS
#define mpi_info_set_ PMPI_INFO_SET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_set_ pmpi_info_set__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_set pmpi_info_set_
#endif
#define mpi_info_set_ pmpi_info_set
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_set_ pmpi_info_set
#endif
#define mpi_info_set_ pmpi_info_set_
#endif

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
#pragma weak MPI_INFO_SET = PMPI_INFO_SET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma weak mpi_info_set__ = pmpi_info_set__
#elif !defined(FORTRANUNDERSCORE)
#pragma weak mpi_info_set = pmpi_info_set
#else
#pragma weak mpi_info_set_ = pmpi_info_set_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_INFO_SET MPI_INFO_SET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_set__ mpi_info_set__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_set mpi_info_set
#else
#pragma _HP_SECONDARY_DEF pmpi_info_set_ mpi_info_set_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_INFO_SET as PMPI_INFO_SET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_info_set__ as pmpi_info_set__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_info_set as pmpi_info_set
#else
#pragma _CRI duplicate mpi_info_set_ as pmpi_info_set_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#else

#ifdef FORTRANCAPS
#define mpi_info_set_ MPI_INFO_SET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_set_ mpi_info_set__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_set mpi_info_set_
#endif
#define mpi_info_set_ mpi_info_set
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_set_ mpi_info_set
#endif
#endif
#endif


void mpi_info_set_(MPI_Fint *info, char *key, char *value, int *ierr, 
                   int keylen, int vallen)
{
    MPI_Info info_c;
    char *newkey, *newvalue;
    int new_keylen, new_vallen, lead_blanks, i;

    if (key <= (char *) 0) {
        FPRINTF(stderr, "MPI_Info_set: key is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (value <= (char *) 0) {
        FPRINTF(stderr, "MPI_Info_set: value is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /* strip leading and trailing blanks in key */
    lead_blanks = 0;
    for (i=0; i<keylen; i++) 
	if (key[i] == ' ') lead_blanks++;
	else break;

    for (i=keylen-1; i>=0; i--) if (key[i] != ' ') break;
    if (i < 0) {
        FPRINTF(stderr, "MPI_Info_set: key is a blank string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    new_keylen = i + 1 - lead_blanks;
    key += lead_blanks;

    newkey = (char *) ADIOI_Malloc((new_keylen+1)*sizeof(char));
    ADIOI_Strncpy(newkey, key, new_keylen);
    newkey[new_keylen] = '\0';


    /* strip leading and trailing blanks in value */
    lead_blanks = 0;
    for (i=0; i<vallen; i++) 
	if (value[i] == ' ') lead_blanks++;
	else break;

    for (i=vallen-1; i>=0; i--) if (value[i] != ' ') break;
    if (i < 0) {
        FPRINTF(stderr, "MPI_Info_set: value is a blank string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    new_vallen = i + 1 - lead_blanks;
    value += lead_blanks;

    newvalue = (char *) ADIOI_Malloc((new_vallen+1)*sizeof(char));
    ADIOI_Strncpy(newvalue, value, new_vallen);
    newvalue[new_vallen] = '\0';

 
    info_c = MPI_Info_f2c(*info);
    *ierr = MPI_Info_set(info_c, newkey, newvalue);
    ADIOI_Free(newkey);
    ADIOI_Free(newvalue);
}
