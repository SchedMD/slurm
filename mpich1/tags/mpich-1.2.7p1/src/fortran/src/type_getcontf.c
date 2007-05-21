/* type_get_cont.c */
/* Fortran interface file */

#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_GET_CONTENTS = PMPI_TYPE_GET_CONTENTS
void MPI_TYPE_GET_CONTENTS (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_get_contents__ = pmpi_type_get_contents__
void mpi_type_get_contents__ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_get_contents = pmpi_type_get_contents
void mpi_type_get_contents (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_type_get_contents_ = pmpi_type_get_contents_
void mpi_type_get_contents_ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_GET_CONTENTS  MPI_TYPE_GET_CONTENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_get_contents__  mpi_type_get_contents__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_get_contents  mpi_type_get_contents
#else
#pragma _HP_SECONDARY_DEF pmpi_type_get_contents_  mpi_type_get_contents_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_GET_CONTENTS as PMPI_TYPE_GET_CONTENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_get_contents__ as pmpi_type_get_contents__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_get_contents as pmpi_type_get_contents
#else
#pragma _CRI duplicate mpi_type_get_contents_ as pmpi_type_get_contents_
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
#define mpi_type_get_contents_ PMPI_TYPE_GET_CONTENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_get_contents_ pmpi_type_get_contents__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_get_contents_ pmpi_type_get_contents
#else
#define mpi_type_get_contents_ pmpi_type_get_contents_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_get_contents_ MPI_TYPE_GET_CONTENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_get_contents_ mpi_type_get_contents__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_get_contents_ mpi_type_get_contents
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_get_contents_ (MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                       MPI_Fint *, MPI_Fint *, MPI_Fint *,
				       MPI_Fint *, MPI_Fint *);

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_type_get_contents_(MPI_Fint *datatype, MPI_Fint *max_integers, MPI_Fint *max_addresses, MPI_Fint *max_datatypes, 
										   MPI_Fint *array_of_integers, MPI_Fint *array_of_addresses, MPI_Fint *array_of_datatypes, MPI_Fint *__ierr )
{
    int i;
    int *l_array_of_integers;
    MPI_Aint *l_array_of_addresses;
    MPI_Datatype *l_array_of_datatypes;
    
    MPIR_FALLOC(l_array_of_integers, (int*)MALLOC(sizeof(int)* (int)*
		max_integers), MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		"MPI_TYPE_GET_CONTENTS");

    MPIR_FALLOC(l_array_of_addresses, (MPI_Aint *)MALLOC(sizeof(MPI_Aint) *
		(int)*max_addresses), MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		"MPI_TYPE_GET_CONTENTS");

    MPIR_FALLOC(l_array_of_datatypes, (MPI_Datatype *)MALLOC(sizeof
               (MPI_Datatype) * (int)*max_datatypes), MPIR_COMM_WORLD, 
		MPI_ERR_EXHAUSTED, "MPI_TYPE_GET_CONTENTS");
    
    *__ierr = MPI_Type_get_contents( MPI_Type_f2c(*datatype), 
				     (int)*max_integers, 
				     (int)*max_addresses,
				     (int)*max_datatypes, 
				     l_array_of_integers,
				     l_array_of_addresses,
				     l_array_of_datatypes );

    for (i=0; i<(int)*max_integers; i++)
	array_of_integers[i] = (MPI_Fint)l_array_of_integers[i];
    for (i=0; i<(int)*max_addresses; i++)
	array_of_addresses[i] = (MPI_Aint)l_array_of_addresses[i];
    if (*__ierr == MPI_SUCCESS) 
        for (i=0; i<(int)*max_datatypes; i++)
            array_of_datatypes[i] = MPI_Type_c2f(l_array_of_datatypes[i]);

    FREE( l_array_of_integers );
    FREE( l_array_of_addresses );
    FREE( l_array_of_datatypes );
}

