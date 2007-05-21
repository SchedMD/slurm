/* ibsend.c */
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
#pragma weak MPI_IBSEND = PMPI_IBSEND
void MPI_IBSEND ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_ibsend__ = pmpi_ibsend__
void mpi_ibsend__ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_ibsend = pmpi_ibsend
void mpi_ibsend ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_ibsend_ = pmpi_ibsend_
void mpi_ibsend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_IBSEND  MPI_IBSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_ibsend__  mpi_ibsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_ibsend  mpi_ibsend
#else
#pragma _HP_SECONDARY_DEF pmpi_ibsend_  mpi_ibsend_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_IBSEND as PMPI_IBSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_ibsend__ as pmpi_ibsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_ibsend as pmpi_ibsend
#else
#pragma _CRI duplicate mpi_ibsend_ as pmpi_ibsend_
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
#define mpi_ibsend_ PMPI_IBSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_ibsend_ pmpi_ibsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_ibsend_ pmpi_ibsend
#else
#define mpi_ibsend_ pmpi_ibsend_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_ibsend_ MPI_IBSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_ibsend_ mpi_ibsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_ibsend_ mpi_ibsend
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS  8

 void mpi_ibsend_( void *unknown, ...)
{
void             *buf;
int		*count;
MPI_Datatype     *datatype;
int		*dest;
int		*tag;
MPI_Comm         *comm;
MPI_Request      *request;
int 		*__ierr;
int		buflen;
va_list ap;
MPI_Request lrequest;

va_start(ap, unknown);
buf = unknown;
if (_numargs() == NUMPARAMS+1) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
count =         va_arg (ap, int *);
datatype =      va_arg(ap, MPI_Datatype *);
dest =          va_arg(ap, int *);
tag =           va_arg(ap, int *);
comm =          va_arg(ap, MPI_Comm*);
request =       va_arg(ap, MPI_Request *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Ibsend(MPIR_F_PTR(buf),*count,*datatype,*dest,*tag,
		     *comm,&lrequest);
*(int*)request = MPI_Request_c2f(lrequest);
}

#else

 void mpi_ibsend_( buf, count, datatype, dest, tag, comm, request, __ierr )
void             *buf;
int*count;
MPI_Datatype     *datatype;
int*dest;
int*tag;
MPI_Comm         *comm;
MPI_Request      *request;
int *__ierr;
{
MPI_Request lrequest;
_fcd temp;
if (_isfcd(buf)) {
	temp = _fcdtocp(buf);
	buf = (void *)temp;
}
*__ierr = MPI_Ibsend(MPIR_F_PTR(buf),*count,*datatype,*dest,*tag,
		     *comm,&lrequest);
*(int*)request = MPI_Request_c2f(lrequest);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_ibsend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                             MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                             MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_ibsend_( void *buf, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm, MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Ibsend(MPIR_F_PTR(buf),(int)*count,MPI_Type_f2c(*datatype),
                         (int)*dest,(int)*tag,MPI_Comm_f2c(*comm),
                         &lrequest);
    if (*__ierr == MPI_SUCCESS) 		     
        *request = MPI_Request_c2f(lrequest);
}
#endif
