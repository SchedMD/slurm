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
#define mpi_info_get_ PMPI_INFO_GET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_get_ pmpi_info_get__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_get pmpi_info_get_
#endif
#define mpi_info_get_ pmpi_info_get
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_get_ pmpi_info_get
#endif
#define mpi_info_get_ pmpi_info_get_
#endif

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
#pragma weak MPI_INFO_GET = PMPI_INFO_GET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma weak mpi_info_get__ = pmpi_info_get__
#elif !defined(FORTRANUNDERSCORE)
#pragma weak mpi_info_get = pmpi_info_get
#else
#pragma weak mpi_info_get_ = pmpi_info_get_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_INFO_GET MPI_INFO_GET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get__ mpi_info_get__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get mpi_info_get
#else
#pragma _HP_SECONDARY_DEF pmpi_info_get_ mpi_info_get_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_INFO_GET as PMPI_INFO_GET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_info_get__ as pmpi_info_get__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_info_get as pmpi_info_get
#else
#pragma _CRI duplicate mpi_info_get_ as pmpi_info_get_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#else

#ifdef FORTRANCAPS
#define mpi_info_get_ MPI_INFO_GET
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_get_ mpi_info_get__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_get mpi_info_get_
#endif
#define mpi_info_get_ mpi_info_get
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_get_ mpi_info_get
#endif
#endif
#endif

void mpi_info_get_(MPI_Fint *info, char *key, int *valuelen, char *value, 
        int *flag, int *ierr, int keylen, int valspace)
{
    MPI_Info info_c;
    char *newkey, *tmpvalue;
    int new_keylen, lead_blanks, i, tmpvaluelen;

    if (key <= (char *) 0) {
        FPRINTF(stderr, "MPI_Info_get: key is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /* strip leading and trailing blanks in key */
    lead_blanks = 0;
    for (i=0; i<keylen; i++) 
        if (key[i] == ' ') lead_blanks++;
        else break;

    for (i=keylen-1; i>=0; i--) if (key[i] != ' ') break;
    if (i < 0) {
        FPRINTF(stderr, "MPI_Info_get: key is a blank string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    new_keylen = i + 1 - lead_blanks;
    key += lead_blanks;

    newkey = (char *) ADIOI_Malloc((new_keylen+1)*sizeof(char));
    ADIOI_Strncpy(newkey, key, new_keylen);
    newkey[new_keylen] = '\0';

    if (value <= (char *) 0) {
        FPRINTF(stderr, "MPI_Info_get: value is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (*valuelen <= 0) {
        FPRINTF(stderr, "MPI_Info_get: Invalid valuelen argument\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (*valuelen > valspace) {
        FPRINTF(stderr, "MPI_Info_get: valuelen is greater than the amount of memory available in value\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    tmpvalue = (char *) ADIOI_Malloc((*valuelen + 1)*sizeof(char));

    info_c = MPI_Info_f2c(*info);
    *ierr = MPI_Info_get(info_c, newkey, *valuelen, tmpvalue, flag);

    if (*flag) {
	tmpvaluelen = strlen(tmpvalue);
	ADIOI_Strncpy(value, tmpvalue, tmpvaluelen);
	/* blank pad the remaining space */
	for (i=tmpvaluelen; i<valspace; i++) value[i] = ' ';
    }
	
    ADIOI_Free(newkey);
    ADIOI_Free(tmpvalue);
}
