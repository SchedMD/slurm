#include "mpi.h"
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

int main( int argc, char **argv)
{
   char *buf;
   int i, iam;
   MPI_Init(&argc, &argv);
   MPI_Barrier(MPI_COMM_WORLD);
   buf = (char *)malloc(32*1024);
   MPI_Comm_rank(MPI_COMM_WORLD, &iam);
   for(i=1; i<=32; i++){
      if (iam == 0){
         *buf=i;
         printf("Broadcasting %d bytes\n", i*64);
         }
      MPI_Bcast(buf, i*64, MPI_BYTE, 0, MPI_COMM_WORLD);
      if (*buf != i) printf("Sanity check error on node %d\n", iam);
/*      gsync();
*/
      MPI_Barrier(MPI_COMM_WORLD);
      }
   Test_Waitforall( );
   MPI_Finalize();

   return 0;
}
