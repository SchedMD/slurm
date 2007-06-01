#include "mpi.h"
#include <stdio.h>

#include "duff.c"

int main(argc,argv)
int  argc;
char **argv;
{
   
    char *from, *to;
    int  i;
    int  bufsize;
    float bufsize1;
    double starttime,time1,time2,time3;

    MPI_Init(&argc,&argv);

    if (argc < 2)
    {
	printf("Usage:  copytest <bufsize>\n");
	MPI_Finalize();
        return 0;
    }
    bufsize  = atoi(argv[1]);
    bufsize1 = bufsize/1000000.0;	/* Megabytes */

    from = (char *) malloc(bufsize);
    to   = (char *) malloc(bufsize);

    starttime = MPI_Wtime();
    memcpy(to,from,bufsize);
    time1 = MPI_Wtime() - starttime;

    starttime = MPI_Wtime();
    memcpy(to,from,bufsize);
    time2 = MPI_Wtime() - starttime;

    starttime = MPI_Wtime();
    memcpy(to,from,bufsize);
    time3 = MPI_Wtime() - starttime;

    printf("Times to copy %d bytes (memcpy)      : %f %f %f\n", 
	   bufsize,time1,time2,time3);
    printf("Rates for %d bytes (MB/s)            : %f %f %f\n", bufsize,
	   bufsize1/time1, bufsize1/time2, bufsize1/time3 );

    starttime = MPI_Wtime();
    MPIR_memcpy(to,from,bufsize);
    time1 = MPI_Wtime() - starttime;

    starttime = MPI_Wtime();
    MPIR_memcpy(to,from,bufsize);
    time2 = MPI_Wtime() - starttime;

    starttime = MPI_Wtime();
    MPIR_memcpy(to,from,bufsize);
    time3 = MPI_Wtime() - starttime;

    printf("Times to copy %d bytes (MPIR_memcpy) : %f %f %f\n", 
	   bufsize,time1,time2,time3);
    printf("Rates for %d bytes (MB/s)            : %f %f %f\n", bufsize,
	   bufsize1/time1, bufsize1/time2, bufsize1/time3 );

    MPI_Finalize();
}

