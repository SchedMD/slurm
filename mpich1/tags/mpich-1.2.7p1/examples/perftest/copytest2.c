#include <mpi.h>
#define BUFSIZE 1000000

main(argc,argv)
int  argc;
char **argv;
{
   
    char from[BUFSIZE];
    char to[BUFSIZE];
    int  i;
    double starttime,time1,time2,time3;
    double bufsize1 = (double) BUFSIZE / 1000000;

    MPI_Init(&argc,&argv);

    starttime = MPI_Wtime();
    memcpy(to,from,BUFSIZE);
    time1 = MPI_Wtime() - starttime;

    starttime = MPI_Wtime();
    memcpy(to,from,BUFSIZE);
    time2 = MPI_Wtime() - starttime;

    starttime = MPI_Wtime();
    memcpy(to,from,BUFSIZE);
    time3 = MPI_Wtime() - starttime;
    printf("Times to copy %d bytes   : %f %f %f\n", BUFSIZE,time1,time2,time3);
    printf("Rates for %d bytes (Mb/s): %f %f %f\n", BUFSIZE,
	   bufsize1/time1, bufsize1/time2, bufsize1/time3 );
    MPI_Finalize();

    return 0;
}

