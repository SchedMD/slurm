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
#define mpi_info_delete_ PMPI_INFO_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_delete_ pmpi_info_delete__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_delete pmpi_info_delete_
#endif
#define mpi_info_delete_ pmpi_info_delete
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_delete_ pmpi_info_delete
#endif
#define mpi_info_delete_ pmpi_info_delete_
#endif

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
#pragma weak MPI_INFO_DELETE = PMPI_INFO_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma weak mpi_info_delete__ = pmpi_info_delete__
#elif !defined(FORTRANUNDERSCORE)
#pragma weak mpi_info_delete = pmpi_info_delete
#else
#pragma weak mpi_info_delete_ = pmpi_info_delete_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_INFO_DELETE MPI_INFO_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_delete__ mpi_info_delete__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_delete mpi_info_delete
#else
#pragma _HP_SECONDARY_DEF pmpi_info_delete_ mpi_info_delete_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_INFO_DELETE as PMPI_INFO_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_info_delete__ as pmpi_info_delete__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_info_delete as pmpi_info_delete
#else
#pragma _CRI duplicate mpi_info_delete_ as pmpi_info_delete_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#else

#ifdef FORTRANCAPS
#define mpi_info_delete_ MPI_INFO_DELETE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_delete_ mpi_info_delete__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_delete mpi_info_delete_
#endif
#define mpi_info_delete_ mpi_info_delete
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_delete_ mpi_info_delete
#endif
#endif
#endif

void mpi_info_delete_(MPI_Fint *info, char *key, int *ierr, int keylen)
{
    MPI_Info info_c;
    char *newkey;
    int new_keylen, lead_blanks, i;

    if (key <= (char *) 0) {
        FPRINTF(stderr, "MPI_Info_delete: key is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /* strip leading and trailing blanks in key */
    lead_blanks = 0;
    for (i=0; i<keylen; i++) 
        if (key[i] == ' ') lead_blanks++;
        else break;

    for (i=keylen-1; i>=0; i--) if (key[i] != ' ') break;
    if (i < 0) {
        FPRINTF(stderr, "MPI_Info_delete: key is a blank string\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    new_keylen = i + 1 - lead_blanks;
    key += lead_blanks;

    newkey = (char *) ADIOI_Malloc((new_keylen+1)*sizeof(char));
    ADIOI_Strncpy(newkey, key, new_keylen);
    newkey[new_keylen] = '\0';

    info_c = MPI_Info_f2c(*info);
    *ierr = MPI_Info_delete(info_c, newkey);
    ADIOI_Free(newkey);
}

