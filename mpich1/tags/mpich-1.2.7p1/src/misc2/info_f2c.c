/* 
 *   $Id: info_f2c.c,v 1.9 1999/11/23 03:13:28 gropp Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_f2c = PMPI_Info_f2c
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_f2c  MPI_Info_f2c
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_f2c as PMPI_Info_f2c
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/*@
    MPI_Info_f2c - Translates a Fortran info handle to a C info handle

Input Parameters:
. info - Fortran info handle (integer)

Return Value:
C info handle (handle)
@*/
MPI_Info MPI_Info_f2c(MPI_Fint info)
{
#ifndef INT_LT_POINTER
    return (MPI_Info) info;
#else
    int mpi_errno;
    static char myname[] = "MPI_INFO_F2C";
    if (!info) return MPI_INFO_NULL;
    if ((info < 0) || (info > MPIR_Infotable_ptr)) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_INFO, MPIR_ERR_DEFAULT, myname, 
				     (char *)0, (char *)0 );
	(void)MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return 0;
    }
    return MPIR_Infotable[info];
#endif
}
