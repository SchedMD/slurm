/* bsend.c */
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
#pragma weak MPI_BSEND = PMPI_BSEND
void MPI_BSEND ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_bsend__ = pmpi_bsend__
void mpi_bsend__ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_bsend = pmpi_bsend
void mpi_bsend ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_bsend_ = pmpi_bsend_
void mpi_bsend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_BSEND  MPI_BSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_bsend__  mpi_bsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_bsend  mpi_bsend
#else
#pragma _HP_SECONDARY_DEF pmpi_bsend_  mpi_bsend_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_BSEND as PMPI_BSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_bsend__ as pmpi_bsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_bsend as pmpi_bsend
#else
#pragma _CRI duplicate mpi_bsend_ as pmpi_bsend_
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
#define mpi_bsend_ PMPI_BSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_bsend_ pmpi_bsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_bsend_ pmpi_bsend
#else
#define mpi_bsend_ pmpi_bsend_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_bsend_ MPI_BSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_bsend_ mpi_bsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_bsend_ mpi_bsend
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 7

 void mpi_bsend_( void *unknown, ...)
{
void             *buf;
int*count,*dest,*tag;
MPI_Datatype     *datatype;
MPI_Comm         *comm;
int *__ierr;
int		buflen;
va_list ap;

va_start(ap, unknown);
buf = unknown;
if (_numargs() == NUMPARAMS+1) {
        buflen = (va_arg(ap, int)) / 8;          /* This is in bits. */
}
count =         va_arg (ap, int *);
datatype =      va_arg(ap, MPI_Datatype*);
dest =          va_arg(ap, int *);
tag =           va_arg(ap, int *);
comm =          va_arg(ap, MPI_Comm *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Bsend(MPIR_F_PTR(buf),*count,*datatype,*dest,*tag,*comm);
}

#else

 void mpi_bsend_( buf, count, datatype, dest, tag, comm, __ierr )
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
*__ierr = MPI_Bsend(MPIR_F_PTR(buf),*count,*datatype,*dest,*tag,*comm);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_bsend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                            MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_bsend_( void *buf, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm, MPI_Fint *__ierr )
{
*__ierr = MPI_Bsend(MPIR_F_PTR(buf),(int)*count,MPI_Type_f2c(*datatype),
                    (int)*dest,(int)*tag,MPI_Comm_f2c(*comm) );
}
#endif
