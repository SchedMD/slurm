/* opcreate.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_OP_CREATE = PMPI_OP_CREATE
#ifdef  FORTRAN_SPECIAL_FUNCTION_PTR
void MPI_OP_CREATE ( MPI_User_function **, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
void MPI_OP_CREATE ( MPI_User_function *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_op_create__ = pmpi_op_create__
#ifdef  FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_op_create__ ( MPI_User_function **, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
void mpi_op_create__ ( MPI_User_function *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_op_create = pmpi_op_create
#ifdef  FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_op_create ( MPI_User_function **, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
void mpi_op_create ( MPI_User_function *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif
#else
#pragma weak mpi_op_create_ = pmpi_op_create_
#ifdef  FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_op_create_ ( MPI_User_function **, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
void mpi_op_create_ ( MPI_User_function *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_OP_CREATE  MPI_OP_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_op_create__  mpi_op_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_op_create  mpi_op_create
#else
#pragma _HP_SECONDARY_DEF pmpi_op_create_  mpi_op_create_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_OP_CREATE as PMPI_OP_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_op_create__ as pmpi_op_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_op_create as pmpi_op_create
#else
#pragma _CRI duplicate mpi_op_create_ as pmpi_op_create_
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
#define mpi_op_create_ PMPI_OP_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_op_create_ pmpi_op_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_op_create_ pmpi_op_create
#else
#define mpi_op_create_ pmpi_op_create_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_op_create_ MPI_OP_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_op_create_ mpi_op_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_op_create_ mpi_op_create
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
#ifdef  FORTRAN_SPECIAL_FUNCTION_PTR
FORTRAN_API void FORT_CALL mpi_op_create_ ( MPI_User_function **, MPI_Fint *, MPI_Fint *, 
				MPI_Fint * );
#else
FORTRAN_API void FORT_CALL mpi_op_create_ ( MPI_User_function *, MPI_Fint *, MPI_Fint *,
                                MPI_Fint * );
#endif

FORTRAN_API void FORT_CALL mpi_op_create_(
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
	MPI_User_function **function,
#else
	MPI_User_function *function,
#endif
	MPI_Fint *commute, MPI_Fint *op, MPI_Fint *__ierr)
{

    MPI_Op l_op;

#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
    *__ierr = MPI_Op_create(*function,MPIR_FROM_FLOG((int)*commute),
                            &l_op);
#elif defined(_CRAY)
    /* FLOG requires something that it can take the address of */
    int tmp = *commute;
    *__ierr = MPI_Op_create(*function,MPIR_FROM_FLOG(tmp),&l_op);

#else
    *__ierr = MPI_Op_create(function,MPIR_FROM_FLOG((int)*commute),
                            &l_op);
#endif
    if (*__ierr == MPI_SUCCESS) 
        *op = MPI_Op_c2f(l_op);
}
