/* comm_split.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_SPLIT = PMPI_COMM_SPLIT
void MPI_COMM_SPLIT ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_split__ = pmpi_comm_split__
void mpi_comm_split__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_split = pmpi_comm_split
void mpi_comm_split ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_comm_split_ = pmpi_comm_split_
void mpi_comm_split_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_SPLIT  MPI_COMM_SPLIT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_split__  mpi_comm_split__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_split  mpi_comm_split
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_split_  mpi_comm_split_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_SPLIT as PMPI_COMM_SPLIT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_split__ as pmpi_comm_split__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_split as pmpi_comm_split
#else
#pragma _CRI duplicate mpi_comm_split_ as pmpi_comm_split_
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
#define mpi_comm_split_ PMPI_COMM_SPLIT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_split_ pmpi_comm_split__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_split_ pmpi_comm_split
#else
#define mpi_comm_split_ pmpi_comm_split_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_split_ MPI_COMM_SPLIT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_split_ mpi_comm_split__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_split_ mpi_comm_split
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_comm_split_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                 MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_comm_split_ ( MPI_Fint *comm, MPI_Fint *color, MPI_Fint *key, MPI_Fint *comm_out, MPI_Fint *__ierr )
{
    MPI_Comm l_comm_out;

    *__ierr = MPI_Comm_split( MPI_Comm_f2c(*comm), (int)*color, (int)*key, 
                              &l_comm_out);
    if (*__ierr == MPI_SUCCESS) 		     
        *comm_out = MPI_Comm_c2f(l_comm_out);
}
