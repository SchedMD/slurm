/* wait.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_WAIT = PMPI_WAIT
void MPI_WAIT ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_wait__ = pmpi_wait__
void mpi_wait__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_wait = pmpi_wait
void mpi_wait ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_wait_ = pmpi_wait_
void mpi_wait_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_WAIT  MPI_WAIT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_wait__  mpi_wait__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_wait  mpi_wait
#else
#pragma _HP_SECONDARY_DEF pmpi_wait_  mpi_wait_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_WAIT as PMPI_WAIT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_wait__ as pmpi_wait__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_wait as pmpi_wait
#else
#pragma _CRI duplicate mpi_wait_ as pmpi_wait_
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
#define mpi_wait_ PMPI_WAIT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_wait_ pmpi_wait__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_wait_ pmpi_wait
#else
#define mpi_wait_ pmpi_wait_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_wait_ MPI_WAIT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_wait_ mpi_wait__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_wait_ mpi_wait
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_wait_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_wait_ ( MPI_Fint *request, MPI_Fint *status, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    MPI_Status c_status;

    lrequest = MPI_Request_f2c(*request);
    *__ierr = MPI_Wait(&lrequest, &c_status);
    *request = MPI_Request_c2f(lrequest);

    if (*__ierr == MPI_SUCCESS) 
        MPI_Status_c2f(&c_status, status);
}
