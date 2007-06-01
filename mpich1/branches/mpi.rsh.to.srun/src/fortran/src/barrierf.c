/* barrier.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_BARRIER = PMPI_BARRIER
void MPI_BARRIER ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_barrier__ = pmpi_barrier__
void mpi_barrier__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_barrier = pmpi_barrier
void mpi_barrier ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_barrier_ = pmpi_barrier_
void mpi_barrier_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_BARRIER  MPI_BARRIER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_barrier__  mpi_barrier__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_barrier  mpi_barrier
#else
#pragma _HP_SECONDARY_DEF pmpi_barrier_  mpi_barrier_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_BARRIER as PMPI_BARRIER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_barrier__ as pmpi_barrier__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_barrier as pmpi_barrier
#else
#pragma _CRI duplicate mpi_barrier_ as pmpi_barrier_
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
#define mpi_barrier_ PMPI_BARRIER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_barrier_ pmpi_barrier__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_barrier_ pmpi_barrier
#else
#define mpi_barrier_ pmpi_barrier_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_barrier_ MPI_BARRIER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_barrier_ mpi_barrier__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_barrier_ mpi_barrier
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_barrier_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_barrier_ ( MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Barrier( MPI_Comm_f2c(*comm) );
}









