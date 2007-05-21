/* type_size.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_SIZE = PMPI_TYPE_SIZE
void MPI_TYPE_SIZE ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_size__ = pmpi_type_size__
void mpi_type_size__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_size = pmpi_type_size
void mpi_type_size ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_size_ = pmpi_type_size_
void mpi_type_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_SIZE  MPI_TYPE_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_size__  mpi_type_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_size  mpi_type_size
#else
#pragma _HP_SECONDARY_DEF pmpi_type_size_  mpi_type_size_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_SIZE as PMPI_TYPE_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_size__ as pmpi_type_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_size as pmpi_type_size
#else
#pragma _CRI duplicate mpi_type_size_ as pmpi_type_size_
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
#define mpi_type_size_ PMPI_TYPE_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_size_ pmpi_type_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_size_ pmpi_type_size
#else
#define mpi_type_size_ pmpi_type_size_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_size_ MPI_TYPE_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_size_ mpi_type_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_size_ mpi_type_size
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_size_ ( MPI_Fint *datatype, MPI_Fint *size, MPI_Fint *__ierr )
{
    /* MPI_Aint c_size;*/
    int c_size;
    *__ierr = MPI_Type_size(MPI_Type_f2c(*datatype), &c_size);
    /* Should check for truncation */
    *size = (MPI_Fint)c_size;
}
