/*
 * Program to test that datatypes that are freed with MPI_TYPE_FREE
 * are not actually deleted until communication that they are a part of
 * has completed.
 *
 */

#include <stdio.h>
#include "test.h"
#include "mpi.h"

#define SIZE 10000
static int src  = 1;
static int dest = 0;

/* Prototypes for picky compilers */
void Generate_Data ( int *, int );

void Generate_Data(buffer, buff_size)
int *buffer;
int buff_size;
{
    int i;

    for (i = 0; i < buff_size; i++)
	buffer[i] = i+1;
}

int main( int argc, char **argv)
{
    int rank; /* My Rank (0 or 1) */
    int tag, count, i, errcnt = 0;
    MPI_Request handle;
    double data[100];
    MPI_Status status;
    MPI_Datatype rowtype;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    tag    = 2001;
    count  = 1;
    for (i = 0; i < 100; i++)
	data[i] = i;
    MPI_Type_vector( 10, 1, 10, MPI_DOUBLE, &rowtype );
    MPI_Type_commit( &rowtype );
    if (rank == src) { 
	MPI_Irecv(data, count, rowtype, dest, tag, MPI_COMM_WORLD,
		 &handle ); 
	MPI_Type_free( &rowtype );
	MPI_Recv( (void *)0, 0, MPI_INT, dest, tag+1, 
		  MPI_COMM_WORLD, &status );
	MPI_Wait( &handle, &status );
	/* Check for correct data */
	for (i = 0; i < 10; i++) if (data[i*10] != i*10) {
	    errcnt++;
	    fprintf( stderr, 
		    "[%d](rcv row-row) %d'th element = %f, should be %f\n",
		     rank, i, data[i*10], 10.0*i );
	    }

    } else if (rank == dest) {
	MPI_Ssend( (void *)0, 0, MPI_INT, src, tag+1, MPI_COMM_WORLD );
	/* By using an Ssend first, we make sure that the Irecv doesn't
	   match until after the type has been freed */
	MPI_Isend( data, count, rowtype, src, tag, MPI_COMM_WORLD, 
		  &handle );
	MPI_Type_free( &rowtype );
	MPI_Wait( &handle, &status );
	}

    i = errcnt;
    MPI_Allreduce( &i, &errcnt, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (errcnt > 0) {
	printf( "Found %d errors in the run\n", errcnt );
	}
    Test_Waitforall( );
    MPI_Finalize();

    return 0;
}



