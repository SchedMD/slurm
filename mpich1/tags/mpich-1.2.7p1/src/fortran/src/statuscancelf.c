/* statuscancel.c */
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
#pragma weak MPI_STATUS_SET_CANCELLED = PMPI_STATUS_SET_CANCELLED
void MPI_STATUS_SET_CANCELLED (MPI_Status *, int *, int * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_status_set_cancelled__ = pmpi_status_set_cancelled__
void mpi_status_set_cancelled__ (MPI_Status *, int *, int * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_status_set_cancelled = pmpi_status_set_cancelled
void mpi_status_set_cancelled (MPI_Status *, int *, int * );
#else
#pragma weak mpi_status_set_cancelled_ = pmpi_status_set_cancelled_
void mpi_status_set_cancelled_ (MPI_Status *, int *, int * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_STATUS_SET_CANCELLED  MPI_STATUS_SET_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_status_set_cancelled__  mpi_status_set_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_status_set_cancelled  mpi_status_set_cancelled
#else
#pragma _HP_SECONDARY_DEF pmpi_status_set_cancelled_  mpi_status_set_cancelled_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_STATUS_SET_CANCELLED as PMPI_STATUS_SET_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_status_set_cancelled__ as pmpi_status_set_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_status_set_cancelled as pmpi_status_set_cancelled
#else
#pragma _CRI duplicate mpi_status_set_cancelled_ as pmpi_status_set_cancelled_
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
#define mpi_status_set_cancelled_ PMPI_STATUS_SET_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_status_set_cancelled_ pmpi_status_set_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_status_set_cancelled_ pmpi_status_set_cancelled
#else
#define mpi_status_set_cancelled_ pmpi_status_set_cancelled_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_status_set_cancelled_ MPI_STATUS_SET_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_status_set_cancelled_ mpi_status_set_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_status_set_cancelled_ mpi_status_set_cancelled
#endif
#endif



FORTRAN_API void FORT_CALL mpi_status_set_cancelled_(MPI_Status *, int *, int * );

/* Definitions of Fortran Wrapper routines */
#if defined(__cplusplus)
extern "C" {
#endif
FORTRAN_API void FORT_CALL mpi_status_set_cancelled_(MPI_Status *status, int *flag, int *__ierr )
{
	*__ierr = MPI_Status_set_cancelled(status,*flag);
}
#if defined(__cplusplus)
}
#endif
