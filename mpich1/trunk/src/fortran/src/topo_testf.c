/* topo_test.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TOPO_TEST = PMPI_TOPO_TEST
void MPI_TOPO_TEST ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_topo_test__ = pmpi_topo_test__
void mpi_topo_test__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_topo_test = pmpi_topo_test
void mpi_topo_test ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_topo_test_ = pmpi_topo_test_
void mpi_topo_test_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TOPO_TEST  MPI_TOPO_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_topo_test__  mpi_topo_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_topo_test  mpi_topo_test
#else
#pragma _HP_SECONDARY_DEF pmpi_topo_test_  mpi_topo_test_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TOPO_TEST as PMPI_TOPO_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_topo_test__ as pmpi_topo_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_topo_test as pmpi_topo_test
#else
#pragma _CRI duplicate mpi_topo_test_ as pmpi_topo_test_
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
#define mpi_topo_test_ PMPI_TOPO_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_topo_test_ pmpi_topo_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_topo_test_ pmpi_topo_test
#else
#define mpi_topo_test_ pmpi_topo_test_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_topo_test_ MPI_TOPO_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_topo_test_ mpi_topo_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_topo_test_ mpi_topo_test
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_topo_test_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_topo_test_ ( MPI_Fint *comm, MPI_Fint *top_type, MPI_Fint *__ierr )
{
    int ltop_type;
    *__ierr = MPI_Topo_test( MPI_Comm_f2c(*comm), &ltop_type);
    *top_type = (MPI_Fint)ltop_type;
}
