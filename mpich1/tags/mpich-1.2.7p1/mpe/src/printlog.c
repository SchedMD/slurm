#include "mpeconf.h"
#include "clogimpl.h"
#include <fcntl.h>
#if defined( HAVE_UNISTD_H )
#include <unistd.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STDLIB_H )
#include <stdlib.h>
#endif


int main( int argc, char *argv[] )
{
    int logfd;			/* logfile */
    int n;
    double buf[ CLOG_BLOCK_SIZE / sizeof( double ) ];

    if (argc < 2) {
	fprintf(stderr,"usage:  printlog <logfile>\n");
	exit(-1);
    }

    if ((logfd = OPEN(argv[1], O_RDONLY, 0)) == -1)
    {
	printf("could not open file %s for reading\n",argv[1]);
	exit(-2);
    }

    n = read(logfd, buf, CLOG_BLOCK_SIZE);
    while (n) {
	if (n != CLOG_BLOCK_SIZE) {
	    fprintf(stderr,"could not read %d bytes\n", CLOG_BLOCK_SIZE);
	    exit(-3);
	}
	CLOG_dumpblock(buf);
	n = read(logfd, buf, CLOG_BLOCK_SIZE);
    }	
    return(0);
}
