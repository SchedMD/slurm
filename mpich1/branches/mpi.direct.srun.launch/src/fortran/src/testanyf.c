/* testany.c */
/* CUSTOM Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TESTANY = PMPI_TESTANY
void MPI_TESTANY ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_testany__ = pmpi_testany__
void mpi_testany__ ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_testany = pmpi_testany
void mpi_testany ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_testany_ = pmpi_testany_
void mpi_testany_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TESTANY  MPI_TESTANY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_testany__  mpi_testany__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_testany  mpi_testany
#else
#pragma _HP_SECONDARY_DEF pmpi_testany_  mpi_testany_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TESTANY as PMPI_TESTANY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_testany__ as pmpi_testany__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_testany as pmpi_testany
#else
#pragma _CRI duplicate mpi_testany_ as pmpi_testany_
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
#define mpi_testany_ PMPI_TESTANY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_testany_ pmpi_testany__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_testany_ pmpi_testany
#else
#define mpi_testany_ pmpi_testany_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_testany_ MPI_TESTANY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_testany_ mpi_testany__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_testany_ mpi_testany
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_testany_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *, 
			      MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_testany_( MPI_Fint *count, MPI_Fint array_of_requests[], MPI_Fint *index, MPI_Fint *flag, MPI_Fint *status, MPI_Fint *__ierr )
{
    int lindex;
    int lflag;
    MPI_Request *lrequest;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status c_status;
    int i;

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(lrequest,(MPI_Request*)MALLOC(sizeof(MPI_Request)* (int)*count),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		        "MPI_TESTANY");
	}
	else 
	    lrequest = local_lrequest;
	
	for (i=0; i<(int)*count; i++) 
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	
    }
    else
	lrequest = 0;

    *__ierr = MPI_Testany((int)*count,lrequest,&lindex,&lflag,&c_status);
    if (*__ierr != MPI_SUCCESS) return;
    if (lindex != MPI_UNDEFINED) {
        if (lflag && !*__ierr) {
	    array_of_requests[lindex] = MPI_Request_c2f(lrequest[lindex]);
        }
     }
    if ((int)*count > MPIR_USE_LOCAL_ARRAY) 
	FREE( lrequest );
    
    *flag = MPIR_TO_FLOG(lflag);
    /* See the description of waitany in the standard; the Fortran index ranges
       are from 1, not zero */
    *index = (MPI_Fint)lindex;
    if ((int)*index >= 0) *index = *index + 1;
    MPI_Status_c2f(&c_status, status);
}



