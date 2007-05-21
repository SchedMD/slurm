/*

  Test for the MPI Graph routines :

  MPI_Graphdims_get
  MPI_Graph_create
  MPI_Graph_get
  MPI_Graph_map
  MPI_Graph_neighbors
  MPI_Graph_neighbors_count
*/


#include "mpi.h"
#include <stdio.h>
/* stdlib.h Needed for malloc declaration */
#include <stdlib.h>
#include "test.h"

void NumberEdges ( int **, int **, int, int, int );
void PrintGraph  ( int, int *, int * );

int main( int argc, char **argv )
{
    MPI_Comm comm, new_comm;
    int      reorder;
    int      nbrarray[3], baseindex;
    int      size, i, j, nnodes, nedges, q_nnodes, q_nedges, q_nnbrs, newrank;
    int      *index, *edges, *q_index, *q_edges, *rankbuf;
    int      worldrank, err = 0, toterr;

    MPI_Init( &argc, &argv );

    MPI_Comm_rank( MPI_COMM_WORLD, &worldrank );

/* Generate the graph for a binary tree.  
   
   Note that EVERY process must have the SAME data
   */
    comm = MPI_COMM_WORLD;
    MPI_Comm_size( comm, &size );

    index = (int *)malloc( (size + 1) * sizeof(int) );
    edges = (int *)malloc( (size + 1) * 3 * sizeof(int) );
    reorder = 0;
    for (i=0; i < size; i++) {
	index[i] = 0;
    }
    NumberEdges( &index, &edges, -1, 0, size - 1 );
    nedges= index[0];
    for (i=1; i<size; i++) {
	nedges += index[i];
	index[i] = index[i] + index[i-1];
    }
    nnodes = size;
#ifdef DEBUG
    PrintGraph( nnodes, index, edges );
#endif
    MPI_Graph_create( comm, nnodes, index, edges, reorder, &new_comm );

/* Now, try to get the information about this graph */
    MPI_Graphdims_get( new_comm, &q_nnodes, &q_nedges );
    if (q_nnodes != nnodes) {
	printf( "Wrong number of nodes, expected %d got %d\n", nnodes, q_nnodes );
	err++;
    }
    if (q_nedges != nedges) {
	printf( "Wrong number of edges; expected %d got %d\n", nedges, q_nedges );
	err++;
    }
    q_index = (int *)malloc( q_nnodes * sizeof(int) );
    q_edges = (int *)malloc( q_nedges * sizeof(int) );

    MPI_Graph_get( new_comm, q_nnodes, q_nedges, q_index, q_edges );

/* Check with original */
    if (worldrank == 0) {
	printf( "Checking graph_get\n" );
    }
/* Because reorder was set to zero, we should have the same data */
    for (i=0; i<size; i++) {
	if (index[i] != q_index[i]) {
	    err++;
	    printf( "index[%d] is %d, should be %d\n", i, q_index[i], index[i] );
	}
    }
    for (i=0; i<nedges; i++) {
	if (edges[i] != q_edges[i]) {
	    err++;
	    printf( "edges[%d] is %d, should be %d\n", i, q_edges[i], edges[i] );
	}
    }

/* Now, get each neighbor set individually */
    for (i=0; i<size; i++) {
	MPI_Graph_neighbors_count( new_comm, i, &q_nnbrs );
	MPI_Graph_neighbors( new_comm, i, 3, nbrarray );

	/* Need to test */
	baseindex = (i > 0) ? index[i-1] : 0;
	for (j=0; j<q_nnbrs; j++) {
	    if (nbrarray[j] != edges[baseindex+j]) {
		err++;
		printf( "nbrarray[%d] for rank %d should be %d, is %d\n",
			j, i, edges[baseindex+j], nbrarray[j] );
	    }
	}
    }

/* Test MPI_Graph_map by seeing what ranks are generated for this graph */
    MPI_Graph_map( comm, nnodes, index, edges, &newrank );

    if (worldrank == 0) {
	printf( "Checking graph_map\n" );
    }
/* Check that the ranks are at least disjoint among all processors. */
    rankbuf = (int *)malloc( size * sizeof(int) );
    MPI_Allgather( &newrank, 1, MPI_INT, rankbuf, 1, MPI_INT, comm );
    for (i=0; i<size; i++) {
	for (j=0; j<size; j++) {
	    if (rankbuf[j] == i) break;
	}
	if (j >= size) {
	    err++;
	    printf( "Rank %d missing in graph_map\n", i );
	}
    }

    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (worldrank == 0) {
	if (toterr == 0) 
	    printf( "No errors in MPI Graph routines\n" );
	else
	    printf( "Found %d errors in MPI Graph routines\n", toterr );
    }

    MPI_Comm_free( &new_comm );
    free( index );
    free( edges );
    free( q_index );
    free( q_edges );
    free( rankbuf );
    MPI_Finalize( );
    return 0;
}

/* 
 * Routine to print out a graph for debugging
 */
void PrintGraph( nnodes, index, edges )
int nnodes, *index, *edges;
{
    int i, lastidx, j;
    lastidx=0;
    printf( "rank\tindex\tedges\n" );
    for (i=0; i<nnodes; i++) {
	printf( "%d\t%d\t", i, index[i] );
	for (j=0; j<index[i] - lastidx; j++) {
	    printf( "%d ", *edges++ );
	}
	printf( "\n" );
	lastidx = index[i];
    }
}

/* 
   Number index[0] as first, add its children, and then number them.
   Note that because of the way the index/edge list is defined, we 
   need to do a depth-first evaluation

   Each process is connected to the processes rank+1
   and rank + 1 + floor((size)/2), where size is the size of the subtree 

   Make index[i] the DEGREE of node i.  We'll make the relative to 0
   at the end.
 */
void NumberEdges( Index, Edges, parent, first, last )
int **Index, **Edges, parent, first, last;
{
    int *index = *Index;
    int *edges = *Edges;
    int right;

    index[0] = 0;
    if (parent >= 0) {
#ifdef DEBUG    
	printf( "Adding parent %d to %d\n", parent, first );
#endif
	*index   = *index + 1;
	*edges++ = parent;
    }
    if (first >= last) {
	/* leaf */
	index++;
	if (parent >= 0) {
	    *Index = index;
	    *Edges = edges;
	}
	return;
    }

/* Internal node.  Always at least a left child */
#ifdef DEBUG
    printf( "Adding left child %d to %d\n", first + 1, first );
#endif
    *index	 = *index + 1;
    *edges++ = first + 1;

/* Try to add a right child */
    right = (last - first)/2;
    right = first + right + 1;
    if (right == first + 1) 
	right++;
    if (right <= last) {
	/* right child */
#ifdef DEBUG    
	printf( "Adding rightchild %d to %d\n", right, first );
#endif
	*index   = *index + 1;
	*edges++ = right;
    }
    index++;
    if (first + 1 <= last && right - 1 > first) {
	NumberEdges( &index, &edges, first, first + 1, 
		     (right <= last) ? right - 1: last );
    }
    if (right <= last) {
	NumberEdges( &index, &edges, first, right, last );
    }
    if (parent >= 0) {
	*Index = index;
	*Edges = edges;
    }
}
