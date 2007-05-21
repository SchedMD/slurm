/*
 * This code was generated from an older version by an automatic tool;
 * it still needs to be cleaned up.
 */
int __NUMNODES, __MYPROCID  ;

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <stdio.h>
#include <time.h>
#include <sys/types.h>       
#include <sys/time.h>        
#include <stdio.h>

#include "getopts.h"

#include "mpi.h"
extern int __NUMNODES, __MYPROCID;

/* Prototypes */
void SetBuffer( unsigned long *, int, int );
void PrintHelp( char ** );
int ErrTest( MPI_Status *status, int partner, int bufsize, 
	     unsigned long *buffer, int pattern );
int CheckBuffer( unsigned long *buf, int size, int pattern );

#define STRESS_PRINT_INTERVAL 60
/*
    This is a simple program to stress the communications performance of
    a parallel machine.  The program tcomm does a more exhaustive
    test of the individual links.

    In order to have "safe(0,0)" versions of these for the blocking case,
    we need to order the sends and receives so that there is always a
    consumer of messages. 
 */
    
int EachToAll( int, int ), EachToAllNB( int, int ), 
    AllToAll( int, int ), AllToAllNB( int, int ), AllToAllPhased( int, int );

#define NPATTERNS 12
static unsigned long Patterns[12] = {
    0xffffffff, 0xaaaaaaaa, 0x88888888, 0x80808080, 0x80008000, 0x80000000,
    0x00000000, 0x55555555, 0x77777777, 0x7f7f7f7f, 0x7fff7fff, 0x7fffffff };
static double bytes_sent;

/* Set needs_newline to 1 if flushes without newlines don't work (IBM SP) */
static int needs_newline = 0;

typedef enum { Blocking, NonBlocking } Protocol;

void BigFlush( FILE *fp, int lastnl )
{
   if (needs_newline && !lastnl) fputs( "\n", fp );
   fflush( fp );
}

int main( int argc, char *argv[] )
{
    int (* f)( int, int );
    int      size;
    unsigned long pattern;
    int      first,last,incr, svals[3];
    Protocol protocol  = Blocking;
    int      toall     = 0, isphased = 0;
    FILE     *fp = stdout;
    int      err, curerr;
    struct timeval endtime, currenttime, nextprint, starttime;
    char     ttime[50];
    const char *test_type_msg = 0;
    int      BeVerbose = 0;
    int      Quiet = 0;
    int      loopcount;
    double   bytes_so_far;

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &__NUMNODES );
    MPI_Comm_rank( MPI_COMM_WORLD, &__MYPROCID );

    if (SYArgHasName( &argc, argv, 1, "-help" )) {
	if (__MYPROCID == 0) {
	    PrintHelp( argv );
	}
	MPI_Finalize();
	return 0;
    }

    if (__NUMNODES < 2) {
	fprintf( stderr, "Must run stress with at least 2 nodes\n" );
	return 1;
    }
    f             = EachToAll;
    svals[0]      = 32;
    svals[1]      = 1024;
    svals[2]      = 32;
    SYGetDayTime( &endtime );
    SYGetDayTime( &currenttime );
    starttime = currenttime;
    nextprint = currenttime;

    if (SYArgHasName( &argc, argv, 1, "-async" ))     protocol  = NonBlocking;
    if (SYArgHasName( &argc, argv, 1, "-sync"  ))     protocol  = Blocking;
    toall = SYArgHasName( &argc, argv, 1, "-all" );
    SYArgGetIntVec( &argc, argv, 1, "-size", 3, svals );
    isphased = SYArgHasName( &argc, argv, 1, "-phased" );
    if (SYArgGetString( &argc, argv, 1, "-ttime", ttime, 50 )) {
	endtime.tv_sec = SYhhmmtoSec( ttime ) + currenttime.tv_sec;
    }

    /* Options for output control */
    if (SYArgHasName( &argc, argv, 1, "-needsnewline" ))
	needs_newline = 1;
    BeVerbose = SYArgHasName( &argc, argv, 1, "-verbose" );
    Quiet     = SYArgHasName( &argc, argv, 1, "-quiet" );

    /* Determine the test */
    f        = EachToAll;
    switch( protocol ) {
    case NonBlocking:
        if (toall) {
	    f = AllToAllNB;
	    test_type_msg = "All to All non-blocking\n";
	}
        else {
	    f = EachToAllNB;
	    test_type_msg = "Each to all non-blocking\n";
        }
        break;
    case Blocking:      
        if (toall) {
	    if (isphased) {
		f = AllToAllPhased;
		test_type_msg = "All to All phased\n";
	    }
	    else {
		f = AllToAll;
		test_type_msg = "All to All (requires buffering)\n";
	    }
	}
        else {
	    f = EachToAll;  
	    test_type_msg = "Each to All\n";
	}
        break;
    }
    first = svals[0];
    last  = svals[1];
    incr  = svals[2];

    if (__MYPROCID == 0) fputs( test_type_msg, stdout );

/* Disable resource checking */
    MPI_Bcast(&endtime.tv_sec, sizeof(int), MPI_BYTE, 0, MPI_COMM_WORLD );
    err       = 0;
    loopcount = 0;
    bytes_sent= 0.0;
    BigFlush( stdout, 1 );
    do {
	for (pattern=0; pattern<=NPATTERNS; pattern++) {
	    for (size=first; size<=last; size+=incr) {
		if (__MYPROCID == 0 && !Quiet) {
		    fprintf( fp, "." ); BigFlush( fp, 0 );
                }
		if (__MYPROCID == 0 && BeVerbose) {
		    fprintf( fp, "Running size = %d longs with pattern %lx\n", 
			     size, (pattern < NPATTERNS) ? Patterns[pattern] : 
			     pattern );
		    BigFlush( fp, 1 );
                }
		curerr = (*f)( pattern, size );
		err += curerr;
		if (curerr > 0) {
		    fprintf( fp, 
			     "[%d] Error running size = %d longs with pattern %lx\n", 
			     __MYPROCID, size, 
			     (pattern < NPATTERNS) ? Patterns[pattern] : pattern );
		    BigFlush( fp, 1 );
		}
            }
	    if (__MYPROCID == 0) {
		if (Quiet) {
		    if (Quiet++ > 70) {
			fputs( "+\n", fp );
			Quiet=1;
		    }
		    else {
			fputs( "+", fp );
		    }
		    BigFlush( fp, 1 );
		}
		else {
		    fputs( "+\n", fp ); 
		    BigFlush( fp, 1 );
		}
	    }
        }
	loopcount++;
	/* Make sure that everyone will do the same test */
	MPI_Allreduce(&err, &size, 1, MPI_INT,MPI_SUM,MPI_COMM_WORLD );
	err = size;
	SYGetDayTime( &currenttime );
	MPI_Bcast(&currenttime.tv_sec, sizeof(int), MPI_BYTE, 0, MPI_COMM_WORLD );
	MPI_Allreduce(&bytes_sent, &bytes_so_far, 1, MPI_DOUBLE, MPI_SUM,
		      MPI_COMM_WORLD );
	if (__MYPROCID == 0 && nextprint.tv_sec <= currenttime.tv_sec) {
	    char   *str, *p;
	    char   timebuf[100];
	    double rate, delta_t;
	    /* Exercise care in case difference in times is zero */
	    delta_t = 1.0e6 * (currenttime.tv_sec - starttime.tv_sec) + 
		   (currenttime.tv_usec - starttime.tv_usec);
	    if (delta_t > 0.0) rate = bytes_so_far / delta_t;
	    else               rate = 0.0;
	    nextprint.tv_sec = currenttime.tv_sec + STRESS_PRINT_INTERVAL;
	    str = ctime(&currenttime.tv_sec);
	    /* Copy str into a buffer, deleting the trailing newline */
	    p = timebuf;
	    while (*str && *str != '\n') *p++ = *str++;
	    *p = 0;
	    fprintf( stdout, "stress runs to %s (%d) [%f MB/s aggregate]\n", 
		     timebuf, loopcount, rate );
	  
	    BigFlush( stdout, 1 );
	}
    } while (err == 0 && currenttime.tv_sec <= endtime.tv_sec );

    if (__MYPROCID == 0) {
	fprintf( stdout, "Stress completed %d tests\n", loopcount );
	fprintf( stdout, "%e bytes sent\n", bytes_so_far );
    }
    MPI_Finalize();
    return 0;
}

int EachToAll( int pattern, int size )
{
    int sender, dest, tag, err=0, bufmsize, bufsize;
    unsigned long *buffer;
    MPI_Status status;

    buffer = (unsigned long *)malloc((unsigned)(size * sizeof(long) ));  
    if (!buffer)return 0;;
    bufsize = size * sizeof(long);
    for (sender=0; sender < __NUMNODES; sender++) {
	tag = sender;
	if (__MYPROCID == sender) {
	    for (dest=0; dest < __NUMNODES; dest++) {
		if (sender != dest) {
		    SetBuffer( buffer, size, pattern );
		    MPI_Send(buffer,bufsize,MPI_BYTE,dest,tag,MPI_COMM_WORLD);
		    bytes_sent += bufsize;
		}
	    }
	}
	else {
	    bufmsize = bufsize;
	    MPI_Recv(buffer,bufmsize,MPI_BYTE,MPI_ANY_SOURCE,tag,
		     MPI_COMM_WORLD,&status);
	    err += ErrTest( &status, sender, bufsize, buffer, pattern );
	}
    }
    free(buffer );
    return err;
}

/*
   Each to all nonblocking

   In sequence, starting from process 0,   
     Process "sender" sends buffer + i*size to process i, except for itself, 
         with tag i
     All processes, except for "sender", receive a 

 */
int EachToAllNB( int pattern, int size )
{
    int sender, dest, err=0, bufsize, i;
    unsigned long *sbuffer, *rbuffer;
    MPI_Request *sid, *rid;
    MPI_Status  status;
    int         mysize, myrank, recvlen;

    MPI_Comm_size( MPI_COMM_WORLD, &mysize );
    MPI_Comm_rank( MPI_COMM_WORLD, &myrank );

    sbuffer = (unsigned long *)malloc((unsigned)(mysize * size * sizeof(long) ));  if (!sbuffer)return 0;
    rbuffer = (unsigned long *)malloc((unsigned)(mysize * size * sizeof(long) ));  if (!rbuffer)return 0;
    sid     = (MPI_Request *)malloc((unsigned)(mysize * sizeof(MPI_Request) )); if (!sid)return 0;
    rid     = (MPI_Request *)malloc((unsigned)(mysize * sizeof(MPI_Request) )); if (!rid)return 0;
    bufsize = size * sizeof(long);

    /* Send a buffer to everyone */
    sid[myrank] = MPI_REQUEST_NULL;
    for (dest = 0; dest < mysize; dest++) {
	if (myrank == dest) continue;
	SetBuffer( sbuffer+dest*size, size, pattern );
	MPI_Isend( sbuffer+dest*size, bufsize, MPI_BYTE,
		   dest, myrank, MPI_COMM_WORLD, &sid[dest] );
	bytes_sent += bufsize;
    }
    /* Receive from everyone */
    rid[myrank] = MPI_REQUEST_NULL;
    for (sender = 0; sender < mysize; sender++) {
	if (myrank == sender) continue;
	MPI_Irecv( rbuffer+sender*size, bufsize, MPI_BYTE,
		   sender, sender, MPI_COMM_WORLD, &rid[sender] );
    }

    /* Complete all communication */
    /* This should be changed to use an MPI_Waitall */
    for (i=0; i<mysize; i++) {
	if (rid[i]) {
	    MPI_Wait(&(rid[i] ),&status);
	    /* i should = tag, since tag == sender in above loop */
	    err += ErrTest( &status, i,
			    bufsize, rbuffer + status.MPI_TAG*size, 
			    pattern );
	}
	if (sid[i]) {
	    MPI_Wait(&(sid[i]),&status);
	}
    }
    
    free( rbuffer );
    free( sbuffer );
    free( sid );
    free( rid );
    return err;
}

/* This routine requires that the message passing system buffer significant
   amounts of data.  The routine AllToAllNB using nonblocking receives
 */
int AllToAll( int pattern, int size )
{
    int sender, dest, tag, err=0, bufmsize, bufsize;
    unsigned long *buffer;
    MPI_Status status;

    buffer	 = (unsigned long *)malloc((unsigned)(size * sizeof(long) ));  
    if (!buffer)return 0;
    bufsize	 = size * sizeof(long);
    bufmsize = bufsize;
    tag	 = __MYPROCID;
    for (dest=0; dest < __NUMNODES; dest++) {
	if (__MYPROCID != dest) {
	    SetBuffer( buffer, size, pattern );
	    MPI_Send(buffer,bufsize,MPI_BYTE,dest,tag,MPI_COMM_WORLD);
	    bytes_sent += bufsize;
	}
    }
    for (sender=0; sender<__NUMNODES; sender++) {
	tag = sender;
	if (__MYPROCID != sender) {
	    bufmsize = bufsize;
	    MPI_Recv(buffer,bufmsize,MPI_BYTE,MPI_ANY_SOURCE,tag,
		     MPI_COMM_WORLD,&status);
	    err += ErrTest( &status, sender,
			    bufsize, buffer, pattern );
	}
    }
    free(buffer );
    return err;
}

int AllToAllNB( int pattern, int size )
{
    int sender, dest, tag, err=0, bufmsize, bufsize;
    MPI_Request *rc;
    unsigned long *buffer;
    MPI_Status status;

    rc      = (MPI_Request *)malloc((unsigned)(__NUMNODES * sizeof(MPI_Request) ));
    if (!rc) {
	MPI_Abort( MPI_COMM_WORLD, 1 ); 
    }

    buffer  = (unsigned long *)malloc((unsigned)(__NUMNODES * size * sizeof(long) ));  if (!buffer)return 0;;
    bufsize = size * sizeof(long);
    bufmsize = bufsize;
    for (sender = 0; sender < __NUMNODES; sender++) {
	if (sender != __MYPROCID) {
	    tag = sender;
	    MPI_Irecv(buffer+sender*size,bufmsize,MPI_BYTE,MPI_ANY_SOURCE,tag,
		      MPI_COMM_WORLD,&(rc[sender] ));
	}
    }
    tag     = __MYPROCID;
    for (dest=0; dest < __NUMNODES; dest++) {
	if (__MYPROCID != dest) {
	    SetBuffer( buffer, size, pattern );
	    MPI_Send(buffer,bufsize,MPI_BYTE,dest,tag,MPI_COMM_WORLD);
	    bytes_sent += bufsize;
	}
    }
    for (sender=0; sender<__NUMNODES; sender++) {
	tag = sender;
	if (__MYPROCID != sender) {
	    bufmsize = bufsize;
	    MPI_Wait(&(rc[sender] ),&status);
	    err += ErrTest( &status, sender, bufsize, buffer, pattern );
	}
    }
    free(buffer );
    free(rc );
    return err;
}

/* This version alternates sends and recieves depending on the mask value 

   If the number of processes is odd, we don't use the last process.
 */
int AllToAllPhased( int pattern, int size )
{
    int  d, np, mytid, to, from, idx, err=0, bufmsize, bufsize;
    unsigned long *buffer;
    MPI_Status status;

    np      = __NUMNODES;
    mytid   = __MYPROCID;
/* Only use an even number of nodes */
    if (np & 0x1) np--;
    if (mytid >= np) return 0;

    buffer  = (unsigned long *)malloc((unsigned)(size * sizeof(long) ));  
    if (!buffer)return 0;;
    bufsize = size * sizeof(long);


    for (d=1; d<=np/2; d++) {
	idx  = mytid / d;
	to   = (mytid + d) % np;
	from = (mytid + np - d) % np;
	if (idx & 0x1) {
	    bufmsize = bufsize;
	    MPI_Recv(buffer,bufmsize,MPI_BYTE,MPI_ANY_SOURCE,from,
		     MPI_COMM_WORLD,&status);
	    err += ErrTest( &status, from, bufsize, buffer, pattern );
	    SetBuffer( buffer, size, pattern );
	    MPI_Send(buffer,bufsize,MPI_BYTE,to,mytid,MPI_COMM_WORLD);
	    bytes_sent += bufsize;
	    MPI_Send(buffer,bufsize,MPI_BYTE,from,mytid,MPI_COMM_WORLD);
	    bytes_sent += bufsize;
	    bufmsize = bufsize;
	    MPI_Recv(buffer,bufmsize,MPI_BYTE,MPI_ANY_SOURCE,to,
		     MPI_COMM_WORLD,&status);
	    err += ErrTest( &status, to, bufsize, buffer, pattern );
	}
	else {
	    SetBuffer( buffer, size, pattern );
	    MPI_Send(buffer,bufsize,MPI_BYTE,to,mytid,MPI_COMM_WORLD);
	    bytes_sent += bufsize;
	    bufmsize = bufsize;
	    MPI_Recv(buffer,bufmsize,MPI_BYTE,MPI_ANY_SOURCE,from,
		     MPI_COMM_WORLD,&status);
	    err += ErrTest( &status, from, bufsize, buffer, pattern );
	    bufmsize = bufsize;
	    MPI_Recv(buffer,bufmsize,MPI_BYTE,MPI_ANY_SOURCE,to,
		     MPI_COMM_WORLD,&status);
	    err += ErrTest( &status, to, bufsize, buffer, pattern );
	    SetBuffer( buffer, size, pattern );
	    MPI_Send(buffer,bufsize,MPI_BYTE,from,mytid,MPI_COMM_WORLD);
	    bytes_sent += bufsize;
	}
    }

    free(buffer );
    return err;
}

/*---------------------------------------------------------------------------
  These routines set and check the buffers by inserting the specified pattern
  and checking it.
 --------------------------------------------------------------------------- */
void SetBuffer( unsigned long *buf, int size, int pattern )
{
    unsigned long val;
    int  i;

    if (pattern < NPATTERNS) {
	val = Patterns[pattern];
	for (i=0; i<size; i++) 
	    buf[i] = val;
    }
    else {
	for (i=0; i<size; i++) {
	    buf[i] = Patterns[pattern % NPATTERNS];
	}
    }
}

int CheckBuffer( unsigned long *buf, int size, int pattern )
{
    unsigned long val;
    int  i;

    if (pattern < NPATTERNS) {
	val = Patterns[pattern];
	for (i=0; i<size; i++) 
	    if (buf[i] != val) return 1;
    }
    else {
	for (i=0; i<size; i++) {
	    if (buf[i] != Patterns[pattern % NPATTERNS]) return 1;
	}
    }
    return 0;
}


int ErrTest( MPI_Status *status, int partner, int bufsize, 
	     unsigned long *buffer, int pattern ) 
{
    int err = 0;
    int actsize;
    int from;
    
    from = status->MPI_SOURCE;
    MPI_Get_count( status, MPI_BYTE, &actsize );

    if (from != partner) {
	fprintf( stderr, 
		 "Message from %d should be from %d\n", from, partner );
	err++;
    }
    if (actsize != bufsize) {
	fprintf( stderr, "Message from %d is wrong size (%d != %d)\n", 
		 partner, actsize, bufsize );
	err++;
    }
    if (CheckBuffer( buffer, (int)(actsize / sizeof(long)), pattern )) {
	fprintf( stderr, "Message from %d is corrupt\n", partner );
	err++;
    }
    return err;
}


void PrintHelp( char *argv[] )
{
    fprintf( stderr, "%s - stress test communication\n", argv[0] );
    fprintf( stderr, 
"[-sync | -async  [-size start end stride]\n\
Stress communication links by various methods.  The tests are \n\
combinations of\n\
  Protocol: \n\
  -sync        Blocking sends/receives    (default)\n\
  -async       NonBlocking sends/receives\n\
  -all         AllToAll instead of EachToAll (requires significant buffering\n\
               and should only be used with -async)\n\
  -phased      Use ordered sends/receives for systems will little buffering\n\
\n" );
    fprintf( stderr, 
"  Message sizes:\n\
  -size start end stride                  (default 0 1024 32)\n\
               Messages of length (start + i*stride) for i=0,1,... until\n\
               the length is greater than end.\n\
\n\
  Number of tests\n\
  -ttime hh:mm Total time to run test (for AT LEAST this long)\n\
      (use 0:01 for 1 minute)\n\
\n" );
    fprintf( stderr, "\
  Output control\n\
  -needsnewline Output a new line after each output (needed for systems\n\
               that do not flush output written to stdout)\n\
  -verbose     Describe test for each pattern\n\
  -quiet       Turn off most output\n" );
    fprintf( stderr, "\
  -help        This information\n" );
    fprintf( stderr, "\
%s should be run with an even number of processes; use all available\n\
processes for the most extensive testing\n", argv[0] ? argv[0] : "stress" );
}



