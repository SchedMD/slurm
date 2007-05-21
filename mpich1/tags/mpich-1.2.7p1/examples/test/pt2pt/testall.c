#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "dtypes.h"
#include "gcomm.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int verbose = 0;
/* 
   Multiple completions
   
   This is similar to a test in allpair.f, but with an expanded range of
   datatypes and communicators.
 */

int main( int argc, char **argv )
{
MPI_Datatype *types;
void         **inbufs, **outbufs;
char         **names;
int          *counts, *bytesize, ntype;
MPI_Comm     comms[20];
int          ncomm = 20, rank, np, partner, tag;
int          i, j, k, err, toterr, world_rank;
MPI_Status   status, statuses[2];
int          flag;
char         *obuf;
MPI_Request  requests[2];


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
    /* This is the test.  
       master:                               worker:
       irecv                                 send
       isend
       testall  (fail)
       sendrecv                              sendrecv
                                             irecv
       sendrecv                              sendrecv
                                             wait
       sendrecv                              sendrecv
       testall  (should succeed)                  
     */
    for (j=0; j<ntype; j++) {
	if (world_rank == 0 && verbose) 
	    fprintf( stdout, "Testing type %s\n", names[j] );
        if (rank == 0) {
	    /* Master */
	    partner = np - 1;
#if 0
	    MPIR_PrintDatatypePack( stdout, counts[j], types[j], 0, 0 );
#endif
	    obuf = outbufs[j];
	    for (k=0; k<bytesize[j]; k++) 
		obuf[k] = 0;
	    
	    MPI_Irecv(outbufs[j], counts[j], types[j], partner, tag, 
		      comms[i], &requests[0] );
	    
	    /* Use issend to ensure that the test cannot complete */
	    MPI_Issend( inbufs[j], counts[j], types[j], partner, tag, 
		        comms[i], &requests[1] );

	    /* Note that the send may have completed */
	    MPI_Testall( 2, &requests[0], &flag, statuses );
	    if (flag) {
		err++;
		fprintf( stderr, "MPI_Testall returned flag == true!\n" );
		}
	    if (requests[1] == MPI_REQUEST_NULL) {
		err++;
		fprintf( stderr, "MPI_Testall freed a request\n" );
		}
	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  comms[i], &status );
	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  comms[i], &status );
	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  comms[i], &status );
	    /* This should succeed, but may fail if the wait below is 
	       still waiting */
	    MPI_Testall( 2, requests, &flag, statuses );
	    if (!flag) {
		err++;
		fprintf( stderr, "MPI_Testall returned flag == false!\n" );
		}
	    if (requests[0] != MPI_REQUEST_NULL || 
		requests[1] != MPI_REQUEST_NULL) {
		err++;
		fprintf( stderr, "MPI_Testall failed to free requests (test %d)\n", j );
		if (requests[0] != MPI_REQUEST_NULL) {
		    fprintf( stderr, "Failed to free Irecv request\n" );
		}
		if (requests[1] != MPI_REQUEST_NULL) {
		    fprintf( stderr, "Failed to free Isend request\n" );
		}
	    }
	    /* Check the received data */
            if (CheckDataAndPrint( inbufs[j], outbufs[j], bytesize[j],
				   names[j], j )) {
		err++;
		}
	    }
	else if (rank == np - 1) {
	    /* receiver */
	    partner = 0;
	    obuf = outbufs[j];
	    for (k=0; k<bytesize[j]; k++) 
		obuf[k] = 0;
	    
	    MPI_Send( inbufs[j], counts[j], types[j], partner, tag, 
		        comms[i] );
	    
	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  comms[i], &status );

	    MPI_Irecv(outbufs[j], counts[j], types[j], partner, tag, 
		      comms[i], &requests[0] );

	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  comms[i], &status );

	    MPI_Wait( requests, statuses );
            if (CheckDataAndPrint( inbufs[j], outbufs[j], bytesize[j],
				   names[j], j )) {
                err++;
		}
	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  MPI_BOTTOM, 0, MPI_INT, partner, ncomm+i, 
			  comms[i], &status );
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
