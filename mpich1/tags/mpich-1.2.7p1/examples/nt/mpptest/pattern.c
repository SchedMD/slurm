#include <stdio.h>

#include "mpi.h"
#include "mpptest.h"
#include "getopts.h"

/*
    This file contains routines to choose the "partners" given a distance or 
    index.
 */

enum { RING, DOUBLE, HYPERCUBE, SHIFT } Pattern;

void SetPattern( argc, argv )
int *argc;
char **argv;
{
    Pattern = RING;

    if (SYArgHasName( argc, argv, 1, "-nbrring" ))  Pattern = RING;
    if (SYArgHasName( argc, argv, 1, "-nbrdbl" ))   Pattern = DOUBLE;
    if (SYArgHasName( argc, argv, 1, "-nbrhc" ))    Pattern = HYPERCUBE;
    if (SYArgHasName( argc, argv, 1, "-nbrshift" )) Pattern = SHIFT;
}

int GetMaxIndex( void )
{
int i, cnt;
switch (Pattern) {
    case RING: 
    case SHIFT:
        return __NUMNODES-1;
    case DOUBLE:
    case HYPERCUBE:
    i   = 1;
    cnt = 1;
    while (i < __NUMNODES) {
	i <<= 1;
	cnt++;
	}
    return cnt;
    }
return 0;
}

/* For operations that do not involve pair operations, we need to separate the
   source and destination 
 */
int GetDestination( int loc, int index, int is_master )
{
    if (Pattern == SHIFT) 
	return (loc + index) % __NUMNODES;
    return GetNeighbor( loc, index, is_master );
}

int GetSource( int loc, int index, int is_master )
{
    if (Pattern == SHIFT) 
	return (loc - index + __NUMNODES) % __NUMNODES;

    return GetNeighbor( loc, index, is_master );
}

/* Exchange operations (partner is both source and destination) */
int GetNeighbor( int loc, int index, int is_master )
{
    int np   = __NUMNODES;

    switch (Pattern) {
    case RING: 
        if (is_master) return (loc + index) % np;
	return (loc + np - index) % np;
    case DOUBLE:
	if (is_master) return (loc + (1 << (index-1))) % np;
	return (loc - (1 << (index-1)) + np) % np;
    case HYPERCUBE:
	return loc ^ (1 << (index-1));
    default:
	fprintf( stderr, "Unknown or unsupported pattern\n" );
    }
    return loc;
}

void PrintPatternHelp( void )
{
    fprintf( stderr, "\n\
Pattern (Neighbor) choices:\n\
  -nbrring  - neighbors are +/- distance\n\
  -nbrdbl   - neighbors are +/- 2**distance\n\
  -nbrhc    - neighbors are hypercube\n\
  -nbrshift - neighbors are + distance (wrapped)\n\
" );
}
