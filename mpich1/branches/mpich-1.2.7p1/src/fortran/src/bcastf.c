/* bcast.c */
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
#pragma weak MPI_BCAST = PMPI_BCAST
void MPI_BCAST ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_bcast__ = pmpi_bcast__
void mpi_bcast__ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_bcast = pmpi_bcast
void mpi_bcast ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_bcast_ = pmpi_bcast_
void mpi_bcast_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_BCAST  MPI_BCAST
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_bcast__  mpi_bcast__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_bcast  mpi_bcast
#else
#pragma _HP_SECONDARY_DEF pmpi_bcast_  mpi_bcast_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_BCAST as PMPI_BCAST
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_bcast__ as pmpi_bcast__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_bcast as pmpi_bcast
#else
#pragma _CRI duplicate mpi_bcast_ as pmpi_bcast_
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
#define mpi_bcast_ PMPI_BCAST
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_bcast_ pmpi_bcast__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_bcast_ pmpi_bcast
#else
#define mpi_bcast_ pmpi_bcast_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_bcast_ MPI_BCAST
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_bcast_ mpi_bcast__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_bcast_ mpi_bcast
#endif
#endif


#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS 6

 void mpi_bcast_ (void *unknown, ...)
{
void             *buffer;
int		*count;
MPI_Datatype    *datatype;
int		*root;
MPI_Comm        *  comm;
int 		*__ierr;
int             buflen;
va_list         ap;

va_start(ap, unknown);
buffer = unknown;
if (_numargs() == NUMPARAMS+1) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
count =     	va_arg (ap, int *);
datatype =      va_arg(ap, MPI_Datatype *);
root =		va_arg(ap, int *);
comm =          va_arg(ap, MPI_Comm *);
__ierr =        va_arg(ap, int *);

*__ierr = MPI_Bcast(MPIR_F_PTR(buffer),*count,*datatype,*root,*comm );
}

#else

 void mpi_bcast_ ( buffer, count, datatype, root, comm, __ierr )
void             *buffer;
int*count;
MPI_Datatype     * datatype;
int*root;
MPI_Comm         * comm;
int *__ierr;
{
_fcd            temp;
if (_isfcd(buffer)) {
        temp = _fcdtocp(buffer);
        buffer = (void *)temp;
}

*__ierr = MPI_Bcast(MPIR_F_PTR(buffer),*count,*datatype,*root,*comm);
}

#endif
#else
/* Prototype to suppress warnings about missing prototypes */

FORTRAN_API void FORT_CALL mpi_bcast_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                            MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_bcast_ ( void *buffer, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *root, MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Bcast(MPIR_F_PTR(buffer), (int)*count, 
                        MPI_Type_f2c(*datatype), (int)*root,
                        MPI_Comm_f2c(*comm));
}
#endif
