/* comm_free.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_FREE = PMPI_COMM_FREE
void MPI_COMM_FREE ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_free__ = pmpi_comm_free__
void mpi_comm_free__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_free = pmpi_comm_free
void mpi_comm_free ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_comm_free_ = pmpi_comm_free_
void mpi_comm_free_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_FREE  MPI_COMM_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_free__  mpi_comm_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_free  mpi_comm_free
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_free_  mpi_comm_free_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_FREE as PMPI_COMM_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_free__ as pmpi_comm_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_free as pmpi_comm_free
#else
#pragma _CRI duplicate mpi_comm_free_ as pmpi_comm_free_
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
#define mpi_comm_free_ PMPI_COMM_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_free_ pmpi_comm_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_free_ pmpi_comm_free
#else
#define mpi_comm_free_ pmpi_comm_free_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_free_ MPI_COMM_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_free_ mpi_comm_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_free_ mpi_comm_free
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_comm_free_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_comm_free_ ( MPI_Fint *comm, MPI_Fint *__ierr )
{
    MPI_Comm l_comm = MPI_Comm_f2c(*comm);
    *__ierr = MPI_Comm_free(&l_comm);
    if (*__ierr == MPI_SUCCESS) 		     
        *comm = MPI_Comm_c2f(l_comm);
}
