/* unpack.c */
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
#pragma weak MPI_UNPACK = PMPI_UNPACK
void MPI_UNPACK ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_unpack__ = pmpi_unpack__
void mpi_unpack__ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_unpack = pmpi_unpack
void mpi_unpack ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_unpack_ = pmpi_unpack_
void mpi_unpack_ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_UNPACK  MPI_UNPACK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_unpack__  mpi_unpack__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_unpack  mpi_unpack
#else
#pragma _HP_SECONDARY_DEF pmpi_unpack_  mpi_unpack_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_UNPACK as PMPI_UNPACK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_unpack__ as pmpi_unpack__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_unpack as pmpi_unpack
#else
#pragma _CRI duplicate mpi_unpack_ as pmpi_unpack_
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
#define mpi_unpack_ PMPI_UNPACK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_unpack_ pmpi_unpack__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_unpack_ pmpi_unpack
#else
#define mpi_unpack_ pmpi_unpack_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_unpack_ MPI_UNPACK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_unpack_ mpi_unpack__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_unpack_ mpi_unpack
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 8

 void mpi_unpack_ ( void *unknown, ...)
{
void         	*inbuf;
int		*insize;
int          	*position;
void         	*outbuf;
int		*outcount;
MPI_Datatype  	*datatype;
MPI_Comm      	*comm;
int 		*__ierr;
int		buflen;
va_list         ap;

va_start(ap, unknown);
inbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* We can't get at the last variable, since there is a fatal 
       error in passing the wrong number of arguments. */
    MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR, "MPI_UNPACK" );
    return;
}
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) / 8;           /* The length is in bits. */
}
insize =        va_arg(ap, int *);
position =      va_arg(ap, int *);
outbuf =	va_arg(ap, void *);
if (_numargs() == NUMPARAMS+2) {
        buflen = va_arg(ap, int) / 8;           /* The length is in bits. */
}
outcount =      va_arg(ap, int *);
datatype =      va_arg(ap, MPI_Datatype*);
comm =          va_arg(ap, MPI_Comm *);
__ierr =        va_arg(ap, int *);


*__ierr = MPI_Unpack(inbuf,*insize,position,MPIR_F_PTR(outbuf),*outcount,
	*datatype,*comm );
}

#else

 void mpi_unpack_ ( inbuf, insize, position, outbuf, outcount, type, comm, __ierr )
void         *inbuf;
int*insize;
int          *position;
void         *outbuf;
int*outcount;
MPI_Datatype  *type;
MPI_Comm      *comm;
int *__ierr;
{
_fcd temp;
if (_isfcd(inbuf)) {
	temp = _fcdtocp(inbuf);
	inbuf = (void *) temp;
}
if (_isfcd(outbuf)) {
	temp = _fcdtocp(outbuf);
	outbuf = (void *) temp;
}
*__ierr = MPI_Unpack(inbuf,*insize,position,MPIR_F_PTR(outbuf),*outcount,
	*type, *comm );
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_unpack_ ( void *, MPI_Fint *, MPI_Fint *, void *, 
                             MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                             MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_unpack_ ( void *inbuf, MPI_Fint *insize, MPI_Fint *position, void *outbuf, MPI_Fint *outcount, MPI_Fint *type, MPI_Fint *comm, 
		   MPI_Fint *__ierr )
{
    int l_position;
    l_position = (int)*position;

    *__ierr = MPI_Unpack(inbuf, (int)*insize, &l_position,
                         MPIR_F_PTR(outbuf), (int)*outcount,
			 MPI_Type_f2c(*type), MPI_Comm_f2c(*comm) );
    *position = (MPI_Fint)l_position;
}
#endif
