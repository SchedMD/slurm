#include "mpi.h"
#include "test.h"

/* 
From: hook@nas.nasa.gov (Edward C. Hook)
 */

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <errno.h>
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#endif

int main( int argc, char *argv[] )
{
    int rank, size;
    int chunk = 4096;
    int i;
    int *sb;
    int *rb;
    int status, gstatus;

    MPI_Init(&argc,&argv);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD,&size);

    for ( i=1 ; i < argc ; ++i ) {
	if ( argv[i][0] != '-' )
	    continue;
	switch(argv[i][1]) {
	case 'm':
	    chunk = atoi(argv[++i]);
	    break;
	default:
	    fprintf(stderr,"Unrecognized argument %s\n",
		    argv[i]);
	    MPI_Abort(MPI_COMM_WORLD,EXIT_FAILURE);
	}
    }

    sb = (int *)malloc(size*chunk*sizeof(int));
    if ( !sb ) {
	perror( "can't allocate send buffer" );
	MPI_Abort(MPI_COMM_WORLD,EXIT_FAILURE);
    }
    rb = (int *)malloc(size*chunk*sizeof(int));
    if ( !rb ) {
	perror( "can't allocate recv buffer");
	free(sb);
	MPI_Abort(MPI_COMM_WORLD,EXIT_FAILURE);
    }
    for ( i=0 ; i < size*chunk ; ++i ) {
	sb[i] = rank + 1;
	rb[i] = 0;
    }

    /* fputs("Before MPI_Alltoall\n",stdout); */

    /* This should really send MPI_CHAR, but since sb and rb were allocated
       as chunk*size*sizeof(int), the buffers are large enough */
    status = MPI_Alltoall(sb,chunk,MPI_INT,rb,chunk,MPI_INT,
			  MPI_COMM_WORLD);

    /* fputs("Before MPI_Allreduce\n",stdout); */
    MPI_Allreduce( &status, &gstatus, 1, MPI_INT, MPI_SUM, 
		   MPI_COMM_WORLD );

    /* fputs("After MPI_Allreduce\n",stdout); */
    if (rank == 0) {
	if (gstatus == 0) printf( " No Errors\n" );
	else 
	    printf("all_to_all returned %d\n",gstatus);
    }

    free(sb);
    free(rb);

    MPI_Finalize();

    return(EXIT_SUCCESS);
}

