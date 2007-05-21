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
#define mpi_info_get_valuelen_ PMPI_INFO_GET_VALUELEN
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_get_valuelen_ pmpi_info_get_valuelen__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_get_valuelen pmpi_info_get_valuelen_
#endif
#define mpi_info_get_valuelen_ pmpi_info_get_valuelen
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_get_valuelen_ pmpi_info_get_valuelen
#endif
#define mpi_info_get_valuelen_ pmpi_info_get_valuelen_
#endif

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
#pragma weak MPI_INFO_GET_VALUELEN = PMPI_INFO_GET_VALUELEN
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma weak mpi_info_get_valuelen__ = pmpi_info_get_valuelen__
#elif !defined(FORTRANUNDERSCORE)
#pragma weak mpi_info_get_valuelen = pmpi_info_get_valuelen
#else
#pragma weak mpi_info_get_valuelen_ = pmpi_info_get_valuelen_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_INFO_GET_VALUELEN MPI_INFO_GET_VALUELEN
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_valuelen__ mpi_info_get_valuelen__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_valuelen mpi_info_get_valuelen
#else
#pragma _HP_SECONDARY_DEF pmpi_info_get_valuelen_ mpi_info_get_valuelen_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_INFO_GET_VALUELEN as PMPI_INFO_GET_VALUELEN
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_info_get_valuelen__ as pmpi_info_get_valuelen__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_info_get_valuelen as pmpi_info_get_valuelen
#else
#pragma _CRI duplicate mpi_info_get_valuelen_ as pmpi_info_get_valuelen_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#else

#ifdef FORTRANCAPS
#define mpi_info_get_valuelen_ MPI_INFO_GET_VALUELEN
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_get_valuelen_ mpi_info_get_valuelen__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_get_valuelen mpi_info_get_valuelen_
#endif
#define mpi_info_get_valuelen_ mpi_info_get_valuelen
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_get_valuelen_ mpi_info_get_valuelen
#endif
#endif
#endif

void mpi_info_get_valuelen_(MPI_Fint *info, char *key, int *valuelen,
                 int *flag, int *ierr, int keylen )
{
    MPI_Info info_c;
    char *newkey;
    int new_keylen, lead_blanks, i;

    if (key <= (char *) 0) {
        FPRINTF(stderr, "MPI_Info_get_valuelen: key is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /* strip leading and trailing blanks in key */
    lead_blanks = 0;
    for (i=0; i<keylen; i++) 
        if (key[i] == ' ') lead_blanks++;
        else break;

    for (i=keylen-1; i>=0; i--) if (key[i] != ' ') break;
    if (i < 0) {
        FPRINTF(stderr, "MPI_Info_get_valuelen: key is a blank string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    new_keylen = i + 1 - lead_blanks;
    key += lead_blanks;

    newkey = (char *) ADIOI_Malloc((new_keylen+1)*sizeof(char));
    ADIOI_Strncpy(newkey, key, new_keylen);
    newkey[new_keylen] = '\0';

    info_c = MPI_Info_f2c(*info);
    *ierr = MPI_Info_get_valuelen(info_c, newkey, valuelen, flag);
    ADIOI_Free(newkey);
}
