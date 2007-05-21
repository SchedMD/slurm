/* red_scat.c */
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
#pragma weak MPI_REDUCE_SCATTER = PMPI_REDUCE_SCATTER
void MPI_REDUCE_SCATTER ( void *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_reduce_scatter__ = pmpi_reduce_scatter__
void mpi_reduce_scatter__ ( void *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_reduce_scatter = pmpi_reduce_scatter
void mpi_reduce_scatter ( void *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_reduce_scatter_ = pmpi_reduce_scatter_
void mpi_reduce_scatter_ ( void *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_REDUCE_SCATTER  MPI_REDUCE_SCATTER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_reduce_scatter__  mpi_reduce_scatter__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_reduce_scatter  mpi_reduce_scatter
#else
#pragma _HP_SECONDARY_DEF pmpi_reduce_scatter_  mpi_reduce_scatter_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_REDUCE_SCATTER as PMPI_REDUCE_SCATTER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_reduce_scatter__ as pmpi_reduce_scatter__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_reduce_scatter as pmpi_reduce_scatter
#else
#pragma _CRI duplicate mpi_reduce_scatter_ as pmpi_reduce_scatter_
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
#define mpi_reduce_scatter_ PMPI_REDUCE_SCATTER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_reduce_scatter_ pmpi_reduce_scatter__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_reduce_scatter_ pmpi_reduce_scatter
#else
#define mpi_reduce_scatter_ pmpi_reduce_scatter_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_reduce_scatter_ MPI_REDUCE_SCATTER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_reduce_scatter_ mpi_reduce_scatter__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_reduce_scatter_ mpi_reduce_scatter
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 7

 void mpi_reduce_scatter_ ( void *unknown, ...)
{
void             *sendbuf;
void             *recvbuf;
int              *recvcnts;
MPI_Datatype     *datatype;
MPI_Op            *op;
MPI_Comm          *comm;
int *__ierr;
int             buflen;
va_list         ap;

va_start(ap, unknown);
sendbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* Note that we can't set __ierr because we don't know where it is! */
    (void) MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR,
		       "MPI_REDUCE_SCATTER" );
    return;
}
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
recvbuf =       va_arg(ap, void *);
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
recvcnts =     	va_arg(ap, int *);
datatype =      va_arg(ap, MPI_Datatype*);
op =		va_arg(ap, MPI_Op *);
comm =          va_arg(ap, MPI_Comm*);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Reduce_scatter(MPIR_F_PTR(sendbuf),
			     MPIR_F_PTR(recvbuf),recvcnts,*datatype,
			     *op,*comm );
}

#else

 void mpi_reduce_scatter_ ( sendbuf, recvbuf, recvcnts, datatype, op, comm, __ierr )
void             *sendbuf;
void             *recvbuf;
int              *recvcnts;
MPI_Datatype     *datatype;
MPI_Op            *op;
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

*__ierr = MPI_Reduce_scatter(MPIR_F_PTR(sendbuf),
			     MPIR_F_PTR(recvbuf),recvcnts,*datatype,
			     *op,*comm);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_reduce_scatter_ ( void *, void *, MPI_Fint *, MPI_Fint *, 
				     MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_reduce_scatter_ ( void *sendbuf, void *recvbuf, MPI_Fint *recvcnts, MPI_Fint *datatype, MPI_Fint *op, MPI_Fint *comm, 
			   MPI_Fint *__ierr )
{

    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Reduce_scatter(MPIR_F_PTR(sendbuf),
				     MPIR_F_PTR(recvbuf), recvcnts,
                                     MPI_Type_f2c(*datatype), MPI_Op_f2c(*op),
                                     MPI_Comm_f2c(*comm));
    else {
        int size;
        int *l_recvcnts;
	int i;

	MPI_Comm_size(MPI_Comm_f2c(*comm), &size);
 
	MPIR_FALLOC(l_recvcnts,(int*)MALLOC(sizeof(int)* size),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Reduce_scatter");
	for (i=0; i<size; i++) 
	    l_recvcnts[i] = (int)recvcnts[i];

        *__ierr = MPI_Reduce_scatter(MPIR_F_PTR(sendbuf),
				     MPIR_F_PTR(recvbuf), l_recvcnts,
                                     MPI_Type_f2c(*datatype), MPI_Op_f2c(*op),
                                     MPI_Comm_f2c(*comm));
	FREE( l_recvcnts);
    }

}
#endif
