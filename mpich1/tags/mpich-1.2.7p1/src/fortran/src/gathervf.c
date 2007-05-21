/* gatherv.c */
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
#pragma weak MPI_GATHERV = PMPI_GATHERV
void MPI_GATHERV ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_gatherv__ = pmpi_gatherv__
void mpi_gatherv__ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_gatherv = pmpi_gatherv
void mpi_gatherv ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_gatherv_ = pmpi_gatherv_
void mpi_gatherv_ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GATHERV  MPI_GATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_gatherv__  mpi_gatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_gatherv  mpi_gatherv
#else
#pragma _HP_SECONDARY_DEF pmpi_gatherv_  mpi_gatherv_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GATHERV as PMPI_GATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_gatherv__ as pmpi_gatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_gatherv as pmpi_gatherv
#else
#pragma _CRI duplicate mpi_gatherv_ as pmpi_gatherv_
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
#define mpi_gatherv_ PMPI_GATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_gatherv_ pmpi_gatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_gatherv_ pmpi_gatherv
#else
#define mpi_gatherv_ pmpi_gatherv_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_gatherv_ MPI_GATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_gatherv_ mpi_gatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_gatherv_ mpi_gatherv
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 10

 void mpi_gatherv_ ( void *unknown, ...)
{
void             *sendbuf;
int		*sendcnt;
MPI_Datatype     *sendtype;
void             *recvbuf;
int              *recvcnts;
int              *displs;
MPI_Datatype     *recvtype;
int		*root;
MPI_Comm          *comm;
int 		*__ierr;
int             buflen;
va_list         ap;

va_start(ap, unknown);
sendbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* Note that we can't set __ierr because we don't know where it is! */
    (void) MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR, "MPI_GATHERV" );
    return;
}
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
sendcnt =     	va_arg(ap, int *);
sendtype =      va_arg(ap, MPI_Datatype *);
recvbuf =       va_arg(ap, void *);
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
recvcnts =     	va_arg(ap, int *);
displs = 	va_arg(ap, int *);
recvtype =      va_arg(ap, MPI_Datatype *);
root =		va_arg(ap, int *);
comm =          va_arg(ap, MPI_Comm *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Gatherv(MPIR_F_PTR(sendbuf),*sendcnt,*sendtype,
        MPIR_F_PTR(recvbuf),recvcnts,displs,*recvtype,*root,*comm);
}

#else

 void mpi_gatherv_ ( sendbuf, sendcnt,  sendtype, 
                  recvbuf, recvcnts, displs, recvtype, 
                  root, comm, __ierr )
void             *sendbuf;
int*sendcnt;
MPI_Datatype     *sendtype;
void             *recvbuf;
int              *recvcnts;
int              *displs;
MPI_Datatype     *recvtype;
int*root;
MPI_Comm          *comm;
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

*__ierr = MPI_Gatherv(MPIR_F_PTR(sendbuf),*sendcnt,*sendtype,
        MPIR_F_PTR(recvbuf),recvcnts,displs,*recvtype,*root,*comm);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */

FORTRAN_API void FORT_CALL mpi_gatherv_ ( void *, MPI_Fint *, MPI_Fint *, void *, 
                              MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                              MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_gatherv_ ( void *sendbuf, MPI_Fint *sendcnt,  MPI_Fint *sendtype, 
                  void *recvbuf, MPI_Fint *recvcnts, MPI_Fint *displs, MPI_Fint *recvtype, 
                  MPI_Fint *root, MPI_Fint *comm, MPI_Fint *__ierr )
{

    if (sizeof(MPI_Fint) == sizeof(int)) 
        *__ierr = MPI_Gatherv(MPIR_F_PTR(sendbuf), *sendcnt,
                              MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                              recvcnts, displs, 
                              MPI_Type_f2c(*recvtype), *root,
                              MPI_Comm_f2c(*comm));
    else {
	int size;
        int *l_recvcnts;
        int *l_displs;
	int i;

	MPI_Comm_size(MPI_Comm_f2c(*comm), &size);
 
	MPIR_FALLOC(l_recvcnts,(int*)MALLOC(sizeof(int)* size),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Gatherv");
	MPIR_FALLOC(l_displs,(int*)MALLOC(sizeof(int)* size),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Gatherv");
	for (i=0; i<size; i++) {
	    l_recvcnts[i] = (int)recvcnts[i];
	    l_displs[i] = (int)displs[i];
	}    
        *__ierr = MPI_Gatherv(MPIR_F_PTR(sendbuf), (int)*sendcnt,
                              MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                              l_recvcnts, l_displs, 
                              MPI_Type_f2c(*recvtype), (int)*root,
                              MPI_Comm_f2c(*comm));
	FREE( l_recvcnts );
	FREE( l_displs );
    }

}
#endif

