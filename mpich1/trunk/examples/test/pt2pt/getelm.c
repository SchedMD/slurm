/*
 * This is a test of getting the number of basic elements
 */

#include "mpi.h"
#include <stdio.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

typedef struct { 
    int len;
    double data[1000];
    } buf_t;

int main( int argc, char **argv )
{
    int err = 0, toterr;
    MPI_Datatype contig1, varstruct1, oldtypes[2], varstruct2;
    MPI_Aint     displs[2];
    int          blens[2];
    MPI_Comm     comm;
    MPI_Status   status;
    int          world_rank;
    int          rank, size, partner, count, i;
    int          send_ibuf[4], recv_ibuf[4];
    buf_t        send_buf, recv_buf;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );

/* Form the datatypes */
    MPI_Type_contiguous( 4, MPI_INT, &contig1 );
    MPI_Type_commit( &contig1 );
    blens[0] = 1;
    blens[1] = 1000;
    oldtypes[0] = MPI_INT;
    oldtypes[1] = MPI_DOUBLE;
/* Note that the displacement for the data is probably double aligned */
    MPI_Address( &send_buf.len, &displs[0] );
    MPI_Address( &send_buf.data[0], &displs[1] );
/* Make relative */
    displs[1] = displs[1] - displs[0];
    displs[0] = 0;
    MPI_Type_struct( 2, blens, displs, oldtypes, &varstruct1 );
    MPI_Type_commit( &varstruct1 );

    comm = MPI_COMM_WORLD;

    MPI_Comm_size( comm, &size );
    MPI_Comm_rank( comm, &rank );

    if (size < 2) {
	fprintf( stderr, "This test requires at least 2 processes\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    if (rank == size - 1) {
	partner = 0;
	/* Send contiguous data */
	for (i=0; i<4; i++) 
	    send_ibuf[i] = i;
	MPI_Send( send_ibuf, 1, contig1, partner, 0, comm );

	/* Send partial structure */
	blens[1] = 23;
	MPI_Type_struct( 2, blens, displs, oldtypes, &varstruct2 );
	MPI_Type_commit( &varstruct2 );

	MPI_Send( &send_buf, 1, varstruct2, partner, 1, comm );
	MPI_Type_free( &varstruct2 );

	/* Send NO data */
	MPI_Send( MPI_BOTTOM, 0, MPI_INT, partner, 2, comm );
    }
    else if (rank == 0) {
	partner = size - 1;
	MPI_Recv( recv_ibuf, 1, contig1, partner, 0, comm, &status );
	MPI_Get_count( &status, MPI_INT, &count );
	if (count != 4) {
	    err++;
	    fprintf( stderr, 
		     "Wrong count for contig recv MPI_INT; got %d expected %d\n",
		     count, 4 );
	}
	MPI_Get_count( &status, contig1, &count );
	if (count != 1) {
	    err++;
	    fprintf( stderr, 
		     "Wrong count for contig recv (contig); got %d expected %d\n",
		     count, 1 );
	}
	MPI_Get_elements( &status, contig1, &count );
	if (count != 4) {
	    err++;
	    fprintf( stderr, 
		     "Wrong elements for contig recv contig; got %d expected %d\n",
		     count, 4 );
	}

	/* Now, try the partial structure */
	MPI_Recv( &recv_buf, 1, varstruct1, partner, 1, comm, &status );
	MPI_Get_elements( &status, varstruct1, &count );
	if (count != 24) {
	    err++;
	    fprintf( stderr, 
		     "Wrong number of elements for struct recv; got %d expected %d\n", 
		     count, 24 );
	}

	{
	    /* Receive nothing using a 0-sized type */
	    MPI_Datatype ztype;
	    MPI_Type_contiguous( 0, MPI_INT, &ztype );
	    MPI_Type_commit( &ztype );
	    MPI_Recv( &recv_buf, 10, ztype, partner, 2, comm, &status );
	    /* Current clarification requires 0 for the result */
	    MPI_Get_elements( &status, ztype, &count );
	    if (count != 0) {
		err++;
		fprintf( stderr, 
			 "Wrong number of elements for 0-size datatype; got %d\n",
			 count );
	    }
	    MPI_Get_count( &status, ztype, &count );
	    if (count != 0) {
		err++;
		fprintf( stderr, 
			 "Wrong count for 0-size datatype; got %d\n",
			 count );
	    }
	    MPI_Type_free( &ztype );
	}
    }
    MPI_Type_free( &contig1 );
    MPI_Type_free( &varstruct1 );
    
    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (world_rank == 0) {
	    if (toterr == 0) 
		printf( " No Errors\n" );
	    else
		printf( "Found %d errors in MPI_Get_elements\n", toterr );
    }
    MPI_Finalize( );
    return toterr;
}
