/* group_compare.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GROUP_COMPARE = PMPI_GROUP_COMPARE
void MPI_GROUP_COMPARE ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_group_compare__ = pmpi_group_compare__
void mpi_group_compare__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_group_compare = pmpi_group_compare
void mpi_group_compare ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_group_compare_ = pmpi_group_compare_
void mpi_group_compare_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GROUP_COMPARE  MPI_GROUP_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_compare__  mpi_group_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_compare  mpi_group_compare
#else
#pragma _HP_SECONDARY_DEF pmpi_group_compare_  mpi_group_compare_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GROUP_COMPARE as PMPI_GROUP_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_group_compare__ as pmpi_group_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_group_compare as pmpi_group_compare
#else
#pragma _CRI duplicate mpi_group_compare_ as pmpi_group_compare_
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
#define mpi_group_compare_ PMPI_GROUP_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_compare_ pmpi_group_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_compare_ pmpi_group_compare
#else
#define mpi_group_compare_ pmpi_group_compare_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_group_compare_ MPI_GROUP_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_compare_ mpi_group_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_compare_ mpi_group_compare
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_group_compare_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                    MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_group_compare_ ( MPI_Fint *group1, MPI_Fint *group2, MPI_Fint *result, MPI_Fint *__ierr )
{
    int l_result;
    *__ierr = MPI_Group_compare( MPI_Group_f2c(*group1), 
                                 MPI_Group_f2c(*group2), &l_result );
    *result = l_result;
}
