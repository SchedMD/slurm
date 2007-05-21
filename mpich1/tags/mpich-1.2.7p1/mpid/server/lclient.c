/*
 * This is a local client interface.  It provides a console connection to 
 * a demon.
 */

/* server.h includes the results of the configure test */
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

int main( argc, argv )
int argc;
char **argv;
{
    int  fd;
    FILE *fp_in, *fp_out;
    char *local_username;
    char outline[1024];
    char inputline[1024];
    struct passwd *pw;

    /* User user_name */
    pw = getpwuid(geteuid());
    if (pw == NULL)
    {
	extern char *getlogin (void);

	local_username = getlogin();
	if (local_username == NULL)
	{
	    fprintf( stderr, "Cannot get pw entry\n" );
	    exit( 1 );
	}
    }
    else
    {
	local_username = pw->pw_name;
    }


    /* Connect to local demon */
    fd = net_create_local_conn( "/tmp/servertest" );
    if (fd < 0) {
	perror( "Failed to create connection" );
	return 1;
    }
    
    /* Establish credentials with the demon */
    fp_in = fdopen( fd, "r" );
    fp_out = fdopen( fd, "a" );
    if (!fp_in || !fp_out) {
	fprintf( stderr, "Could not convert fd to FILE *\n" );
	exit( 1 );
    }
    fputs( local_username, fp_out ); 
    fputs( "\n", fp_out );
    fputs( local_username, fp_out );
    fputs( "\n", fp_out );
    fflush( fp_out );
    if (!fgets( outline, 1024, fp_in )) {
	fprintf( stderr, "Error reading proceed from server\n" );
    }
    /* fputs( "from server:", stdout ); 
    fputs( outline, stdout );
    fflush( stdout ); */

    /* Read commands from stdin, get answers from demon */
    while ( 1 ) {
	fd_set readfds;
	int    nfds;

	FD_ZERO(&readfds);
	FD_SET(fd,&readfds);
	FD_SET(0,&readfds);
	nfds = select( fd+1, &readfds, (void *)0, (void *)0, (void *)0 );
	if (nfds > 0) {
	    if (FD_ISSET(0,&readfds)) {
		if (!fgets( inputline, 1024, stdin )) break;
		fputs( inputline, fp_out );
		fflush( fp_out );
	    }
	    if (FD_ISSET(fd,&readfds)) {
		if (!fgets( inputline, 1024, fp_in )) break;
		fputs( inputline, stdout );
		fflush( stdout );
	    }
	}
    }

    return 0;
}
