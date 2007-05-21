/* dup_fn.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#undef MPI_DUP_FN

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_DUP_FN = PMPI_DUP_FN
void MPI_DUP_FN ( MPI_Fint, MPI_Fint *, void *, void **, void **, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_dup_fn__ = pmpi_dup_fn__
void mpi_dup_fn__ ( MPI_Fint, MPI_Fint *, void *, void **, void **, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_dup_fn = pmpi_dup_fn
void mpi_dup_fn ( MPI_Fint, MPI_Fint *, void *, void **, void **, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_dup_fn_ = pmpi_dup_fn_
void mpi_dup_fn_ ( MPI_Fint, MPI_Fint *, void *, void **, void **, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_DUP_FN  MPI_DUP_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_dup_fn__  mpi_dup_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_dup_fn  mpi_dup_fn
#else
#pragma _HP_SECONDARY_DEF pmpi_dup_fn_  mpi_dup_fn_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_DUP_FN as PMPI_DUP_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_dup_fn__ as pmpi_dup_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_dup_fn as pmpi_dup_fn
#else
#pragma _CRI duplicate mpi_dup_fn_ as pmpi_dup_fn_
#endif

/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#include "mpiprof.h"
#endif

#ifdef F77_NAME_UPPER
#define mpi_dup_fn_ PMPI_DUP_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_dup_fn_ pmpi_dup_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_dup_fn_ pmpi_dup_fn
#else
#define mpi_dup_fn_ pmpi_dup_fn_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_dup_fn_ MPI_DUP_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_dup_fn_ mpi_dup_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_dup_fn_ mpi_dup_fn
#endif
#endif

/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_dup_fn_ ( MPI_Fint, MPI_Fint *, void *, void **, 
				  void **,  MPI_Fint *, MPI_Fint * );

/* Fortran functions aren't quite the same */
FORTRAN_API void FORT_CALL mpi_dup_fn_ ( MPI_Fint comm, MPI_Fint *keyval,
				  void *extra_state, void **attr_in, 
				  void **attr_out, MPI_Fint *flag, 
				  MPI_Fint *ierr  )
{
    int l_flag;

    MPIR_dup_fn(MPI_Comm_f2c(comm), (int)*keyval, extra_state, *attr_in,
                attr_out, &l_flag);
    *flag = MPIR_TO_FLOG(l_flag);
    *ierr = MPI_SUCCESS;
}
