/* type_commit.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_COMMIT = PMPI_TYPE_COMMIT
void MPI_TYPE_COMMIT ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_commit__ = pmpi_type_commit__
void mpi_type_commit__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_commit = pmpi_type_commit
void mpi_type_commit ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_commit_ = pmpi_type_commit_
void mpi_type_commit_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_COMMIT  MPI_TYPE_COMMIT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_commit__  mpi_type_commit__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_commit  mpi_type_commit
#else
#pragma _HP_SECONDARY_DEF pmpi_type_commit_  mpi_type_commit_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_COMMIT as PMPI_TYPE_COMMIT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_commit__ as pmpi_type_commit__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_commit as pmpi_type_commit
#else
#pragma _CRI duplicate mpi_type_commit_ as pmpi_type_commit_
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
#define mpi_type_commit_ PMPI_TYPE_COMMIT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_commit_ pmpi_type_commit__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_commit_ pmpi_type_commit
#else
#define mpi_type_commit_ pmpi_type_commit_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_commit_ MPI_TYPE_COMMIT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_commit_ mpi_type_commit__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_commit_ mpi_type_commit
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_commit_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_commit_ ( MPI_Fint *datatype, MPI_Fint *__ierr )
{
    MPI_Datatype ldatatype = MPI_Type_f2c(*datatype);
    *__ierr = MPI_Type_commit( &ldatatype );
    if (*__ierr == MPI_SUCCESS) 
        *datatype = MPI_Type_c2f(ldatatype);    
}
