/* sendrecv.c */
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
#pragma weak MPI_SENDRECV = PMPI_SENDRECV
void MPI_SENDRECV ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_sendrecv__ = pmpi_sendrecv__
void mpi_sendrecv__ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_sendrecv = pmpi_sendrecv
void mpi_sendrecv ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_sendrecv_ = pmpi_sendrecv_
void mpi_sendrecv_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_SENDRECV  MPI_SENDRECV
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_sendrecv__  mpi_sendrecv__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_sendrecv  mpi_sendrecv
#else
#pragma _HP_SECONDARY_DEF pmpi_sendrecv_  mpi_sendrecv_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_SENDRECV as PMPI_SENDRECV
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_sendrecv__ as pmpi_sendrecv__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_sendrecv as pmpi_sendrecv
#else
#pragma _CRI duplicate mpi_sendrecv_ as pmpi_sendrecv_
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
#define mpi_sendrecv_ PMPI_SENDRECV
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_sendrecv_ pmpi_sendrecv__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_sendrecv_ pmpi_sendrecv
#else
#define mpi_sendrecv_ pmpi_sendrecv_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_sendrecv_ MPI_SENDRECV
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_sendrecv_ mpi_sendrecv__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_sendrecv_ mpi_sendrecv
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 13

 void mpi_sendrecv_( void * unknown, ...)
{
void         	*sendbuf;
int		*sendcount;
MPI_Datatype  	*sendtype;
int		*dest,*sendtag;
void         	*recvbuf;
int		*recvcount;
MPI_Datatype  	*recvtype;
int		*source,*recvtag;
MPI_Comm      	*comm;
MPI_Status   	*status;
int 		*__ierr;
int             buflen;
va_list         ap;

va_start(ap, unknown);
sendbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* Note that we can't set __ierr because we don't know where it is!, and
       we can't reliably find it because we don't know what is wrong with the
       arg list */
    (void) MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR, 
			  "MPI_SENDRECV" );
    return;
}
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int ) / 8;         /* The length is in bits. */
}
sendcount =         va_arg(ap, int *);
sendtype =      va_arg(ap, MPI_Datatype*);
dest =          va_arg(ap, int *);
sendtag =           va_arg(ap, int *);
recvbuf =		va_arg(ap, void *);
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) / 8;         /* The length is in bits. */
}
recvcount =         va_arg(ap, int *);
recvtype =      va_arg(ap, MPI_Datatype*);
source =          va_arg(ap, int *);
recvtag =          va_arg(ap, int *);
comm =          va_arg(ap, MPI_Comm *);
status =        va_arg(ap, MPI_Status *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Sendrecv(MPIR_F_PTR(sendbuf),*sendcount,*sendtype,*dest,*sendtag,
         MPIR_F_PTR(recvbuf),*recvcount,*recvtype,*source,*recvtag,*comm,
		       status);
}

#else

 void mpi_sendrecv_( sendbuf, sendcount, sendtype, dest, sendtag, 
                  recvbuf, recvcount, recvtype, source, recvtag, 
                  comm, status, __ierr )
void         *sendbuf;
int*sendcount;
MPI_Datatype  *sendtype;
int*dest,*sendtag;
void         *recvbuf;
int*recvcount;
MPI_Datatype  *recvtype;
int*source,*recvtag;
MPI_Comm     *comm;
MPI_Status   *status;
int *__ierr;
{
_fcd temp;
if (_isfcd(sendbuf)) {
	temp = _fcdtocp(sendbuf);
	sendbuf = (void *)temp;
}
if (_isfcd(recvbuf)) {
	temp = _fcdtocp(recvbuf);
	recvbuf = (void *)temp;
}
*__ierr = MPI_Sendrecv(MPIR_F_PTR(sendbuf),*sendcount,*sendtype,*dest,*sendtag,
         MPIR_F_PTR(recvbuf),*recvcount,*recvtype,*source,*recvtag,*comm,
		       status);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_sendrecv_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                               MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, 
                               MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                               MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_sendrecv_( void *sendbuf, MPI_Fint *sendcount, MPI_Fint *sendtype, MPI_Fint *dest, MPI_Fint *sendtag, 
                  void *recvbuf, MPI_Fint *recvcount, MPI_Fint *recvtype, MPI_Fint *source, MPI_Fint *recvtag, 
                  MPI_Fint *comm, MPI_Fint *status, MPI_Fint *__ierr )
{
    MPI_Status c_status;

    *__ierr = MPI_Sendrecv(MPIR_F_PTR(sendbuf), (int)*sendcount, 
                           MPI_Type_f2c(*sendtype), (int)*dest, 
                           (int)*sendtag, MPIR_F_PTR(recvbuf), 
                           (int)*recvcount, MPI_Type_f2c(*recvtype),
			   (int)*source, (int)*recvtag,
                           MPI_Comm_f2c(*comm), &c_status);
    if (*__ierr == MPI_SUCCESS) 
        MPI_Status_c2f(&c_status, status);
}
#endif
