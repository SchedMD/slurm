/*
 * This program came from i-hamzu@kazoo.cs.uiuc.edu (Ilker Hamzaoglu) 
 * how had problems on a Convex Exemplar.  It may have identified a
 * problem in managing pools of packets.  This must be run with 5 processes.
 */
#include "mpi.h"
#include <stdlib.h>
#include <stdio.h>

#define SIZE 1024
/* #define ITERATION 16384/2 */
#define ITERATION 16384/256


int main(argc, argv )
int argc;
char * argv[];
{

    int myid, numprocs, i;

    MPI_Status last_mpi_status;
    double total_time;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);

    total_time = MPI_Wtime();

        if (myid == 0)
        {
            void *buf;

            buf=malloc(SIZE);

            for (i=0;i<ITERATION;i++)  {

  MPI_Send(buf,SIZE,MPI_BYTE,1,1,MPI_COMM_WORLD);
  MPI_Recv(buf,SIZE,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&last_mpi_status);

  MPI_Send(buf,SIZE,MPI_BYTE,3,1,MPI_COMM_WORLD);               
  MPI_Recv(buf,SIZE,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&last_mpi_status);

  /* printf( "Iteration %d done (0) \n", i ); */
	    }            
	}
        else if (myid == 1) 
        {
            void *buf;
            buf=malloc(SIZE);

            for (i=0;i<ITERATION;i++) {

  MPI_Recv(buf,SIZE,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&last_mpi_status);
  MPI_Send(buf,SIZE,MPI_BYTE,2,1,MPI_COMM_WORLD);

  MPI_Recv(buf,SIZE,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&last_mpi_status);
  MPI_Send(buf,SIZE,MPI_BYTE,0,1,MPI_COMM_WORLD);
	    }
        }
        else if (myid == 2)
        {
            void *buf;
            buf=malloc(SIZE);

            for (i=0;i<ITERATION;i++) {

  MPI_Recv(buf,SIZE,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&last_mpi_status);
  MPI_Send(buf,SIZE,MPI_BYTE,1,1,MPI_COMM_WORLD);            
	    }
        }
        else if (myid == 3) 
        {
            void *buf;
            buf=malloc(SIZE);

            for (i=0;i<ITERATION;i++) {

  MPI_Recv(buf,SIZE,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&last_mpi_status);
  MPI_Send(buf,SIZE,MPI_BYTE,4,1,MPI_COMM_WORLD);

  MPI_Recv(buf,SIZE,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&last_mpi_status);
  MPI_Send(buf,SIZE,MPI_BYTE,0,1,MPI_COMM_WORLD);
	    }
        }
        else if (myid == 4)
        {
            void *buf;
            buf=malloc(SIZE);

            for (i=0;i<ITERATION;i++) {

  MPI_Recv(buf,SIZE,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&last_mpi_status);
  MPI_Send(buf,SIZE,MPI_BYTE,3,1,MPI_COMM_WORLD);            
	    }
        }


    total_time = MPI_Wtime() - total_time;

    fprintf( stdout, "total time (%d)      = %f\n", myid, total_time );
    fprintf( stdout, "rate (%d)      = %fMB/sec\n", myid, 
	     ITERATION * 2 * SIZE * 1.0e-6 / total_time );
    

    MPI_Finalize();

  return 0;
  }
