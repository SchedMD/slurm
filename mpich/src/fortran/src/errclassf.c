/* errclass.c */
/* Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ERROR_CLASS = PMPI_ERROR_CLASS
void MPI_ERROR_CLASS ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_error_class__ = pmpi_error_class__
void mpi_error_class__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_error_class = pmpi_error_class
void mpi_error_class ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_error_class_ = pmpi_error_class_
void mpi_error_class_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ERROR_CLASS  MPI_ERROR_CLASS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_error_class__  mpi_error_class__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_error_class  mpi_error_class
#else
#pragma _HP_SECONDARY_DEF pmpi_error_class_  mpi_error_class_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ERROR_CLASS as PMPI_ERROR_CLASS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_error_class__ as pmpi_error_class__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_error_class as pmpi_error_class
#else
#pragma _CRI duplicate mpi_error_class_ as pmpi_error_class_
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
#define mpi_error_class_ PMPI_ERROR_CLASS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_error_class_ pmpi_error_class__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_error_class_ pmpi_error_class
#else
#define mpi_error_class_ pmpi_error_class_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_error_class_ MPI_ERROR_CLASS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_error_class_ mpi_error_class__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_error_class_ mpi_error_class
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_error_class_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_error_class_( MPI_Fint *errorcode, MPI_Fint *errorclass, MPI_Fint *__ierr )
{
    int l_errorclass;

    *__ierr = MPI_Error_class((int)*errorcode, &l_errorclass);
    *errorclass = l_errorclass;
}
