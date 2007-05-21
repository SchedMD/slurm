/* commreq_free.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_REQUEST_FREE = PMPI_REQUEST_FREE
void MPI_REQUEST_FREE ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_request_free__ = pmpi_request_free__
void mpi_request_free__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_request_free = pmpi_request_free
void mpi_request_free ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_request_free_ = pmpi_request_free_
void mpi_request_free_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_REQUEST_FREE  MPI_REQUEST_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_request_free__  mpi_request_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_request_free  mpi_request_free
#else
#pragma _HP_SECONDARY_DEF pmpi_request_free_  mpi_request_free_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_REQUEST_FREE as PMPI_REQUEST_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_request_free__ as pmpi_request_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_request_free as pmpi_request_free
#else
#pragma _CRI duplicate mpi_request_free_ as pmpi_request_free_
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
#define mpi_request_free_ PMPI_REQUEST_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_request_free_ pmpi_request_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_request_free_ pmpi_request_free
#else
#define mpi_request_free_ pmpi_request_free_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_request_free_ MPI_REQUEST_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_request_free_ mpi_request_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_request_free_ mpi_request_free
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_request_free_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_request_free_( MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest = MPI_Request_f2c(*request);
    *__ierr = MPI_Request_free( &lrequest );
    if (*__ierr == MPI_SUCCESS) 		     
        *request = MPI_Request_c2f(lrequest);
}

