/* type_get_env.c */
/* Fortran interface file */

/*
* This file was generated automatically by bfort from the C source
* file.  
 */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_GET_ENVELOPE = PMPI_TYPE_GET_ENVELOPE
void MPI_TYPE_GET_ENVELOPE (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_get_envelope__ = pmpi_type_get_envelope__
void mpi_type_get_envelope__ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_get_envelope = pmpi_type_get_envelope
void mpi_type_get_envelope (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_type_get_envelope_ = pmpi_type_get_envelope_
void mpi_type_get_envelope_ (MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_GET_ENVELOPE  MPI_TYPE_GET_ENVELOPE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_get_envelope__  mpi_type_get_envelope__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_get_envelope  mpi_type_get_envelope
#else
#pragma _HP_SECONDARY_DEF pmpi_type_get_envelope_  mpi_type_get_envelope_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_GET_ENVELOPE as PMPI_TYPE_GET_ENVELOPE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_get_envelope__ as pmpi_type_get_envelope__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_get_envelope as pmpi_type_get_envelope
#else
#pragma _CRI duplicate mpi_type_get_envelope_ as pmpi_type_get_envelope_
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
#define mpi_type_get_envelope_ PMPI_TYPE_GET_ENVELOPE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_get_envelope_ pmpi_type_get_envelope__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_get_envelope_ pmpi_type_get_envelope
#else
#define mpi_type_get_envelope_ pmpi_type_get_envelope_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_get_envelope_ MPI_TYPE_GET_ENVELOPE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_get_envelope_ mpi_type_get_envelope__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_get_envelope_ mpi_type_get_envelope
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_get_envelope_ (MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                       MPI_Fint *, MPI_Fint *, MPI_Fint *);

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_type_get_envelope_(MPI_Fint *datatype, MPI_Fint *num_integers, MPI_Fint *num_addresses, MPI_Fint *num_datatypes, MPI_Fint *combiner, MPI_Fint *__ierr )
{
    int l_num_integers;
    int l_num_addresses;
    int l_num_datatypes;
    int l_combiner;

*__ierr = MPI_Type_get_envelope(MPI_Type_f2c(*datatype), &l_num_integers,
				&l_num_addresses, &l_num_datatypes,
				&l_combiner);

    if (*__ierr != MPI_SUCCESS) return;
    *num_integers = l_num_integers;
    *num_addresses = l_num_addresses;
    *num_datatypes = l_num_datatypes;
    *combiner = l_combiner;

}

