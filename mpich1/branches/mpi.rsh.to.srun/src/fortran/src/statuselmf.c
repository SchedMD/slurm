/* statuselm.c */
/* Fortran interface file */

#include "mpi_fortimpl.h"

/*
* This file was generated automatically by bfort from the C source
* file.  
 */

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_STATUS_SET_ELEMENTS = PMPI_STATUS_SET_ELEMENTS
void MPI_STATUS_SET_ELEMENTS (MPI_Status *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_status_set_elements__ = pmpi_status_set_elements__
void mpi_status_set_elements__ (MPI_Status *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_status_set_elements = pmpi_status_set_elements
void mpi_status_set_elements (MPI_Status *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_status_set_elements_ = pmpi_status_set_elements_
void mpi_status_set_elements_ (MPI_Status *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_STATUS_SET_ELEMENTS  MPI_STATUS_SET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_status_set_elements__  mpi_status_set_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_status_set_elements  mpi_status_set_elements
#else
#pragma _HP_SECONDARY_DEF pmpi_status_set_elements_  mpi_status_set_elements_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_STATUS_SET_ELEMENTS as PMPI_STATUS_SET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_status_set_elements__ as pmpi_status_set_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_status_set_elements as pmpi_status_set_elements
#else
#pragma _CRI duplicate mpi_status_set_elements_ as pmpi_status_set_elements_
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
#define mpi_status_set_elements_ PMPI_STATUS_SET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_status_set_elements_ pmpi_status_set_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_status_set_elements_ pmpi_status_set_elements
#else
#define mpi_status_set_elements_ pmpi_status_set_elements_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_status_set_elements_ MPI_STATUS_SET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_status_set_elements_ mpi_status_set_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_status_set_elements_ mpi_status_set_elements
#endif
#endif


FORTRAN_API void FORT_CALL mpi_status_set_elements_(MPI_Status *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

/* Definitions of Fortran Wrapper routines */
#if defined(__cplusplus)
extern "C" {
#endif
FORTRAN_API void FORT_CALL mpi_status_set_elements_(MPI_Status *status, MPI_Fint *datatype,
        MPI_Fint *count, MPI_Fint *__ierr )
{
    *__ierr = MPI_Status_set_elements(status, MPI_Type_f2c( *datatype ), 
	*count);
}
#if defined(__cplusplus)
}
#endif
