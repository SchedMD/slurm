/* pack_size.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_PACK_SIZE = PMPI_PACK_SIZE
void MPI_PACK_SIZE ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_pack_size__ = pmpi_pack_size__
void mpi_pack_size__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_pack_size = pmpi_pack_size
void mpi_pack_size ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_pack_size_ = pmpi_pack_size_
void mpi_pack_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_PACK_SIZE  MPI_PACK_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_pack_size__  mpi_pack_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_pack_size  mpi_pack_size
#else
#pragma _HP_SECONDARY_DEF pmpi_pack_size_  mpi_pack_size_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_PACK_SIZE as PMPI_PACK_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_pack_size__ as pmpi_pack_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_pack_size as pmpi_pack_size
#else
#pragma _CRI duplicate mpi_pack_size_ as pmpi_pack_size_
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
#define mpi_pack_size_ PMPI_PACK_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_pack_size_ pmpi_pack_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_pack_size_ pmpi_pack_size
#else
#define mpi_pack_size_ pmpi_pack_size_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_pack_size_ MPI_PACK_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_pack_size_ mpi_pack_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_pack_size_ mpi_pack_size
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_pack_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_pack_size_ ( MPI_Fint *incount, MPI_Fint *datatype, MPI_Fint *comm, MPI_Fint *size, MPI_Fint *__ierr )
{
    int lsize;

    *__ierr = MPI_Pack_size((int)*incount, MPI_Type_f2c(*datatype),
                            MPI_Comm_f2c(*comm), &lsize);
    *size = (MPI_Fint)lsize;
}
