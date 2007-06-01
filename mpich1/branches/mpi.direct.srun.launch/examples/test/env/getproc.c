/*
 * Test get processor name
 *
 */
#include "mpi.h"
#include <string.h>
#include <stdio.h>

int main( int argc, char *argv[] )
{
    char name[MPI_MAX_PROCESSOR_NAME+10];
    int  resultlen;
    int  err = 0;

    MPI_Init( &argc, &argv );

    memset( name, 0xFF, MPI_MAX_PROCESSOR_NAME+10 );
    resultlen = 0;

    MPI_Get_processor_name( name, &resultlen );
    /* Test that name has only printing characters */
    if (resultlen > MPI_MAX_PROCESSOR_NAME || resultlen <= 0) {
	fprintf( stderr, "resultlen (%d) invalid\n", resultlen );
	err++;
    }
    if (!err) {
	int i;
	for (i=0; i<resultlen; i++) {
	    if (!isprint(name[i])) {
		fprintf( stderr, "Character number %d is not printable\n", i );
		err++;
	    }
	}
	if (name[resultlen]) {
	    fprintf( stderr, "No null at end of name\n" );
	    err++;
	}
	for (i=resultlen+1; i<MPI_MAX_PROCESSOR_NAME+10; i++) {
	    unsigned char *usname = (unsigned char*)name;
	    if ((int)(usname[i]) != 0xFF) {
		fprintf( stderr, "Characters changed at end of name\n" );
		err++;
	    }
	}
    }

    if (err) {
	printf( " Found %d errors\n", err );
    }
    else {
	printf( " No Errors\n" );
    }
	
    MPI_Finalize();
    return 0;
}
