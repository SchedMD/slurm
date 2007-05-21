/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_c2f = PMPI_Info_c2f
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_c2f MPI_Info_c2f
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_c2f as PMPI_Info_c2f
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif
#include "adio_extern.h"

/*@
    MPI_Info_c2f - Translates a C info handle to a Fortran info handle

Input Parameters:
. info - C info handle (integer)

Return Value:
  Fortran info handle (handle)
@*/
MPI_Fint MPI_Info_c2f(MPI_Info info)
{
#ifndef INT_LT_POINTER
    return (MPI_Fint) info;
#else
    int i;

    if ((info <= (MPI_Info) 0) || (info->cookie != MPIR_INFO_COOKIE)) 
	return (MPI_Fint) 0;
    if (!MPIR_Infotable) {
	MPIR_Infotable_max = 1024;
	MPIR_Infotable = (MPI_Info *)
	    ADIOI_Malloc(MPIR_Infotable_max*sizeof(MPI_Info)); 
        MPIR_Infotable_ptr = 0;  /* 0 can't be used though, because 
                                  MPI_INFO_NULL=0 */
	for (i=0; i<MPIR_Infotable_max; i++) MPIR_Infotable[i] = MPI_INFO_NULL;
    }
    if (MPIR_Infotable_ptr == MPIR_Infotable_max-1) {
	MPIR_Infotable = (MPI_Info *) ADIOI_Realloc(MPIR_Infotable, 
                           (MPIR_Infotable_max+1024)*sizeof(MPI_Info));
	for (i=MPIR_Infotable_max; i<MPIR_Infotable_max+1024; i++) 
	    MPIR_Infotable[i] = MPI_INFO_NULL;
	MPIR_Infotable_max += 1024;
    }
    MPIR_Infotable_ptr++;
    MPIR_Infotable[MPIR_Infotable_ptr] = info;
    return (MPI_Fint) MPIR_Infotable_ptr;
#endif
}
