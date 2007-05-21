/* group_free.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GROUP_FREE = PMPI_GROUP_FREE
void MPI_GROUP_FREE ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_group_free__ = pmpi_group_free__
void mpi_group_free__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_group_free = pmpi_group_free
void mpi_group_free ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_group_free_ = pmpi_group_free_
void mpi_group_free_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GROUP_FREE  MPI_GROUP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_free__  mpi_group_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_free  mpi_group_free
#else
#pragma _HP_SECONDARY_DEF pmpi_group_free_  mpi_group_free_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GROUP_FREE as PMPI_GROUP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_group_free__ as pmpi_group_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_group_free as pmpi_group_free
#else
#pragma _CRI duplicate mpi_group_free_ as pmpi_group_free_
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
#define mpi_group_free_ PMPI_GROUP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_free_ pmpi_group_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_free_ pmpi_group_free
#else
#define mpi_group_free_ pmpi_group_free_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_group_free_ MPI_GROUP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_free_ mpi_group_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_free_ mpi_group_free
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_group_free_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_group_free_ ( MPI_Fint *group, MPI_Fint *__ierr )
{
    MPI_Group l_group = MPI_Group_f2c(*group);
    *__ierr = MPI_Group_free(&l_group);
    if (*__ierr == MPI_SUCCESS) 		     
        *group = MPI_Group_c2f(l_group);
}


