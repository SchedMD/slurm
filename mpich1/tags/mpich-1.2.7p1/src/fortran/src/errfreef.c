/* errfree.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ERRHANDLER_FREE = PMPI_ERRHANDLER_FREE
void MPI_ERRHANDLER_FREE ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_errhandler_free__ = pmpi_errhandler_free__
void mpi_errhandler_free__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_errhandler_free = pmpi_errhandler_free
void mpi_errhandler_free ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_errhandler_free_ = pmpi_errhandler_free_
void mpi_errhandler_free_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ERRHANDLER_FREE  MPI_ERRHANDLER_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_free__  mpi_errhandler_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_free  mpi_errhandler_free
#else
#pragma _HP_SECONDARY_DEF pmpi_errhandler_free_  mpi_errhandler_free_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ERRHANDLER_FREE as PMPI_ERRHANDLER_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_errhandler_free__ as pmpi_errhandler_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_errhandler_free as pmpi_errhandler_free
#else
#pragma _CRI duplicate mpi_errhandler_free_ as pmpi_errhandler_free_
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
#define mpi_errhandler_free_ PMPI_ERRHANDLER_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_free_ pmpi_errhandler_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_free_ pmpi_errhandler_free
#else
#define mpi_errhandler_free_ pmpi_errhandler_free_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_errhandler_free_ MPI_ERRHANDLER_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_free_ mpi_errhandler_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_free_ mpi_errhandler_free
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_errhandler_free_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_errhandler_free_( MPI_Fint *errhandler, MPI_Fint *__ierr )
{
    MPI_Errhandler l_errhandler = MPI_Errhandler_c2f(*errhandler);
    *__ierr = MPI_Errhandler_free( &l_errhandler );
}
