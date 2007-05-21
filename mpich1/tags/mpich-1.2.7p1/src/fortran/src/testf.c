/* test.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TEST = PMPI_TEST
void MPI_TEST ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_test__ = pmpi_test__
void mpi_test__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_test = pmpi_test
void mpi_test ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_test_ = pmpi_test_
void mpi_test_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TEST  MPI_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_test__  mpi_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_test  mpi_test
#else
#pragma _HP_SECONDARY_DEF pmpi_test_  mpi_test_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TEST as PMPI_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_test__ as pmpi_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_test as pmpi_test
#else
#pragma _CRI duplicate mpi_test_ as pmpi_test_
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
#define mpi_test_ PMPI_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_test_ pmpi_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_test_ pmpi_test
#else
#define mpi_test_ pmpi_test_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_test_ MPI_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_test_ mpi_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_test_ mpi_test
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_test_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                           MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_test_ ( MPI_Fint *request, MPI_Fint *flag, MPI_Fint *status, MPI_Fint *__ierr )
{
    int        l_flag;
    MPI_Status c_status;
    MPI_Request lrequest = MPI_Request_f2c(*request);

    *__ierr = MPI_Test( &lrequest, &l_flag, &c_status);
    if (*__ierr != MPI_SUCCESS) return;
    *request = MPI_Request_c2f(lrequest);

    *flag = MPIR_TO_FLOG(l_flag);
    if (l_flag) 
	MPI_Status_c2f(&c_status, status);
}
