/*
   Test the group routines
   (some tested elsewere)

MPI_Group_compare
MPI_Group_excl
MPI_Group_intersection
MPI_Group_range_excl
MPI_Group_rank
MPI_Group_size
MPI_Group_translate_ranks
MPI_Group_union
MPI_Group_range_incl
MPI_Group_incl

 */
#include "mpi.h"
#include <stdio.h>
/* stdlib.h Needed for malloc declaration */
#include <stdlib.h>
#include "test.h"

int main( int argc, char **argv )
{
    int errs=0, toterr;
    MPI_Group basegroup;
    MPI_Group g1, g2, g3, g4, g5, g6, g7, g8, g9, g10, g11, g12;
    MPI_Comm  comm, newcomm, splitcomm, dupcomm;
    int       i, grp_rank, rank, grp_size, size, result;
    int       nranks, *ranks, *ranks_out;
    int       range[2][3];
    int       worldrank;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &worldrank );

    comm = MPI_COMM_WORLD;

    MPI_Comm_group( comm, &basegroup );

/* Get the basic information on this group */
    MPI_Group_rank( basegroup, &grp_rank );
    MPI_Comm_rank( comm, &rank );
    if (grp_rank != rank) {
	errs++;
	fprintf( stdout, "group rank %d != comm rank %d\n", grp_rank, rank );
    }

    MPI_Group_size( basegroup, &grp_size );
    MPI_Comm_size( comm, &size );
    if (grp_size != size) {
	errs++;
	fprintf( stdout, "group size %d != comm size %d\n", grp_size, size );
    }


/* Form a new communicator with inverted ranking */
    MPI_Comm_split( comm, 0, size - rank, &newcomm );
    MPI_Comm_group( newcomm, &g1 );
    ranks	  = (int *)malloc( size * sizeof(int) );
    ranks_out = (int *)malloc( size * sizeof(int) );
    for (i=0; i<size; i++) ranks[i] = i;
    nranks = size;
    MPI_Group_translate_ranks( g1, nranks, ranks, basegroup, ranks_out );
    for (i=0; i<size; i++) {
	if (ranks_out[i] != (size - 1) - i) {
	    errs++;
	    fprintf( stdout, "Translate ranks got %d expected %d\n", 
		     ranks_out[i], (size - 1) - i );
	}
    }

/* Check Compare */
    MPI_Group_compare( basegroup, g1, &result );
    if (result != MPI_SIMILAR) {
	errs++;
	fprintf( stdout, "Group compare should have been similar, was %d\n",
		 result );
    }
    MPI_Comm_dup( comm, &dupcomm );
    MPI_Comm_group( dupcomm, &g2 );
    MPI_Group_compare( basegroup, g2, &result );
    if (result != MPI_IDENT) {
	errs++;
	fprintf( stdout, "Group compare should have been ident, was %d\n",
		 result );
    }
    MPI_Comm_split( comm, rank < size/2, rank, &splitcomm );
    MPI_Comm_group( splitcomm, &g3 );
    MPI_Group_compare( basegroup, g3, &result );
    if (result != MPI_UNEQUAL) {
	errs++;
	fprintf( stdout, "Group compare should have been unequal, was %d\n",
		 result );
    }

/* Build two new groups by excluding members; use Union to put them
   together again */

/* Exclude 0 */
    MPI_Group_excl( basegroup, 1, ranks, &g4 );
/* Exclude 1-(size-1) */
    MPI_Group_excl( basegroup, size-1, ranks+1, &g5 );
    MPI_Group_union( g5, g4, &g6 );
    MPI_Group_compare( basegroup, g6, &result );
    if (result != MPI_IDENT) {
	errs++;
	/* See ordering requirements on union */
	fprintf( stdout, "Group excl and union did not give ident groups\n" );
    }
    MPI_Group_union( basegroup, g4, &g7 );
    MPI_Group_compare( basegroup, g7, &result );
    if (result != MPI_IDENT) {
	errs++;
	fprintf( stdout, "Group union of overlapping groups failed\n" );
    }

/* Use range_excl instead of ranks */
    range[0][0] = 1;
    range[0][1] = size-1;
    range[0][2] = 1;
    MPI_Group_range_excl( basegroup, 1, range, &g8 );
    MPI_Group_compare( g5, g8, &result );
    if (result != MPI_IDENT) {
	errs++;
	fprintf( stdout, "Group range excl did not give ident groups\n" );
    }

    MPI_Group_intersection( basegroup, g4, &g9 );
    MPI_Group_compare( g9, g4, &result );
    if (result != MPI_IDENT) {
	errs++;
	fprintf( stdout, "Group intersection did not give ident groups\n" );
    }

/* Exclude EVERYTHING and check against MPI_GROUP_EMPTY */
    range[0][0] = 0;
    range[0][1] = size-1;
    range[0][2] = 1;
    MPI_Group_range_excl( basegroup, 1, range, &g10 );
    MPI_Group_compare( g10, MPI_GROUP_EMPTY, &result );
    if (result != MPI_IDENT) {
	errs++;
	fprintf( stdout, 
		 "MPI_GROUP_EMPTY didn't compare against empty group\n");
    }

/* Grouptest usually runs with 4 processes.  Pick a range that specifies
   1, size-1, but where "last" is size.  This checks for an 
   error case that MPICH2 got wrong */
    range[0][0] = 1;
    range[0][1] = size ;
    range[0][2] = size - 2;
    MPI_Group_range_incl( basegroup, 1, range, &g11 );
    ranks[0] = 1;
    ranks[1] = size-1;
    MPI_Group_incl( basegroup, 2, ranks, &g12 );
    MPI_Group_compare( g11, g12, &result );
    if (result != MPI_IDENT) {
	errs++;
	fprintf( stderr, 
		 "MPI_Group_range_incl didn't compare against MPI_Group_incl\n" );
    }

    MPI_Group_free( &basegroup );
    MPI_Group_free( &g1 );
    MPI_Group_free( &g2 );
    MPI_Group_free( &g3 );
    MPI_Group_free( &g4 );
    MPI_Group_free( &g5 );
    MPI_Group_free( &g6 );
    MPI_Group_free( &g7 );
    MPI_Group_free( &g8 );
    MPI_Group_free( &g9 );
    MPI_Group_free( &g10 );
    MPI_Group_free( &g11 );
    MPI_Group_free( &g12 );
    MPI_Comm_free( &dupcomm );
    MPI_Comm_free( &splitcomm );
    MPI_Comm_free( &newcomm );

    MPI_Allreduce( &errs, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (worldrank == 0) {
	if (toterr == 0) 
	    printf( " No Errors\n" );
	else
	    printf( "Found %d errors in MPI Group routines\n", toterr );
    }

    MPI_Finalize();
    return toterr;
}
