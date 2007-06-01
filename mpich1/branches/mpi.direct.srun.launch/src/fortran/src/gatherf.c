/* gather.c */
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
#pragma weak MPI_GATHER = PMPI_GATHER
void MPI_GATHER ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_gather__ = pmpi_gather__
void mpi_gather__ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_gather = pmpi_gather
void mpi_gather ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_gather_ = pmpi_gather_
void mpi_gather_ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GATHER  MPI_GATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_gather__  mpi_gather__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_gather  mpi_gather
#else
#pragma _HP_SECONDARY_DEF pmpi_gather_  mpi_gather_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GATHER as PMPI_GATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_gather__ as pmpi_gather__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_gather as pmpi_gather
#else
#pragma _CRI duplicate mpi_gather_ as pmpi_gather_
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
#define mpi_gather_ PMPI_GATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_gather_ pmpi_gather__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_gather_ pmpi_gather
#else
#define mpi_gather_ pmpi_gather_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_gather_ MPI_GATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_gather_ mpi_gather__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_gather_ mpi_gather
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 9

 void mpi_gather_ ( void *unknown, ...)
{
void            *sendbuf;
int		*sendcnt;
MPI_Datatype    *sendtype;
void            *recvbuf;
int		*recvcount;
MPI_Datatype    *recvtype;
int		*root;
MPI_Comm        *comm;
int 		*__ierr;
int             buflen;
va_list         ap;

va_start(ap, unknown);
sendbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* Note that we can't set __ierr because we don't know where it is! */
    (void)MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR, "MPI_GATHER" );
    return;
}
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
sendcnt =     	va_arg (ap, int *);
sendtype =      va_arg(ap, MPI_Datatype *);
recvbuf =       va_arg(ap, void *);
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
recvcount =     va_arg (ap, int *);
recvtype =      va_arg(ap, MPI_Datatype *);
root =		va_arg(ap, int *);
comm =          va_arg(ap, MPI_Comm *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Gather(MPIR_F_PTR(sendbuf),*sendcnt,*sendtype,
        MPIR_F_PTR(recvbuf),*recvcount,*recvtype,*root,*comm);
}

#else

 void mpi_gather_ ( sendbuf, sendcnt, sendtype, recvbuf, recvcount, recvtype, 
   root, comm, __ierr )
void             *sendbuf;
int*sendcnt;
MPI_Datatype     *sendtype;
void             *recvbuf;
int*recvcount;
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

*__ierr = MPI_Gather(MPIR_F_PTR(sendbuf),*sendcnt,*sendtype,
        MPIR_F_PTR(recvbuf),*recvcount,*recvtype,*root,*comm);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_gather_ ( void *, MPI_Fint *, MPI_Fint *, void *, 
                             MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                             MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_gather_ ( void *sendbuf, MPI_Fint *sendcnt, MPI_Fint *sendtype, void *recvbuf, MPI_Fint *recvcount, MPI_Fint *recvtype, 
   MPI_Fint *root, MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Gather(MPIR_F_PTR(sendbuf), (int)*sendcnt,
                         MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                         (int)*recvcount, MPI_Type_f2c(*recvtype),
                         (int)*root, MPI_Comm_f2c(*comm));
}
#endif
