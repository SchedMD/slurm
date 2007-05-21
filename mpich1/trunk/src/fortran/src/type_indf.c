/* type_ind.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_INDEXED = PMPI_TYPE_INDEXED
void MPI_TYPE_INDEXED ( MPI_Fint *, MPI_Fint [], MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_indexed__ = pmpi_type_indexed__
void mpi_type_indexed__ ( MPI_Fint *, MPI_Fint [], MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_indexed = pmpi_type_indexed
void mpi_type_indexed ( MPI_Fint *, MPI_Fint [], MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_indexed_ = pmpi_type_indexed_
void mpi_type_indexed_ ( MPI_Fint *, MPI_Fint [], MPI_Fint [], MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_INDEXED  MPI_TYPE_INDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_indexed__  mpi_type_indexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_indexed  mpi_type_indexed
#else
#pragma _HP_SECONDARY_DEF pmpi_type_indexed_  mpi_type_indexed_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_INDEXED as PMPI_TYPE_INDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_indexed__ as pmpi_type_indexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_indexed as pmpi_type_indexed
#else
#pragma _CRI duplicate mpi_type_indexed_ as pmpi_type_indexed_
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
#define mpi_type_indexed_ PMPI_TYPE_INDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_indexed_ pmpi_type_indexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_indexed_ pmpi_type_indexed
#else
#define mpi_type_indexed_ pmpi_type_indexed_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_indexed_ MPI_TYPE_INDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_indexed_ mpi_type_indexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_indexed_ mpi_type_indexed
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_indexed_ ( MPI_Fint *, MPI_Fint [], MPI_Fint [], 
                                   MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_indexed_( MPI_Fint *count, MPI_Fint blocklens[], MPI_Fint indices[], MPI_Fint *old_type, MPI_Fint *newtype, MPI_Fint *__ierr )
{
    int          i;
    int          *l_blocklens = 0;
    int          local_l_blocklens[MPIR_USE_LOCAL_ARRAY];
    int          *l_indices = 0;
    int          local_l_indices[MPIR_USE_LOCAL_ARRAY];
    MPI_Datatype ldatatype;
    static char myname[] = "MPI_TYPE_INDEXED";

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(l_blocklens,(int *) MALLOC( *count * sizeof(int) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );

	    MPIR_FALLOC(l_indices,(int *) MALLOC( *count * sizeof(int) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
	}
	else {
	    l_blocklens = local_l_blocklens;
	    l_indices = local_l_indices;
	}

        for (i=0; i<(int)*count; i++) {
	    l_indices[i] = (int)indices[i];
	    l_blocklens[i] = (int)blocklens[i];
         }
    }
 
    *__ierr = MPI_Type_indexed((int)*count, l_blocklens, l_indices,
                               MPI_Type_f2c(*old_type), 
                               &ldatatype);
    if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
        FREE( l_indices );
        FREE( l_blocklens );
    }
    if (*__ierr == MPI_SUCCESS) 
        *newtype = MPI_Type_c2f(ldatatype);
}




