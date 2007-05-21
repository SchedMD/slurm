#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "dtypes.h"
#include "gcomm.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int verbose = 0;
/* Nonblocking ready persistent sends 
   
   This is similar to a test in allpair.f, but with an expanded range of
   datatypes and communicators.

   This is like irsend.c, but with multiple starts of the same persistent
   request.
 */

int main( int argc, char **argv )
{
    MPI_Datatype *types;
    void         **inbufs, **outbufs;
    char         **names;
    int          *counts, *bytesize, ntype;
    MPI_Comm     comms[20];
    int          ncomm = 20, rank, np, partner, tag;
    int          i, j, k, err, toterr, world_rank, errloc;
    MPI_Status   status;
    int          flag, index;
    char         *obuf;
    MPI_Request  requests[2];
    int          mcnt;


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
	tag = i;
	for (j=0; j<ntype; j++) {
	    if (world_rank == 0 && verbose) 
		fprintf( stdout, "Testing type %s\n", names[j] );
	    /* This test does an irsend between both partners, with 
	       a sendrecv after the irecv used to guarentee that the
	       irsend has a matching receive
	       */
	    if (rank == 0) {
		partner = np - 1;
#if 0
		MPIR_PrintDatatypePack( stdout, counts[j], types[j], 0, 0 );
#endif
		obuf = outbufs[j];
		for (k=0; k<bytesize[j]; k++) 
		    obuf[k] = 0;
	    
		MPI_Recv_init(outbufs[j], counts[j], types[j], partner, tag, 
			      comms[i], &requests[0] );
		MPI_Rsend_init( inbufs[j], counts[j], types[j], partner, tag, 
				comms[i], &requests[1] );
	    
		for (mcnt=0; mcnt<10; mcnt++) {
		    MPI_Start( &requests[0] );
		    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
				  MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
				  comms[i], &status );
		    MPI_Start( &requests[1] );
		    do {
			MPI_Waitany( 2, requests, &index, &status );
		    } while (index != 0);
		    
		    if ((errloc = CheckData( inbufs[j], outbufs[j], 
					     bytesize[j] ))) {
			char *p1, *p2;
			fprintf( stderr, 
    "Error in data with type %s (type %d on %d) at byte %d in %dth test\n", 
				 names[j], j, world_rank, errloc - 1, mcnt );
			p1 = (char *)inbufs[j];
			p2 = (char *)outbufs[j];
			fprintf( stderr, 
			"Got %x expected %x\n", p1[errloc-1], p2[errloc-1] );
			err++;
#if 0
			MPIR_PrintDatatypeUnpack( stderr, counts[j], types[j], 
						  0, 0 );
#endif
		    }
		    MPI_Waitall(1, &requests[1], &status );
		}
		MPI_Request_free( &requests[0] );
		MPI_Request_free( &requests[1] );
	    }
	    else if (rank == np - 1) {
		partner = 0;
		obuf = outbufs[j];
		for (k=0; k<bytesize[j]; k++) 
		    obuf[k] = 0;
	    
		MPI_Recv_init(outbufs[j], counts[j], types[j], partner, tag, 
			      comms[i], &requests[0] );
		MPI_Rsend_init( inbufs[j], counts[j], types[j], partner, tag, 
				comms[i], &requests[1] );
		for (mcnt=0; mcnt<10; mcnt++) {
		    MPI_Start( &requests[0] );
		    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
				  MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
				  comms[i], &status );
		    MPI_Start( &requests[1] );
		    /* Wait for irecv to complete */
		    do {
			MPI_Test( &requests[0], &flag, &status );
		    } while (!flag);
		    if ((errloc = CheckData( inbufs[j], outbufs[j], 
					     bytesize[j] ))) {
			char *p1, *p2;
			fprintf( stderr, 
		    "Error in data with type %s (type %d on %d) at byte %d\n", 
				 names[j], j, world_rank, errloc - 1 );
			p1 = (char *)inbufs[j];
			p2 = (char *)outbufs[j];
			fprintf( stderr, 
		        "Got %x expected %x\n", p1[errloc-1], p2[errloc-1] );
			err++;
#if 0
			MPIR_PrintDatatypeUnpack( stderr, counts[j], types[j], 
					      0, 0 );
#endif
		    }

		    MPI_Waitall(1, &requests[1], &status );
		}
		MPI_Request_free( &requests[0] );
		MPI_Request_free( &requests[1] );
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
    MPI_Finalize();

    return err;
}
