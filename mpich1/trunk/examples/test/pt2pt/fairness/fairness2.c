/*
 * This program should be run with at least 8 nodes just to (un)fair
 *
 * Patrick Bridges * bridges@mcs.anl.gov * patrick@CS.MsState.Edu 
 */

#include <stdio.h>
#include "test.h"
#include "mpi.h"

int main(argc, argv)
int argc;
char **argv;
{
    int rank, size, an_int; 
    char *Current_Test = NULL;
    int *num_array, i, j;
    MPI_Status Status;
    
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    Test_Init("fairness2", rank);

    /* Wait for everyone to be ready */
    
    if (rank == 0) { 
	/* Initialize an array to keep statistics in */
	num_array = (int *)malloc((size - 1) * sizeof(int));

	/* Make sure everyone is ready */
	MPI_Barrier(MPI_COMM_WORLD);

	/* Wait for all of the senders to send all of their messages */
	Test_Message("Waiting for all of the senders to say they're through.");
	for (i = 0 ; i < size - 1; i++)
	    MPI_Recv(&an_int, 1, MPI_INT, MPI_ANY_SOURCE, 5000,
		     MPI_COMM_WORLD, &Status);
	
	Test_Message("Starting to dequeue messages...");
	/* Now start dequeuing messages */
	for (i = 0; i < size - 1; i++) {
	    /* Clear the buffer of counts */
	    memset(num_array, 0, (size - 1) * sizeof(int));
	    for (j = 0; j < 200; j++) {
		MPI_Recv(&an_int, 1, MPI_INT, MPI_ANY_SOURCE, 2000, 
			 MPI_COMM_WORLD, &Status);
		num_array[Status.MPI_SOURCE - 1]++;
	    }
	    Test_Printf("Statistics for message group %d:\n", i + 1);
	    for (j = 0; j < size -1 ; j++)
		Test_Printf("%f%% of last 200 messages received \
were from source %d.\n",
			    num_array[j]/2.0, j + 1);
	}

	free(num_array);
	(void)Summarize_Test_Results();
	MPI_Finalize();

    } else {
	MPI_Request ReqArray[200];
	MPI_Status StatArray[200];
	
	MPI_Barrier(MPI_COMM_WORLD);
	an_int = rank;
	
	Test_Message("About to send all of the little messages.");
	/* Send 200 tiny messages - nonblocking so we don't deadlock */
	for (i = 0; i < 200; i++)
	    MPI_Isend(&an_int, 1, MPI_INT, 0, 2000, MPI_COMM_WORLD, 
		      &ReqArray[i]);

	Test_Message("Sending the final message.");
	/* Tell receiver we've sent all of our messages */
	MPI_Send(&an_int, 1, MPI_INT, 0, 5000, MPI_COMM_WORLD);
	Test_Message("Waiting on the nonblocking requests.");
	MPI_Waitall(200,ReqArray,StatArray);
	(void)Summarize_Test_Results();
	MPI_Finalize();
    }

    return 0;
}



