/* cart_shift.c */
/* Custom Fortran interface file */
/*
 *  
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_CART_SHIFT = PMPI_CART_SHIFT
void MPI_CART_SHIFT ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_cart_shift__ = pmpi_cart_shift__
void mpi_cart_shift__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_cart_shift = pmpi_cart_shift
void mpi_cart_shift ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_cart_shift_ = pmpi_cart_shift_
void mpi_cart_shift_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_CART_SHIFT  MPI_CART_SHIFT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_shift__  mpi_cart_shift__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_shift  mpi_cart_shift
#else
#pragma _HP_SECONDARY_DEF pmpi_cart_shift_  mpi_cart_shift_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_CART_SHIFT as PMPI_CART_SHIFT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_cart_shift__ as pmpi_cart_shift__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_cart_shift as pmpi_cart_shift
#else
#pragma _CRI duplicate mpi_cart_shift_ as pmpi_cart_shift_
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
#define mpi_cart_shift_ PMPI_CART_SHIFT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_shift_ pmpi_cart_shift__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_shift_ pmpi_cart_shift
#else
#define mpi_cart_shift_ pmpi_cart_shift_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_cart_shift_ MPI_CART_SHIFT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_shift_ mpi_cart_shift__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_shift_ mpi_cart_shift
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_cart_shift_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                 MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_cart_shift_( MPI_Fint *comm, MPI_Fint *direction, MPI_Fint *shift, MPI_Fint *source, MPI_Fint *dest, MPI_Fint *ierr )
{
    int lsource;
    int ldest;

    *ierr =     MPI_Cart_shift( MPI_Comm_f2c(*comm), (int)*direction, 
                                (int)*shift, &lsource, &ldest );
    *source = (MPI_Fint)lsource;
    *dest = (MPI_Fint)ldest;
}
