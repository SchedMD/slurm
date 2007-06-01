/* comm_compare.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_COMPARE = PMPI_COMM_COMPARE
void MPI_COMM_COMPARE ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_compare__ = pmpi_comm_compare__
void mpi_comm_compare__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_compare = pmpi_comm_compare
void mpi_comm_compare ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_comm_compare_ = pmpi_comm_compare_
void mpi_comm_compare_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_COMPARE  MPI_COMM_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_compare__  mpi_comm_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_compare  mpi_comm_compare
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_compare_  mpi_comm_compare_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_COMPARE as PMPI_COMM_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_compare__ as pmpi_comm_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_compare as pmpi_comm_compare
#else
#pragma _CRI duplicate mpi_comm_compare_ as pmpi_comm_compare_
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
#define mpi_comm_compare_ PMPI_COMM_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_compare_ pmpi_comm_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_compare_ pmpi_comm_compare
#else
#define mpi_comm_compare_ pmpi_comm_compare_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_compare_ MPI_COMM_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_compare_ mpi_comm_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_compare_ mpi_comm_compare
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_comm_compare_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                   MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_comm_compare_ ( MPI_Fint *comm1, MPI_Fint *comm2, MPI_Fint *result, MPI_Fint *__ierr )
{
    int l_result;

    *__ierr = MPI_Comm_compare( MPI_Comm_f2c(*comm1), 
                                MPI_Comm_f2c(*comm2), &l_result);
    *result = l_result;
}
