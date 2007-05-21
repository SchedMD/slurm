/* type_contig.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_CONTIGUOUS = PMPI_TYPE_CONTIGUOUS
void MPI_TYPE_CONTIGUOUS ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_contiguous__ = pmpi_type_contiguous__
void mpi_type_contiguous__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_contiguous = pmpi_type_contiguous
void mpi_type_contiguous ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_contiguous_ = pmpi_type_contiguous_
void mpi_type_contiguous_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_CONTIGUOUS  MPI_TYPE_CONTIGUOUS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_contiguous__  mpi_type_contiguous__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_contiguous  mpi_type_contiguous
#else
#pragma _HP_SECONDARY_DEF pmpi_type_contiguous_  mpi_type_contiguous_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_CONTIGUOUS as PMPI_TYPE_CONTIGUOUS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_contiguous__ as pmpi_type_contiguous__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_contiguous as pmpi_type_contiguous
#else
#pragma _CRI duplicate mpi_type_contiguous_ as pmpi_type_contiguous_
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
#define mpi_type_contiguous_ PMPI_TYPE_CONTIGUOUS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_contiguous_ pmpi_type_contiguous__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_contiguous_ pmpi_type_contiguous
#else
#define mpi_type_contiguous_ pmpi_type_contiguous_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_contiguous_ MPI_TYPE_CONTIGUOUS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_contiguous_ mpi_type_contiguous__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_contiguous_ mpi_type_contiguous
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_contiguous_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *,
				      MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_contiguous_( MPI_Fint *count, MPI_Fint *old_type, MPI_Fint *newtype, MPI_Fint *__ierr )
{
    MPI_Datatype  ldatatype;

    *__ierr = MPI_Type_contiguous((int)*count, MPI_Type_f2c(*old_type),
                                  &ldatatype);
    if (*__ierr == MPI_SUCCESS) 
        *newtype = MPI_Type_c2f(ldatatype);
}
