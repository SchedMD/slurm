/* buffree.c */
/* Custom Fortran interface file */

/* Note that the calling args are different in Fortran and C */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_BUFFER_DETACH = PMPI_BUFFER_DETACH
void MPI_BUFFER_DETACH ( void **, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_buffer_detach__ = pmpi_buffer_detach__
void mpi_buffer_detach__ ( void **, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_buffer_detach = pmpi_buffer_detach
void mpi_buffer_detach ( void **, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_buffer_detach_ = pmpi_buffer_detach_
void mpi_buffer_detach_ ( void **, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_BUFFER_DETACH  MPI_BUFFER_DETACH
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_buffer_detach__  mpi_buffer_detach__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_buffer_detach  mpi_buffer_detach
#else
#pragma _HP_SECONDARY_DEF pmpi_buffer_detach_  mpi_buffer_detach_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_BUFFER_DETACH as PMPI_BUFFER_DETACH
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_buffer_detach__ as pmpi_buffer_detach__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_buffer_detach as pmpi_buffer_detach
#else
#pragma _CRI duplicate mpi_buffer_detach_ as pmpi_buffer_detach_
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
#define mpi_buffer_detach_ PMPI_BUFFER_DETACH
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_buffer_detach_ pmpi_buffer_detach__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_buffer_detach_ pmpi_buffer_detach
#else
#define mpi_buffer_detach_ pmpi_buffer_detach_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_buffer_detach_ MPI_BUFFER_DETACH
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_buffer_detach_ mpi_buffer_detach__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_buffer_detach_ mpi_buffer_detach
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_buffer_detach_ ( void **, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_buffer_detach_( void **buffer, MPI_Fint *size, MPI_Fint *__ierr )
{
  void *tmp = (void *)buffer;
  int lsize;

  *__ierr = MPI_Buffer_detach(&tmp,&lsize);
  if (*__ierr == MPI_SUCCESS) 		     
      *size = (MPI_Fint)lsize;
}
