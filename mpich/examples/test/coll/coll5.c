#include "mpi.h"
#include <stdio.h>
#include "test.h"

#define MAX_PROCESSES 10

int main( int argc, char **argv )
{
    int              rank, size, i,j;
    int              table[MAX_PROCESSES][MAX_PROCESSES];
    int              row[MAX_PROCESSES];
    int              errors=0;
    int              participants;
    int              displs[MAX_PROCESSES];
    int              send_counts[MAX_PROCESSES];

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    /* A maximum of MAX_PROCESSES processes can participate */
    if ( size > MAX_PROCESSES ) participants = MAX_PROCESSES;
    else              participants = size;
    if ( (rank < participants) ) {
      int recv_count = MAX_PROCESSES;
      
      /* If I'm the root (process 0), then fill out the big table */
      /* and setup  send_counts and displs arrays */
      if (rank == 0) 
	for ( i=0; i<participants; i++) {
	  send_counts[i] = recv_count;
	  displs[i] = i * MAX_PROCESSES;
	  for ( j=0; j<MAX_PROCESSES; j++ ) 
	    table[i][j] = i+j;
	}
      
      /* Scatter the big table to everybody's little table */
      MPI_Scatterv(&table[0][0], send_counts, displs, MPI_INT, 
		   &row[0]     , recv_count, MPI_INT, 0, MPI_COMM_WORLD);

      /* Now see if our row looks right */
      for (i=0; i<MAX_PROCESSES; i++) 
	if ( row[i] != i+rank ) errors++;
    } 

    Test_Waitforall( );
    MPI_Finalize();
    if (errors)
      printf( "[%d] done with ERRORS(%d)!\n", rank, errors );
    return errors;
}
