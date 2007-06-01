/*
 * Program to test that the "synchronous send" semantics
 * of point to point communications in MPI is (probably) satisfied. 
 * This is done by starting two synchronous sends and then testing that
 * they do not complete until the matchine receives are issued.
 */

#include <stdio.h>
#include "test.h"
#include "mpi.h"

#define SIZE 10000
/* Amount of time in seconds to wait for the receipt of the second Ssend
   message */
#define MAX_TIME 20
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

int main( int argc, char **argv )
{
    int rank; /* My Rank (0 or 1) */
    int act_size = 1000;
    int flag;
    int buffer[SIZE];
    double t0;
    char *Current_Test = NULL;
    MPI_Status status;
    MPI_Request r[2];

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == src) { 
	Test_Init("issendinit", rank);
	Generate_Data(buffer, SIZE);
	Current_Test = "Ssend_init waits for recv";
	MPI_Recv( buffer, 0, MPI_INT, dest, 0, MPI_COMM_WORLD, &status );
	MPI_Send( buffer, 0, MPI_INT, dest, 0, MPI_COMM_WORLD );
	MPI_Ssend_init( buffer, act_size, MPI_INT, dest, 1, MPI_COMM_WORLD, 
			&r[0] );
	MPI_Ssend_init( buffer, act_size, MPI_INT, dest, 2, MPI_COMM_WORLD, 
			&r[1] );
	MPI_Startall( 2, r );
	t0 = MPI_Wtime();
	flag = 0;
	while (MPI_Wtime() - t0 < MAX_TIME) {
	    MPI_Test( &r[0], &flag, &status );
	    if (flag) {
		Test_Failed(Current_Test);
		break;
		}
	    }
	if (!flag) 
	    Test_Passed(Current_Test);
	MPI_Wait( &r[1], &status );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, dest, 13,
		      MPI_BOTTOM, 0, MPI_INT, dest, 13,
		      MPI_COMM_WORLD, &status );
	MPI_Wait( &r[0], &status );
	MPI_Request_free( &r[0] );
	MPI_Request_free( &r[1] );
	Test_Waitforall( );
	{
	    int rval = Summarize_Test_Results(); /* Returns number of tests;
						    that failed */
	    Test_Finalize();
	    MPI_Finalize();
	    return rval;
	}

    } else if (rank == dest) {
	MPI_Send( buffer, 0, MPI_INT, src, 0, MPI_COMM_WORLD );
	MPI_Recv( buffer, 0, MPI_INT, src, 0, MPI_COMM_WORLD, &status );
	MPI_Recv( buffer, act_size, MPI_INT, src, 2, MPI_COMM_WORLD, &status );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, src, 13,
		      MPI_BOTTOM, 0, MPI_INT, src, 13,
		      MPI_COMM_WORLD, &status );
	MPI_Recv( buffer, act_size, MPI_INT, src, 1, MPI_COMM_WORLD, &status );
	/* Test 1 */
	Test_Waitforall( );
	MPI_Finalize();
    } else {
	fprintf(stderr, "*** This program uses exactly 2 processes! ***\n");
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    return 0;
}



