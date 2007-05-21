/* bufattach.c */
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
#pragma weak MPI_BUFFER_ATTACH = PMPI_BUFFER_ATTACH
void MPI_BUFFER_ATTACH ( void *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_buffer_attach__ = pmpi_buffer_attach__
void mpi_buffer_attach__ ( void *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_buffer_attach = pmpi_buffer_attach
void mpi_buffer_attach ( void *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_buffer_attach_ = pmpi_buffer_attach_
void mpi_buffer_attach_ ( void *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_BUFFER_ATTACH  MPI_BUFFER_ATTACH
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_buffer_attach__  mpi_buffer_attach__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_buffer_attach  mpi_buffer_attach
#else
#pragma _HP_SECONDARY_DEF pmpi_buffer_attach_  mpi_buffer_attach_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_BUFFER_ATTACH as PMPI_BUFFER_ATTACH
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_buffer_attach__ as pmpi_buffer_attach__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_buffer_attach as pmpi_buffer_attach
#else
#pragma _CRI duplicate mpi_buffer_attach_ as pmpi_buffer_attach_
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
#define mpi_buffer_attach_ PMPI_BUFFER_ATTACH
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_buffer_attach_ pmpi_buffer_attach__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_buffer_attach_ pmpi_buffer_attach
#else
#define mpi_buffer_attach_ pmpi_buffer_attach_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_buffer_attach_ MPI_BUFFER_ATTACH
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_buffer_attach_ mpi_buffer_attach__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_buffer_attach_ mpi_buffer_attach
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS  3

 void mpi_buffer_attach_( void *unknown, ...)
{
void *buffer;
int*size;
int *__ierr;
int buflen;
va_list	ap;

va_start(ap, unknown);
buffer = unknown;
if (_numargs() == NUMPARAMS+1) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
size		= va_arg(ap, int *);
__ierr		= va_arg(ap, int *);	

*__ierr = MPI_Buffer_attach(buffer,*size);
}

#else
 void mpi_buffer_attach_( buffer, size, __ierr )
void *buffer;
int*size;
int *__ierr;
{
_fcd temp;
if (_isfcd(buffer)) {
	temp = _fcdtocp(buffer);
	buffer = (void *) temp;
}
*__ierr = MPI_Buffer_attach(buffer,*size);
}

#endif
#else

/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_buffer_attach_ ( void *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_buffer_attach_( void *buffer, MPI_Fint *size, MPI_Fint *__ierr )
{
    *__ierr = MPI_Buffer_attach(buffer,(int)*size);
}
#endif



