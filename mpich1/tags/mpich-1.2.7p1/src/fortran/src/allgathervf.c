/* allgatherv.c */
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
#pragma weak MPI_ALLGATHERV = PMPI_ALLGATHERV
void MPI_ALLGATHERV ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_allgatherv__ = pmpi_allgatherv__
void mpi_allgatherv__ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_allgatherv = pmpi_allgatherv
void mpi_allgatherv ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_allgatherv_ = pmpi_allgatherv_
void mpi_allgatherv_ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ALLGATHERV  MPI_ALLGATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_allgatherv__  mpi_allgatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_allgatherv  mpi_allgatherv
#else
#pragma _HP_SECONDARY_DEF pmpi_allgatherv_  mpi_allgatherv_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ALLGATHERV as PMPI_ALLGATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_allgatherv__ as pmpi_allgatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_allgatherv as pmpi_allgatherv
#else
#pragma _CRI duplicate mpi_allgatherv_ as pmpi_allgatherv_
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
#define mpi_allgatherv_ PMPI_ALLGATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_allgatherv_ pmpi_allgatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_allgatherv_ pmpi_allgatherv
#else
#define mpi_allgatherv_ pmpi_allgatherv_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_allgatherv_ MPI_ALLGATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_allgatherv_ mpi_allgatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_allgatherv_ mpi_allgatherv
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 9

 void mpi_allgatherv_ ( void *unknown, ...)
{
void             *sendbuf;
int		*sendcount;
MPI_Datatype     *sendtype;
void             *recvbuf;
int              *recvcounts;
int              *displs;
MPI_Datatype     *recvtype;
MPI_Comm         *comm;
int 		*__ierr;
int             buflen;
va_list         ap;

va_start(ap, unknown);
sendbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* Note that we can't set __ierr because we don't know where it is! */
    (void) MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR, "MPI_ALLGATHERV" );
    return;
}
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
sendcount =     va_arg (ap, int *);
sendtype =      va_arg(ap, MPI_Datatype *);
recvbuf =       va_arg(ap, void *);
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
recvcounts =    va_arg (ap, int *);
displs =	va_arg(ap, int *);
recvtype =      va_arg(ap, MPI_Datatype *);
comm =          va_arg(ap, MPI_Comm *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Allgatherv(MPIR_F_PTR(sendbuf),*sendcount,*sendtype,
        MPIR_F_PTR(recvbuf),recvcounts,displs,*recvtype,*comm);
}

#else

 void mpi_allgatherv_ ( sendbuf, sendcount,  sendtype, 
                     recvbuf, recvcounts, displs,   recvtype, comm, __ierr )
void             *sendbuf;
int*sendcount;
MPI_Datatype     *sendtype;
void             *recvbuf;
int              *recvcounts;
int              *displs;
MPI_Datatype     *recvtype;
MPI_Comm         *comm;
int *__ierr;
{
_fcd		temp;
if (_isfcd(sendbuf)) {
	temp = _fcdtocp(sendbuf);
	sendbuf = (void *)temp;
}
if (_isfcd(recvbuf)) {
	temp = _fcdtocp(recvbuf);
	recvbuf = (void *)temp;
}
*__ierr = MPI_Allgatherv(MPIR_F_PTR(sendbuf),*sendcount,*sendtype,
        MPIR_F_PTR(recvbuf),recvcounts,displs,*recvtype,*comm);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_allgatherv_ ( void *, MPI_Fint *, MPI_Fint *, void *, 
                                 MPI_Fint *, MPI_Fint *, MPI_Fint *,
				 MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_allgatherv_ ( void *sendbuf, MPI_Fint *sendcount,  MPI_Fint *sendtype, 
		       void *recvbuf, MPI_Fint *recvcounts, MPI_Fint *displs, MPI_Fint *recvtype, MPI_Fint *comm, MPI_Fint *__ierr )
{

    if (sizeof(MPI_Fint) == sizeof(int)) 
        *__ierr = MPI_Allgatherv(MPIR_F_PTR(sendbuf), *sendcount,
                                 MPI_Type_f2c(*sendtype),
			         MPIR_F_PTR(recvbuf), recvcounts,
                                 displs, MPI_Type_f2c(*recvtype),
			         MPI_Comm_f2c(*comm));
    else {
	int size;
        int *l_recvcounts;
        int *l_displs;
	int i;

	MPI_Comm_size(MPI_Comm_f2c(*comm), &size);
 
	MPIR_FALLOC(l_recvcounts,(int*)MALLOC(sizeof(int)* size),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Allgatherv");
	MPIR_FALLOC(l_displs,(int*)MALLOC(sizeof(int)* size),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Allgatherv");
	for (i=0; i<size; i++) {
	    l_recvcounts[i] = (int)recvcounts[i];
	    l_displs[i] = (int)displs[i];
	}    

        *__ierr = MPI_Allgatherv(MPIR_F_PTR(sendbuf), (int)*sendcount,
                                 MPI_Type_f2c(*sendtype),
			         MPIR_F_PTR(recvbuf), l_recvcounts,
                                 l_displs, MPI_Type_f2c(*recvtype),
			         MPI_Comm_f2c(*comm));
	FREE( l_recvcounts );
	FREE( l_displs );
    }


}
#endif
