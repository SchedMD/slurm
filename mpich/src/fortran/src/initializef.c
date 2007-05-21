/* initialize.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_INITIALIZED = PMPI_INITIALIZED
void MPI_INITIALIZED ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_initialized__ = pmpi_initialized__
void mpi_initialized__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_initialized = pmpi_initialized
void mpi_initialized ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_initialized_ = pmpi_initialized_
void mpi_initialized_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_INITIALIZED  MPI_INITIALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_initialized__  mpi_initialized__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_initialized  mpi_initialized
#else
#pragma _HP_SECONDARY_DEF pmpi_initialized_  mpi_initialized_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_INITIALIZED as PMPI_INITIALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_initialized__ as pmpi_initialized__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_initialized as pmpi_initialized
#else
#pragma _CRI duplicate mpi_initialized_ as pmpi_initialized_
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
#define mpi_initialized_ PMPI_INITIALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_initialized_ pmpi_initialized__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_initialized_ pmpi_initialized
#else
#define mpi_initialized_ pmpi_initialized_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_initialized_ MPI_INITIALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_initialized_ mpi_initialized__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_initialized_ mpi_initialized
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_initialized_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_initialized_( MPI_Fint *flag, MPI_Fint *__ierr )
{
    int lflag;
    *__ierr = MPI_Initialized(&lflag);
    *flag = MPIR_TO_FLOG(lflag);
}
