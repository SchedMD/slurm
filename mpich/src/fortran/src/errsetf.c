/* errset.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ERRHANDLER_SET = PMPI_ERRHANDLER_SET
void MPI_ERRHANDLER_SET ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_errhandler_set__ = pmpi_errhandler_set__
void mpi_errhandler_set__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_errhandler_set = pmpi_errhandler_set
void mpi_errhandler_set ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_errhandler_set_ = pmpi_errhandler_set_
void mpi_errhandler_set_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ERRHANDLER_SET  MPI_ERRHANDLER_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_set__  mpi_errhandler_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_set  mpi_errhandler_set
#else
#pragma _HP_SECONDARY_DEF pmpi_errhandler_set_  mpi_errhandler_set_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ERRHANDLER_SET as PMPI_ERRHANDLER_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_errhandler_set__ as pmpi_errhandler_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_errhandler_set as pmpi_errhandler_set
#else
#pragma _CRI duplicate mpi_errhandler_set_ as pmpi_errhandler_set_
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
#define mpi_errhandler_set_ PMPI_ERRHANDLER_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_set_ pmpi_errhandler_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_set_ pmpi_errhandler_set
#else
#define mpi_errhandler_set_ pmpi_errhandler_set_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_errhandler_set_ MPI_ERRHANDLER_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_set_ mpi_errhandler_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_set_ mpi_errhandler_set
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_errhandler_set_ ( MPI_Fint *, MPI_Fint *, 
                                     MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_errhandler_set_( MPI_Fint *comm, MPI_Fint *errhandler, MPI_Fint *__ierr )
{
    MPI_Errhandler l_errhandler = MPI_Errhandler_f2c(*errhandler);
    *__ierr = MPI_Errhandler_set(MPI_Comm_f2c(*comm), l_errhandler );
}
