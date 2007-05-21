#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static int err = 0;
static int childpid;

void set_fd_nonblock( int );
int bread( int, void *, int );

#if !defined(EAGAIN) 
#define EAGAIN EWOULDBLOCK
#endif

int main( int argc, char *argv[] )
{
    int fds[2];
    int val, n;

    /* Create the pipe */
    /* Some systems create bi-directional pipes, others unidirectional */
    /* For unidirectional systems, fds[0] is for reading, fds[1] is for
       writing */
    if (pipe(fds)) {
	perror( "Pipe creating failed" );
	exit(1);
    }
    /* Set the properties */
    set_fd_nonblock( fds[0] );
    set_fd_nonblock( fds[1] );

    /* Create the child */
    childpid = fork();
    if (childpid < 0) {
	perror( "Fork failed" );
	exit(1);
    }
    /* We must be careful.  Once a process uses an end of the pipe, it
       gets that end of the pipe forever.  Thus, if the pipes are 
       unidirectional, and both the parent and child need to write, 
       we need two pipe sets. (LINUX is unidirectional, for example.) 
     */
    if (childpid) {
	/* I am the parent */
#ifdef FOO
	/* I do the receiving so that I can exit with a status code
	   reflecting the success or failure of the test */
	/* Synchronize with child */
	/* ???? what is needed???? */
	/* This is needed on Linux running gcc version 2.91.66 */

	val = -2;
	n = write( fds[1], &val, sizeof(int) );
	if (n < 0) {
	    perror( "Write failed in parent" );
	    exit( 1 );
	}
#ifdef DEBUG
	printf( "Parent synchronized\n" ); fflush(stdout);
#endif	
#endif
	close(fds[1]);
	
	/* Start reading */
	do { 
	    val = 1;
	    n = bread( fds[0], &val, sizeof(int) );
	    if (n < 0) {
		if (errno == EAGAIN) continue;
		printf( "n = %d and errno = %d\n", n, errno );
		perror( "Read error in parent" );
		err++;
		break;
	    }
#ifdef DEBUG
	    printf( "Read %d in parent\n", val );
#endif
	} while (val > 0);
	if (val == -1) {
	    /* We have received the EOF */
#ifdef DEBUG
	    printf( "Checking for EOF in parent\n" );
#endif	    
	    n = bread( fds[0], &val, sizeof(int) );
	    if (n != 0) {
		fprintf( stderr, "Expected EOF on pipe\n" );
		err ++;
	    }
    	}
	else {
	    fprintf( stderr, "Unexpected output data (%d) read in parent\n", 
		     val );
	    err ++;
	}
#ifdef DEBUG
	printf( "Parent found %d errors\n", err ); fflush(stdout);
#endif
	close( fds[0] );
	if (err) kill( childpid, SIGINT );
	return err;
    }
    else {
	/* I am the child */
	/* I will read and write to fds[0] */
#ifdef FOO
	/* Synchronize with parent */
	/* This is needed on Linux running gcc version 2.91.66 */
	n = bread( fds[0], &val, sizeof(int) );
#ifdef DEBUG
	printf( "Child synchronized\n" ); fflush(stdout);
#endif	
	if (val != -2) {
	    perror ("Read failed in child" );
	    exit(1);
	}
#endif

	close(fds[0]);
	/* Start writing */
	for (val=1; val<10; val++) {
#ifdef DEBUG
	    printf( "Writing %d in child\n", val );
#endif
	    n = write( fds[1], &val, sizeof(int) );
	    if (n < 0) {
		perror( "Write failed in child" );
		exit(1);
	    }
	}
	/* pause */
	sleep( 2 );

	/* Write some more */
	for (val=20; val<30; val++) {
#ifdef DEBUG
	    printf( "Writing %d in parent\n", val );
#endif
	    n = write( fds[1], &val, sizeof(int) );
	    if (n < 0) {
		perror( "Write failed in child" );
		exit(1);
	    }
	}
	
	/* Send shutdown */
	val = -1;
	n = write( fds[1], &val, sizeof(int) );
	if (close(fds[1])) {
	    perror( "Error closing pipe" );
	    exit(1);
	}

    }
    /* statement unreached ? */
    return 0;
}

void set_fd_nonblock( int fd )
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
	perror( "Could not get fcntl for pipe" );
	exit(1);
    }
#ifdef O_NONBLOCK
    flags |= O_NONBLOCK;
#endif
#ifdef O_NDELAY
    flags |= O_NDELAY;
#endif
#if !defined(O_NONBLOCK) && !defined(O_NDELAY)
    fprintf( stderr, "fcntl flags not defined\n" );
    exit(1);
#endif
    if (fcntl(fd, F_SETFL, flags) < 0) {
	perror( "Could not set fcntl for pipe" );
	exit(1);
    }
}

/* Blocking read */
int bread( int fd, void *buf, int size )
{
    int n;
    
#ifdef DEBUG
	printf( "Blocking read for %d bytes in %s\n", size,
		childpid ? "parent" : "child" ); fflush(stdout);
#endif	
    while (1) {
	n = read( fd, buf, size );
	if (n < 0) {
	    if (errno == EAGAIN) continue;
	    /* printf( "n = %d and errno = %d\n", n, errno ); */
	    perror( "Read error in child" );
	    err++;
	    break;
	}
#ifdef DEBUG
	printf( "Read %d bytes in %s\n", n,
		childpid ? "parent" : "child" ); fflush(stdout);
#endif	
	if (n == size) break;
	if (n == 0) break; /* EOF */
    }
#ifdef DEBUG
    printf( "Returning n = %d from bread\n", n ); fflush(stdout);
#endif    
    return n;
}
