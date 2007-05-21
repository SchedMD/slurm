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
#define MPG 200
#define MSZ 1

int 
main(argc, argv)
int argc;
char **argv;
{
    int rank, size, an_int[MSZ]; 
    int dummy[4], d1, d2;
    char *Current_Test = NULL;
    int *num_array, i, j;
    int dontcare, allgrp;
    
    /* Initialize the environment */
    mp_environ(&size,&rank);

    /* Get allgrp from the task */
    d1 = 4; d2 = 3;
    mp_task_query(dummy,&d1,&d2);
    allgrp = dummy[3];
    dontcare = dummy[0];

    Test_Init("fairness", rank);

    /* Wait for everyone to be ready */
    if (rank == 0) { 
	/* Initialize an array to keep statistics in */
	num_array = (int *)malloc((size - 1) * sizeof(int));

	mp_sync(&allgrp);
	
	for (i = 0; i < size - 1; i++) {
	    /* Clear the buffer of counts */
	    memset(num_array, 0, (size - 1) * sizeof(int));
	    for (j = 0; j < MPG; j++) {
		d1 = sizeof(int)*MSZ;
		d2 = 2000;
		mp_brecv(an_int, &d1, &dontcare, &d2);
		num_array[d1 - 1]++;
	    }
	    Test_Printf("Statistics for message group %d:\n", i + 1);
	    for (j = 0; j < size -1 ; j++)
		Test_Printf("%f%% of last %d messages received \
were from source %d.\n",
			    num_array[j]*100.0/MPG, MPG, j + 1);
	}
	free(num_array);
	(void)Summarize_Test_Results();
    } else {
	mp_sync(&allgrp);
	for (i = 0; i < MPG; i++) {
	    int d3, d4;

	    d1 = MSZ*sizeof(int);
	    d2 = 0;
	    d3 = 2000;
	    d4 = 0;
	    mp_bend(an_int, &d1, &d2, &d3, &d4);
	}
    }

    return 0;
}
