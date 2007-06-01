/* comm_create.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_CREATE = PMPI_COMM_CREATE
void MPI_COMM_CREATE ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_create__ = pmpi_comm_create__
void mpi_comm_create__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_create = pmpi_comm_create
void mpi_comm_create ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_comm_create_ = pmpi_comm_create_
void mpi_comm_create_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_CREATE  MPI_COMM_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_create__  mpi_comm_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_create  mpi_comm_create
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_create_  mpi_comm_create_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_CREATE as PMPI_COMM_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_create__ as pmpi_comm_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_create as pmpi_comm_create
#else
#pragma _CRI duplicate mpi_comm_create_ as pmpi_comm_create_
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
#define mpi_comm_create_ PMPI_COMM_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_create_ pmpi_comm_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_create_ pmpi_comm_create
#else
#define mpi_comm_create_ pmpi_comm_create_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_create_ MPI_COMM_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_create_ mpi_comm_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_create_ mpi_comm_create
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_comm_create_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
				  MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_comm_create_ ( MPI_Fint *comm, MPI_Fint *group, MPI_Fint *comm_out, MPI_Fint *__ierr )
{
    MPI_Comm l_comm_out;

    *__ierr = MPI_Comm_create( MPI_Comm_f2c(*comm), MPI_Group_f2c(*group),
                               &l_comm_out);
    if (*__ierr == MPI_SUCCESS) 		     
        *comm_out = MPI_Comm_c2f(l_comm_out);
}
