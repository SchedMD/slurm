/* type_ub.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_UB = PMPI_TYPE_UB
void MPI_TYPE_UB ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_ub__ = pmpi_type_ub__
void mpi_type_ub__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_ub = pmpi_type_ub
void mpi_type_ub ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_ub_ = pmpi_type_ub_
void mpi_type_ub_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_UB  MPI_TYPE_UB
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_ub__  mpi_type_ub__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_ub  mpi_type_ub
#else
#pragma _HP_SECONDARY_DEF pmpi_type_ub_  mpi_type_ub_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_UB as PMPI_TYPE_UB
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_ub__ as pmpi_type_ub__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_ub as pmpi_type_ub
#else
#pragma _CRI duplicate mpi_type_ub_ as pmpi_type_ub_
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
#define mpi_type_ub_ PMPI_TYPE_UB
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_ub_ pmpi_type_ub__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_ub_ pmpi_type_ub
#else
#define mpi_type_ub_ pmpi_type_ub_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_ub_ MPI_TYPE_UB
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_ub_ mpi_type_ub__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_ub_ mpi_type_ub
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_ub_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_ub_ ( MPI_Fint *datatype, MPI_Fint *displacement, MPI_Fint *__ierr )
{
    MPI_Aint c_displacement;

    *__ierr = MPI_Type_ub(MPI_Type_f2c(*datatype), &c_displacement);
    /* Should check for truncation */
    *displacement = (MPI_Fint)c_displacement;
}
