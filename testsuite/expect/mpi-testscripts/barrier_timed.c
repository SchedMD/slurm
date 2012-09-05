/****************************************************************************

 MESSAGE PASSING INTERFACE TEST CASE SUITE

 Copyright IBM Corp. 1995

 IBM Corp. hereby grants a non-exclusive license to use, copy, modify, and
 distribute this software for any purpose and without fee provided that the
 above copyright notice and the following paragraphs appear in all copies.

 IBM Corp. makes no representation that the test cases comprising this
 suite are correct or are an accurate representation of any standard.

 In no event shall IBM be liable to any party for direct, indirect, special
 incidental, or consequential damage arising out of the use of this software
 even if IBM Corp. has been advised of the possibility of such damage.

 IBM CORP. SPECIFICALLY DISCLAIMS ANY WARRANTIES INCLUDING, BUT NOT LIMITED
 TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS AND IBM
 CORP. HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
 ENHANCEMENTS, OR MODIFICATIONS.

****************************************************************************

 These test cases reflect an interpretation of the MPI Standard.  They are
 are, in most cases, unit tests of specific MPI behaviors.  If a user of any
 test case from this set believes that the MPI Standard requires behavior
 different than that implied by the test case we would appreciate feedback.

 Comments may be sent to:
    Richard Treumann
    treumann@kgn.ibm.com

****************************************************************************
*/
#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"

#define BARRIER_COUNT 1000

#define EXPECTED_AVG_uSEC 6

int main(int argc, char **argv)
{
   int me,tasks,i, errcount=0;
   double start,end,diff,avg_diff_usec;

   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD,&tasks);
   if(tasks < 2)
   {
	   printf("MUST RUN WITH AT LEAST 2 TASKS\n");
	   errcount++;
	   MPI_Finalize();
	   exit(0);
   }

   MPI_Comm_rank(MPI_COMM_WORLD,&me);

   MPI_Barrier(MPI_COMM_WORLD);

   if (!me) {
     start = MPI_Wtime();
   }

   for(i=0;i<BARRIER_COUNT;i++)
     MPI_Barrier(MPI_COMM_WORLD);

   if (!me) {
     end = MPI_Wtime();
     diff = end - start;
     avg_diff_usec = diff * (1000000/BARRIER_COUNT);
     printf("AFTER BARRIERS, START TIME = %f, END TIME = %f, DIFF (sec) = %f,\n",start,end,diff);
     printf("\t\tITERS = %d, AVG (usec) = %f, EXPECTED = %d\n",BARRIER_COUNT,avg_diff_usec, EXPECTED_AVG_uSEC);
     if (avg_diff_usec < EXPECTED_AVG_uSEC) {
       printf ("PASSED\n");
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


