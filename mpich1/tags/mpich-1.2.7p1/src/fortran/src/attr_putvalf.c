/* attr_putval.c */
/* THIS IS A CUSTOM WRAPPER */

#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ATTR_PUT = PMPI_ATTR_PUT
void MPI_ATTR_PUT ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_attr_put__ = pmpi_attr_put__
void mpi_attr_put__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_attr_put = pmpi_attr_put
void mpi_attr_put ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_attr_put_ = pmpi_attr_put_
void mpi_attr_put_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ATTR_PUT  MPI_ATTR_PUT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_attr_put__  mpi_attr_put__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_attr_put  mpi_attr_put
#else
#pragma _HP_SECONDARY_DEF pmpi_attr_put_  mpi_attr_put_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ATTR_PUT as PMPI_ATTR_PUT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_attr_put__ as pmpi_attr_put__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_attr_put as pmpi_attr_put
#else
#pragma _CRI duplicate mpi_attr_put_ as pmpi_attr_put_
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
#define mpi_attr_put_ PMPI_ATTR_PUT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_attr_put_ pmpi_attr_put__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_attr_put_ pmpi_attr_put
#else
#define mpi_attr_put_ pmpi_attr_put_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_attr_put_ MPI_ATTR_PUT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_attr_put_ mpi_attr_put__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_attr_put_ mpi_attr_put
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_attr_put_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                               MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_attr_put_ ( MPI_Fint *comm, MPI_Fint *keyval, MPI_Fint *attr_value, MPI_Fint *__ierr )
{
    *__ierr = MPI_Attr_put( MPI_Comm_f2c(*comm), (int)*keyval,
                            (void *)(MPI_Aint)((int)*attr_value));
}
