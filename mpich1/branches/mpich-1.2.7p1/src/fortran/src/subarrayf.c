/* 
 *   $Id: subarrayf.c,v 1.7 2001/12/12 23:36:50 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_CREATE_SUBARRAY = PMPI_TYPE_CREATE_SUBARRAY
void MPI_TYPE_CREATE_SUBARRAY (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_create_subarray__ = pmpi_type_create_subarray__
void mpi_type_create_subarray__ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_create_subarray = pmpi_type_create_subarray
void mpi_type_create_subarray (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_type_create_subarray_ = pmpi_type_create_subarray_
void mpi_type_create_subarray_ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_CREATE_SUBARRAY  MPI_TYPE_CREATE_SUBARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_create_subarray__  mpi_type_create_subarray__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_create_subarray  mpi_type_create_subarray
#else
#pragma _HP_SECONDARY_DEF pmpi_type_create_subarray_  mpi_type_create_subarray_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_CREATE_SUBARRAY as PMPI_TYPE_CREATE_SUBARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_create_subarray__ as pmpi_type_create_subarray__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_create_subarray as pmpi_type_create_subarray
#else
#pragma _CRI duplicate mpi_type_create_subarray_ as pmpi_type_create_subarray_
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
#define mpi_type_create_subarray_ PMPI_TYPE_CREATE_SUBARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_create_subarray_ pmpi_type_create_subarray__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_create_subarray_ pmpi_type_create_subarray
#else
#define mpi_type_create_subarray_ pmpi_type_create_subarray_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_create_subarray_ MPI_TYPE_CREATE_SUBARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_create_subarray_ mpi_type_create_subarray__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_create_subarray_ mpi_type_create_subarray
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_create_subarray_ (MPI_Fint *, MPI_Fint *, MPI_Fint *,
					  MPI_Fint *, MPI_Fint *, MPI_Fint *,
					  MPI_Fint *, MPI_Fint * );

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_type_create_subarray_(MPI_Fint *ndims, MPI_Fint *array_of_sizes,
                               MPI_Fint *array_of_subsizes,
			       MPI_Fint *array_of_starts, MPI_Fint *order,
			       MPI_Fint *oldtype, MPI_Fint *newtype, 
			       MPI_Fint *__ierr )
{
    int i;
    int *l_array_of_sizes = 0;
    int local_l_array_of_sizes[MPIR_USE_LOCAL_ARRAY];
    int *l_array_of_subsizes = 0;
    int local_l_array_of_subsizes[MPIR_USE_LOCAL_ARRAY];
    int *l_array_of_starts = 0;
    int local_l_array_of_starts[MPIR_USE_LOCAL_ARRAY];
    MPI_Datatype oldtype_c, newtype_c;

    oldtype_c = MPI_Type_f2c(*oldtype);

    if ((int)*ndims > 0) {
	if ((int)*ndims > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(l_array_of_sizes,(int *) MALLOC( *ndims * sizeof(int) ), 
			MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
			"MPI_TYPE_CREATE_SUBARRAY" );

	    MPIR_FALLOC(l_array_of_subsizes,(int *) MALLOC( *ndims * sizeof(int) ), 
			MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
			"MPI_TYPE_CREATE_SUBARRAY" );

	    MPIR_FALLOC(l_array_of_starts,(int *) MALLOC( *ndims * sizeof(int) ), 
			MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
			"MPI_TYPE_CREATE_SUBARRAY" );

	}
	else {
	    l_array_of_sizes = local_l_array_of_sizes;
	    l_array_of_subsizes = local_l_array_of_subsizes;
	    l_array_of_starts = local_l_array_of_starts;
	}

	for (i=0; i<(int)*ndims; i++) {
	    l_array_of_sizes[i] = (int)array_of_sizes[i];
	    l_array_of_subsizes[i] = (int)array_of_subsizes[i];
	    l_array_of_starts[i] = (int)array_of_starts[i];
	}
    }

    *__ierr = MPI_Type_create_subarray((int)*ndims, l_array_of_sizes,
				       l_array_of_subsizes, l_array_of_starts,
				       (int)*order, oldtype_c, &newtype_c);

    if ((int)*ndims > MPIR_USE_LOCAL_ARRAY) {
	FREE( l_array_of_sizes );
	FREE( l_array_of_subsizes );
	FREE( l_array_of_starts );
    }

    if (*__ierr == MPI_SUCCESS) 
        *newtype = MPI_Type_c2f(newtype_c);
}

