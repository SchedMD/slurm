/* waitsome.c */
/* CUSTOM Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_WAITSOME = PMPI_WAITSOME
void MPI_WAITSOME ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_waitsome__ = pmpi_waitsome__
void mpi_waitsome__ ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_waitsome = pmpi_waitsome
void mpi_waitsome ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint * );
#else
#pragma weak mpi_waitsome_ = pmpi_waitsome_
void mpi_waitsome_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_WAITSOME  MPI_WAITSOME
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_waitsome__  mpi_waitsome__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_waitsome  mpi_waitsome
#else
#pragma _HP_SECONDARY_DEF pmpi_waitsome_  mpi_waitsome_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_WAITSOME as PMPI_WAITSOME
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_waitsome__ as pmpi_waitsome__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_waitsome as pmpi_waitsome
#else
#pragma _CRI duplicate mpi_waitsome_ as pmpi_waitsome_
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
#define mpi_waitsome_ PMPI_WAITSOME
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_waitsome_ pmpi_waitsome__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_waitsome_ pmpi_waitsome
#else
#define mpi_waitsome_ pmpi_waitsome_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_waitsome_ MPI_WAITSOME
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_waitsome_ mpi_waitsome__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_waitsome_ mpi_waitsome
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_waitsome_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *, 
                               MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE],
                               MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_waitsome_( MPI_Fint *incount, MPI_Fint array_of_requests[], MPI_Fint *outcount, MPI_Fint array_of_indices[], 
    MPI_Fint array_of_statuses[][MPI_STATUS_SIZE], MPI_Fint *__ierr )
{
    int i,j,found;
    int loutcount;
    int *l_indices = 0;
    int local_l_indices[MPIR_USE_LOCAL_ARRAY];
    MPI_Request *lrequest = 0;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status * c_status = 0;
    MPI_Status local_c_status[MPIR_USE_LOCAL_ARRAY];

    if ((int)*incount > 0) {
	if ((int)*incount > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(lrequest,(MPI_Request*)MALLOC(sizeof(MPI_Request)* (int)*incount),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITSOME" );

	    MPIR_FALLOC(l_indices,(int*)MALLOC(sizeof(int)* (int)*incount),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITSOME" );

	    MPIR_FALLOC(c_status,(MPI_Status*)MALLOC(sizeof(MPI_Status)* (int)*incount),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITSOME" );
	}
	else {
	    lrequest = local_lrequest;
	    l_indices = local_l_indices;
	    c_status = local_c_status;
	}

	for (i=0; i<(int)*incount; i++) 
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	
	*__ierr = MPI_Waitsome((int)*incount,lrequest,&loutcount,l_indices,
			       c_status);

/* By checking for lrequest[l_indices[i]] = 0, 
   we handle persistant requests */
        for (i=0; i<(int)*incount; i++) {
	    if ( i < loutcount) {
                if (l_indices[i] >= 0) {
		        array_of_requests[l_indices[i]] = 
			      MPI_Request_c2f( lrequest[l_indices[i]] );
		}
	    }
	    else {
		found = 0;
		j = 0;
		while ( (!found) && (j<loutcount) ) {
		    if (l_indices[j++] == i)
			found = 1;
		}
		if (!found)
	            array_of_requests[i] = MPI_Request_c2f( lrequest[i] );
	    }
	}
    }
    else 
	*__ierr = MPI_Waitsome( (int)*incount, (MPI_Request *)0, &loutcount,
			       l_indices, c_status );

    if (*__ierr != MPI_SUCCESS) return;
    for (i=0; i<loutcount; i++) {
	MPI_Status_c2f( &c_status[i], &(array_of_statuses[i][0]) );
	if (l_indices[i] >= 0)
	    array_of_indices[i] = l_indices[i] + 1;

    }
    *outcount = (MPI_Fint)loutcount;
    if ((int)*incount > MPIR_USE_LOCAL_ARRAY) {
        FREE( l_indices );
        FREE( lrequest );
        FREE( c_status );
    }
}
