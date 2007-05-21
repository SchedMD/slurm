/* cart_map.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_CART_MAP = PMPI_CART_MAP
void MPI_CART_MAP ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_cart_map__ = pmpi_cart_map__
void mpi_cart_map__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_cart_map = pmpi_cart_map
void mpi_cart_map ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_cart_map_ = pmpi_cart_map_
void mpi_cart_map_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_CART_MAP  MPI_CART_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_map__  mpi_cart_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_map  mpi_cart_map
#else
#pragma _HP_SECONDARY_DEF pmpi_cart_map_  mpi_cart_map_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_CART_MAP as PMPI_CART_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_cart_map__ as pmpi_cart_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_cart_map as pmpi_cart_map
#else
#pragma _CRI duplicate mpi_cart_map_ as pmpi_cart_map_
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
#define mpi_cart_map_ PMPI_CART_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_map_ pmpi_cart_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_map_ pmpi_cart_map
#else
#define mpi_cart_map_ pmpi_cart_map_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_cart_map_ MPI_CART_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_map_ mpi_cart_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_map_ mpi_cart_map
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_cart_map_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                               MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_cart_map_ ( MPI_Fint *comm_old, MPI_Fint *ndims, MPI_Fint *dims, MPI_Fint *periods, MPI_Fint *newrank, MPI_Fint *__ierr )
{
    int lperiods[20], i;
    int ldims[20];
    int lnewrank;
    static char myname[] = "MPI_CART_MAP";

    if ((int)*ndims > 20) {
	struct MPIR_COMMUNICATOR *comm_old_ptr;
	comm_old_ptr = MPIR_GET_COMM_PTR(MPI_Comm_f2c(*comm_old));
	*__ierr = MPIR_Err_setmsg( MPI_ERR_DIMS, MPIR_ERR_DIMS_TOOLARGE,
				   myname, (char *)0, (char *)0, 
				   (int)*ndims, 20 );
	*__ierr = MPIR_ERROR( comm_old_ptr, *__ierr, myname );
	return;
	}
    for (i=0; i<(int)*ndims; i++) {
	lperiods[i] = MPIR_FROM_FLOG(periods[i]);
	ldims[i] = (int)dims[i];
    }
    *__ierr = MPI_Cart_map( MPI_Comm_f2c(*comm_old), (int)*ndims, ldims,
			   lperiods, &lnewrank);
    *newrank = (MPI_Fint)lnewrank;
}
