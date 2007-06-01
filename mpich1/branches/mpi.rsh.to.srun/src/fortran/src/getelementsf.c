/* get_elements.c */
/* Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GET_ELEMENTS = PMPI_GET_ELEMENTS
void MPI_GET_ELEMENTS ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_get_elements__ = pmpi_get_elements__
void mpi_get_elements__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_get_elements = pmpi_get_elements
void mpi_get_elements ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_get_elements_ = pmpi_get_elements_
void mpi_get_elements_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GET_ELEMENTS  MPI_GET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_get_elements__  mpi_get_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_get_elements  mpi_get_elements
#else
#pragma _HP_SECONDARY_DEF pmpi_get_elements_  mpi_get_elements_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GET_ELEMENTS as PMPI_GET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_get_elements__ as pmpi_get_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_get_elements as pmpi_get_elements
#else
#pragma _CRI duplicate mpi_get_elements_ as pmpi_get_elements_
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
#define mpi_get_elements_ PMPI_GET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_get_elements_ pmpi_get_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_get_elements_ pmpi_get_elements
#else
#define mpi_get_elements_ pmpi_get_elements_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_get_elements_ MPI_GET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_get_elements_ mpi_get_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_get_elements_ mpi_get_elements
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_get_elements_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                   MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_get_elements_ ( MPI_Fint *status, MPI_Fint *datatype, MPI_Fint *elements, MPI_Fint *__ierr )
{
    int lelements;
    MPI_Status c_status;

    MPI_Status_f2c(status, &c_status);
    *__ierr = MPI_Get_elements(&c_status,MPI_Type_f2c(*datatype),
                               &lelements);
    *elements = (MPI_Fint)lelements;
}
