/* group_tranks.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GROUP_TRANSLATE_RANKS = PMPI_GROUP_TRANSLATE_RANKS
void MPI_GROUP_TRANSLATE_RANKS ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_group_translate_ranks__ = pmpi_group_translate_ranks__
void mpi_group_translate_ranks__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_group_translate_ranks = pmpi_group_translate_ranks
void mpi_group_translate_ranks ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_group_translate_ranks_ = pmpi_group_translate_ranks_
void mpi_group_translate_ranks_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GROUP_TRANSLATE_RANKS  MPI_GROUP_TRANSLATE_RANKS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_translate_ranks__  mpi_group_translate_ranks__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_translate_ranks  mpi_group_translate_ranks
#else
#pragma _HP_SECONDARY_DEF pmpi_group_translate_ranks_  mpi_group_translate_ranks_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GROUP_TRANSLATE_RANKS as PMPI_GROUP_TRANSLATE_RANKS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_group_translate_ranks__ as pmpi_group_translate_ranks__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_group_translate_ranks as pmpi_group_translate_ranks
#else
#pragma _CRI duplicate mpi_group_translate_ranks_ as pmpi_group_translate_ranks_
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
#define mpi_group_translate_ranks_ PMPI_GROUP_TRANSLATE_RANKS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_translate_ranks_ pmpi_group_translate_ranks__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_translate_ranks_ pmpi_group_translate_ranks
#else
#define mpi_group_translate_ranks_ pmpi_group_translate_ranks_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_group_translate_ranks_ MPI_GROUP_TRANSLATE_RANKS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_translate_ranks_ mpi_group_translate_ranks__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_translate_ranks_ mpi_group_translate_ranks
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_group_translate_ranks_ ( MPI_Fint *, MPI_Fint *, 
                                            MPI_Fint *, MPI_Fint *, 
                                            MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_group_translate_ranks_ ( MPI_Fint *group_a, 
     MPI_Fint *n, MPI_Fint *ranks_a, MPI_Fint *group_b, MPI_Fint *ranks_b,
				  MPI_Fint *__ierr )
{

    if (sizeof(MPI_Fint) == sizeof(int)) {
	/* We cast ranges here in case MPI_Fint != int and the compiler
	   wants to complain...*/
        *__ierr = MPI_Group_translate_ranks(MPI_Group_f2c(*group_a),*n,
                                            (int *)ranks_a,
                                            MPI_Group_f2c(*group_b), 
                                            (int *)ranks_b);
    }
    else {
        int *l_ranks_a;
        int *l_ranks_b;
        int i;

	MPIR_FALLOC(l_ranks_a,(int*)MALLOC(sizeof(int)* (int)*n),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Group_translate_ranks");

	MPIR_FALLOC(l_ranks_b,(int*)MALLOC(sizeof(int)* *(n)),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Group_translate_ranks");

        for (i=0; i<(int)*n; i++) {
            l_ranks_a[i] = (int)ranks_a[i];
    } 
        *__ierr = MPI_Group_translate_ranks(MPI_Group_f2c(*group_a),(int)*n,
                                            l_ranks_a,
                                            MPI_Group_f2c(*group_b), 
                                            l_ranks_b);
        for (i=0; i<(int)*n; i++) {
            ranks_b[i] = (MPI_Fint)(l_ranks_b[i]);
        }
	FREE( l_ranks_a );
	FREE( l_ranks_b );
    }
    
}
