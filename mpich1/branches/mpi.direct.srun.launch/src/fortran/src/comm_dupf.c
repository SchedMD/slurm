/* comm_dup.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_DUP = PMPI_COMM_DUP
void MPI_COMM_DUP ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_dup__ = pmpi_comm_dup__
void mpi_comm_dup__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_dup = pmpi_comm_dup
void mpi_comm_dup ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_comm_dup_ = pmpi_comm_dup_
void mpi_comm_dup_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_DUP  MPI_COMM_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_dup__  mpi_comm_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_dup  mpi_comm_dup
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_dup_  mpi_comm_dup_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_DUP as PMPI_COMM_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_dup__ as pmpi_comm_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_dup as pmpi_comm_dup
#else
#pragma _CRI duplicate mpi_comm_dup_ as pmpi_comm_dup_
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
#define mpi_comm_dup_ PMPI_COMM_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_dup_ pmpi_comm_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_dup_ pmpi_comm_dup
#else
#define mpi_comm_dup_ pmpi_comm_dup_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_dup_ MPI_COMM_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_dup_ mpi_comm_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_dup_ mpi_comm_dup
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_comm_dup_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_comm_dup_ ( MPI_Fint *comm, MPI_Fint *comm_out, MPI_Fint *__ierr )
{
    MPI_Comm l_comm_out;

    *__ierr = MPI_Comm_dup( MPI_Comm_f2c(*comm), &l_comm_out );
    if (*__ierr == MPI_SUCCESS) 		     
        *comm_out = MPI_Comm_c2f(l_comm_out);
}
