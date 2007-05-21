/* type_hind.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_HINDEXED = PMPI_TYPE_HINDEXED
void MPI_TYPE_HINDEXED ( MPI_Fint *, MPI_Fint [], MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_hindexed__ = pmpi_type_hindexed__
void mpi_type_hindexed__ ( MPI_Fint *, MPI_Fint [], MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_hindexed = pmpi_type_hindexed
void mpi_type_hindexed ( MPI_Fint *, MPI_Fint [], MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_hindexed_ = pmpi_type_hindexed_
void mpi_type_hindexed_ ( MPI_Fint *, MPI_Fint [], MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_HINDEXED  MPI_TYPE_HINDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_hindexed__  mpi_type_hindexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_hindexed  mpi_type_hindexed
#else
#pragma _HP_SECONDARY_DEF pmpi_type_hindexed_  mpi_type_hindexed_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_HINDEXED as PMPI_TYPE_HINDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_hindexed__ as pmpi_type_hindexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_hindexed as pmpi_type_hindexed
#else
#pragma _CRI duplicate mpi_type_hindexed_ as pmpi_type_hindexed_
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
#define mpi_type_hindexed_ PMPI_TYPE_HINDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_hindexed_ pmpi_type_hindexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_hindexed_ pmpi_type_hindexed
#else
#define mpi_type_hindexed_ pmpi_type_hindexed_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_hindexed_ MPI_TYPE_HINDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_hindexed_ mpi_type_hindexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_hindexed_ mpi_type_hindexed
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_hindexed_ ( MPI_Fint *, MPI_Fint [], MPI_Fint [], 
                                    MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_hindexed_( MPI_Fint *count, MPI_Fint blocklens[], MPI_Fint indices[], MPI_Fint *old_type, MPI_Fint *newtype, MPI_Fint *__ierr )
{
    MPI_Aint     *c_indices;
    MPI_Aint     local_c_indices[MPIR_USE_LOCAL_ARRAY];
    int          i, mpi_errno;
    int          *l_blocklens; 
    int          local_l_blocklens[MPIR_USE_LOCAL_ARRAY];
    MPI_Datatype ldatatype;
    static char  myname[] = "MPI_TYPE_HINDEXED";

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	/* We really only need to do this when 
	   sizeof(MPI_Aint) != sizeof(INTEGER) */
	    MPIR_FALLOC(c_indices,(MPI_Aint *) MALLOC( *count * sizeof(MPI_Aint) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );

	    MPIR_FALLOC(l_blocklens,(int *) MALLOC( *count * sizeof(int) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
	}
	else {
	    c_indices = local_c_indices;
	    l_blocklens = local_l_blocklens;
	}

	for (i=0; i<(int)*count; i++) {
	    c_indices[i] = (MPI_Aint) indices[i];
            l_blocklens[i] = (int) blocklens[i];
	}
	*__ierr = MPI_Type_hindexed((int)*count,l_blocklens,c_indices,
                                    MPI_Type_f2c(*old_type),
				    &ldatatype);
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    FREE( c_indices );
            FREE( l_blocklens );
	}
        if (*__ierr == MPI_SUCCESS) 
            *newtype = MPI_Type_c2f(ldatatype);
    }
    else if ((int)*count == 0) {
	*__ierr = MPI_SUCCESS;
        *newtype = 0;
    }
    else {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_COUNT, MPIR_ERR_DEFAULT, myname,
				     (char *)0, (char *)0, (int)(*count) );
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }
}
