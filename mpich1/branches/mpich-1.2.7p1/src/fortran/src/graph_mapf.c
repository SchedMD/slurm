/* graph_map.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_GRAPH_MAP = PMPI_GRAPH_MAP
void MPI_GRAPH_MAP ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_graph_map__ = pmpi_graph_map__
void mpi_graph_map__ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_graph_map = pmpi_graph_map
void mpi_graph_map ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
#pragma weak mpi_graph_map_ = pmpi_graph_map_
void mpi_graph_map_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GRAPH_MAP  MPI_GRAPH_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_graph_map__  mpi_graph_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_graph_map  mpi_graph_map
#else
#pragma _HP_SECONDARY_DEF pmpi_graph_map_  mpi_graph_map_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GRAPH_MAP as PMPI_GRAPH_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_graph_map__ as pmpi_graph_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_graph_map as pmpi_graph_map
#else
#pragma _CRI duplicate mpi_graph_map_ as pmpi_graph_map_
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
#define mpi_graph_map_ PMPI_GRAPH_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_graph_map_ pmpi_graph_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_graph_map_ pmpi_graph_map
#else
#define mpi_graph_map_ pmpi_graph_map_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_graph_map_ MPI_GRAPH_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_graph_map_ mpi_graph_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_graph_map_ mpi_graph_map
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_graph_map_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                                MPI_Fint *, MPI_Fint *, MPI_Fint * );

FORTRAN_API void FORT_CALL mpi_graph_map_ ( MPI_Fint *comm_old, MPI_Fint *nnodes, MPI_Fint *index, MPI_Fint *edges, MPI_Fint *newrank, MPI_Fint *__ierr )
{

    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Graph_map( MPI_Comm_f2c(*comm_old), *nnodes,
                                 index, edges, newrank);
    else {
        int i;
        int *lindex;
        int *ledges;
        int lnewrank;
	int nedges;

        MPI_Graphdims_get(MPI_Comm_f2c(*comm_old), nnodes, &nedges);
	MPIR_FALLOC(lindex,(int*)MALLOC(sizeof(int)* (int)*nnodes),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Graph_map");
	MPIR_FALLOC(ledges,(int*)MALLOC(sizeof(int)* (int)nedges),
		    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		    "MPI_Graph_map");

        for (i=0; i<(int)*nnodes; i++)
	    lindex[i] = (int)index[i];

        for (i=0; i<nedges; i++)
	    ledges[i] = (int)edges[i];

        *__ierr = MPI_Graph_map( MPI_Comm_f2c(*comm_old), (int)*nnodes,
                                 lindex, ledges, &lnewrank);
        *newrank = (MPI_Fint)lnewrank;

	FREE( lindex );
	FREE( ledges );
    }
}
