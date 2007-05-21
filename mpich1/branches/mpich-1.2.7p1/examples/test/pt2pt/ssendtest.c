/*
 * Program to test that the "synchronous send" semantics
 * of point to point communications in MPI is (probably) satisfied. 
 * Two messages are send in one order; the destination uses MPI_Iprobe
 * to look for the SECOND message before doing a receive on the first.
 * To give a finite-termination, a fixed amount of time is used for
 * the Iprobe test.
 *
 * This program has been patterned off of "overtake.c"
 *
 *				William Gropp
 *				gropp@mcs.anl.gov
 */

#include <stdio.h>
#include "test.h"
#include "mpi.h"

#define SIZE 10000
/* Amount of time in seconds to wait for the receipt of the second Ssend
   message */
#define MAX_TIME 10
static int src  = 0;
static int dest = 1;

/* Prototypes for picky compilers */
void Generate_Data ( int *, int );

void Generate_Data( int *buffer, int buff_size)
{
    int i;

    for (i = 0; i < buff_size; i++)
	buffer[i] = i+1;
}

int main( int argc, char **argv)
{
    int rank; /* My Rank (0 or 1) */
    int act_size = 0;
    int flag, np, rval, i;
    int buffer[SIZE];
    double t0;
    char *Current_Test = NULL;
    MPI_Status status, status1, status2;
    int count1, count2;
    int sizes[4];

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size( MPI_COMM_WORLD, &np );
    if (np != 2) {
        fprintf(stderr, "*** This program uses exactly 2 processes! ***\n");
        MPI_Abort( MPI_COMM_WORLD, 1 );
        }

    sizes[0] = 0;
    sizes[1] = 1;
    sizes[2] = 1000;
    sizes[3] = SIZE;
/*    for (i = 0; i < 4; i++ ) { */
    for (i = 1; i < 2; i++ ) {
	act_size = sizes[i];
        if (rank == src) { 
            Generate_Data(buffer, SIZE);
            MPI_Recv( buffer, 0, MPI_INT, dest, 0, MPI_COMM_WORLD, &status );
            MPI_Send( buffer, 0, MPI_INT, dest, 0, MPI_COMM_WORLD );
            MPI_Ssend( buffer, act_size, MPI_INT, dest, 1, MPI_COMM_WORLD );
            MPI_Ssend( buffer, act_size, MPI_INT, dest, 2, MPI_COMM_WORLD );
            
        } else if (rank == dest) {
            Test_Init("ssendtest", rank);
            /* Test 1 */
            Current_Test = "Ssend Test (Synchronous Send -> Normal Recieve)";
            MPI_Send( buffer, 0, MPI_INT, src, 0, MPI_COMM_WORLD );
            MPI_Recv( buffer, 0, MPI_INT, src, 0, MPI_COMM_WORLD, &status );
            t0 = MPI_Wtime();
            flag = 0;
	    /* This test depends on a working wtime.  Make a simple check */
	    if (t0 == 0 && MPI_Wtime() == 0) {
		int loopcount = 1000000;
		/* This test is too severe (systems with fast 
		   processors and large MPI_Wtick values can 
		   fail.  Try harder to test MPI_Wtime */
		while (loopcount-- && MPI_Wtime() == 0) ;
		if (loopcount <= 0) {
		    fprintf( stderr, 
			     "MPI_WTIME is returning 0; a working value is needed\n\
for this test.\n" );
		    Test_Failed(Current_Test);
		    MPI_Abort( MPI_COMM_WORLD, 1 );
		}
		t0 = MPI_Wtime();
	    }
            while (MPI_Wtime() - t0 < MAX_TIME) {
                MPI_Iprobe( src, 2, MPI_COMM_WORLD, &flag, &status );
                if (flag) {
                    Test_Failed(Current_Test);
                    break;
                    }
                }
            if (!flag) 
                Test_Passed(Current_Test);
            MPI_Recv( buffer, act_size, MPI_INT, src, 1, MPI_COMM_WORLD, 
                     &status1 );
            MPI_Recv( buffer, act_size, MPI_INT, src, 2, MPI_COMM_WORLD, 
                     &status2 );
            
            MPI_Get_count( &status1, MPI_INT, &count1 );
            MPI_Get_count( &status2, MPI_INT, &count2 );
            if (count1 != act_size) {
                fprintf( stdout, 
                        "(1) Wrong count from recv of ssend: got %d (%d)\n", 
                        count1, act_size );
                }
            if (status1.MPI_TAG != 1) {
                fprintf( stdout, "(1) Wrong tag from recv of ssend: got %d\n", 
                        status1.MPI_TAG );
                }
            if (count2 != act_size) {
                fprintf( stdout, 
                        "(2) Wrong count from recv of ssend: got %d (%d)\n", 
                        count1, act_size );
                }
            if (status2.MPI_TAG != 2) {
                fprintf( stdout, "(2) Wrong tag from recv of ssend: got %d\n", 
                        status2.MPI_TAG );
                }

            }
        }

    Test_Waitforall( );
    rval = 0;
    if (rank == dest) {
	    rval = Summarize_Test_Results(); /* Returns number of tests;
						    that failed */
	    Test_Finalize();
	    }
    MPI_Finalize();
    return rval;
}



