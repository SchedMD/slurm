/* probe.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_PROBE = PMPI_PROBE
void MPI_PROBE ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_probe__ = pmpi_probe__
void mpi_probe__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_probe = pmpi_probe
void mpi_probe ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_probe_ = pmpi_probe_
void mpi_probe_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_PROBE  MPI_PROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_probe__  mpi_probe__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_probe  mpi_probe
#else
#pragma _HP_SECONDARY_DEF pmpi_probe_  mpi_probe_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_PROBE as PMPI_PROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_probe__ as pmpi_probe__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_probe as pmpi_probe
#else
#pragma _CRI duplicate mpi_probe_ as pmpi_probe_
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
#define mpi_probe_ PMPI_PROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_probe_ pmpi_probe__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_probe_ pmpi_probe
#else
#define mpi_probe_ pmpi_probe_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_probe_ MPI_PROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_probe_ mpi_probe__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_probe_ mpi_probe
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_probe_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                            MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_probe_( MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm, MPI_Fint *status, MPI_Fint *__ierr )
{
    MPI_Status c_status;
   
    *__ierr = MPI_Probe((int)*source, (int)*tag, MPI_Comm_f2c(*comm),
                        &c_status);
    if (*__ierr == MPI_SUCCESS) 
        MPI_Status_c2f(&c_status, status);
    
}
