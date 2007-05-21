/* attr_delval.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ATTR_DELETE = PMPI_ATTR_DELETE
void MPI_ATTR_DELETE ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_attr_delete__ = pmpi_attr_delete__
void mpi_attr_delete__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_attr_delete = pmpi_attr_delete
void mpi_attr_delete ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_attr_delete_ = pmpi_attr_delete_
void mpi_attr_delete_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ATTR_DELETE  MPI_ATTR_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_attr_delete__  mpi_attr_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_attr_delete  mpi_attr_delete
#else
#pragma _HP_SECONDARY_DEF pmpi_attr_delete_  mpi_attr_delete_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ATTR_DELETE as PMPI_ATTR_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_attr_delete__ as pmpi_attr_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_attr_delete as pmpi_attr_delete
#else
#pragma _CRI duplicate mpi_attr_delete_ as pmpi_attr_delete_
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
#define mpi_attr_delete_ PMPI_ATTR_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_attr_delete_ pmpi_attr_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_attr_delete_ pmpi_attr_delete
#else
#define mpi_attr_delete_ pmpi_attr_delete_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_attr_delete_ MPI_ATTR_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_attr_delete_ mpi_attr_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_attr_delete_ mpi_attr_delete
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_attr_delete_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_attr_delete_ ( MPI_Fint *comm, MPI_Fint *keyval, MPI_Fint *__ierr )
{
    *__ierr = MPI_Attr_delete( MPI_Comm_f2c(*comm), (int)*keyval);
}
