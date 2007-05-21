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
int i, errs, rank;
errs = 0;

MPI_Comm_rank( MPI_COMM_WORLD, &rank );
for (i=0; i<10; i++) {
    if (buf[i*20] != i) {
	printf( "[%d] incorrect data, got %d(%x) expected %d(%x)\n", 
	        rank, buf[i*20], buf[i*20], i, i );
	errs++;
	}
    }
return errs;
}
void ClearData( buf )
short *buf;
{
int i;
for (i=0; i<10*20; i++) {
    buf[i] = 0;
    }
}
void SetData( buf )
short *buf;
{
int i;
for (i=0; i<10; i++) {
    buf[i*20] = i;
    }
}

/*
 */
int main( int argc, char **argv )
{
    int rank, c, size, master, slave, step, errs;
    MPI_Status status;
    short buf[10*20];
    MPI_Datatype dtype;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    MPI_Type_vector( 10, 1, 20, MPI_SHORT, &dtype );
    MPI_Type_commit( &dtype );
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
		MPI_Recv( buf, 1, dtype, MPI_ANY_SOURCE, step, 
			 MPI_COMM_WORLD, &status );
		MPI_Get_count( &status, dtype, &c );
		if (c != 1) { 
		    errs++;
		    printf( 
	    "[%d] (%d)Did not get correct count; expected 10, got %d\n", 
			   rank, step, c );
		    }
		if (CheckData( buf )) {
		    errs++;
		    }
		}
	    else if (rank == master) {
		SetData( buf );
		MPI_Send( buf, 1, dtype, slave, step, MPI_COMM_WORLD );
		}
	    
	    step++;

	    if (rank == 0) 
		printf( "Sending from %d to %d\n", master, slave );
	    /* Receives from specific node check for special cases */
	    if (rank == slave) {
		ClearData( buf );
		MPI_Recv( buf, 1, dtype, master, step, MPI_COMM_WORLD, 
			 &status );
		MPI_Get_count( &status, dtype, &c );
		if (c != 1) { 
		    errs++;
		    printf( 
	    "[%d] (%d)Did not get correct count; expected 10, got %d\n", 
			   rank, step, c );
		    }
		if (CheckData( buf )) {
		    errs++;
		    }
		}
	    else if (rank == master) {
		SetData( buf );
		MPI_Send( buf, 1, dtype, slave, step, MPI_COMM_WORLD );
		}
	    step++;
	    }
	}
    MPI_Type_free( &dtype );
    MPI_Finalize();
return 0;
}
