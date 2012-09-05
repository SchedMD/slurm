#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"


#define MAXLEN 10000

int main(int argc, char **argv)
{
   int *out, *in,i,j,k;
   int myself,tasks, errcount=0;

   MPI_Init(&argc, &argv);
   MPI_Comm_rank(MPI_COMM_WORLD,&myself);
   MPI_Comm_size(MPI_COMM_WORLD,&tasks);
   for(j=1;j<=MAXLEN;j*=10)  {
      out=(int *)calloc(j*tasks, sizeof(int));
      in=(int *)calloc(j*tasks,sizeof(int));
      for(i=0;i<j*tasks;i++)  out[i] = myself;

      MPI_Alltoall(out,j,MPI_INT,in,j,MPI_INT,MPI_COMM_WORLD);

      for(i=0;i<tasks;i++)  {
         for(k=0;k<j;k++) {
if ((k+i*j) >= MAXLEN) continue;
            if(in[k+i*j] != i)
			{
				printf("[%d] bad answer (%d) at index %d of %d (should be %d)\n",tasks,in[k+i*j],k+i*j,j*tasks,i);
			    errcount++;
			}
         }
      }
     free(out);
     free(in);
   }
   MPI_Barrier(MPI_COMM_WORLD);

   if ((!errcount) && (!myself)) {
     printf ("PASSED\n");
     fflush(stdout);
   }
   MPI_Finalize();
return 0;
}
