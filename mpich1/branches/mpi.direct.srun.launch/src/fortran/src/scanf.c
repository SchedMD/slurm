/* scan.c */
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
#pragma weak MPI_SCAN = PMPI_SCAN
void MPI_SCAN ( void *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_scan__ = pmpi_scan__
void mpi_scan__ ( void *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_scan = pmpi_scan
void mpi_scan ( void *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_scan_ = pmpi_scan_
void mpi_scan_ ( void *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_SCAN  MPI_SCAN
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_scan__  mpi_scan__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_scan  mpi_scan
#else
#pragma _HP_SECONDARY_DEF pmpi_scan_  mpi_scan_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_SCAN as PMPI_SCAN
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_scan__ as pmpi_scan__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_scan as pmpi_scan
#else
#pragma _CRI duplicate mpi_scan_ as pmpi_scan_
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
#define mpi_scan_ PMPI_SCAN
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_scan_ pmpi_scan__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_scan_ pmpi_scan
#else
#define mpi_scan_ pmpi_scan_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_scan_ MPI_SCAN
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_scan_ mpi_scan__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_scan_ mpi_scan
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 7

 void mpi_scan_ ( void *unknown, ...)
{
void             *sendbuf;
void             *recvbuf;
int		*count;
MPI_Datatype     *datatype;
MPI_Op            *op;
MPI_Comm          *comm;
int 		*__ierr;
int             buflen;
va_list         ap;

va_start(ap, unknown);
sendbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* Note that we can't set __ierr because we don't know where it is! */
    (void) MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR, "MPI_SCANF" );
    return;
}
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
recvbuf =       va_arg(ap, void *);
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
count =     	va_arg (ap, int *);
datatype =      va_arg(ap, MPI_Datatype*);
op =		va_arg(ap, MPI_Op *);
comm =          va_arg(ap, MPI_Comm*);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Scan(MPIR_F_PTR(sendbuf),MPIR_F_PTR(recvbuf),*count,*datatype,
		   *op,*comm);
}

#else

 void mpi_scan_ ( sendbuf, recvbuf, count, datatype, op, comm, __ierr )
void             *sendbuf;
void             *recvbuf;
int*count;
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

*__ierr = MPI_Scan(MPIR_F_PTR(sendbuf),MPIR_F_PTR(recvbuf),*count,*datatype,
		   *op,*comm);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_scan_ ( void *, void *, MPI_Fint *, MPI_Fint *, 
                           MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_scan_ ( void *sendbuf, void *recvbuf, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *op, MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Scan(MPIR_F_PTR(sendbuf), MPIR_F_PTR(recvbuf),
                       (int)*count, MPI_Type_f2c(*datatype),
                       MPI_Op_f2c(*op), MPI_Comm_f2c(*comm));
}
#endif
