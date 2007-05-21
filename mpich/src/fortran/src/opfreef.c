/* opfree.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_OP_FREE = PMPI_OP_FREE
void MPI_OP_FREE ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_op_free__ = pmpi_op_free__
void mpi_op_free__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_op_free = pmpi_op_free
void mpi_op_free ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_op_free_ = pmpi_op_free_
void mpi_op_free_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_OP_FREE  MPI_OP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_op_free__  mpi_op_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_op_free  mpi_op_free
#else
#pragma _HP_SECONDARY_DEF pmpi_op_free_  mpi_op_free_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_OP_FREE as PMPI_OP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_op_free__ as pmpi_op_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_op_free as pmpi_op_free
#else
#pragma _CRI duplicate mpi_op_free_ as pmpi_op_free_
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
#define mpi_op_free_ PMPI_OP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_op_free_ pmpi_op_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_op_free_ pmpi_op_free
#else
#define mpi_op_free_ pmpi_op_free_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_op_free_ MPI_OP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_op_free_ mpi_op_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_op_free_ mpi_op_free
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_op_free_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_op_free_( MPI_Fint *op, MPI_Fint *__ierr )
{
    MPI_Op l_op = MPI_Op_f2c(*op);
    *__ierr = MPI_Op_free(&l_op);
    if (*__ierr == MPI_SUCCESS) 
        *op = MPI_Op_c2f( l_op );
}
