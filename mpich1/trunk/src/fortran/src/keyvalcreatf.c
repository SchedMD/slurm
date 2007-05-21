/* keyval_create.c */
/* CUSTOM WRAPPER */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_KEYVAL_CREATE = PMPI_KEYVAL_CREATE
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
void MPI_KEYVAL_CREATE ( MPI_Copy_function **, MPI_Delete_function **, MPI_Fint *, void *, MPI_Fint * );
#else
void MPI_KEYVAL_CREATE ( MPI_Copy_function *, MPI_Delete_function *, MPI_Fint *, void *, MPI_Fint * );
#endif
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_keyval_create__ = pmpi_keyval_create__
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_keyval_create__ ( MPI_Copy_function **, MPI_Delete_function **, MPI_Fint *, void *, MPI_Fint * );
#else
void mpi_keyval_create__ ( MPI_Copy_function *, MPI_Delete_function *, MPI_Fint *, void *, MPI_Fint * );
#endif
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_keyval_create = pmpi_keyval_create
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_keyval_create ( MPI_Copy_function **, MPI_Delete_function **, MPI_Fint *, void *, MPI_Fint * );
#else
void mpi_keyval_create ( MPI_Copy_function *, MPI_Delete_function *, MPI_Fint *, void *, MPI_Fint * );
#endif
#else
#pragma weak mpi_keyval_create_ = pmpi_keyval_create_
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_keyval_create_ ( MPI_Copy_function **, MPI_Delete_function **, MPI_Fint *, void *, MPI_Fint * );
#else
void mpi_keyval_create_ ( MPI_Copy_function *, MPI_Delete_function *, MPI_Fint *, void *, MPI_Fint * );
#endif
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_KEYVAL_CREATE  MPI_KEYVAL_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_keyval_create__  mpi_keyval_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_keyval_create  mpi_keyval_create
#else
#pragma _HP_SECONDARY_DEF pmpi_keyval_create_  mpi_keyval_create_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_KEYVAL_CREATE as PMPI_KEYVAL_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_keyval_create__ as pmpi_keyval_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_keyval_create as pmpi_keyval_create
#else
#pragma _CRI duplicate mpi_keyval_create_ as pmpi_keyval_create_
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
#define mpi_keyval_create_ PMPI_KEYVAL_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_keyval_create_ pmpi_keyval_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_keyval_create_ pmpi_keyval_create
#else
#define mpi_keyval_create_ pmpi_keyval_create_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_keyval_create_ MPI_KEYVAL_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_keyval_create_ mpi_keyval_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_keyval_create_ mpi_keyval_create
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
FORTRAN_API void FORT_CALL mpi_keyval_create_ ( MPI_Copy_function **, 
				    MPI_Delete_function **, MPI_Fint *, 
                                    void *, MPI_Fint * );
#else
FORTRAN_API void FORT_CALL mpi_keyval_create_ ( MPI_Copy_function *, 
				    MPI_Delete_function *, MPI_Fint *, 
                                    void *, MPI_Fint * );
#endif

FORTRAN_API void FORT_CALL mpi_keyval_create_ (
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
	MPI_Copy_function   **copy_fn,
	MPI_Delete_function **delete_fn,
#else
	MPI_Copy_function   *copy_fn,
	MPI_Delete_function *delete_fn,
#endif
	MPI_Fint *keyval, void *extra_state, MPI_Fint *__ierr)
{
    int l_keyval = 0;
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
    *__ierr = MPIR_Keyval_create( *copy_fn, *delete_fn, &l_keyval, 
				  extra_state, 1 );
#else
    *__ierr = MPIR_Keyval_create( copy_fn, delete_fn, &l_keyval, 
                                  extra_state, 1 );
#endif
    *keyval = (MPI_Fint)l_keyval;
}
