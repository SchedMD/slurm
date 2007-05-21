/* wtick.c */
/* Custom Fortran interface file*/
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_WTICK = PMPI_WTICK
double MPI_WTICK ( void );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_wtick__ = pmpi_wtick__
double mpi_wtick__ ( void );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_wtick = pmpi_wtick
double mpi_wtick ( void );
#else
#pragma weak mpi_wtick_ = pmpi_wtick_
double mpi_wtick_ ( void );

#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_WTICK  MPI_WTICK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_wtick__  mpi_wtick__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_wtick  mpi_wtick
#else
#pragma _HP_SECONDARY_DEF pmpi_wtick_  mpi_wtick_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_WTICK as PMPI_WTICK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_wtick__ as pmpi_wtick__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_wtick as pmpi_wtick
#else
#pragma _CRI duplicate mpi_wtick_ as pmpi_wtick_
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
#define mpi_wtick_ PMPI_WTICK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_wtick_ pmpi_wtick__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_wtick_ pmpi_wtick
#else
#define mpi_wtick_ pmpi_wtick_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_wtick_ MPI_WTICK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_wtick_ mpi_wtick__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_wtick_ mpi_wtick
#endif
#endif

/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API double FORT_CALL mpi_wtick_ ( void );

FORTRAN_API double FORT_CALL mpi_wtick_()
{
    return MPI_Wtick();
}



