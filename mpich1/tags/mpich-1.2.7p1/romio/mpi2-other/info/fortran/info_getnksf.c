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
#define mpi_info_get_nkeys_ PMPI_INFO_GET_NKEYS
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_get_nkeys_ pmpi_info_get_nkeys__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nkeys pmpi_info_get_nkeys_
#endif
#define mpi_info_get_nkeys_ pmpi_info_get_nkeys
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nkeys_ pmpi_info_get_nkeys
#endif
#define mpi_info_get_nkeys_ pmpi_info_get_nkeys_
#endif

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
#pragma weak MPI_INFO_GET_NKEYS = PMPI_INFO_GET_NKEYS
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma weak mpi_info_get_nkeys__ = pmpi_info_get_nkeys__
#elif !defined(FORTRANUNDERSCORE)
#pragma weak mpi_info_get_nkeys = pmpi_info_get_nkeys
#else
#pragma weak mpi_info_get_nkeys_ = pmpi_info_get_nkeys_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_INFO_GET_NKEYS MPI_INFO_GET_NKEYS
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nkeys__ mpi_info_get_nkeys__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nkeys mpi_info_get_nkeys
#else
#pragma _HP_SECONDARY_DEF pmpi_info_get_nkeys_ mpi_info_get_nkeys_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_INFO_GET_NKEYS as PMPI_INFO_GET_NKEYS
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_info_get_nkeys__ as pmpi_info_get_nkeys__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_info_get_nkeys as pmpi_info_get_nkeys
#else
#pragma _CRI duplicate mpi_info_get_nkeys_ as pmpi_info_get_nkeys_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#else

#ifdef FORTRANCAPS
#define mpi_info_get_nkeys_ MPI_INFO_GET_NKEYS
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_get_nkeys_ mpi_info_get_nkeys__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_get_nkeys mpi_info_get_nkeys_
#endif
#define mpi_info_get_nkeys_ mpi_info_get_nkeys
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_get_nkeys_ mpi_info_get_nkeys
#endif
#endif
#endif

void mpi_info_get_nkeys_(MPI_Fint *info, int *nkeys, int *ierr )
{
    MPI_Info info_c;
    
    info_c = MPI_Info_f2c(*info);
    *ierr = MPI_Info_get_nkeys(info_c, nkeys);
}
