/* errget.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ERRHANDLER_GET = PMPI_ERRHANDLER_GET
void MPI_ERRHANDLER_GET ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_errhandler_get__ = pmpi_errhandler_get__
void mpi_errhandler_get__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_errhandler_get = pmpi_errhandler_get
void mpi_errhandler_get ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_errhandler_get_ = pmpi_errhandler_get_
void mpi_errhandler_get_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ERRHANDLER_GET  MPI_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_get__  mpi_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_get  mpi_errhandler_get
#else
#pragma _HP_SECONDARY_DEF pmpi_errhandler_get_  mpi_errhandler_get_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ERRHANDLER_GET as PMPI_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_errhandler_get__ as pmpi_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_errhandler_get as pmpi_errhandler_get
#else
#pragma _CRI duplicate mpi_errhandler_get_ as pmpi_errhandler_get_
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
#define mpi_errhandler_get_ PMPI_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_get_ pmpi_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_get_ pmpi_errhandler_get
#else
#define mpi_errhandler_get_ pmpi_errhandler_get_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_errhandler_get_ MPI_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_get_ mpi_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_get_ mpi_errhandler_get
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_errhandler_get_ ( MPI_Fint *, MPI_Fint *, 
                                     MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_errhandler_get_( MPI_Fint *comm, MPI_Fint *errhandler, MPI_Fint *__ierr )
{
    MPI_Errhandler l_errhandler;
    *__ierr = MPI_Errhandler_get( MPI_Comm_f2c(*comm), &l_errhandler );
    if (*__ierr == MPI_SUCCESS) 		     
        *errhandler = MPI_Errhandler_c2f(l_errhandler);
}
