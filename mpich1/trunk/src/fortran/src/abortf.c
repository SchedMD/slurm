/* abort.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ABORT = PMPI_ABORT
void MPI_ABORT ( MPI_Fint *comm, MPI_Fint *errorcode, MPI_Fint *__ierr );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_abort__ = pmpi_abort__
void mpi_abort__ ( MPI_Fint *comm, MPI_Fint *errorcode, MPI_Fint *__ierr );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_abort = pmpi_abort
void mpi_abort ( MPI_Fint *comm, MPI_Fint *errorcode, MPI_Fint *__ierr );
#else
#pragma weak mpi_abort_ = pmpi_abort_
void mpi_abort_ ( MPI_Fint *comm, MPI_Fint *errorcode, MPI_Fint *__ierr );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ABORT  MPI_ABORT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_abort__  mpi_abort__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_abort  mpi_abort
#else
#pragma _HP_SECONDARY_DEF pmpi_abort_  mpi_abort_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ABORT as PMPI_ABORT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_abort__ as pmpi_abort__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_abort as pmpi_abort
#else
#pragma _CRI duplicate mpi_abort_ as pmpi_abort_
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
#define mpi_abort_ PMPI_ABORT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_abort_ pmpi_abort__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_abort_ pmpi_abort
#else
#define mpi_abort_ pmpi_abort_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_abort_ MPI_ABORT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_abort_ mpi_abort__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_abort_ mpi_abort
#endif
#endif

/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_abort_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_abort_( MPI_Fint *comm, MPI_Fint *errorcode, MPI_Fint *__ierr )
{
    *__ierr = MPI_Abort( MPI_Comm_f2c(*comm), (int)*errorcode);
}



