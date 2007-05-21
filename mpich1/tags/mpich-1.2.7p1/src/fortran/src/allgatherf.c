/* allgather.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#ifdef _CRAY
#include <fortran.h>
#include <stdarg.h>
#endif


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ALLGATHER = PMPI_ALLGATHER
void MPI_ALLGATHER ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_allgather__ = pmpi_allgather__
void mpi_allgather__ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_allgather = pmpi_allgather
void mpi_allgather ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_allgather_ = pmpi_allgather_
void mpi_allgather_ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ALLGATHER  MPI_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_allgather__  mpi_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_allgather  mpi_allgather
#else
#pragma _HP_SECONDARY_DEF pmpi_allgather_  mpi_allgather_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ALLGATHER as PMPI_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_allgather__ as pmpi_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_allgather as pmpi_allgather
#else
#pragma _CRI duplicate mpi_allgather_ as pmpi_allgather_
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
#define mpi_allgather_ PMPI_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_allgather_ pmpi_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_allgather_ pmpi_allgather
#else
#define mpi_allgather_ pmpi_allgather_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_allgather_ MPI_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_allgather_ mpi_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_allgather_ mpi_allgather
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 8

 void mpi_allgather_ ( void *unknown, ...)
{
void            *sendbuf;
int		*sendcount;
MPI_Datatype    *sendtype;
void            *recvbuf;
int		*recvcount;
MPI_Datatype    *recvtype;
MPI_Comm        *comm;
int 		*__ierr;
int             buflen;
va_list 	ap;

va_start(ap, unknown);
sendbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* Note that we can't set __ierr because we don't know where it is! */
    (void) MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR, "MPI_ALLGATHER" );
    return;
}
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
sendcount =     va_arg (ap, int *);
sendtype =      va_arg(ap, MPI_Datatype *);
recvbuf =	va_arg(ap, void *);
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
recvcount =     va_arg (ap, int *);
recvtype =      va_arg(ap, MPI_Datatype *);
comm =          va_arg(ap, MPI_Comm *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Allgather(MPIR_F_PTR(sendbuf),*sendcount,*sendtype,
        MPIR_F_PTR(recvbuf),*recvcount,*recvtype,*comm);
}

#else

 void mpi_allgather_ ( sendbuf, sendcount, sendtype,
                    recvbuf, recvcount, recvtype, comm, __ierr )
void             *sendbuf;
int*sendcount;
MPI_Datatype      *sendtype;
void             *recvbuf;
int*recvcount;
MPI_Datatype      *recvtype;
MPI_Comm          *comm;
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
*__ierr = MPI_Allgather(MPIR_F_PTR(sendbuf),*sendcount,*sendtype,
        MPIR_F_PTR(recvbuf),*recvcount,*recvtype,*comm);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */

FORTRAN_API void FORT_CALL mpi_allgather_ ( void *, MPI_Fint *, MPI_Fint *, void *, 
				     MPI_Fint *, MPI_Fint *, MPI_Fint *, 
				     MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_allgather_ ( void *sendbuf, MPI_Fint *sendcount, MPI_Fint *sendtype,
                    void *recvbuf, MPI_Fint *recvcount, MPI_Fint *recvtype, MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Allgather(MPIR_F_PTR(sendbuf), (int)*sendcount,
                            MPI_Type_f2c(*sendtype),
			    MPIR_F_PTR(recvbuf),
                            (int)*recvcount,
                            MPI_Type_f2c(*recvtype),
                            MPI_Comm_f2c(*comm));
}
#endif
