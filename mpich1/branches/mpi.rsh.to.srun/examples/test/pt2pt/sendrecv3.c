#include "mpi.h"
#include <stdlib.h>
#include <stdio.h>
#include "dtypes.h"
#include "gcomm.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int verbose = 0;
/*
   This program is from mpich/tsuite/pt2pt and should be changed there only.
   It needs gcomm and dtype from mpich/tsuite, and can be run with 
   any number of processes > 1.

   This version uses Pack to send a message and Unpack OR the datatype 
   to receive it.
 */
int main( int argc, char **argv )
{
MPI_Datatype *types;
void         **inbufs, **outbufs;
char         **names;
char         *packbuf, *unpackbuf;
int          packsize, unpacksize, position;
int          *counts, *bytesize, ntype;
MPI_Comm     comms[20];
int          ncomm = 20, rank, np, partner, tag, count;
int          i, j, k, err, toterr, world_rank;
int          errloc;
MPI_Status   status;
char         *obuf;

MPI_Init( &argc, &argv );

AllocateForData( &types, &inbufs, &outbufs, &counts, &bytesize, 
		 &names, &ntype );
GenerateData( types, inbufs, outbufs, counts, bytesize, names, &ntype );

MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );
MakeComms( comms, 20, &ncomm, 0 );

/* Test over a wide range of datatypes and communicators */
err = 0;
for (i=0; i<ncomm; i++) {
    MPI_Comm_rank( comms[i], &rank );
    MPI_Comm_size( comms[i], &np );
    if (np < 2) continue;
    if (world_rank == 0 && verbose) {
	fprintf( stdout, "Testing with communicator with %d members\n", np );
	}
    tag = i;
    for (j=0; j<ntype; j++) {
	if (world_rank == 0 && verbose) 
	    fprintf( stdout, "Testing type %s\n", names[j] );
        if (rank == 0) {
	    partner = np - 1;
	    MPI_Pack_size( counts[j], types[j], comms[i], &packsize );
	    packbuf = (char *)malloc( packsize );
	    if (!packbuf) 
		MPI_Abort( MPI_COMM_WORLD, 1 );
	    position = 0;
	    MPI_Pack( inbufs[j], counts[j], types[j], packbuf, packsize, 
		      &position, comms[i] );
	    /* Send twice */
            MPI_Send( packbuf, position, MPI_PACKED, partner, tag, comms[i] );
            MPI_Send( packbuf, position, MPI_PACKED, partner, tag, comms[i] );
	    free( packbuf );
            }
        else if (rank == np-1) {
	    partner = 0;
	    obuf = outbufs[j];
	    for (k=0; k<bytesize[j]; k++) 
		obuf[k] = 0;
	    /* Receive directly */
            MPI_Recv( outbufs[j], counts[j], types[j], partner, tag, comms[i],
                      &status );
            /* Test correct */
            MPI_Get_count( &status, types[j], &count );
            if (count != counts[j]) {
		fprintf( stderr, 
			"Error in counts (got %d expected %d) with type %s\n",
			 count, counts[j], names[j] );
                err++;
                }
            if (status.MPI_SOURCE != partner) {
		fprintf( stderr, 
			"Error in source (got %d expected %d) with type %s\n",
			 status.MPI_SOURCE, partner, names[j] );
                err++;
                }
            if ((errloc = CheckData( inbufs[j], outbufs[j], bytesize[j] ))) {
		fprintf( stderr, 
                    "Error in data at byte %d with type %s (type %d on %d)\n", 
			 errloc - 1, names[j], j, world_rank );
                err++;
                }
	    /* Receive packed, then unpack */
	    MPI_Pack_size( counts[j], types[j], comms[i], &unpacksize ); 
	    unpackbuf = (char *)malloc( unpacksize );
	    if (!unpackbuf) 
		MPI_Abort( MPI_COMM_WORLD, 1 );
            MPI_Recv( unpackbuf, unpacksize, MPI_PACKED, partner, tag, 
		      comms[i], &status );
	    obuf = outbufs[j];
	    for (k=0; k<bytesize[j]; k++) 
		obuf[k] = 0;
	    position = 0;
            MPI_Get_count( &status, MPI_PACKED, &unpacksize );
	    MPI_Unpack( unpackbuf, unpacksize, &position, 
		        outbufs[j], counts[j], types[j], comms[i] );
	    free( unpackbuf );
            /* Test correct */
#ifdef FOO
	    /* Length is tricky; a correct code will have signaled an error 
	       in MPI_Unpack */
	    count = position;
            if (count != counts[j]) {
		fprintf( stderr, 
		"Error in counts (got %d expected %d) with type %s (Unpack)\n",
			 count, counts[j], names[j] );
                err++;
                }
#endif
            if (status.MPI_SOURCE != partner) {
		fprintf( stderr, 
		"Error in source (got %d expected %d) with type %s (Unpack)\n",
			 status.MPI_SOURCE, partner, names[j] );
                err++;
                }
            if ((errloc = CheckData( inbufs[j], outbufs[j], bytesize[j] ))) {
		fprintf( stderr, 
            "Error in data at byte %d with type %s (type %d on %d, Unpack)\n", 
			errloc - 1, names[j], j, world_rank );
                err++;
                }
            }
	}
    }
if (err > 0) {
    fprintf( stderr, "%d errors on %d\n", err, rank );
    }
MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
 if (world_rank == 0) {
     if (toterr == 0) {
	 printf( " No Errors\n" );
     }
     else {
	 printf (" Found %d errors\n", toterr );
     }
 }
FreeDatatypes( types, inbufs, outbufs, counts, bytesize, names, ntype );
FreeComms( comms, ncomm );
MPI_Barrier( MPI_COMM_WORLD );
MPI_Finalize();
return err;
}
