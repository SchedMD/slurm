/* comm_rank.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_RANK = PMPI_COMM_RANK
void MPI_COMM_RANK ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_rank__ = pmpi_comm_rank__
void mpi_comm_rank__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_rank = pmpi_comm_rank
void mpi_comm_rank ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_comm_rank_ = pmpi_comm_rank_
void mpi_comm_rank_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_RANK  MPI_COMM_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_rank__  mpi_comm_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_rank  mpi_comm_rank
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_rank_  mpi_comm_rank_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_RANK as PMPI_COMM_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_rank__ as pmpi_comm_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_rank as pmpi_comm_rank
#else
#pragma _CRI duplicate mpi_comm_rank_ as pmpi_comm_rank_
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
#define mpi_comm_rank_ PMPI_COMM_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_rank_ pmpi_comm_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_rank_ pmpi_comm_rank
#else
#define mpi_comm_rank_ pmpi_comm_rank_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_rank_ MPI_COMM_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_rank_ mpi_comm_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_rank_ mpi_comm_rank
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_comm_rank_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_comm_rank_ ( MPI_Fint *comm, MPI_Fint *rank, MPI_Fint *__ierr )
{
    int l_rank;
    *__ierr = MPI_Comm_rank( MPI_Comm_f2c(*comm), &l_rank);
    *rank = (MPI_Fint)l_rank;
}
