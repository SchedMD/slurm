/* cancel.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_CANCEL = PMPI_CANCEL
void MPI_CANCEL ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_cancel__ = pmpi_cancel__
void mpi_cancel__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_cancel = pmpi_cancel
void mpi_cancel ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_cancel_ = pmpi_cancel_
void mpi_cancel_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_CANCEL  MPI_CANCEL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cancel__  mpi_cancel__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cancel  mpi_cancel
#else
#pragma _HP_SECONDARY_DEF pmpi_cancel_  mpi_cancel_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_CANCEL as PMPI_CANCEL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_cancel__ as pmpi_cancel__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_cancel as pmpi_cancel
#else
#pragma _CRI duplicate mpi_cancel_ as pmpi_cancel_
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
#define mpi_cancel_ PMPI_CANCEL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cancel_ pmpi_cancel__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cancel_ pmpi_cancel
#else
#define mpi_cancel_ pmpi_cancel_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_cancel_ MPI_CANCEL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cancel_ mpi_cancel__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cancel_ mpi_cancel
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_cancel_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_cancel_( MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;

    lrequest = MPI_Request_f2c(*request);  
    *__ierr = MPI_Cancel(&lrequest); 
}
