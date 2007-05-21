/*
 *  $Id: getversionf.c,v 1.4 2001/12/12 23:36:36 ashton Exp $
 *
 *  (C) 1997 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GET_VERSION = PMPI_GET_VERSION
void MPI_GET_VERSION ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_get_version__ = pmpi_get_version__
void mpi_get_version__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_get_version = pmpi_get_version
void mpi_get_version ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_get_version_ = pmpi_get_version_
void mpi_get_version_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GET_VERSION  MPI_GET_VERSION
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_get_version__  mpi_get_version__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_get_version  mpi_get_version
#else
#pragma _HP_SECONDARY_DEF pmpi_get_version_  mpi_get_version_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GET_VERSION as PMPI_GET_VERSION
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_get_version__ as pmpi_get_version__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_get_version as pmpi_get_version
#else
#pragma _CRI duplicate mpi_get_version_ as pmpi_get_version_
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
#define mpi_get_version_ PMPI_GET_VERSION
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_get_version_ pmpi_get_version__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_get_version_ pmpi_get_version
#else
#define mpi_get_version_ pmpi_get_version_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_get_version_ MPI_GET_VERSION
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_get_version_ mpi_get_version__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_get_version_ mpi_get_version
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_get_version_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_get_version_( MPI_Fint *version, MPI_Fint *subversion, MPI_Fint *ierr )
{
    *version    = MPI_VERSION;
    *subversion = MPI_SUBVERSION;
    *ierr       = MPI_SUCCESS;
}
