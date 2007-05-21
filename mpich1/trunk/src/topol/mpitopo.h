#ifndef MPIR_GRAPH_TOPOL_COOKIE

extern int MPIR_TOPOLOGY_KEYVAL;  /* Keyval for topology information */
extern void *MPIR_topo_els;       /* sbcnst topology elements */ 

#define MPIR_GRAPH_TOPOL_COOKIE 0x0101beaf
typedef struct {
  int type;
  MPIR_COOKIE
  int nnodes;
  int nedges;
  int *index;
  int *edges;
} MPIR_GRAPH_TOPOLOGY;

#define MPIR_CART_TOPOL_COOKIE 0x0102beaf
typedef struct {
  int type;
  MPIR_COOKIE
  int nnodes;
  int ndims;
  int *dims;
  int *periods;
  int *position;
} MPIR_CART_TOPOLOGY;

typedef union {
  int type;
  MPIR_GRAPH_TOPOLOGY  graph;
  MPIR_CART_TOPOLOGY   cart;
} MPIR_TOPOLOGY;


int MPIR_Topology_copy_fn ( MPI_Comm, int, void *, void *, void *, int * );
int MPIR_Topology_delete_fn ( MPI_Comm, int, void *, void * );
void MPIR_Topology_finalize ( void );
#endif
