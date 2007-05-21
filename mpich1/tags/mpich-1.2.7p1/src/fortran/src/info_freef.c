/* 
 *   $Id: info_freef.c,v 1.5 2001/12/12 23:36:42 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_INFO_FREE = PMPI_INFO_FREE
void MPI_INFO_FREE (MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_info_free__ = pmpi_info_free__
void mpi_info_free__ (MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_info_free = pmpi_info_free
void mpi_info_free (MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_info_free_ = pmpi_info_free_
void mpi_info_free_ (MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_INFO_FREE  MPI_INFO_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_free__  mpi_info_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_free  mpi_info_free
#else
#pragma _HP_SECONDARY_DEF pmpi_info_free_  mpi_info_free_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_INFO_FREE as PMPI_INFO_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_info_free__ as pmpi_info_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_info_free as pmpi_info_free
#else
#pragma _CRI duplicate mpi_info_free_ as pmpi_info_free_
#endif

/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

#ifdef F77_NAME_UPPER
#define mpi_info_free_ PMPI_INFO_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_free_ pmpi_info_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_free_ pmpi_info_free
#else
#define mpi_info_free_ pmpi_info_free_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_info_free_ MPI_INFO_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_free_ mpi_info_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_free_ mpi_info_free
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_info_free_ (MPI_Fint *, MPI_Fint *);

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_info_free_(MPI_Fint *info, MPI_Fint *__ierr )
{
    MPI_Info info_c;

    info_c = MPI_Info_f2c(*info);
    *__ierr = MPI_Info_free(&info_c);
    if (*__ierr == MPI_SUCCESS) 		     
        *info = MPI_Info_c2f(info_c);
}

