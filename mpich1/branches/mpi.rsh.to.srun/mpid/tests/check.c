#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"
#include "mpimem.h"
#include "aditest.h"

/* Data test routine */
int CheckData( sbuf, rbuf, len )
char *sbuf, *rbuf;
int len;
{
    int i;
    int errcnt = 0;
    for (i=0; i<len; i++) {
	if (sbuf[i] != rbuf[i]) {
	    fprintf( stderr, "[%d] Expected %d but saw %d at rbuf[%d]\n",
		     MPID_MyWorldRank, sbuf[i], rbuf[i], i );
	    if (errcnt++ > 10) break;
	}
    }
    return errcnt;
}

/* Data test routine */
int CheckDataS( sbuf, rbuf, len, msg )
short *sbuf, *rbuf;
int   len;
char  *msg;
{
    int i;
    int errcnt = 0;
    for (i=0; i<len; i++) {
	if (sbuf[i] != rbuf[i]) {
	    fprintf( stderr, "[%d] Expected %d but saw %d at rbuf[%d] %s\n",
		     MPID_MyWorldRank, sbuf[i], rbuf[i], i, msg );
	    if (errcnt++ > 10) break;
	}
    }
    return errcnt;
}

/* Check status */
int CheckStatus( status, source, tag, len )
MPI_Status *status;
int        source, tag, len;
{
    int errcnt = 0;
    if (status->MPI_SOURCE != source) {
	errcnt++;
	fprintf( stderr, 
		 "%d received message from %d, expected %d\n", 
		 MPID_MyWorldRank, status->MPI_SOURCE, source );
    }
    if (status->MPI_TAG != tag) {
	errcnt++;
	fprintf( stderr, 
		 "%d received message tag %d, expected %d\n", 
		 MPID_MyWorldRank, status->MPI_TAG, tag );
    }
    if (status->count != len) {
	errcnt++;
	fprintf( stderr, 
		 "%d received %d bytes, expected %d\n",
		 MPID_MyWorldRank, status->count, len );
    }
    return errcnt;
}

void SetupArgs( argc, argv, len, master, slave )
int argc, *len, *master, *slave;
char **argv;
{
    int i;

    for (i=1; i<argc; i++) {
	if (argv[i]) {
	    if (strcmp(argv[i],"-len") == 0) {
		i++;
		*len = atoi(argv[i]);
	    }
	    else if (strcmp(argv[i],"-swap") == 0) {
		*master = 0;
		*slave  = 1;
	    }
	    else 
		printf( "Unrecognized argument %s\n", argv[i] );
	}
    }
}

void SetupTests( argc, argv, len, master, slave, sbuf, rbuf )
int argc, *len, *master, *slave;
char **argv, **sbuf, **rbuf;
{
    int i;
    char *p;

    SetupArgs( argc, argv, len, master, slave );

    *sbuf = (char *)MALLOC( *len );
    if (!*sbuf) {
	MPID_Abort( (MPI_Comm)0, 1, (char *)0, "No buffer space" );
	exit(1); /* just in case */
    }
    *rbuf = (char *)MALLOC( *len );
    if (!*rbuf) {
	MPID_Abort( (MPI_Comm)0, 1, (char *)0, "No buffer space" );
	exit(1); /* just in case */
    }
    p = *sbuf;
    for (i=0; i<*len; i++) 
	p[i] = (char)i;
}

void SetupTestsS( argc, argv, len, master, slave, sbuf, rbuf )
int argc, *len, *master, *slave;
char  **argv;
short **sbuf, **rbuf;
{
    int   i;
    short *p;

    SetupArgs( argc, argv, len, master, slave );

    if (sbuf) {
	*sbuf = (short *)MALLOC( *len * sizeof(short) );
	if (!*sbuf) {
	    MPID_Abort( (MPI_Comm)0, 1, (char *)0, "No buffer space" );
	    exit(1); /* just in case */
	}
	p = *sbuf;
	for (i=0; i<*len; i++) 
	    p[i] = (short)i;
    }
    if (rbuf) {
	*rbuf = (short *)MALLOC( *len * sizeof(short) );
	if (!*rbuf) {
	    MPID_Abort( (MPI_Comm)0, 1, (char *)0, "No buffer space" );
	    exit(1); /* just in case */
	}
    }
}

void EndTests( sbuf, rbuf )
void *sbuf, *rbuf;
{
    FREE( sbuf );
    FREE( rbuf );
}
