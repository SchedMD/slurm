/* group_rexcl.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GROUP_RANGE_EXCL = PMPI_GROUP_RANGE_EXCL
void MPI_GROUP_RANGE_EXCL ( MPI_Fint *, MPI_Fint *, MPI_Fint [][3], MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_group_range_excl__ = pmpi_group_range_excl__
void mpi_group_range_excl__ ( MPI_Fint *, MPI_Fint *, MPI_Fint [][3], MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_group_range_excl = pmpi_group_range_excl
void mpi_group_range_excl ( MPI_Fint *, MPI_Fint *, MPI_Fint [][3], MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_group_range_excl_ = pmpi_group_range_excl_
void mpi_group_range_excl_ ( MPI_Fint *, MPI_Fint *, MPI_Fint [][3], MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GROUP_RANGE_EXCL  MPI_GROUP_RANGE_EXCL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_range_excl__  mpi_group_range_excl__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_range_excl  mpi_group_range_excl
#else
#pragma _HP_SECONDARY_DEF pmpi_group_range_excl_  mpi_group_range_excl_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GROUP_RANGE_EXCL as PMPI_GROUP_RANGE_EXCL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_group_range_excl__ as pmpi_group_range_excl__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_group_range_excl as pmpi_group_range_excl
#else
#pragma _CRI duplicate mpi_group_range_excl_ as pmpi_group_range_excl_
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
#define mpi_group_range_excl_ PMPI_GROUP_RANGE_EXCL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_range_excl_ pmpi_group_range_excl__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_range_excl_ pmpi_group_range_excl
#else
#define mpi_group_range_excl_ pmpi_group_range_excl_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_group_range_excl_ MPI_GROUP_RANGE_EXCL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_range_excl_ mpi_group_range_excl__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_range_excl_ mpi_group_range_excl
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_group_range_excl_ ( MPI_Fint *, MPI_Fint *, 
                                       MPI_Fint [][3], MPI_Fint *, 
                                       MPI_Fint * );

/* See the comments in group_rinclf.c.  ranges is correct without changes */
FORTRAN_API void FORT_CALL mpi_group_range_excl_ ( MPI_Fint *group, MPI_Fint *n, MPI_Fint ranges[][3], MPI_Fint *newgroup, MPI_Fint *__ierr )
{
    MPI_Group l_newgroup;

#ifdef FINT_IS_INT
    *__ierr = MPI_Group_range_excl(MPI_Group_f2c(*group),*n,
				   (int(*)[3])ranges, &l_newgroup);
#else
#ifdef FINT_TYPE_UNKNOWN
    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Group_range_excl(MPI_Group_f2c(*group),*n,
                                       (int(*)[3])ranges, &l_newgroup);
    else 
#endif
    {
	int *l_ranges;
	int i;
	int j = 0;

        MPIR_FALLOC(l_ranges,(int*)MALLOC(sizeof(int)* ((int)*n * 3)),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Group_range_excl");

        for (i=0; i<*n; i++) {
	    l_ranges[j++] = (int)ranges[i][0];
	    l_ranges[j++] = (int)ranges[i][1];
	    l_ranges[j++] = (int)ranges[i][2];
	}
	
        *__ierr = MPI_Group_range_excl(MPI_Group_f2c(*group), (int)*n,
                                       (int (*)[3])l_ranges, &l_newgroup);
	FREE( l_ranges );
	
    }
#endif
    if (*__ierr == MPI_SUCCESS) 		     
        *newgroup = MPI_Group_c2f(l_newgroup);
}
