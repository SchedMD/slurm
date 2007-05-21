/* waitany.c */
/* CUSTOM Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_WAITANY = PMPI_WAITANY
void MPI_WAITANY ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_waitany__ = pmpi_waitany__
void mpi_waitany__ ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_waitany = pmpi_waitany
void mpi_waitany ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_waitany_ = pmpi_waitany_
void mpi_waitany_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_WAITANY  MPI_WAITANY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_waitany__  mpi_waitany__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_waitany  mpi_waitany
#else
#pragma _HP_SECONDARY_DEF pmpi_waitany_  mpi_waitany_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_WAITANY as PMPI_WAITANY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_waitany__ as pmpi_waitany__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_waitany as pmpi_waitany
#else
#pragma _CRI duplicate mpi_waitany_ as pmpi_waitany_
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
#define mpi_waitany_ PMPI_WAITANY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_waitany_ pmpi_waitany__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_waitany_ pmpi_waitany
#else
#define mpi_waitany_ pmpi_waitany_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_waitany_ MPI_WAITANY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_waitany_ mpi_waitany__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_waitany_ mpi_waitany
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_waitany_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *, 
                              MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_waitany_(MPI_Fint *count, MPI_Fint array_of_requests[], MPI_Fint *index, MPI_Fint *status, MPI_Fint *__ierr )
{

    int lindex;
    MPI_Request *lrequest;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status c_status;
    int i;

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(lrequest,(MPI_Request*)MALLOC(sizeof(MPI_Request) * (int)*count),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITANY" );
	}
	else 
	    lrequest = local_lrequest;
	
	for (i=0; i<(int)*count; i++) 
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
    }
    else
	lrequest = 0;

    *__ierr = MPI_Waitany((int)*count,lrequest,&lindex,&c_status);

    if (lindex >= 0) {
	/* lindex may be MPI_UNDEFINED if all are null */
	if (!*__ierr) {
	    array_of_requests[lindex] = MPI_Request_c2f(lrequest[lindex]);
	}
    }

   if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	FREE( lrequest );
    }

    if (*__ierr != MPI_SUCCESS) return;

    /* See the description of waitany in the standard; the Fortran index ranges
       are from 1, not zero */
    *index = (MPI_Fint)lindex;
    if ((int)*index >= 0) *index = (MPI_Fint)*index + 1;
    MPI_Status_c2f(&c_status, status);
}

