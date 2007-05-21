/* address.c */
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
#pragma weak MPI_ADDRESS = PMPI_ADDRESS
void MPI_ADDRESS ( void *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_address__ = pmpi_address__
void mpi_address__ ( void *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_address = pmpi_address
void mpi_address ( void *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_address_ = pmpi_address_
void mpi_address_ ( void *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ADDRESS  MPI_ADDRESS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_address__  mpi_address__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_address  mpi_address
#else
#pragma _HP_SECONDARY_DEF pmpi_address_  mpi_address_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ADDRESS as PMPI_ADDRESS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_address__ as pmpi_address__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_address as pmpi_address
#else
#pragma _CRI duplicate mpi_address_ as pmpi_address_
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
#define mpi_address_ PMPI_ADDRESS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_address_ pmpi_address__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_address_ pmpi_address
#else
#define mpi_address_ pmpi_address_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_address_ MPI_ADDRESS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_address_ mpi_address__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_address_ mpi_address
#endif
#endif


/*
   This code is a little subtle.  By making all addresses relative 
   to MPIR_F_MPI_BOTTOM, we can all ways add a computed address to the Fortran
   MPI_BOTTOM to get the correct address.  In addition, this can fix 
   problems on systems where Fortran integers are too short for addresses,
   since often, addresses will be within 2 GB of each other, and making them
   relative to MPIR_F_MPI_BOTTOM makes the relative addresses fit into
   a Fortran integer.

   (Note that ALL addresses in MPI are relative; an absolute address is
   just one that is relative to MPI_BOTTOM.)
 */

#ifdef _CRAY
#ifdef _TWO_WORD_FCD
#define NUMPARAMS  3

void mpi_address_( void *unknown, ...)
{
void *location;
int*address;
int *__ierr;
int buflen;
va_list ap;
MPI_Aint a;

va_start(ap, unknown);
location = unknown;
if (_numargs() == NUMPARAMS+1) {
        buflen = va_arg(ap, int) /8;          /* This is in bits. */
}
address         = va_arg(ap, int *);
__ierr          = va_arg(ap, int *);

*__ierr = MPI_Address(location,&a);
*address = (int)( a - (MPI_Aint)MPIR_F_MPI_BOTTOM);
}

#else

void mpi_address_( location, address, __ierr )
void     *location;
int      *address;
int      *__ierr;
{
_fcd temp;
MPI_Aint a;
if (_isfcd(location)) {
	temp = _fcdtocp(location);
	location = (void *) temp;
}
*__ierr = MPI_Address( location, &a );
*address = (int)( a - (MPI_Aint)MPIR_F_MPI_BOTTOM);
}
#endif
#else

/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_address_ ( void *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_address_( void *location, MPI_Fint *address, MPI_Fint *__ierr )
{
    MPI_Aint a, b;

    *__ierr = MPI_Address( location, &a );
    if (*__ierr != MPI_SUCCESS) return;
    
    b = a - (MPI_Aint)MPIR_F_MPI_BOTTOM;
    *address = (MPI_Fint)( b );
    if (((MPI_Aint)*address) - b != 0) {
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD,     
      MPIR_ERRCLASS_TO_CODE(MPI_ERR_ARG,MPIR_ERR_FORTRAN_ADDRESS_RANGE),
			      "MPI_ADDRESS" );
    }
}
#endif

