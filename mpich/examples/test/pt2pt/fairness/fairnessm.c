/* 
 * Program to test the fairness of the MPI implementation over source.
 * All of the programs wait on a barrier, then node 0 starts receiving
 * small messages using ANY_SOURCE from all of the other nodes who
 * send as much as they can.  Node 0 collects statistics on the rate
 * messages are received from each source. (Every N messages it
 * prints out what percentage of the last N received were from each
 * source. It does this for <size-1> times.
 *
 * This program should be run with at least 8 nodes just to be (un)fair
 *
 * Patrick Bridges * bridges@mcs.anl.gov * patrick@CS.MsState.Edu 
 */

#include <stdio.h>
#include "test.h"
#include "mpi.h"
#include "mpe.h"
#define MPG 25
#define MSZ 1

int main(argc, argv)
int argc;
char **argv;
{
    int rank, size, an_int[MSZ]; 
    char *Current_Test = NULL;
    int *num_array, i, j;
    MPI_Status Status;
    
    MPI_Init(&argc, &argv);
    MPE_Init_log();
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    Test_Init("fairnessm", rank);

    /* Wait for everyone to be ready */
    
    if (rank == 0) { 
	/* Initialize an array to keep statistics in */
	num_array = (int *)malloc((size - 1) * sizeof(int));
	MPID_SetRecvDebugFlag(1);
	MPI_Barrier(MPI_COMM_WORLD);
	
	for (i = 0; i < size - 1; i++) {
	    /* Clear the buffer of counts */
	    memset(num_array, 0, (size - 1) * sizeof(int));
	    for (j = 0; j < MPG; j++) {
		MPI_Recv(an_int, MSZ, MPI_INT, MPI_ANY_SOURCE, 2000, 
			 MPI_COMM_WORLD, &Status);
		MPE_Log_receive(Status.MPI_SOURCE, 2000, MSZ * sizeof(int));
		num_array[Status.MPI_SOURCE - 1]++;
	    }
	    Test_Printf("Statistics for message group %d:\n", i + 1);
	    for (j = 0; j < size -1 ; j++)
		Test_Printf("%f%% of last %d messages received \
were from source %d.\n",
			    num_array[j]/2.0, MPG, j + 1);
	}
	free(num_array);
	(void)Summarize_Test_Results();
	MPE_Finish_log("/home/bridges/fairness.log");
	MPI_Finalize();

    } else {
	MPI_Barrier(MPI_COMM_WORLD);
	for (i = 0; i < MPG; i++) {
	    MPI_Send(an_int, MSZ, MPI_INT, 0, 2000, MPI_COMM_WORLD);
	    MPE_Log_send(0, 2000, MSZ * sizeof(int));
	}
	MPE_Finish_log("/home/bridges/fairness.log");
	MPI_Finalize();
    }

    return 0;
}



