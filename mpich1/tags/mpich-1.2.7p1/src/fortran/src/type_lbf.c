/* type_lb.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_TYPE_LB = PMPI_TYPE_LB
void MPI_TYPE_LB ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_type_lb__ = pmpi_type_lb__
void mpi_type_lb__ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_type_lb = pmpi_type_lb
void mpi_type_lb ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_type_lb_ = pmpi_type_lb_
void mpi_type_lb_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_TYPE_LB  MPI_TYPE_LB
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_lb__  mpi_type_lb__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_type_lb  mpi_type_lb
#else
#pragma _HP_SECONDARY_DEF pmpi_type_lb_  mpi_type_lb_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_TYPE_LB as PMPI_TYPE_LB
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_type_lb__ as pmpi_type_lb__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_type_lb as pmpi_type_lb
#else
#pragma _CRI duplicate mpi_type_lb_ as pmpi_type_lb_
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
#define mpi_type_lb_ PMPI_TYPE_LB
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_lb_ pmpi_type_lb__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_lb_ pmpi_type_lb
#else
#define mpi_type_lb_ pmpi_type_lb_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_type_lb_ MPI_TYPE_LB
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_type_lb_ mpi_type_lb__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_type_lb_ mpi_type_lb
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_type_lb_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_type_lb_ ( MPI_Fint *datatype, MPI_Fint *displacement, MPI_Fint *__ierr )
{
    MPI_Aint   c_displacement;
  
    *__ierr = MPI_Type_lb(MPI_Type_f2c(*datatype), &c_displacement);
    /* Should check for truncation */
    *displacement = (MPI_Fint)c_displacement;
}
