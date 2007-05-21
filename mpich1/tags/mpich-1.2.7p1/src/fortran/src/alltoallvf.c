/* alltoallv.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef _CRAY
#include <fortran.h>
#include <stdarg.h>
#endif


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ALLTOALLV = PMPI_ALLTOALLV
void MPI_ALLTOALLV ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_alltoallv__ = pmpi_alltoallv__
void mpi_alltoallv__ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_alltoallv = pmpi_alltoallv
void mpi_alltoallv ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_alltoallv_ = pmpi_alltoallv_
void mpi_alltoallv_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ALLTOALLV  MPI_ALLTOALLV
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_alltoallv__  mpi_alltoallv__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_alltoallv  mpi_alltoallv
#else
#pragma _HP_SECONDARY_DEF pmpi_alltoallv_  mpi_alltoallv_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ALLTOALLV as PMPI_ALLTOALLV
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_alltoallv__ as pmpi_alltoallv__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_alltoallv as pmpi_alltoallv
#else
#pragma _CRI duplicate mpi_alltoallv_ as pmpi_alltoallv_
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
#define mpi_alltoallv_ PMPI_ALLTOALLV
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_alltoallv_ pmpi_alltoallv__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_alltoallv_ pmpi_alltoallv
#else
#define mpi_alltoallv_ pmpi_alltoallv_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_alltoallv_ MPI_ALLTOALLV
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_alltoallv_ mpi_alltoallv__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_alltoallv_ mpi_alltoallv
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 10

 void mpi_alltoallv_ ( void *unknown, ...)
{
void             *sendbuf;
int              *sendcnts;
int              *sdispls;
MPI_Datatype     *sendtype;
void             *recvbuf;
int              *recvcnts;
int              *rdispls; 
MPI_Datatype     *recvtype;
MPI_Comm         * comm;
int *__ierr;
int             buflen;
va_list         ap;

va_start(ap, unknown);
sendbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* Note that we can't set __ierr because we don't know where it is! */
    (void) MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR, "MPI_ALLTOALLV" );
    return;
}
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
sendcnts =     	va_arg(ap, int *);
sdispls = 	va_arg(ap, int *);
sendtype =     	va_arg(ap, MPI_Datatype *);
recvbuf =      	va_arg(ap, void *);
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
recvcnts =     	va_arg(ap, int *);
rdispls =	va_arg(ap, int *);
recvtype =      va_arg(ap, MPI_Datatype *);
comm =          va_arg(ap, MPI_Comm *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Alltoallv(MPIR_F_PTR(sendbuf),sendcnts,sdispls,*sendtype,
        MPIR_F_PTR(recvbuf),recvcnts,rdispls,*recvtype,*comm );
}

#else

 void mpi_alltoallv_ ( sendbuf, sendcnts, sdispls, sendtype, 
                    recvbuf, recvcnts, rdispls, recvtype, comm, __ierr )
void             *sendbuf;
int              *sendcnts;
int              *sdispls;
MPI_Datatype     *sendtype;
void             *recvbuf;
int              *recvcnts;
int              *rdispls; 
MPI_Datatype     *recvtype;
MPI_Comm         *comm;
int *__ierr;
{
_fcd            temp;
if (_isfcd(sendbuf)) {
        temp = _fcdtocp(sendbuf);
        sendbuf = (void *)temp;
}
if (_isfcd(recvbuf)) {
        temp = _fcdtocp(recvbuf);
        recvbuf = (void *)temp;
}

*__ierr = MPI_Alltoallv(MPIR_F_PTR(sendbuf),sendcnts,sdispls,*sendtype,
        MPIR_F_PTR(recvbuf),recvcnts,rdispls,*recvtype,*comm );
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_alltoallv_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
				void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
				MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_alltoallv_ ( void *sendbuf, MPI_Fint *sendcnts, MPI_Fint *sdispls, MPI_Fint *sendtype, 
                    void *recvbuf, MPI_Fint *recvcnts, MPI_Fint *rdispls, MPI_Fint *recvtype, MPI_Fint *comm, MPI_Fint *__ierr )
{
    
    if (sizeof(MPI_Fint) == sizeof(int))
	*__ierr = MPI_Alltoallv(MPIR_F_PTR(sendbuf), sendcnts, 
                                sdispls, MPI_Type_f2c(*sendtype),
			        MPIR_F_PTR(recvbuf), recvcnts, 
                                rdispls, MPI_Type_f2c(*recvtype),
			        MPI_Comm_f2c(*comm) );
    else {

        int *l_sendcnts;
        int *l_sdispls;
        int *l_recvcnts;
        int *l_rdispls;
	int size;
	int i;

	MPI_Comm_size(MPI_Comm_f2c(*comm), &size);
 
	MPIR_FALLOC(l_sendcnts,(int*)MALLOC(sizeof(int)* size),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Alltoallv");
	MPIR_FALLOC(l_sdispls,(int*)MALLOC(sizeof(int)* size),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Alltoallv");
	MPIR_FALLOC(l_recvcnts,(int*)MALLOC(sizeof(int)* size),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Alltoallv");
	MPIR_FALLOC(l_rdispls,(int*)MALLOC(sizeof(int)* size),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Alltoallv");

	for (i=0; i<size; i++) {
	    l_sendcnts[i] = (int)sendcnts[i];
	    l_sdispls[i] = (int)sdispls[i];
	    l_recvcnts[i] = (int)recvcnts[i];
	    l_rdispls[i] = (int)rdispls[i];
	}    

	*__ierr = MPI_Alltoallv(MPIR_F_PTR(sendbuf), l_sendcnts, 
                                l_sdispls, MPI_Type_f2c(*sendtype),
			        MPIR_F_PTR(recvbuf), l_recvcnts, 
                                l_rdispls, MPI_Type_f2c(*recvtype),
			        MPI_Comm_f2c(*comm) );
	FREE( l_sendcnts);
	FREE( l_sdispls );
	FREE( l_recvcnts);
	FREE( l_rdispls );
    }

}
#endif
