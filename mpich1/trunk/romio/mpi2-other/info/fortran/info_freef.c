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
#define mpi_info_free_ PMPI_INFO_FREE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_free_ pmpi_info_free__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_free pmpi_info_free_
#endif
#define mpi_info_free_ pmpi_info_free
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_free_ pmpi_info_free
#endif
#define mpi_info_free_ pmpi_info_free_
#endif

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
#pragma weak MPI_INFO_FREE = PMPI_INFO_FREE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma weak mpi_info_free__ = pmpi_info_free__
#elif !defined(FORTRANUNDERSCORE)
#pragma weak mpi_info_free = pmpi_info_free
#else
#pragma weak mpi_info_free_ = pmpi_info_free_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_INFO_FREE MPI_INFO_FREE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_free__ mpi_info_free__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_free mpi_info_free
#else
#pragma _HP_SECONDARY_DEF pmpi_info_free_ mpi_info_free_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_INFO_FREE as PMPI_INFO_FREE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_info_free__ as pmpi_info_free__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_info_free as pmpi_info_free
#else
#pragma _CRI duplicate mpi_info_free_ as pmpi_info_free_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#else

#ifdef FORTRANCAPS
#define mpi_info_free_ MPI_INFO_FREE
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_free_ mpi_info_free__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_free mpi_info_free_
#endif
#define mpi_info_free_ mpi_info_free
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_free_ mpi_info_free
#endif
#endif
#endif

void mpi_info_free_(MPI_Fint *info, int *ierr )
{
    MPI_Info info_c;

    info_c = MPI_Info_f2c(*info);
    *ierr = MPI_Info_free(&info_c);
    *info = MPI_Info_c2f(info_c);
}

