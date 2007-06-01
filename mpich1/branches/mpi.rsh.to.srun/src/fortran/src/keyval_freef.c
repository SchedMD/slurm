/* keyval_free.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_KEYVAL_FREE = PMPI_KEYVAL_FREE
void MPI_KEYVAL_FREE ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_keyval_free__ = pmpi_keyval_free__
void mpi_keyval_free__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_keyval_free = pmpi_keyval_free
void mpi_keyval_free ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_keyval_free_ = pmpi_keyval_free_
void mpi_keyval_free_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_KEYVAL_FREE  MPI_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_keyval_free__  mpi_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_keyval_free  mpi_keyval_free
#else
#pragma _HP_SECONDARY_DEF pmpi_keyval_free_  mpi_keyval_free_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_KEYVAL_FREE as PMPI_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_keyval_free__ as pmpi_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_keyval_free as pmpi_keyval_free
#else
#pragma _CRI duplicate mpi_keyval_free_ as pmpi_keyval_free_
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
#define mpi_keyval_free_ PMPI_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_keyval_free_ pmpi_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_keyval_free_ pmpi_keyval_free
#else
#define mpi_keyval_free_ pmpi_keyval_free_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_keyval_free_ MPI_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_keyval_free_ mpi_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_keyval_free_ mpi_keyval_free
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_keyval_free_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_keyval_free_ ( MPI_Fint *keyval, MPI_Fint *__ierr )
{
    int l_keyval = (int)*keyval;
    *__ierr = MPI_Keyval_free(&l_keyval);
    *keyval = (MPI_Fint)l_keyval;
}
