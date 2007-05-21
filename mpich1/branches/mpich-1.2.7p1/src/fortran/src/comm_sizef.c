/* comm_size.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_SIZE = PMPI_COMM_SIZE
void MPI_COMM_SIZE ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_size__ = pmpi_comm_size__
void mpi_comm_size__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_size = pmpi_comm_size
void mpi_comm_size ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_comm_size_ = pmpi_comm_size_
void mpi_comm_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_SIZE  MPI_COMM_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_size__  mpi_comm_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_size  mpi_comm_size
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_size_  mpi_comm_size_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_SIZE as PMPI_COMM_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_size__ as pmpi_comm_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_size as pmpi_comm_size
#else
#pragma _CRI duplicate mpi_comm_size_ as pmpi_comm_size_
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
#define mpi_comm_size_ PMPI_COMM_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_size_ pmpi_comm_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_size_ pmpi_comm_size
#else
#define mpi_comm_size_ pmpi_comm_size_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_size_ MPI_COMM_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_size_ mpi_comm_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_size_ mpi_comm_size
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_comm_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_comm_size_ ( MPI_Fint *comm, MPI_Fint *size, MPI_Fint *__ierr )
{
    int l_size;

    *__ierr = MPI_Comm_size( MPI_Comm_f2c(*comm), &l_size );
    *size = (MPI_Fint)l_size;
}

