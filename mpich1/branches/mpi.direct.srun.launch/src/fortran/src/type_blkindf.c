/* type_blkind.c */
/* Custom Fortran interface file */

/*
* This file was generated automatically by bfort from the C source
* file.  
 */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_CREATE_INDEXED_BLOCK = PMPI_TYPE_CREATE_INDEXED_BLOCK
void MPI_TYPE_CREATE_INDEXED_BLOCK (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_create_indexed_block__ = pmpi_type_create_indexed_block__
void mpi_type_create_indexed_block__ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_create_indexed_block = pmpi_type_create_indexed_block
void mpi_type_create_indexed_block (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_type_create_indexed_block_ = pmpi_type_create_indexed_block_
void mpi_type_create_indexed_block_ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_CREATE_INDEXED_BLOCK  MPI_TYPE_CREATE_INDEXED_BLOCK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_create_indexed_block__  mpi_type_create_indexed_block__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_create_indexed_block  mpi_type_create_indexed_block
#else
#pragma _HP_SECONDARY_DEF pmpi_type_create_indexed_block_  mpi_type_create_indexed_block_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_CREATE_INDEXED_BLOCK as PMPI_TYPE_CREATE_INDEXED_BLOCK
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_create_indexed_block__ as pmpi_type_create_indexed_block__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_create_indexed_block as pmpi_type_create_indexed_block
#else
#pragma _CRI duplicate mpi_type_create_indexed_block_ as pmpi_type_create_indexed_block_
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
#define mpi_type_create_indexed_block_ PMPI_TYPE_CREATE_INDEXED_BLOCK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_create_indexed_block_ pmpi_type_create_indexed_block__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_create_indexed_block_ pmpi_type_create_indexed_block
#else
#define mpi_type_create_indexed_block_ pmpi_type_create_indexed_block_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_create_indexed_block_ MPI_TYPE_CREATE_INDEXED_BLOCK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_create_indexed_block_ mpi_type_create_indexed_block__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_create_indexed_block_ mpi_type_create_indexed_block
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_create_indexed_block_ (MPI_Fint *, MPI_Fint *, 
					       MPI_Fint *, MPI_Fint *,
					       MPI_Fint *, MPI_Fint *);
/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_type_create_indexed_block_( MPI_Fint *count, MPI_Fint *blocklength, 
				     MPI_Fint array_of_displacements[], MPI_Fint *old_type, 
				     MPI_Fint *newtype, MPI_Fint *__ierr )
{

    int i;
    int *l_array_of_displacements = 0;
    int local_l_array_of_displacements[MPIR_USE_LOCAL_ARRAY];
    MPI_Datatype lnewtype;

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(l_array_of_displacements,(int *) MALLOC( *count * 
			sizeof(int) ), MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
			"MPI_TYPE_CREATE_INDEXED_BLOCK");
	}
	else {
	    l_array_of_displacements = local_l_array_of_displacements;
	}

	for (i=0; i<(int)*count; i++)
	    l_array_of_displacements[i] = (int)(array_of_displacements[i]);
    }

    *__ierr = MPI_Type_create_indexed_block((int)*count, (int)*blocklength,
					l_array_of_displacements,
					MPI_Type_c2f( *old_type ),&lnewtype);

    if ((int)*count > MPIR_USE_LOCAL_ARRAY) 
	FREE( l_array_of_displacements );

    if (*__ierr == MPI_SUCCESS) 
        *newtype = MPI_Type_c2f( lnewtype );
}




