#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"

#define ALLTOALL_COUNT 1000

/* A wild guess... */
#define EXPECTED_AVG_uSEC 100

int main(int argc, char **argv)
{
   int *out, *in,j,k;
   int me,tasks,i, errcount=0;
   double start,end,diff,avg_diff_usec;

   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD,&tasks);
   if(tasks < 2) {
     printf("MUST RUN WITH AT LEAST 2 TASKS\n");
     errcount++;
     MPI_Finalize();
     exit(0);
   }

   MPI_Comm_rank(MPI_COMM_WORLD,&me);

   out=(int *)calloc(tasks, sizeof(int));
   in=(int *)calloc(tasks,sizeof(int));
   for(i=0;i<tasks;i++)  out[i] = me;

   MPI_Barrier(MPI_COMM_WORLD);
   if (!me) {
     start = MPI_Wtime();
   }

   for(i=0;i<ALLTOALL_COUNT;i++)
     MPI_Alltoall(out,1,MPI_INT,in,1,MPI_INT,MPI_COMM_WORLD);

   if (!me) {
     end = MPI_Wtime();
     diff = end - start;
     avg_diff_usec = diff * (1000000/ALLTOALL_COUNT);
     printf("AFTER ALLTOALLS, START TIME = %f, END TIME = %f, DIFF (sec) = %f,\n",start,end,diff);
     printf("\t\tITERS = %d, AVG (usec) = %f, EXPECTED = %d\n",ALLTOALL_COUNT,avg_diff_usec, EXPECTED_AVG_uSEC);
     if (avg_diff_usec < EXPECTED_AVG_uSEC) {
       printf ("PASSED\n");
     }
     else if (avg_diff_usec < (2* EXPECTED_AVG_uSEC)) {
       printf ("Acceptable\n");
     }
     else {
       printf ("SLOW\n");
     }
     fflush (stdout);
   }

   MPI_Finalize();
return 0;
}
