/* Test program for mpd startup */
#include "mpdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAXLINE 256

void main( argc, argv, envp )
int argc;
char *argv[];
char *envp[];
{
    FILE *outfile;
    int  rc;
    int size, job, rank = 99999; 
    char filename[80];
    int i,rjob,rrank;
    char buf[256];

    sprintf( filename, "hellofile.%d", rank );
    outfile = fopen( filename, "w" );
    fprintf( outfile, "Hello %d was here\n", rank );

    fprintf( outfile, "argc = %d", argc );
    for ( i = 0; i < argc; i++ ) 
    {
        fprintf( outfile, ", argv[%d] = %s", i, argv[i] );
	fprintf( outfile, "\n" );
    }
    i = 0;
    while ( envp[i] ) {
	fprintf( outfile, "envp[%d]=%s\n", i, envp[i] );
	i++;
    }
}
