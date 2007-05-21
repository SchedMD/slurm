/* 
 *   $Id: darrayf.c,v 1.6 2001/12/12 23:36:32 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_CREATE_DARRAY = PMPI_TYPE_CREATE_DARRAY
void MPI_TYPE_CREATE_DARRAY (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_create_darray__ = pmpi_type_create_darray__
void mpi_type_create_darray__ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_create_darray = pmpi_type_create_darray
void mpi_type_create_darray (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_type_create_darray_ = pmpi_type_create_darray_
void mpi_type_create_darray_ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_CREATE_DARRAY  MPI_TYPE_CREATE_DARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_create_darray__  mpi_type_create_darray__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_create_darray  mpi_type_create_darray
#else
#pragma _HP_SECONDARY_DEF pmpi_type_create_darray_  mpi_type_create_darray_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_CREATE_DARRAY as PMPI_TYPE_CREATE_DARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_create_darray__ as pmpi_type_create_darray__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_create_darray as pmpi_type_create_darray
#else
#pragma _CRI duplicate mpi_type_create_darray_ as pmpi_type_create_darray_
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
#define mpi_type_create_darray_ PMPI_TYPE_CREATE_DARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_create_darray_ pmpi_type_create_darray__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_create_darray_ pmpi_type_create_darray
#else
#define mpi_type_create_darray_ pmpi_type_create_darray_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_create_darray_ MPI_TYPE_CREATE_DARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_create_darray_ mpi_type_create_darray__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_create_darray_ mpi_type_create_darray
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_create_darray_ (MPI_Fint *, MPI_Fint *, MPI_Fint *,
					MPI_Fint *, MPI_Fint *, MPI_Fint *,
					MPI_Fint *, MPI_Fint *, MPI_Fint *,
					MPI_Fint *, MPI_Fint *);

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_type_create_darray_(MPI_Fint *size, MPI_Fint *rank, 
			     MPI_Fint *ndims,
                             MPI_Fint *array_of_gsizes,
			     MPI_Fint *array_of_distribs,
			     MPI_Fint *array_of_dargs,
			     MPI_Fint *array_of_psizes, MPI_Fint *order, 
			     MPI_Fint *oldtype, MPI_Fint *newtype, 
			     MPI_Fint *__ierr )
{
    int i;
    int *l_array_of_gsizes = 0; /* to suppress warnings */
    int local_l_array_of_gsizes[MPIR_USE_LOCAL_ARRAY];
    int *l_array_of_distribs = 0;
    int local_l_array_of_distribs[MPIR_USE_LOCAL_ARRAY];
    int *l_array_of_dargs = 0;
    int local_l_array_of_dargs[MPIR_USE_LOCAL_ARRAY];
    int *l_array_of_psizes = 0;
    int local_l_array_of_psizes[MPIR_USE_LOCAL_ARRAY];
    MPI_Datatype oldtype_c, newtype_c;

    oldtype_c = MPI_Type_f2c(*oldtype);

    if ((int)*ndims > 0) {
	if ((int)*ndims > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(l_array_of_gsizes,(int *) MALLOC( *ndims * sizeof(int) ), 
			MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
			"MPI_TYPE_CREATE_DARRAY" );

	    MPIR_FALLOC(l_array_of_distribs,(int *) MALLOC( *ndims * sizeof(int) ), 
			MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
			"MPI_TYPE_CREATE_DARRAY" );

	    MPIR_FALLOC(l_array_of_dargs,(int *) MALLOC( *ndims * sizeof(int) ), 
			MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
			"MPI_TYPE_CREATE_DARRAY" );

	    MPIR_FALLOC(l_array_of_psizes,(int *) MALLOC( *ndims * sizeof(int) ), 
			MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
			"MPI_TYPE_CREATE_DARRAY" );
	}
	else {
	    l_array_of_gsizes = local_l_array_of_gsizes;
	    l_array_of_distribs = local_l_array_of_distribs;
	    l_array_of_dargs = local_l_array_of_dargs;
	    l_array_of_psizes = local_l_array_of_psizes;
	}

	for (i=0; i<(int)*ndims; i++) {
	    l_array_of_gsizes[i] = (int)array_of_gsizes[i];
	    l_array_of_distribs[i] = (int)array_of_distribs[i];
	    l_array_of_dargs[i] = (int)array_of_dargs[i];
	    l_array_of_psizes[i] = (int)array_of_psizes[i];
	}
    }

    *__ierr = MPI_Type_create_darray((int)*size, (int)*rank, (int)*ndims,
				     l_array_of_gsizes, l_array_of_distribs,
				     l_array_of_dargs, l_array_of_psizes,
				     (int)*order, oldtype_c, &newtype_c);

    if ((int)*ndims > MPIR_USE_LOCAL_ARRAY) {
	FREE( l_array_of_gsizes );
	FREE( l_array_of_distribs );
	FREE( l_array_of_dargs );
	FREE( l_array_of_psizes );
    }
    if (*__ierr == MPI_SUCCESS) 		     
        *newtype = MPI_Type_c2f(newtype_c);
}

