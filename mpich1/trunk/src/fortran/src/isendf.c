/* isend.c */
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
#pragma weak MPI_ISEND = PMPI_ISEND
void MPI_ISEND ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_isend__ = pmpi_isend__
void mpi_isend__ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_isend = pmpi_isend
void mpi_isend ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_isend_ = pmpi_isend_
void mpi_isend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ISEND  MPI_ISEND
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_isend__  mpi_isend__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_isend  mpi_isend
#else
#pragma _HP_SECONDARY_DEF pmpi_isend_  mpi_isend_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ISEND as PMPI_ISEND
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_isend__ as pmpi_isend__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_isend as pmpi_isend
#else
#pragma _CRI duplicate mpi_isend_ as pmpi_isend_
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
#define mpi_isend_ PMPI_ISEND
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_isend_ pmpi_isend__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_isend_ pmpi_isend
#else
#define mpi_isend_ pmpi_isend_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_isend_ MPI_ISEND
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_isend_ mpi_isend__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_isend_ mpi_isend
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 8

void mpi_isend_( void *unknown, ...)
{
void             *buf;
int*count;
MPI_Datatype     *datatype;
int*dest;
int*tag;
MPI_Comm         *comm;
MPI_Request      *request;
int *__ierr;
MPI_Request lrequest;
int		buflen;
va_list ap;

va_start(ap, unknown);
buf = unknown;
if (_numargs() == NUMPARAMS+1) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
count =         va_arg (ap, int *);
datatype =      va_arg(ap, MPI_Datatype*);
dest =          va_arg(ap, int *);
tag =           va_arg(ap, int *);
comm =          va_arg(ap, MPI_Comm *);
request =       va_arg(ap, MPI_Request *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Isend(MPIR_F_PTR(buf),*count,*datatype,*dest,*tag,*comm,
	&lrequest);
*(int*)request = MPI_Request_c2f(lrequest);
}

#else
void mpi_isend_( buf, count, datatype, dest, tag, comm, request, __ierr )
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

*__ierr = MPI_Isend(MPIR_F_PTR(buf),*count,*datatype,*dest,*tag,
	*comm,&lrequest);
*(int*)request = MPI_Request_c2f(lrequest);
}

#endif
#else

/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_isend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                            MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_isend_( void *buf, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm, MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Isend(MPIR_F_PTR(buf),(int)*count,MPI_Type_f2c(*datatype),
                        (int)*dest,
                        (int)*tag,MPI_Comm_f2c(*comm),
			&lrequest);
    if (*__ierr == MPI_SUCCESS) 
        *request = MPI_Request_c2f(lrequest);
}
#endif
