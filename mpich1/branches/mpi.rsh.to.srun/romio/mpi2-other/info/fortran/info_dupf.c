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
#define mpi_info_dup_ PMPI_INFO_DUP
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_dup_ pmpi_info_dup__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_dup pmpi_info_dup_
#endif
#define mpi_info_dup_ pmpi_info_dup
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpi_info_dup_ pmpi_info_dup
#endif
#define mpi_info_dup_ pmpi_info_dup_
#endif

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
#pragma weak MPI_INFO_DUP = PMPI_INFO_DUP
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma weak mpi_info_dup__ = pmpi_info_dup__
#elif !defined(FORTRANUNDERSCORE)
#pragma weak mpi_info_dup = pmpi_info_dup
#else
#pragma weak mpi_info_dup_ = pmpi_info_dup_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPI_INFO_DUP MPI_INFO_DUP
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_dup__ mpi_info_dup__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_dup mpi_info_dup
#else
#pragma _HP_SECONDARY_DEF pmpi_info_dup_ mpi_info_dup_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPI_INFO_DUP as PMPI_INFO_DUP
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpi_info_dup__ as pmpi_info_dup__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpi_info_dup as pmpi_info_dup
#else
#pragma _CRI duplicate mpi_info_dup_ as pmpi_info_dup_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#else

#ifdef FORTRANCAPS
#define mpi_info_dup_ MPI_INFO_DUP
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpi_info_dup_ mpi_info_dup__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_dup mpi_info_dup_
#endif
#define mpi_info_dup_ mpi_info_dup
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpi_info_dup_ mpi_info_dup
#endif
#endif
#endif

void mpi_info_dup_(MPI_Fint *info, MPI_Fint *newinfo, int *ierr )
{
    MPI_Info info_c, newinfo_c;

    info_c = MPI_Info_f2c(*info);
    *ierr = MPI_Info_dup(info_c, &newinfo_c);
    *newinfo = MPI_Info_c2f(newinfo_c);
}
