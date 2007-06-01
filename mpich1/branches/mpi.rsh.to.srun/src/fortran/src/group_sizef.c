/* group_size.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GROUP_SIZE = PMPI_GROUP_SIZE
void MPI_GROUP_SIZE ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_group_size__ = pmpi_group_size__
void mpi_group_size__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_group_size = pmpi_group_size
void mpi_group_size ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_group_size_ = pmpi_group_size_
void mpi_group_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GROUP_SIZE  MPI_GROUP_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_size__  mpi_group_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_size  mpi_group_size
#else
#pragma _HP_SECONDARY_DEF pmpi_group_size_  mpi_group_size_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GROUP_SIZE as PMPI_GROUP_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_group_size__ as pmpi_group_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_group_size as pmpi_group_size
#else
#pragma _CRI duplicate mpi_group_size_ as pmpi_group_size_
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
#define mpi_group_size_ PMPI_GROUP_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_size_ pmpi_group_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_size_ pmpi_group_size
#else
#define mpi_group_size_ pmpi_group_size_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_group_size_ MPI_GROUP_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_size_ mpi_group_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_size_ mpi_group_size
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_group_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_group_size_ ( MPI_Fint *group, MPI_Fint *size, MPI_Fint *__ierr )
{
    int l_size;
    *__ierr = MPI_Group_size( MPI_Group_f2c(*group), &l_size );
    *size = l_size;
}
