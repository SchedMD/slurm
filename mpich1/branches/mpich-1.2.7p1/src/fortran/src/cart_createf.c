/*
 *  $Id: cart_createf.c,v 1.5 2001/12/12 23:36:27 ashton Exp $
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 * Custom Fortran wrapper
 */

#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_CART_CREATE = PMPI_CART_CREATE
void MPI_CART_CREATE ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_cart_create__ = pmpi_cart_create__
void mpi_cart_create__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_cart_create = pmpi_cart_create
void mpi_cart_create ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_cart_create_ = pmpi_cart_create_
void mpi_cart_create_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_CART_CREATE  MPI_CART_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_create__  mpi_cart_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_cart_create  mpi_cart_create
#else
#pragma _HP_SECONDARY_DEF pmpi_cart_create_  mpi_cart_create_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_CART_CREATE as PMPI_CART_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_cart_create__ as pmpi_cart_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_cart_create as pmpi_cart_create
#else
#pragma _CRI duplicate mpi_cart_create_ as pmpi_cart_create_
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
#define mpi_cart_create_ PMPI_CART_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_create_ pmpi_cart_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_create_ pmpi_cart_create
#else
#define mpi_cart_create_ pmpi_cart_create_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_cart_create_ MPI_CART_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_cart_create_ mpi_cart_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_cart_create_ mpi_cart_create
#endif
#endif


/*
MPI_Cart_create - Make a new communicator to which topology information
                  has been attached

*/
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_cart_create_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
			MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_cart_create_ (MPI_Fint *comm_old, MPI_Fint *ndims, MPI_Fint *dims, MPI_Fint *periods, MPI_Fint *reorder, MPI_Fint *comm_cart, 
		      MPI_Fint *ierr ) 
{
    MPI_Comm l_comm_cart;
    int lperiods[20];
    int ldims[20];
    int i;
    static char myname[] = "MPI_CART_CREATE";

    if ((int)*ndims > 20) {
	struct MPIR_COMMUNICATOR *comm_old_ptr;
	comm_old_ptr = MPIR_GET_COMM_PTR(*comm_old);
	*ierr = MPIR_Err_setmsg( MPI_ERR_DIMS, MPIR_ERR_DIMS_TOOLARGE,
			 myname, (char *)0, (char *)0, (int)*ndims, 20 );
	*ierr = MPIR_ERROR( comm_old_ptr, *ierr, myname );
	return;
	}
    for (i=0; i<(int)*ndims; i++) {
	lperiods[i] = MPIR_FROM_FLOG(periods[i]);
	ldims[i] = (int)dims[i];
    }
#if defined(_TWO_WORD_FCD)
    int tmp = *reorder;
    *ierr = MPI_Cart_create( MPI_Comm_f2c(*comm_old), 
			     (int)*ndims, ldims, 
			     lperiods, MPIR_FROM_FLOG(tmp), 
			     &l_comm_cart);
#else
    *ierr = MPI_Cart_create( MPI_Comm_f2c(*comm_old), 
			     (int)*ndims, ldims, 
			     lperiods, MPIR_FROM_FLOG(*reorder), 
			     &l_comm_cart);
#endif
    if (*ierr == MPI_SUCCESS) 		     
        *comm_cart = MPI_Comm_c2f(l_comm_cart);
}
