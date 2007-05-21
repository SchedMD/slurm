#include "mpi.h"
#include "mpe.h"
#include <math.h>
#include <stdio.h>

double f( double a );
double f(double a)
{
    return (4.0 / (1.0 + a*a));
}

int main( int argc, char *argv[] )
{
    int  n, myid, numprocs, i, j;
    double PI25DT = 3.141592653589793238462643;
    double mypi, pi, h, sum, x;
    double startwtime=0.0, endwtime;
    int namelen; 
    int event1a, event1b, event2a, event2b,
        event3a, event3b, event4a, event4b;
    char processor_name[MPI_MAX_PROCESSOR_NAME];

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);

    MPI_Get_processor_name(processor_name,&namelen);
    fprintf(stderr,"Process %d running on %s\n", myid, processor_name);

    /*
        MPE_Init_log() & MPE_Finish_log() are NOT needed when
        liblmpi is linked with this program.  In that case,
        MPI_Init() would have called MPE_Init_log() already.
    */

    MPE_Init_log();

    /*  Get event ID from MPE, user should NOT assign event ID  */
    event1a = MPE_Log_get_event_number(); 
    event1b = MPE_Log_get_event_number(); 
    event2a = MPE_Log_get_event_number(); 
    event2b = MPE_Log_get_event_number(); 
    event3a = MPE_Log_get_event_number(); 
    event3b = MPE_Log_get_event_number(); 
    event4a = MPE_Log_get_event_number(); 
    event4b = MPE_Log_get_event_number(); 

    if (myid == 0) {
	MPE_Describe_state(event1a, event1b, "Broadcast", "red");
	MPE_Describe_state(event2a, event2b, "Compute",   "blue");
	MPE_Describe_state(event3a, event3b, "Reduce",    "green");
	MPE_Describe_state(event4a, event4b, "Sync",      "orange");
    }

    if (myid == 0) 
    {
	n = 1000000;
	startwtime = MPI_Wtime();
    }
    MPI_Barrier(MPI_COMM_WORLD);

    MPE_Start_log();

    for (j = 0; j < 5; j++)
    {
	MPE_Log_event(event1a, 0, "start broadcast");
	MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPE_Log_event(event1b, 0, "end broadcast");
    
	MPE_Log_event(event4a,0,"Start Sync");
	MPI_Barrier(MPI_COMM_WORLD);
	MPE_Log_event(event4b,0,"End Sync");

	MPE_Log_event(event2a, 0, "start compute");
	h   = 1.0 / (double) n;
	sum = 0.0;
	for (i = myid + 1; i <= n; i += numprocs)
	{
	    x = h * ((double)i - 0.5);
	    sum += f(x);
	}
	mypi = h * sum;
	MPE_Log_event(event2b, 0, "end compute");

	MPE_Log_event(event3a, 0, "start reduce");
	MPI_Reduce(&mypi, &pi, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPE_Log_event(event3b, 0, "end reduce");
    }

    MPE_Finish_log("cpilog");

    if (myid == 0)
    {
	endwtime = MPI_Wtime();
	printf("pi is approximately %.16f, Error is %.16f\n",
	       pi, fabs(pi - PI25DT));
	printf("wall clock time = %f\n", endwtime-startwtime);
    }
    MPI_Finalize();
    return(0);
}
