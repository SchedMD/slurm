/* cart_sub.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_CART_SUB = PMPI_CART_SUB
void MPI_CART_SUB ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_cart_sub__ = pmpi_cart_sub__
void mpi_cart_sub__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_cart_sub = pmpi_cart_sub
void mpi_cart_sub ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_cart_sub_ = pmpi_cart_sub_
void mpi_cart_sub_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_CART_SUB  MPI_CART_SUB
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_sub__  mpi_cart_sub__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_sub  mpi_cart_sub
#else
#pragma _HP_SECONDARY_DEF pmpi_cart_sub_  mpi_cart_sub_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_CART_SUB as PMPI_CART_SUB
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_cart_sub__ as pmpi_cart_sub__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_cart_sub as pmpi_cart_sub
#else
#pragma _CRI duplicate mpi_cart_sub_ as pmpi_cart_sub_
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
#define mpi_cart_sub_ PMPI_CART_SUB
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_sub_ pmpi_cart_sub__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_sub_ pmpi_cart_sub
#else
#define mpi_cart_sub_ pmpi_cart_sub_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_cart_sub_ MPI_CART_SUB
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_sub_ mpi_cart_sub__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_sub_ mpi_cart_sub
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_cart_sub_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_cart_sub_ ( MPI_Fint *comm, MPI_Fint *remain_dims, MPI_Fint *comm_new, MPI_Fint *__ierr )
{
    int lremain_dims[20], i, ndims;
    MPI_Comm lcomm_new;
    static char myname[] = "MPI_CART_SUB";

    MPI_Cartdim_get( MPI_Comm_f2c(*comm), &ndims );
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
	lremain_dims[i] = MPIR_FROM_FLOG(remain_dims[i]);

    *__ierr = MPI_Cart_sub( MPI_Comm_f2c(*comm), lremain_dims, 
                            &lcomm_new);
    if (*__ierr == MPI_SUCCESS) 		     
        *comm_new = MPI_Comm_c2f(lcomm_new);
}

