/* group_incl.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GROUP_INCL = PMPI_GROUP_INCL
void MPI_GROUP_INCL ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_group_incl__ = pmpi_group_incl__
void mpi_group_incl__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_group_incl = pmpi_group_incl
void mpi_group_incl ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_group_incl_ = pmpi_group_incl_
void mpi_group_incl_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GROUP_INCL  MPI_GROUP_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_incl__  mpi_group_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_incl  mpi_group_incl
#else
#pragma _HP_SECONDARY_DEF pmpi_group_incl_  mpi_group_incl_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GROUP_INCL as PMPI_GROUP_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_group_incl__ as pmpi_group_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_group_incl as pmpi_group_incl
#else
#pragma _CRI duplicate mpi_group_incl_ as pmpi_group_incl_
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
#define mpi_group_incl_ PMPI_GROUP_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_incl_ pmpi_group_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_incl_ pmpi_group_incl
#else
#define mpi_group_incl_ pmpi_group_incl_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_group_incl_ MPI_GROUP_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_incl_ mpi_group_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_incl_ mpi_group_incl
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_group_incl_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                 MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_group_incl_ ( MPI_Fint *group, MPI_Fint *n, MPI_Fint *ranks, MPI_Fint *group_out, MPI_Fint *__ierr )
{
    MPI_Group l_group_out;

#ifdef FINT_IS_INT
    *__ierr = MPI_Group_incl( MPI_Group_f2c(*group), *n,
			      (int *)ranks, &l_group_out );
#else
#ifdef FINT_TYPE_UNKNOWN
    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Group_incl( MPI_Group_f2c(*group), *n,
                                  (int *)ranks, &l_group_out );
    else 
#endif
      {
	int *l_ranks;
	int i;

	MPIR_FALLOC(l_ranks,(int*)MALLOC(sizeof(int)* (int)*n),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Group_incl");
	for (i=0; i<*n; i++)
	    l_ranks[i] = (int)ranks[i];

        *__ierr = MPI_Group_incl( MPI_Group_f2c(*group), (int)*n,
                                  l_ranks, &l_group_out );
	
	FREE( l_ranks );
    }
#endif
    if (*__ierr == MPI_SUCCESS) 		     
        *group_out = MPI_Group_c2f(l_group_out);
}
