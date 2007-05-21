/* type_extent.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_EXTENT = PMPI_TYPE_EXTENT
void MPI_TYPE_EXTENT ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_extent__ = pmpi_type_extent__
void mpi_type_extent__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_extent = pmpi_type_extent
void mpi_type_extent ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_extent_ = pmpi_type_extent_
void mpi_type_extent_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_EXTENT  MPI_TYPE_EXTENT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_extent__  mpi_type_extent__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_extent  mpi_type_extent
#else
#pragma _HP_SECONDARY_DEF pmpi_type_extent_  mpi_type_extent_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_EXTENT as PMPI_TYPE_EXTENT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_extent__ as pmpi_type_extent__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_extent as pmpi_type_extent
#else
#pragma _CRI duplicate mpi_type_extent_ as pmpi_type_extent_
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
#define mpi_type_extent_ PMPI_TYPE_EXTENT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_extent_ pmpi_type_extent__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_extent_ pmpi_type_extent
#else
#define mpi_type_extent_ pmpi_type_extent_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_extent_ MPI_TYPE_EXTENT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_extent_ mpi_type_extent__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_extent_ mpi_type_extent
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_extent_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_extent_( MPI_Fint *datatype, MPI_Fint *extent, MPI_Fint *__ierr )
{
    MPI_Aint c_extent;
    *__ierr = MPI_Type_extent(MPI_Type_f2c(*datatype), &c_extent);
    /* Really should check for truncation, ala mpi_address_ */
    *extent = (MPI_Fint)c_extent;
}
