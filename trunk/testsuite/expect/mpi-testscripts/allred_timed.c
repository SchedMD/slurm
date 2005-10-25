#include <stdio.h>
#include "mpi.h"

#define MAX_SUM_RANK 1000

#define ALLRED_COUNT 1000

#define EXPECTED_AVG_uSEC 30

int main(int argc, char **argv)
{
   int me,tasks,i, errcount=0;
   double start,end,diff,avg_diff_usec,in,out = 0.0;

   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD,&tasks);
   if(tasks < 2) {
     printf("MUST RUN WITH AT LEAST 2 TASKS\n");
     errcount++;
     MPI_Finalize();
     exit(0);
   }

   MPI_Comm_rank(MPI_COMM_WORLD,&me);

   in = (me < MAX_SUM_RANK) ? (double) me: 0.0;

   MPI_Barrier(MPI_COMM_WORLD);
   if (!me) {
     start = MPI_Wtime();
   }

   for(i=0;i<ALLRED_COUNT;i++)
     MPI_Allreduce( &in, &out, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD );

   if (!me) {
     end = MPI_Wtime();
     diff = end - start;
     avg_diff_usec = diff * (1000000/ALLRED_COUNT);
     printf("AFTER ALLREDS, START TIME = %f, END TIME = %f, DIFF (sec) = %f,\n",start,end,diff);
     printf("\t\tITERS = %d, AVG (usec) = %f, EXPECTED = %d\n",ALLRED_COUNT,avg_diff_usec, EXPECTED_AVG_uSEC);
     if (avg_diff_usec < EXPECTED_AVG_uSEC) {
       printf ("Passed\n");
     }
     else if (avg_diff_usec < (2* EXPECTED_AVG_uSEC)) {
       printf ("Acceptable\n");
     }
     else {
       printf ("FAILED\n");
     }
     fflush (stdout);
   }

   MPI_Finalize();
return 0;
}


