/* startall.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_STARTALL = PMPI_STARTALL
void MPI_STARTALL ( MPI_Fint *, MPI_Fint [], MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_startall__ = pmpi_startall__
void mpi_startall__ ( MPI_Fint *, MPI_Fint [], MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_startall = pmpi_startall
void mpi_startall ( MPI_Fint *, MPI_Fint [], MPI_Fint * );
#else
#pragma weak mpi_startall_ = pmpi_startall_
void mpi_startall_ ( MPI_Fint *, MPI_Fint [], MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_STARTALL  MPI_STARTALL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_startall__  mpi_startall__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_startall  mpi_startall
#else
#pragma _HP_SECONDARY_DEF pmpi_startall_  mpi_startall_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_STARTALL as PMPI_STARTALL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_startall__ as pmpi_startall__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_startall as pmpi_startall
#else
#pragma _CRI duplicate mpi_startall_ as pmpi_startall_
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
#define mpi_startall_ PMPI_STARTALL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_startall_ pmpi_startall__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_startall_ pmpi_startall
#else
#define mpi_startall_ pmpi_startall_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_startall_ MPI_STARTALL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_startall_ mpi_startall__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_startall_ mpi_startall
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_startall_ ( MPI_Fint *, MPI_Fint [], MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_startall_( MPI_Fint *count, MPI_Fint array_of_requests[], MPI_Fint *__ierr )
{ 
   MPI_Request *lrequest = 0;
   MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
   int i;

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(lrequest,(MPI_Request*)MALLOC(sizeof(MPI_Request) * (int)*count),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_STARTALL" );
	}
	else {
	    lrequest = local_lrequest;
	}
	for (i=0; i<(int)*count; i++) {
            lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	}
	*__ierr = MPI_Startall((int)*count,lrequest);
    }
    else 
	*__ierr = MPI_Startall((int)*count,(MPI_Request *)0);

    if (*__ierr != MPI_SUCCESS) return;
    for (i=0; i<(int)*count; i++) {
        array_of_requests[i] = MPI_Request_c2f( lrequest[i]);
    }
    if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	FREE( lrequest );
    }
}


