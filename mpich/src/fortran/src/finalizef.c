/* finalize.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_FINALIZE = PMPI_FINALIZE
void MPI_FINALIZE ( MPI_Fint *__ierr );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_finalize__ = pmpi_finalize__
void mpi_finalize__ ( MPI_Fint *__ierr );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_finalize = pmpi_finalize
void mpi_finalize ( MPI_Fint *__ierr );
#else
#pragma weak mpi_finalize_ = pmpi_finalize_
void mpi_finalize_ ( MPI_Fint *__ierr );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_FINALIZE  MPI_FINALIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_finalize__  mpi_finalize__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_finalize  mpi_finalize
#else
#pragma _HP_SECONDARY_DEF pmpi_finalize_  mpi_finalize_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_FINALIZE as PMPI_FINALIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_finalize__ as pmpi_finalize__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_finalize as pmpi_finalize
#else
#pragma _CRI duplicate mpi_finalize_ as pmpi_finalize_
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
#define mpi_finalize_ PMPI_FINALIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_finalize_ pmpi_finalize__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_finalize_ pmpi_finalize
#else
#define mpi_finalize_ pmpi_finalize_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_finalize_ MPI_FINALIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_finalize_ mpi_finalize__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_finalize_ mpi_finalize
#endif
#endif

/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_finalize_( MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_finalize_( MPI_Fint *__ierr )
{
    *__ierr = MPI_Finalize();
}
