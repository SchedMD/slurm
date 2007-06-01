/* errcreate.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ERRHANDLER_CREATE = PMPI_ERRHANDLER_CREATE
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
void MPI_ERR_HANDLER_CREATE ( MPI_Handler_function **, MPI_Fint *, MPI_Fint * );
#else
void MPI_ERRHANDLER_CREATE ( MPI_Handler_function *, MPI_Fint *, MPI_Fint * );
#endif
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_errhandler_create__ = pmpi_errhandler_create__
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_errhandler_create__ ( MPI_Handler_function **, MPI_Fint *, MPI_Fint * );
#else
void mpi_errhandler_create__ ( MPI_Handler_function *, MPI_Fint *, MPI_Fint * );
#endif
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_errhandler_create = pmpi_errhandler_create
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_errhandler_create ( MPI_Handler_function **, MPI_Fint *, MPI_Fint * );
#else
void mpi_errhandler_create ( MPI_Handler_function *, MPI_Fint *, MPI_Fint * );
#endif
#else
#pragma weak mpi_errhandler_create_ = pmpi_errhandler_create_
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_errhandler_create_ ( MPI_Handler_function **, MPI_Fint *, MPI_Fint * );
#else
void mpi_errhandler_create_ ( MPI_Handler_function *, MPI_Fint *, MPI_Fint * );
#endif
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ERRHANDLER_CREATE  MPI_ERRHANDLER_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_create__  mpi_errhandler_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_create  mpi_errhandler_create
#else
#pragma _HP_SECONDARY_DEF pmpi_errhandler_create_  mpi_errhandler_create_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ERRHANDLER_CREATE as PMPI_ERRHANDLER_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_errhandler_create__ as pmpi_errhandler_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_errhandler_create as pmpi_errhandler_create
#else
#pragma _CRI duplicate mpi_errhandler_create_ as pmpi_errhandler_create_
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
#define mpi_errhandler_create_ PMPI_ERRHANDLER_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_create_ pmpi_errhandler_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_create_ pmpi_errhandler_create
#else
#define mpi_errhandler_create_ pmpi_errhandler_create_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_errhandler_create_ MPI_ERRHANDLER_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_create_ mpi_errhandler_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_create_ mpi_errhandler_create
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
FORTRAN_API void FORT_CALL mpi_errhandler_create_ ( MPI_Handler_function **, 
					MPI_Fint *, MPI_Fint * );
#else
FORTRAN_API void FORT_CALL mpi_errhandler_create_ ( MPI_Handler_function *, 
					MPI_Fint *, MPI_Fint * );
#endif

FORTRAN_API void FORT_CALL mpi_errhandler_create_(
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
	MPI_Handler_function **function,
#else
	MPI_Handler_function *function,
#endif
	MPI_Fint *errhandler, MPI_Fint *__ierr)
{

    MPI_Errhandler l_errhandler;
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
    *__ierr = MPI_Errhandler_create( *function, &l_errhandler );
#else
    *__ierr = MPI_Errhandler_create( function, &l_errhandler );
#endif
    if (*__ierr == MPI_SUCCESS) 		     
        *errhandler = MPI_Errhandler_c2f(l_errhandler);
}
