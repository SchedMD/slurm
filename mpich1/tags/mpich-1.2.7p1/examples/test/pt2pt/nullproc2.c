/*
 *  Test for null proc handling with blocking routines
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
   MPI_Status      st[2], sts[2];
   MPI_Request     req[2];
   int             count, errcnt = 0;

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
   MPI_Isend(&a[1], 1, MPI_INT, left, 0, MPI_COMM_WORLD, &req[0]);
   MPI_Isend(&a[2], 1, MPI_INT, right,  1, MPI_COMM_WORLD, &req[1]);
   st[0].MPI_SOURCE = nproc;
   st[0].MPI_TAG = -1;
   st[1].MPI_SOURCE = nproc;
   st[1].MPI_TAG = -1;
   MPI_Recv(&a[0], 1, MPI_INT, left,  1, MPI_COMM_WORLD, &st[0]);
   MPI_Recv(&a[3], 1, MPI_INT, right, 0, MPI_COMM_WORLD, &st[1]);
   MPI_Waitall( 2, req, sts );

   /* Test the end points */
   if (left == MPI_PROC_NULL) {
       if (st[0].MPI_TAG != MPI_ANY_TAG ||
	   st[0].MPI_SOURCE != MPI_PROC_NULL) {
	   errcnt ++;
	   fprintf( stderr, "Incorrect null status for left\n" );
	   if (st[0].MPI_SOURCE != MPI_PROC_NULL) {
	       fprintf( stderr, "Source returned was %d but should be %d\n",
			st[0].MPI_SOURCE, MPI_PROC_NULL );
	   }
       }
       MPI_Get_count( &st[0], MPI_INT, &count );
       if (count != 0) {
	   errcnt ++;
	   fprintf( stderr, "Incorrect null status for left (count)\n" );
	   fprintf( stderr, "Count was %d but should be 0\n", count );
       }
   }
   else if (right == MPI_PROC_NULL) {
       if (st[1].MPI_TAG != MPI_ANY_TAG ||
	   st[1].MPI_SOURCE != MPI_PROC_NULL) {
	   errcnt ++;
	   fprintf( stderr, "Incorrect null status for right\n" );
	   if (st[1].MPI_SOURCE != MPI_PROC_NULL) {
	       fprintf( stderr, "Source returned was %d but should be %d\n",
			st[1].MPI_SOURCE, MPI_PROC_NULL );
	   }
       }
       MPI_Get_count( &st[1], MPI_INT, &count );
       if (count != 0) {
	   errcnt ++;
	   fprintf( stderr, "Incorrect null status for right (count)\n" );
	   fprintf( stderr, "Count was %d but should be 0\n", count );
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
