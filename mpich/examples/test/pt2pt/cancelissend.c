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
/* 
 * WDG - July 6, 2004
 *
 * This is a modified version that:
 *  Uses a shorter message (in case the implementation uses eager delivery
 *  even with synchronous send)
 *  Allows control of which process is executing the Issend and which the
 *  receive (to simplify debugging)
 */
#include <stdio.h>
#include "mpi.h"

int main(int argc, char *argv[])
{
	int me, tasks, data, flag;
	int err0 = 0;
	int err1 = 0;
	int errs, toterrs;
	int master = 1, worker = 0;
	MPI_Request request;
	MPI_Status status;

	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD,&me);
	MPI_Comm_size(MPI_COMM_WORLD,&tasks);

	if (tasks < 2) {
	    printf( "Cancel test requires at least 2 processes\n" );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}

	/* The original test sent 10000 elements with Issend.  This
	   one uses less data but keeps the array the same size */
	{ int data[100000]; if (me == master)  
	{
		MPI_Irecv(data, 1, MPI_INT, worker, 1, MPI_COMM_WORLD,&request);
		MPI_Cancel(&request);
		MPI_Wait(&request,&status);
		MPI_Test_cancelled(&status,&flag);
		if (!flag) {
		    err0++;
		    printf("task %d ERROR: Receive request not cancelled!\n", me);
		}

		/* This is short enough to use eager but because it is
		   a Synchronous send, it must still be possible to 
		   cancel it, even when it is a short message */
		MPI_Issend(data, 100, MPI_INT, worker, 1, MPI_COMM_WORLD,&request);
		MPI_Cancel(&request);
		for (flag = 0;; )  
		{
			MPI_Test(&request,&flag,&status);
			if (flag) break;
		}
		
		MPI_Test_cancelled(&status,&flag);
		if (!flag) {
		    err0++;
		    printf("task %d ERROR: Send request not cancelled! (1)\n", me);
		}
	}}
	
	if (me == master)  
	{
		data = 5;
		MPI_Isend(&data, 1, MPI_INT, worker, 1, MPI_COMM_WORLD,&request);
		MPI_Cancel(&request);
		MPI_Wait(&request,&status);
		MPI_Test_cancelled(&status,&flag);
		if (!flag) {
		    err0++;
		    printf("task %d ERROR: Send request not cancelled! (2)\n", me);
		}
		MPI_Barrier(MPI_COMM_WORLD);
		status.MPI_TAG=MPI_SUCCESS;
		data = 6;
		MPI_Send(&data, 1, MPI_INT, worker, 5, MPI_COMM_WORLD);
		
		MPI_Isend(&data, 1, MPI_INT, worker, 1, MPI_COMM_WORLD,&request);
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Cancel(&request);
		MPI_Wait(&request,&status);
		MPI_Test_cancelled(&status,&flag);
		if (flag) {
		    err0++;
		    printf("task %d ERROR: Send request cancelled!\n", me);
		}
	} 
	else if (me == worker) 
	{
	    MPI_Barrier(MPI_COMM_WORLD);
	    data = 0;
	    MPI_Recv(&data, 1, MPI_INT, master, 1, MPI_COMM_WORLD,&status);
	    if (data != 6) {
		err1++;
		printf("task %d ERROR: Send request not cancelled!\n", me);
	    }
	    
	    MPI_Recv(&data, 1, MPI_INT, master, 5, MPI_COMM_WORLD,&status);
	    if (data != 6) {
		err1++;
		printf("task %d ERROR: Send request not cancelled!\n", me);
	    }
	    MPI_Barrier(MPI_COMM_WORLD);
	}
	else {
	    /* These are needed when the size of MPI_COMM_WORLD > 2 */
	    MPI_Barrier( MPI_COMM_WORLD );
	    MPI_Barrier( MPI_COMM_WORLD );
	}
	
	errs = err0 + err1;
	MPI_Reduce( &errs, &toterrs, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD );
	    
	if ( errs ) {
	    printf( "Test failed with %d errors.\n", errs );
	}
	if (me == 0 && toterrs == 0) {
	    printf( " No Errors\n" );
	}
	      
	MPI_Finalize();
	return 0;
}
