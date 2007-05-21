/* testcancel.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TEST_CANCELLED = PMPI_TEST_CANCELLED
void MPI_TEST_CANCELLED ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_test_cancelled__ = pmpi_test_cancelled__
void mpi_test_cancelled__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_test_cancelled = pmpi_test_cancelled
void mpi_test_cancelled ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_test_cancelled_ = pmpi_test_cancelled_
void mpi_test_cancelled_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TEST_CANCELLED  MPI_TEST_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_test_cancelled__  mpi_test_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_test_cancelled  mpi_test_cancelled
#else
#pragma _HP_SECONDARY_DEF pmpi_test_cancelled_  mpi_test_cancelled_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TEST_CANCELLED as PMPI_TEST_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_test_cancelled__ as pmpi_test_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_test_cancelled as pmpi_test_cancelled
#else
#pragma _CRI duplicate mpi_test_cancelled_ as pmpi_test_cancelled_
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
#define mpi_test_cancelled_ PMPI_TEST_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_test_cancelled_ pmpi_test_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_test_cancelled_ pmpi_test_cancelled
#else
#define mpi_test_cancelled_ pmpi_test_cancelled_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_test_cancelled_ MPI_TEST_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_test_cancelled_ mpi_test_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_test_cancelled_ mpi_test_cancelled
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_test_cancelled_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_test_cancelled_(MPI_Fint *status, MPI_Fint *flag, MPI_Fint *__ierr)
{
    int lflag;
    MPI_Status c_status;

    MPI_Status_f2c(status, &c_status); 
    *__ierr = MPI_Test_cancelled(&c_status, &lflag);
    *flag = MPIR_TO_FLOG(lflag);
}
