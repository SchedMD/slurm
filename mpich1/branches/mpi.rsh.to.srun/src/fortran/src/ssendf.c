/* ssend.c */
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
#pragma weak MPI_SSEND = PMPI_SSEND
void MPI_SSEND ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_ssend__ = pmpi_ssend__
void mpi_ssend__ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_ssend = pmpi_ssend
void mpi_ssend ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_ssend_ = pmpi_ssend_
void mpi_ssend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_SSEND  MPI_SSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_ssend__  mpi_ssend__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_ssend  mpi_ssend
#else
#pragma _HP_SECONDARY_DEF pmpi_ssend_  mpi_ssend_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_SSEND as PMPI_SSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_ssend__ as pmpi_ssend__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_ssend as pmpi_ssend
#else
#pragma _CRI duplicate mpi_ssend_ as pmpi_ssend_
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
#define mpi_ssend_ PMPI_SSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_ssend_ pmpi_ssend__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_ssend_ pmpi_ssend
#else
#define mpi_ssend_ pmpi_ssend_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_ssend_ MPI_SSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_ssend_ mpi_ssend__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_ssend_ mpi_ssend
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 7

 void mpi_ssend_( void *unknown, ...)
{
void             *buf;
int*count,*dest,*tag;
MPI_Datatype     *datatype;
MPI_Comm         *comm;
int *__ierr;
int		buflen;
va_list		ap;

va_start(ap, unknown);
buf = unknown;
if (_numargs() == NUMPARAMS+1) {
	buflen = va_arg(ap, int) / 8;		/* The length is in bits. */
}
count =		va_arg(ap, int *);
datatype =	va_arg(ap, MPI_Datatype*);
dest =		va_arg(ap, int *);
tag =		va_arg(ap, int *);
comm =		va_arg(ap, MPI_Comm*);
__ierr =	va_arg(ap, int *);
*__ierr = MPI_Ssend(MPIR_F_PTR(buf),*count,*datatype,*dest,*tag,*comm);
}
#else
 void mpi_ssend_( buf, count, datatype, dest, tag, comm, __ierr )
void             *buf;
int*count,*dest,*tag;
MPI_Datatype     *datatype;
MPI_Comm         *comm;
int *__ierr;
{
_fcd temp;
if (_isfcd(buf)) {
	temp = _fcdtocp(buf);
	buf = (void *)temp;
}
*__ierr = MPI_Ssend(MPIR_F_PTR(buf),*count,*datatype,*dest,*tag,*comm);
}
#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_ssend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                            MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_ssend_( void *buf, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Ssend(MPIR_F_PTR(buf), (int)*count, 
                        MPI_Type_f2c(*datatype), (int)*dest, (int)*tag,
                        MPI_Comm_f2c(*comm));
}
#endif
