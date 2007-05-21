/* graph_get.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GRAPH_GET = PMPI_GRAPH_GET
void MPI_GRAPH_GET ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_graph_get__ = pmpi_graph_get__
void mpi_graph_get__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_graph_get = pmpi_graph_get
void mpi_graph_get ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_graph_get_ = pmpi_graph_get_
void mpi_graph_get_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GRAPH_GET  MPI_GRAPH_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_graph_get__  mpi_graph_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_graph_get  mpi_graph_get
#else
#pragma _HP_SECONDARY_DEF pmpi_graph_get_  mpi_graph_get_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GRAPH_GET as PMPI_GRAPH_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_graph_get__ as pmpi_graph_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_graph_get as pmpi_graph_get
#else
#pragma _CRI duplicate mpi_graph_get_ as pmpi_graph_get_
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
#define mpi_graph_get_ PMPI_GRAPH_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_graph_get_ pmpi_graph_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_graph_get_ pmpi_graph_get
#else
#define mpi_graph_get_ pmpi_graph_get_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_graph_get_ MPI_GRAPH_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_graph_get_ mpi_graph_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_graph_get_ mpi_graph_get
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_graph_get_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_graph_get_ ( MPI_Fint *comm, MPI_Fint *maxindex, MPI_Fint *maxedges, MPI_Fint *index, MPI_Fint *edges, MPI_Fint *__ierr )
{
    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Graph_get( MPI_Comm_f2c(*comm), *maxindex,
                                 *maxedges, index, edges);
    else {
        int *lindex;
        int *ledges;
        int i;

	MPIR_FALLOC(lindex,(int*)MALLOC(sizeof(int)* (int)*maxindex),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Graph_get");
	MPIR_FALLOC(ledges,(int*)MALLOC(sizeof(int)* (int)*maxedges),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Graph_get");

        *__ierr = MPI_Graph_get( MPI_Comm_f2c(*comm), (int)*maxindex,
                                 (int)*maxedges, lindex, ledges);
        for (i=0; i<*maxindex; i++)
	    index[i] = (MPI_Fint)lindex[i];
        for (i=0; i<*maxedges; i++)
	    edges[i] = (MPI_Fint)ledges[i];

	FREE( lindex );
	FREE( ledges );
    }

}
