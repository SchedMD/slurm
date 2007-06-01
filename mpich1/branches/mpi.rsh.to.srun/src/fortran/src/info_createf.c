/* 
 *   $Id: info_createf.c,v 1.5 2001/12/12 23:36:41 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_INFO_CREATE = PMPI_INFO_CREATE
void MPI_INFO_CREATE (MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_info_create__ = pmpi_info_create__
void mpi_info_create__ (MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_info_create = pmpi_info_create
void mpi_info_create (MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_info_create_ = pmpi_info_create_
void mpi_info_create_ (MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_INFO_CREATE  MPI_INFO_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_create__  mpi_info_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_create  mpi_info_create
#else
#pragma _HP_SECONDARY_DEF pmpi_info_create_  mpi_info_create_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_INFO_CREATE as PMPI_INFO_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_info_create__ as pmpi_info_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_info_create as pmpi_info_create
#else
#pragma _CRI duplicate mpi_info_create_ as pmpi_info_create_
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
#define mpi_info_create_ PMPI_INFO_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_create_ pmpi_info_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_create_ pmpi_info_create
#else
#define mpi_info_create_ pmpi_info_create_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_info_create_ MPI_INFO_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_create_ mpi_info_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_create_ mpi_info_create
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_info_create_ (MPI_Fint *, MPI_Fint * );

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_info_create_(MPI_Fint *info, MPI_Fint *__ierr )
{
    MPI_Info info_c;

    *__ierr = MPI_Info_create(&info_c);
    if (*__ierr == MPI_SUCCESS) 		     
        *info = MPI_Info_c2f(info_c);
}
