/* group_rank.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GROUP_RANK = PMPI_GROUP_RANK
void MPI_GROUP_RANK ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_group_rank__ = pmpi_group_rank__
void mpi_group_rank__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_group_rank = pmpi_group_rank
void mpi_group_rank ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_group_rank_ = pmpi_group_rank_
void mpi_group_rank_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GROUP_RANK  MPI_GROUP_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_rank__  mpi_group_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_rank  mpi_group_rank
#else
#pragma _HP_SECONDARY_DEF pmpi_group_rank_  mpi_group_rank_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GROUP_RANK as PMPI_GROUP_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_group_rank__ as pmpi_group_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_group_rank as pmpi_group_rank
#else
#pragma _CRI duplicate mpi_group_rank_ as pmpi_group_rank_
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
#define mpi_group_rank_ PMPI_GROUP_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_rank_ pmpi_group_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_rank_ pmpi_group_rank
#else
#define mpi_group_rank_ pmpi_group_rank_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_group_rank_ MPI_GROUP_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_rank_ mpi_group_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_rank_ mpi_group_rank
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_group_rank_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_group_rank_ ( MPI_Fint *group, MPI_Fint *rank, MPI_Fint *__ierr )
{
    int l_rank;
    *__ierr = MPI_Group_rank( MPI_Group_f2c(*group), &l_rank );
    *rank = l_rank;
}
