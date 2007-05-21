/* type_hvec.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_HVECTOR = PMPI_TYPE_HVECTOR
void MPI_TYPE_HVECTOR ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_hvector__ = pmpi_type_hvector__
void mpi_type_hvector__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_hvector = pmpi_type_hvector
void mpi_type_hvector ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_hvector_ = pmpi_type_hvector_
void mpi_type_hvector_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_HVECTOR  MPI_TYPE_HVECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_hvector__  mpi_type_hvector__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_hvector  mpi_type_hvector
#else
#pragma _HP_SECONDARY_DEF pmpi_type_hvector_  mpi_type_hvector_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_HVECTOR as PMPI_TYPE_HVECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_hvector__ as pmpi_type_hvector__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_hvector as pmpi_type_hvector
#else
#pragma _CRI duplicate mpi_type_hvector_ as pmpi_type_hvector_
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
#define mpi_type_hvector_ PMPI_TYPE_HVECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_hvector_ pmpi_type_hvector__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_hvector_ pmpi_type_hvector
#else
#define mpi_type_hvector_ pmpi_type_hvector_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_hvector_ MPI_TYPE_HVECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_hvector_ mpi_type_hvector__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_hvector_ mpi_type_hvector
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_hvector_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                   MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_hvector_( MPI_Fint *count, MPI_Fint *blocklen, MPI_Fint *stride, MPI_Fint *old_type, MPI_Fint *newtype, MPI_Fint *__ierr )
{
    MPI_Aint     c_stride = (MPI_Aint)*stride;
    MPI_Datatype ldatatype;

    *__ierr = MPI_Type_hvector((int)*count, (int)*blocklen, c_stride,
                               MPI_Type_f2c(*old_type),
                               &ldatatype);
    if (*__ierr == MPI_SUCCESS) 
        *newtype = MPI_Type_c2f(ldatatype);
}
