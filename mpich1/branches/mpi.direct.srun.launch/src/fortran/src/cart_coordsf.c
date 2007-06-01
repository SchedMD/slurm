/* cart_coords.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_CART_COORDS = PMPI_CART_COORDS
void MPI_CART_COORDS ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_cart_coords__ = pmpi_cart_coords__
void mpi_cart_coords__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_cart_coords = pmpi_cart_coords
void mpi_cart_coords ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_cart_coords_ = pmpi_cart_coords_
void mpi_cart_coords_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_CART_COORDS  MPI_CART_COORDS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_coords__  mpi_cart_coords__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_coords  mpi_cart_coords
#else
#pragma _HP_SECONDARY_DEF pmpi_cart_coords_  mpi_cart_coords_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_CART_COORDS as PMPI_CART_COORDS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_cart_coords__ as pmpi_cart_coords__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_cart_coords as pmpi_cart_coords
#else
#pragma _CRI duplicate mpi_cart_coords_ as pmpi_cart_coords_
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
#define mpi_cart_coords_ PMPI_CART_COORDS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_coords_ pmpi_cart_coords__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_coords_ pmpi_cart_coords
#else
#define mpi_cart_coords_ pmpi_cart_coords_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_cart_coords_ MPI_CART_COORDS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_coords_ mpi_cart_coords__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_coords_ mpi_cart_coords
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_cart_coords_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                  MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_cart_coords_ ( MPI_Fint *comm, MPI_Fint *rank, MPI_Fint *maxdims, MPI_Fint *coords, MPI_Fint *__ierr )
{

    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Cart_coords( MPI_Comm_f2c(*comm),*rank,
                                   *maxdims, coords);
    else {
        int *lcoords;
        int i;

	MPIR_FALLOC(lcoords,(int*)MALLOC(sizeof(int)* (int)*maxdims),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Cart_coords");

        *__ierr = MPI_Cart_coords( MPI_Comm_f2c(*comm), (int)*rank,
                               (int)*maxdims, lcoords);

        for (i=0; i<(int)*maxdims; i++)
	    coords[i] = (MPI_Fint)(lcoords[i]);

	FREE( lcoords );
    }
}
