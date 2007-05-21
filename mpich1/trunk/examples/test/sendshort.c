#include <stdio.h>

#include "mpi.h"

/*
   This is a simple test that can be used on heterogeneous systems that
   use XDR encoding to check for correct lengths. 

   Sends back and forth to check on one-sided conversion schemes

   Handles multiple processors.  In particular, this test should be
   run with several combinations:
    2 (1 of each)
    4 (1 and 3, 2 and 2)

   The test uses short data because it can reveal problems with byte-swapping
   and is represented as a different length in XDR.
 */

int CheckData( buf )
short *buf;
{
int i, errs;
errs = 0;
for (i=0; i<10; i++) {
    if (buf[i] != i) {
	printf( "incorrect data, got %d expected %d\n", buf[i], i );
	errs++;
	}
    }
return errs;
}
void ClearData( buf )
short *buf;
{
int i;
for (i=0; i<10; i++) {
    buf[i] = 0;
    }
}
void SetData( buf )
short *buf;
{
int i;
for (i=0; i<10; i++) {
    buf[i] = i;
    }
}

/*
 */
int main( int argc, char **argv )
{
    int rank, c, size, master, slave, step, errs;
    MPI_Status status;
    short buf[10];

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    step = 0;
    errs = 0;
    for (master = 0; master < size; master++ ) {
	for (slave = 0; slave < size; slave++) {
	    if (master == slave) continue;
    
	    /* Receives from ANY check for common format */
	    if (rank == 0) 
		printf( "Sending from %d to %d\n", master, slave );
	    if (rank == slave) {
		ClearData( buf );
		MPI_Recv( buf, 10, MPI_SHORT, MPI_ANY_SOURCE, step, 
			 MPI_COMM_WORLD, &status );
		MPI_Get_count( &status, MPI_SHORT, &c );
		if (c != 10) { 
		    errs++;
		    printf( 
		    "(%d)Did not get correct count; expected 10, got %d\n", 
			   step, c );
		    }
		if (CheckData( buf )) {
		    errs++;
		    }
		}
	    else if (rank == master) {
		SetData( buf );
		MPI_Send( buf, 10, MPI_SHORT, slave, step, MPI_COMM_WORLD );
		}
	    
	    step++;

	    if (rank == 0) 
		printf( "Sending from %d to %d\n", master, slave );
	    /* Receives from specific node check for special cases */
	    if (rank == slave) {
		ClearData( buf );
		MPI_Recv( buf, 10, MPI_SHORT, master, step, MPI_COMM_WORLD, 
			 &status );
		MPI_Get_count( &status, MPI_SHORT, &c );
		if (c != 10) { 
		    errs++;
		    printf( 
		    "(%d)Did not get correct count; expected 10, got %d\n", 
			   step, c );
		    }
		if (CheckData( buf )) {
		    errs++;
		    }
		}
	    else if (rank == master) {
		SetData( buf );
		MPI_Send( buf, 10, MPI_SHORT, slave, step, MPI_COMM_WORLD );
		}
	    step++;
	    }
	}
    
    MPI_Finalize();
return 0;
}
