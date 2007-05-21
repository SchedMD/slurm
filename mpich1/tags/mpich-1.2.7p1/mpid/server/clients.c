/*
 * This file contains routines to contact the demon and interact with it
 */

#include <sys/types.h>
#include <stdio.h>

#include <errno.h>
#include <sys/param.h>
#include <sys/socket.h>

/* sockaddr_in (Internet) */
#include <netinet/in.h>
/* TCP_NODELAY */
#include <netinet/tcp.h>

/* sockaddr_un (Unix) */
#include <sys/un.h>

/* defs of gethostbyname */
#include <netdb.h>

/* fcntl, F_GET/SETFL */
#include <fcntl.h>

#ifndef MAX_HOST_NAME
#define MAX_HOST_NAME 1024
#endif

/* This is really IP!? */
#ifndef TCP
#define TCP 0
#endif

/*
 * Create a local network connection.
 * Returns fd, or -1 if failure.
 */
int net_create_local_conn( server_path )
char *server_path;
{
    struct sockaddr_un sa;
    int                fd;

    bzero( &sa, sizeof(sa) );
    sa.sun_family = AF_UNIX;
    strncpy( sa.sun_path, server_path, sizeof(sa.sun_path) - 1 );

    fd = socket( AF_UNIX, SOCK_STREAM, TCP );
    if (fd < 0) {
	return -1;
    }

    if (connect( fd, &sa, sizeof(sa) ) < 0) {
	perror( "Failed to connect: " );
	switch (errno) {
	case ECONNREFUSED:
	    /* (close socket, get new socket, try again) */
	    return -1;

	case EISCONN: /*  (already connected) */
	    break;
	
	case ETIMEDOUT: /* timed out */
	    return -1;

	default:
	    return -1;
	}
    }

    return fd;
}
