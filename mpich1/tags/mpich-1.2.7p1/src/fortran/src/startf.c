/* start.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_START = PMPI_START
void MPI_START ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_start__ = pmpi_start__
void mpi_start__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_start = pmpi_start
void mpi_start ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_start_ = pmpi_start_
void mpi_start_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_START  MPI_START
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_start__  mpi_start__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_start  mpi_start
#else
#pragma _HP_SECONDARY_DEF pmpi_start_  mpi_start_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_START as PMPI_START
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_start__ as pmpi_start__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_start as pmpi_start
#else
#pragma _CRI duplicate mpi_start_ as pmpi_start_
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
#define mpi_start_ PMPI_START
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_start_ pmpi_start__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_start_ pmpi_start
#else
#define mpi_start_ pmpi_start_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_start_ MPI_START
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_start_ mpi_start__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_start_ mpi_start
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_start_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_start_( MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest = MPI_Request_f2c(*request );
    *__ierr = MPI_Start( &lrequest );
    if (*__ierr == MPI_SUCCESS) 
        *request = MPI_Request_c2f(lrequest);
}
