/*
   MPICH 1.0.8 on Intel Paragons is alleged to have failed this test.
   (Original code from 
    From: weber@zam212.zam.kfa-juelich.de (M.Weber)
    Reply-To: M.Weber@kfa-juelich.de
   modified slightly to meet our test rules.)
 */
#include <stdio.h>
#include "mpi.h"
#define SIZE 100
/* SIZE 16 worked on Paragon */

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int main( int argc, char *argv[])
{
  int num_procs,my_id,flag;
  int buf[SIZE][SIZE];
  MPI_Status status;
  MPI_Request handle;

  MPI_Init(&argc,&argv);
  MPI_Comm_size(MPI_COMM_WORLD,&num_procs);
  MPI_Comm_rank(MPI_COMM_WORLD,&my_id);
  
  if ( my_id == 1 ) {
     MPI_Isend (buf, SIZE*SIZE, MPI_INT, 0, 0, MPI_COMM_WORLD, &handle );

     flag = 0;
     while (flag == 0) {
        MPI_Test (&handle, &flag, &status);
        printf("%d Wait for completition flag = %d handle = %ld ....\n",
               my_id, flag, (long) handle);
     }
  }
  else if (my_id == 0 ) {
     MPI_Recv (buf, SIZE*SIZE, MPI_INT, 1, 0, MPI_COMM_WORLD, &status );
  }

  printf("%d Done ....\n",my_id);

  MPI_Finalize();
  return 0;
}


