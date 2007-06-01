/* pcontrol.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_PCONTROL = PMPI_PCONTROL
void MPI_PCONTROL ( MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_pcontrol__ = pmpi_pcontrol__
void mpi_pcontrol__ ( MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_pcontrol = pmpi_pcontrol
void mpi_pcontrol ( MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_pcontrol_ = pmpi_pcontrol_
void mpi_pcontrol_ ( MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_PCONTROL  MPI_PCONTROL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_pcontrol__  mpi_pcontrol__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_pcontrol  mpi_pcontrol
#else
#pragma _HP_SECONDARY_DEF pmpi_pcontrol_  mpi_pcontrol_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_PCONTROL as PMPI_PCONTROL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_pcontrol__ as pmpi_pcontrol__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_pcontrol as pmpi_pcontrol
#else
#pragma _CRI duplicate mpi_pcontrol_ as pmpi_pcontrol_
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
#define mpi_pcontrol_ PMPI_PCONTROL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_pcontrol_ pmpi_pcontrol__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_pcontrol_ pmpi_pcontrol
#else
#define mpi_pcontrol_ pmpi_pcontrol_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_pcontrol_ MPI_PCONTROL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_pcontrol_ mpi_pcontrol__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_pcontrol_ mpi_pcontrol
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_pcontrol_ ( MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_pcontrol_( MPI_Fint *level, MPI_Fint *__ierr )
{
    *__ierr = MPI_Pcontrol((int)*level);
}
