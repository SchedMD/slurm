/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2004 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpi.h"
#include <stdio.h>

/*
 * This program reports on properties of the MPICH library, such as the
 * version, device, and what patches have been applied.  This is available
 * only since MPICH 1.2.6
 */

extern const int MPIR_Version_patches[];
extern const int MPIR_Version_major;
extern const int MPIR_Version_minor;
extern const int MPIR_Version_subminor;
extern const char MPIR_Version_string[];
extern const char MPIR_Version_date[];
extern const char MPIR_Version_configure[];
extern const char MPIR_Version_device[];

typedef enum { Version_number=0, Date=1, 
	       Patches=2, Configure_args=3, Device=4 } fields;

/*D
  mpichversion - Report on the MPICh version

  Command Line Arguments:
+ -version - Show the version of MPICH
. -date    - Show the release date of this version
. -patches - Show the identifiers for any applied patches
. -configure - Show the configure arguments used to build MPICH
- -device  - Show the device for which MPICH was configured

  Using this program:
  To use this program, link it against 'libmpich.a' (use 'mpicc' or 
  the whichever compiler command is used to create MPICH programs)
  D*/

int main( int argc, char *argv[] )
{
    int i, flags[6];
    
    if (argc <= 1) {
	/* Show all values */
	for (i=0; i<5; i++) flags[i] = 1;
    }
    else {
	/* Show only requested values */
	for (i=0; i<5; i++) flags[i] = 0;
	for (i=1; i<argc; i++) {
	    if (strcmp( argv[i], "-version" ) == 0) 
		flags[Version_number] = 1;
	    else if (strcmp( argv[i], "-date" ) == 0) 
		flags[Date] = 1;
	    else if (strcmp( argv[i], "-patches" ) == 0)
		flags[Patches] = 1;
	    else if (strcmp( argv[i], "-configure" ) == 0) 
		flags[Configure_args] = 1;
	    else if (strcmp( argv[i], "-device" ) == 0)
		flags[Device] = 1;
	    else {
		fprintf( stderr, "Unrecognized argument %s\n", argv[i] );
		exit(1);
	    }
	}
    }

    /* Print out the information, one item per line */
    if (flags[Version_number]) {
	printf( "MPICH Version:    \t%s\n", MPIR_Version_string );
    }
    if (flags[Date]) {
	printf( "MPICH Release date:\t%s\n", MPIR_Version_date );
    }
    if (flags[Patches]) {
	printf( "MPICH Patches applied:\t" );
	if (MPIR_Version_patches[0] < 0) {
	    printf( "none\n" );
	}
	else {
	    i = 0;
	    while (i < 100 && MPIR_Version_patches[i] > 0) {
		printf( "%d ", MPIR_Version_patches[i] );
		i++;
	    }
	    printf( "\n" );
	}
    }
    if (flags[Configure_args]) {
	printf( "MPICH configure: \t%s\n", MPIR_Version_configure );
    }
    if (flags[Device]) {
	printf( "MPICH Device:    \t%s\n", MPIR_Version_device );
    }

    return 0;
}
