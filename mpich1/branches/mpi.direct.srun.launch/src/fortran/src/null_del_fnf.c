/* null_del_fn.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#undef MPI_NULL_DELETE_FN

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_NULL_DELETE_FN = PMPI_NULL_DELETE_FN
void MPI_NULL_DELETE_FN ( MPI_Fint *, MPI_Fint *, void *, void *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_null_delete_fn__ = pmpi_null_delete_fn__
void mpi_null_delete_fn__ ( MPI_Fint *, MPI_Fint *, void *, void *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_null_delete_fn = pmpi_null_delete_fn
void mpi_null_delete_fn ( MPI_Fint *, MPI_Fint *, void *, void *, MPI_Fint * );
#else
#pragma weak mpi_null_delete_fn_ = pmpi_null_delete_fn_
void mpi_null_delete_fn_ ( MPI_Fint *, MPI_Fint *, void *, void *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_NULL_DELETE_FN  MPI_NULL_DELETE_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_null_delete_fn__  mpi_null_delete_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_null_delete_fn  mpi_null_delete_fn
#else
#pragma _HP_SECONDARY_DEF pmpi_null_delete_fn_  mpi_null_delete_fn_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_NULL_DELETE_FN as PMPI_NULL_DELETE_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_null_delete_fn__ as pmpi_null_delete_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_null_delete_fn as pmpi_null_delete_fn
#else
#pragma _CRI duplicate mpi_null_delete_fn_ as pmpi_null_delete_fn_
#endif

/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#include "mpiprof.h"
#endif

#ifdef F77_NAME_UPPER
#define mpi_null_delete_fn_ PMPI_NULL_DELETE_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_null_delete_fn_ pmpi_null_delete_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_null_delete_fn_ pmpi_null_delete_fn
#else
#define mpi_null_delete_fn_ pmpi_null_delete_fn_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_null_delete_fn_ MPI_NULL_DELETE_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_null_delete_fn_ mpi_null_delete_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_null_delete_fn_ mpi_null_delete_fn
#endif
#endif

/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_null_delete_fn_ ( MPI_Fint *, MPI_Fint *, void *, 
					  void *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_null_delete_fn_ ( MPI_Fint *comm, MPI_Fint *keyval, 
					  void *attr, void *extra_state, 
					  MPI_Fint *ierr )
{
    /* Null function doesn't do anything */
    *ierr = MPI_SUCCESS;
}
