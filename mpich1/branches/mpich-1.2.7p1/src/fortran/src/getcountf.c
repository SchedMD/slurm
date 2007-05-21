/* getcount.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GET_COUNT = PMPI_GET_COUNT
void MPI_GET_COUNT ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_get_count__ = pmpi_get_count__
void mpi_get_count__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_get_count = pmpi_get_count
void mpi_get_count ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_get_count_ = pmpi_get_count_
void mpi_get_count_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GET_COUNT  MPI_GET_COUNT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_get_count__  mpi_get_count__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_get_count  mpi_get_count
#else
#pragma _HP_SECONDARY_DEF pmpi_get_count_  mpi_get_count_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GET_COUNT as PMPI_GET_COUNT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_get_count__ as pmpi_get_count__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_get_count as pmpi_get_count
#else
#pragma _CRI duplicate mpi_get_count_ as pmpi_get_count_
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
#define mpi_get_count_ PMPI_GET_COUNT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_get_count_ pmpi_get_count__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_get_count_ pmpi_get_count
#else
#define mpi_get_count_ pmpi_get_count_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_get_count_ MPI_GET_COUNT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_get_count_ mpi_get_count__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_get_count_ mpi_get_count
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_get_count_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_get_count_( MPI_Fint *status, MPI_Fint *datatype, MPI_Fint *count, MPI_Fint *__ierr )
{
    int lcount;
    MPI_Status c_status;

    MPI_Status_f2c(status, &c_status); 
    *__ierr = MPI_Get_count(&c_status, MPI_Type_f2c(*datatype), 
                            &lcount);
    *count = (MPI_Fint)lcount;

}

