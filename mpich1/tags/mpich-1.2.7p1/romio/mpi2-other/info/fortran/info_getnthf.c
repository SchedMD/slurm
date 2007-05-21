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
#define mpi_info_get_nthkey_ PMPI_INFO_GET_NTHKEY
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_get_nthkey_ pmpi_info_get_nthkey__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nthkey pmpi_info_get_nthkey_
#endif
#define mpi_info_get_nthkey_ pmpi_info_get_nthkey
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nthkey_ pmpi_info_get_nthkey
#endif
#define mpi_info_get_nthkey_ pmpi_info_get_nthkey_
#endif

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
#pragma weak MPI_INFO_GET_NTHKEY = PMPI_INFO_GET_NTHKEY
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma weak mpi_info_get_nthkey__ = pmpi_info_get_nthkey__
#elif !defined(FORTRANUNDERSCORE)
#pragma weak mpi_info_get_nthkey = pmpi_info_get_nthkey
#else
#pragma weak mpi_info_get_nthkey_ = pmpi_info_get_nthkey_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_INFO_GET_NTHKEY MPI_INFO_GET_NTHKEY
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nthkey__ mpi_info_get_nthkey__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nthkey mpi_info_get_nthkey
#else
#pragma _HP_SECONDARY_DEF pmpi_info_get_nthkey_ mpi_info_get_nthkey_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_INFO_GET_NTHKEY as PMPI_INFO_GET_NTHKEY
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_info_get_nthkey__ as pmpi_info_get_nthkey__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_info_get_nthkey as pmpi_info_get_nthkey
#else
#pragma _CRI duplicate mpi_info_get_nthkey_ as pmpi_info_get_nthkey_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#else

#ifdef FORTRANCAPS
#define mpi_info_get_nthkey_ MPI_INFO_GET_NTHKEY
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_get_nthkey_ mpi_info_get_nthkey__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_get_nthkey mpi_info_get_nthkey_
#endif
#define mpi_info_get_nthkey_ mpi_info_get_nthkey
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_get_nthkey_ mpi_info_get_nthkey
#endif
#endif
#endif

void mpi_info_get_nthkey_(MPI_Fint *info, int *n, char *key, int *ierr,
                          int keylen)
{
    MPI_Info info_c;
    int i, tmpkeylen;
    char *tmpkey;

    if (key <= (char *) 0) {
        FPRINTF(stderr, "MPI_Info_get_nthkey: key is an invalid address\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    tmpkey = (char *) ADIOI_Malloc((MPI_MAX_INFO_KEY+1) * sizeof(char));
    info_c = MPI_Info_f2c(*info);
    *ierr = MPI_Info_get_nthkey(info_c, *n, tmpkey);

    tmpkeylen = strlen(tmpkey);

    if (tmpkeylen <= keylen) {
	ADIOI_Strncpy(key, tmpkey, tmpkeylen);

	/* blank pad the remaining space */
	for (i=tmpkeylen; i<keylen; i++) key[i] = ' ';
    }
    else {
	/* not enough space */
	ADIOI_Strncpy(key, tmpkey, keylen);
	/* this should be flagged as an error. */
	*ierr = MPI_ERR_UNKNOWN;
    }

    ADIOI_Free(tmpkey);
}

