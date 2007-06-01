/* attr_getval.c */
/* THIS IS A CUSTOM WRAPPER */

#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ATTR_GET = PMPI_ATTR_GET
void MPI_ATTR_GET ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_attr_get__ = pmpi_attr_get__
void mpi_attr_get__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_attr_get = pmpi_attr_get
void mpi_attr_get ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_attr_get_ = pmpi_attr_get_
void mpi_attr_get_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ATTR_GET  MPI_ATTR_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_attr_get__  mpi_attr_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_attr_get  mpi_attr_get
#else
#pragma _HP_SECONDARY_DEF pmpi_attr_get_  mpi_attr_get_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ATTR_GET as PMPI_ATTR_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_attr_get__ as pmpi_attr_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_attr_get as pmpi_attr_get
#else
#pragma _CRI duplicate mpi_attr_get_ as pmpi_attr_get_
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
#define mpi_attr_get_ PMPI_ATTR_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_attr_get_ pmpi_attr_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_attr_get_ pmpi_attr_get
#else
#define mpi_attr_get_ pmpi_attr_get_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_attr_get_ MPI_ATTR_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_attr_get_ mpi_attr_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_attr_get_ mpi_attr_get
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_attr_get_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                               MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_attr_get_ ( MPI_Fint *comm, MPI_Fint *keyval, MPI_Fint *attr_value, MPI_Fint *found, MPI_Fint *__ierr )
{
    void *vval;
    int  l_found;

    *__ierr = MPI_Attr_get( MPI_Comm_f2c(*comm), (int)*keyval, &vval,
                            &l_found);

    /* Convert attribute value to integer.  This code handles the case
       where sizeof(int) < sizeof(void *), and the value was stored as a
       void * 
     */
    if ((int)*__ierr || l_found == 0)
	*attr_value = 0;
    else {
	MPI_Aint lvval = (MPI_Aint)vval;
	*attr_value = (int)lvval;
    }

    *found = MPIR_TO_FLOG(l_found);
    return;
}
