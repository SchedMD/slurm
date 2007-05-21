/* pack.c */
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
#pragma weak MPI_PACK = PMPI_PACK
void MPI_PACK ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_pack__ = pmpi_pack__
void mpi_pack__ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_pack = pmpi_pack
void mpi_pack ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_pack_ = pmpi_pack_
void mpi_pack_ ( void *, MPI_Fint *, MPI_Fint *, void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_PACK  MPI_PACK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_pack__  mpi_pack__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_pack  mpi_pack
#else
#pragma _HP_SECONDARY_DEF pmpi_pack_  mpi_pack_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_PACK as PMPI_PACK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_pack__ as pmpi_pack__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_pack as pmpi_pack
#else
#pragma _CRI duplicate mpi_pack_ as pmpi_pack_
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
#define mpi_pack_ PMPI_PACK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_pack_ pmpi_pack__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_pack_ pmpi_pack
#else
#define mpi_pack_ pmpi_pack_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_pack_ MPI_PACK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_pack_ mpi_pack__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_pack_ mpi_pack
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS  8

 void mpi_pack_ ( void *unknown, ...)
{
void         *inbuf;
int*incount;
MPI_Datatype  *datatype;
void         *outbuf;
int*outcount;
int          *position;
MPI_Comm      *comm;
int *__ierr;
int		buflen;
va_list ap;

va_start(ap, unknown);
inbuf = unknown;
if (_numargs() == NUMPARAMS+1) {
    /* We can't get at the last variable, since there is a fatal 
       error in passing the wrong number of arguments. */
    MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ONE_CHAR, "MPI_PACK" );
    return;
} else { if (_numargs() == NUMPARAMS+2) {
        buflen = 	va_arg(ap, int) /8;          /* This is in bits. */
	incount =       va_arg (ap, int *);
	datatype =      va_arg(ap, MPI_Datatype*);
	outbuf = 	va_arg(ap, void *);
	buflen =	va_arg(ap, int ) /8;
} else {
	incount =       va_arg (ap, int *);
	datatype =      va_arg(ap, MPI_Datatype*);
	outbuf =	va_arg(ap, void *);
}
}

outcount =	va_arg(ap, int *);
position =	va_arg(ap, int *);
comm =          va_arg(ap, MPI_Comm *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Pack(MPIR_F_PTR(inbuf),*incount,*datatype,
		   outbuf,*outcount,position,*comm);
}

#else

 void mpi_pack_ ( inbuf, incount, type, outbuf, outcount, position, comm, __ierr )
void         *inbuf;
int*incount;
MPI_Datatype  *type;
void         *outbuf;
int*outcount;
int          *position;
MPI_Comm     *comm;
int *__ierr;
{
_fcd temp;
if (_isfcd(inbuf)) {
	temp = _fcdtocp(inbuf);
	inbuf = (void *)temp;
}
if (_isfcd(outbuf)) {
	temp = _fcdtocp(outbuf);
	outbuf = (void *)temp;
}

*__ierr = MPI_Pack(MPIR_F_PTR(inbuf),*incount,*type,outbuf,*outcount,position,
		   *comm );
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_pack_ ( void *, MPI_Fint *, MPI_Fint *, void *, 
                           MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_pack_ ( void *inbuf, MPI_Fint *incount, MPI_Fint *type, void *outbuf, MPI_Fint *outcount, MPI_Fint *position, MPI_Fint *comm, 
		 MPI_Fint *__ierr )
{
    int lposition;

    lposition = (int)*position;
    *__ierr = MPI_Pack(MPIR_F_PTR(inbuf), (int)*incount, MPI_Type_f2c(*type),
		       outbuf, (int)*outcount, &lposition,
                       MPI_Comm_f2c(*comm));
    *position = (MPI_Fint)lposition;
}
#endif
