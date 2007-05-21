/* cart_rank.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_CART_RANK = PMPI_CART_RANK
void MPI_CART_RANK ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_cart_rank__ = pmpi_cart_rank__
void mpi_cart_rank__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_cart_rank = pmpi_cart_rank
void mpi_cart_rank ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_cart_rank_ = pmpi_cart_rank_
void mpi_cart_rank_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_CART_RANK  MPI_CART_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_rank__  mpi_cart_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_rank  mpi_cart_rank
#else
#pragma _HP_SECONDARY_DEF pmpi_cart_rank_  mpi_cart_rank_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_CART_RANK as PMPI_CART_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_cart_rank__ as pmpi_cart_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_cart_rank as pmpi_cart_rank
#else
#pragma _CRI duplicate mpi_cart_rank_ as pmpi_cart_rank_
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
#define mpi_cart_rank_ PMPI_CART_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_rank_ pmpi_cart_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_rank_ pmpi_cart_rank
#else
#define mpi_cart_rank_ pmpi_cart_rank_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_cart_rank_ MPI_CART_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_rank_ mpi_cart_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_rank_ mpi_cart_rank
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_cart_rank_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_cart_rank_ ( MPI_Fint *comm, MPI_Fint *coords, MPI_Fint *rank, MPI_Fint *__ierr )
{
    MPI_Comm l_comm;
    int lcoords[20];
    int lrank;
    int ndims;
    int i;
    static char myname[] = "MPI_CART_RANK";

    l_comm = MPI_Comm_f2c(*comm);
    if (l_comm == MPI_COMM_NULL) {
	struct MPIR_COMMUNICATOR *comm_ptr;
	comm_ptr = MPIR_GET_COMM_PTR(MPI_Comm_f2c(*comm));
	*__ierr = MPIR_ERROR( comm_ptr, 
	      MPIR_ERRCLASS_TO_CODE(MPI_ERR_COMM,MPIR_ERR_COMM_NULL), myname );
	return;
	}
	
    MPI_Cartdim_get( MPI_Comm_f2c(*comm), &ndims);
  
    if (ndims > 20) {
	struct MPIR_COMMUNICATOR *comm_ptr;
	comm_ptr = MPIR_GET_COMM_PTR(MPI_Comm_f2c(*comm));
	*__ierr = MPIR_Err_setmsg( MPI_ERR_DIMS, MPIR_ERR_DIMS_TOOLARGE,
				   myname, (char *)0, (char *)0, 
				   (int)ndims, 20 );
	*__ierr = MPIR_ERROR( comm_ptr, *__ierr, myname );
	return;
	}
 
    for (i=0; i<ndims; i++)
	lcoords[i] = (int)coords[i];
    *__ierr = MPI_Cart_rank( MPI_Comm_f2c(*comm), lcoords, &lrank);
    *rank = (MPI_Fint)lrank;

}

