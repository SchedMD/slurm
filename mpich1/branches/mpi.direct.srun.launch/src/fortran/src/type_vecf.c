/* type_vec.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_VECTOR = PMPI_TYPE_VECTOR
void MPI_TYPE_VECTOR ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_vector__ = pmpi_type_vector__
void mpi_type_vector__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_vector = pmpi_type_vector
void mpi_type_vector ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_vector_ = pmpi_type_vector_
void mpi_type_vector_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_VECTOR  MPI_TYPE_VECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_vector__  mpi_type_vector__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_vector  mpi_type_vector
#else
#pragma _HP_SECONDARY_DEF pmpi_type_vector_  mpi_type_vector_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_VECTOR as PMPI_TYPE_VECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_vector__ as pmpi_type_vector__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_vector as pmpi_type_vector
#else
#pragma _CRI duplicate mpi_type_vector_ as pmpi_type_vector_
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
#define mpi_type_vector_ PMPI_TYPE_VECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_vector_ pmpi_type_vector__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_vector_ pmpi_type_vector
#else
#define mpi_type_vector_ pmpi_type_vector_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_vector_ MPI_TYPE_VECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_vector_ mpi_type_vector__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_vector_ mpi_type_vector
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_vector_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                  MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_vector_( MPI_Fint *count, MPI_Fint *blocklen, MPI_Fint *stride, MPI_Fint *old_type, MPI_Fint *newtype, MPI_Fint *__ierr )
{
    MPI_Datatype l_datatype;

    *__ierr = MPI_Type_vector((int)*count, (int)*blocklen, (int)*stride,
                              MPI_Type_f2c(*old_type), 
                              &l_datatype);
    if (*__ierr == MPI_SUCCESS) 
        *newtype = MPI_Type_c2f(l_datatype);
}
