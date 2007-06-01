/* finalized.c */
/* Custom Fortran interface file */

#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_FINALIZED = PMPI_FINALIZED
void MPI_FINALIZED ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_finalized__ = pmpi_finalized__
void mpi_finalized__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_finalized = pmpi_finalized
void mpi_finalized ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_finalized_ = pmpi_finalized_
void mpi_finalized_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_FINALIZED  MPI_FINALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_finalized__  mpi_finalized__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_finalized  mpi_finalized
#else
#pragma _HP_SECONDARY_DEF pmpi_finalized_  mpi_finalized_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_FINALIZED as PMPI_FINALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_finalized__ as pmpi_finalized__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_finalized as pmpi_finalized
#else
#pragma _CRI duplicate mpi_finalized_ as pmpi_finalized_
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
#define mpi_finalized_ PMPI_FINALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_finalized_ pmpi_finalized__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_finalized_ pmpi_finalized
#else
#define mpi_finalized_ pmpi_finalized_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_finalized_ MPI_FINALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_finalized_ mpi_finalized__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_finalized_ mpi_finalized
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_finalized_ ( MPI_Fint *, MPI_Fint * );

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_finalized_( MPI_Fint *flag, MPI_Fint *__ierr )
{
    int lflag;
    *__ierr = MPI_Finalized(&lflag);
    *flag = MPIR_TO_FLOG(lflag);
}
