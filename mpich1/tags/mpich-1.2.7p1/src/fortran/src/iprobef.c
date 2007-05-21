/* iprobe.c */
/* Custom Fortran interface file  */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_IPROBE = PMPI_IPROBE
void MPI_IPROBE ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_iprobe__ = pmpi_iprobe__
void mpi_iprobe__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_iprobe = pmpi_iprobe
void mpi_iprobe ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_iprobe_ = pmpi_iprobe_
void mpi_iprobe_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_IPROBE  MPI_IPROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_iprobe__  mpi_iprobe__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_iprobe  mpi_iprobe
#else
#pragma _HP_SECONDARY_DEF pmpi_iprobe_  mpi_iprobe_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_IPROBE as PMPI_IPROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_iprobe__ as pmpi_iprobe__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_iprobe as pmpi_iprobe
#else
#pragma _CRI duplicate mpi_iprobe_ as pmpi_iprobe_
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
#define mpi_iprobe_ PMPI_IPROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_iprobe_ pmpi_iprobe__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_iprobe_ pmpi_iprobe
#else
#define mpi_iprobe_ pmpi_iprobe_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_iprobe_ MPI_IPROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_iprobe_ mpi_iprobe__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_iprobe_ mpi_iprobe
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_iprobe_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                             MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_iprobe_( MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm, MPI_Fint *flag, MPI_Fint *status, MPI_Fint *__ierr )
{
    int lflag;
    MPI_Status c_status;

    *__ierr = MPI_Iprobe((int)*source,(int)*tag,MPI_Comm_f2c(*comm),
                         &lflag,&c_status);
    if (*__ierr == MPI_SUCCESS) {
        *flag = MPIR_TO_FLOG(lflag);
        MPI_Status_c2f(&c_status, status);
    }
}
