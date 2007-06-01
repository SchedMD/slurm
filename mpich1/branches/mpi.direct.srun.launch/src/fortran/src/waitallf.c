/* waitall.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_WAITALL = PMPI_WAITALL
void MPI_WAITALL ( MPI_Fint *, MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_waitall__ = pmpi_waitall__
void mpi_waitall__ ( MPI_Fint *, MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_waitall = pmpi_waitall
void mpi_waitall ( MPI_Fint *, MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint *);
#else
#pragma weak mpi_waitall_ = pmpi_waitall_
void mpi_waitall_ ( MPI_Fint *, MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_WAITALL  MPI_WAITALL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_waitall__  mpi_waitall__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_waitall  mpi_waitall
#else
#pragma _HP_SECONDARY_DEF pmpi_waitall_  mpi_waitall_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_WAITALL as PMPI_WAITALL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_waitall__ as pmpi_waitall__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_waitall as pmpi_waitall
#else
#pragma _CRI duplicate mpi_waitall_ as pmpi_waitall_
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
#define mpi_waitall_ PMPI_WAITALL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_waitall_ pmpi_waitall__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_waitall_ pmpi_waitall
#else
#define mpi_waitall_ pmpi_waitall_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_waitall_ MPI_WAITALL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_waitall_ mpi_waitall__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_waitall_ mpi_waitall
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_waitall_ ( MPI_Fint *, MPI_Fint [],
			      MPI_Fint [][MPI_STATUS_SIZE], 
			      MPI_Fint *);

FORTRAN_API void FORT_CALL mpi_waitall_(MPI_Fint *count, MPI_Fint array_of_requests[], MPI_Fint array_of_statuses[][MPI_STATUS_SIZE], MPI_Fint *__ierr )
{
    int i;
    MPI_Request *lrequest = 0;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status *c_status = 0;
    MPI_Status local_c_status[MPIR_USE_LOCAL_ARRAY];

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(lrequest,(MPI_Request*)MALLOC(sizeof(MPI_Request) * 
                        (int)*count), MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITALL" );

	    MPIR_FALLOC(c_status,(MPI_Status*)MALLOC(sizeof(MPI_Status) * 
                        (int)*count), MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITALL" );
	}
	else {
	    lrequest = local_lrequest;
	    c_status = local_c_status;
	}

	for (i=0; i<(int)*count; i++) {
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	}

	*__ierr = MPI_Waitall((int)*count,lrequest,c_status);
	/* By checking for lrequest[i] = 0, we handle persistant requests */
	for (i=0; i<(int)*count; i++) {
	        array_of_requests[i] = MPI_Request_c2f( lrequest[i] );
	}
    }
    else 
	*__ierr = MPI_Waitall((int)*count,(MPI_Request *)0, c_status );

    if (*__ierr == MPI_SUCCESS) 
        for (i=0; i<(int)*count; i++) 
            MPI_Status_c2f(&(c_status[i]), &(array_of_statuses[i][0]) );
    
    if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
        FREE( lrequest );
        FREE( c_status );
    }
}






