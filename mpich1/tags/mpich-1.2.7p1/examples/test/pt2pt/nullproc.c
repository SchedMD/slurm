/*
 *  Test for null proc handling with non-blocking routines
 */


#include <stdio.h>
#include "mpi.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int main( int argc, char *argv[] )
{
   int             a[4];
   int             i, nproc;
   int             rank, right, left;
   MPI_Status      status;
   MPI_Request     req[4];
   int             index, it, count, errcnt = 0;

   /* start up */
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &nproc);
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);

   /* set up processor chain (Apps should use Cart_create/shift) */
   left = (rank == 0) ? MPI_PROC_NULL : rank - 1;
   right = (rank == nproc - 1) ? MPI_PROC_NULL : rank + 1;

   /* initialize local matrix */
   /* globally: a[i] = i, i = 1 .. 2*nproc */
   /* locally : a[i] = 2*rank+i, i=1,2 */
   a[0] = -1;
   a[1] = 2*rank + 1; 
   a[2] = 2*rank + 2; 
   a[3] = -1;

   /* start all receives and sends */
   MPI_Irecv(&a[0], 1, MPI_INT, left,  1, MPI_COMM_WORLD, &req[0]);
   MPI_Irecv(&a[3], 1, MPI_INT, right, 0, MPI_COMM_WORLD, &req[3]);
   MPI_Isend(&a[1], 1, MPI_INT, left, 0, MPI_COMM_WORLD, &req[1]);
   MPI_Isend(&a[2], 1, MPI_INT, right,  1, MPI_COMM_WORLD, &req[2]);

   for (it=0; it<4; it++) {
       status.MPI_SOURCE = nproc;
       status.MPI_TAG = nproc;
       MPI_Waitany( 4, req, &index, &status );
       if (index == 0 && left == MPI_PROC_NULL) {
	   if (status.MPI_TAG != MPI_ANY_TAG ||
	       status.MPI_SOURCE != MPI_PROC_NULL) {
	       errcnt ++;
	       fprintf( stderr, "Incorrect null status for left\n" );
	   }
	   MPI_Get_count( &status, MPI_INT, &count );
	   if (count != 0) {
	       errcnt ++;
	       fprintf( stderr, "Incorrect null status for left (count)\n" );
	   }
       }
       else if (index == 3 && right == MPI_PROC_NULL) {
	   if (status.MPI_TAG != MPI_ANY_TAG ||
	       status.MPI_SOURCE != MPI_PROC_NULL) {
	       errcnt ++;
	       fprintf( stderr, "Incorrect null status for right\n" );
	   }
	   MPI_Get_count( &status, MPI_INT, &count );
	   if (count != 0) {
	       errcnt ++;
	       fprintf( stderr, "Incorrect null status for right (count)\n" );
	   }
       }
   }
   
   /* Test results */
   if (left == MPI_PROC_NULL) {
       if (a[0] != -1) {
	   fprintf( stderr, "Expected -1, found %d in left partner\n", a[0] );
	   errcnt ++;
       }
   }
   else {
       if (a[0] != 2 * left + 2) {
	   fprintf( stderr, "Expected %d, found %d in left partner\n", 
		    2 * left + 2, a[0] );
	   errcnt ++;
       }
   }

   if (right == MPI_PROC_NULL) {
       if (a[3] != -1) {
	   fprintf( stderr, "Expected -1, found %d in right partner\n", a[3] );
	   errcnt ++;
       }
   }
   else {
       if (a[3] != 2 * right + 1) {
	   fprintf( stderr, "Expected %d, found %d in right partner\n", 
		    2 * right + 1, a[3] );
	   errcnt ++;
       }
   }

   
   i = errcnt;
   MPI_Allreduce( &i, &errcnt, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
   if (rank == 0) {
       if (errcnt > 0) {
	   printf( "Found %d errors in the run \n", errcnt );
       }
       else
	   printf( "No errors in handling MPI_PROC_NULL\n" );
   }
   
   /* clean up */
   MPI_Finalize();
   return 0;
}
