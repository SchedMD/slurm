#include "mpi.h"
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

   This version sends and receives EVERYTHING from MPI_BOTTOM, by putting
   the data into a structure.

   This code isn't quite correct, since the MPI_Type_struct that is 
   created for the type may not have the correct extent.  
   One possible change is to make the struct type include the count, and
   send/receive one instance of the data item.  

   The GenerateData call should return extents; when the extent of the 
   created structure doesn't match, we can at least issue an error message.
 */
int main( int argc, char **argv )
{
MPI_Datatype *types;
void         **inbufs, **outbufs;
char         **names;
int          *counts, *bytesize, ntype;
MPI_Comm     comms[20];
int          ncomm = 20, rank, np, partner, tag, count;
int          i, j, k, err, toterr, world_rank, errloc;
MPI_Status   status;
char         *obuf;
MPI_Datatype offsettype;
int          blen;
MPI_Aint     displ, extent, natural_extent;

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
        if (rank == 0) {
	    MPI_Address( inbufs[j], &displ );
	    blen = 1;
	    MPI_Type_struct( 1, &blen, &displ, types + j, &offsettype );
	    MPI_Type_commit( &offsettype );
	    /* Warning: if the type has an explicit MPI_UB, then using a
	       simple shift of the offset won't work.  For now, we skip
	       types whose extents are negative; the correct solution is
	       to add, where required, an explicit MPI_UB */
	    MPI_Type_extent( offsettype, &extent );
	    if (extent < 0) {
		if (world_rank == 0) 
		    fprintf( stdout, 
			"... skipping (appears to have explicit MPI_UB\n" );
		MPI_Type_free( &offsettype );
		continue;
		}
	    MPI_Type_extent( types[j], &natural_extent );
	    if (natural_extent != extent) {
		MPI_Type_free( &offsettype );
		continue;
	    }
	    partner = np - 1;
#if 0
		MPIR_PrintDatatypePack( stdout, counts[j], offsettype, 
					  0, 0 );
#endif
            MPI_Send( MPI_BOTTOM, counts[j], offsettype, partner, tag, 
		      comms[i] );
	    MPI_Type_free( &offsettype );
            }
        else if (rank == np-1) {
	    partner = 0;
	    obuf = outbufs[j];
	    for (k=0; k<bytesize[j]; k++) 
		obuf[k] = 0;
	    MPI_Address( outbufs[j], &displ );
	    blen = 1;
	    MPI_Type_struct( 1, &blen, &displ, types + j, &offsettype );
	    MPI_Type_commit( &offsettype );
	    /* Warning: if the type has an explicit MPI_UB, then using a
	       simple shift of the offset won't work.  For now, we skip
	       types whose extents are negative; the correct solution is
	       to add, where required, an explicit MPI_UB */
	    MPI_Type_extent( offsettype, &extent );
	    if (extent < 0) {
		MPI_Type_free( &offsettype );
		continue;
		}
	    MPI_Type_extent( types[j], &natural_extent );
	    if (natural_extent != extent) {
		MPI_Type_free( &offsettype );
		continue;
	    }
            MPI_Recv( MPI_BOTTOM, counts[j], offsettype, 
		     partner, tag, comms[i], &status );
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
                  "Error in data with type %s (type %d on %d) at byte %d\n", 
			 names[j], j, world_rank, errloc - 1 );
		if (err < 10) {
		    /* Give details on only the first 10 errors */
		    unsigned char *in_p = (unsigned char *)inbufs[j],
			*out_p = (unsigned char *)outbufs[j];
		    int jj;
		    jj = errloc - 1;
 		    jj &= 0xfffffffc; /* lop off a few bits */ 
		    in_p += jj;
		    out_p += jj;
		    fprintf( stderr, "%02x%02x%02x%02x should be %02x%02x%02x%02x\n",
			     out_p[0], out_p[1], out_p[2], out_p[3],
			     in_p[0], in_p[1], in_p[2], in_p[3] );
		}
                err++;
#if 0
		MPIR_PrintDatatypeUnpack( stdout, counts[j], offsettype, 
					  0, 0 );
#endif
                }
	    MPI_Type_free( &offsettype );
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
