/* comm_rgroup.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_REMOTE_GROUP = PMPI_COMM_REMOTE_GROUP
void MPI_COMM_REMOTE_GROUP ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_remote_group__ = pmpi_comm_remote_group__
void mpi_comm_remote_group__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_remote_group = pmpi_comm_remote_group
void mpi_comm_remote_group ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_comm_remote_group_ = pmpi_comm_remote_group_
void mpi_comm_remote_group_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_REMOTE_GROUP  MPI_COMM_REMOTE_GROUP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_remote_group__  mpi_comm_remote_group__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_remote_group  mpi_comm_remote_group
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_remote_group_  mpi_comm_remote_group_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_REMOTE_GROUP as PMPI_COMM_REMOTE_GROUP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_remote_group__ as pmpi_comm_remote_group__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_remote_group as pmpi_comm_remote_group
#else
#pragma _CRI duplicate mpi_comm_remote_group_ as pmpi_comm_remote_group_
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
#define mpi_comm_remote_group_ PMPI_COMM_REMOTE_GROUP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_remote_group_ pmpi_comm_remote_group__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_remote_group_ pmpi_comm_remote_group
#else
#define mpi_comm_remote_group_ pmpi_comm_remote_group_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_remote_group_ MPI_COMM_REMOTE_GROUP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_remote_group_ mpi_comm_remote_group__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_remote_group_ mpi_comm_remote_group
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_comm_remote_group_ ( MPI_Fint *, MPI_Fint *, 
                                        MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_comm_remote_group_ ( MPI_Fint *comm, MPI_Fint *group, MPI_Fint *__ierr )
{
    MPI_Group l_group;

    *__ierr = MPI_Comm_remote_group( MPI_Comm_f2c(*comm), 
                                     &l_group);
    if (*__ierr == MPI_SUCCESS) 		     
        *group = MPI_Group_c2f( l_group );
}
