#include "mpd.h"

extern struct portentry porttable[MAXFDENTRIES];
extern int    myrank;	
extern int    console_idx;
extern int    debug;

int setup_network_socket( int *port ) /* returns fd */
{
    int backlog = 15;
    int rc;
    mpd_sockopt_len_t sinlen;
    int skt_fd;
    struct sockaddr_in s_in;

    s_in.sin_family	 = AF_INET;
    s_in.sin_addr.s_addr = INADDR_ANY;
    s_in.sin_port	 = htons( *port );
    sinlen		 = sizeof( s_in );

    skt_fd = socket( AF_INET, SOCK_STREAM, 0 );
    error_check( skt_fd, "setup_network_socket: socket" );

    rc = bind( skt_fd, ( struct sockaddr * ) &s_in, sizeof( s_in ) );
    error_check( rc, "setup_network_socket: bind" );

    rc = getsockname( skt_fd, (struct sockaddr *) &s_in, &sinlen ); 
    error_check( rc, "setup_network_socket: getsockname" );

    mpdprintf( 0, "network socket port is %d, len = %d\n",
	    ntohs(s_in.sin_port), sinlen);
    *port = ntohs(s_in.sin_port);

    rc = listen( skt_fd, backlog );
    error_check( rc, "setup_network_socket: listen" );
    mpdprintf( debug, "listening on network socket %d\n", skt_fd );

    return skt_fd;
}

int setup_unix_socket( char *pathname )	
{
    int backlog = 15;
    int rc;
    int skt_fd;
    struct sockaddr_un sa;

    bzero( (void *) &sa, sizeof( sa ) );
    sa.sun_family = AF_UNIX;
    strncpy( sa.sun_path, pathname, sizeof( sa.sun_path ) - 1 );

    skt_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( skt_fd < 0 )
        return( skt_fd );

    rc = bind( skt_fd, ( struct sockaddr * )&sa, sizeof( sa ) );
    if ( skt_fd < 0 )
        return( skt_fd );

    rc = listen( skt_fd, backlog );
    if ( rc < 0 )
        return( rc );
    
    mpdprintf( debug, "listening on local socket %d\n", skt_fd );
    return( skt_fd );
}

int network_connect( char *hostname, int port )
{
    int s;
    struct sockaddr_in sa;
    struct hostent *hp;
    int optval = 1;
#   define NUMTOTRY 100
    int rc, numtriesleft, connected;

    hp = gethostbyname( hostname );
    if (hp == NULL)
    {
	char errmsg[80];
	sprintf( errmsg, "network_connect: gethostbyname failed for %s h_errno=%d", 
		 hostname,h_errno );
	if ( h_errno == HOST_NOT_FOUND ) 
	    strcat( errmsg, " HOST_NOT_FOUND " );
	mpdprintf( 1, "%s\n", errmsg );
	fatal_error( -1, errmsg );	
    }

    mpdprintf( debug, "attempting network connection to %s, port %d\n",
	     hostname, port );

    bzero((void *)&sa, sizeof(sa));
    bcopy((void *)hp->h_addr, (void *)&sa.sin_addr, hp->h_length);
    sa.sin_family = hp->h_addrtype;
    sa.sin_port	  = htons(port);

    connected = 0;
    numtriesleft  = NUMTOTRY; 

    s = socket( AF_INET, SOCK_STREAM, 0 );
    error_check( s, "network_connect, socket" );

    rc = setsockopt( s, IPPROTO_TCP, TCP_NODELAY, (char *) &optval, sizeof( optval ) );
    error_check( rc, "network_connect, setsockopt" );

    while ( !connected && numtriesleft > 0 ) {
	rc = connect( s, (struct sockaddr *) &sa, sizeof(sa) );
	if ( rc == 0 )
	    connected = 1;
	else 
	    numtriesleft--;
    }
    if ( !connected ) {
	mpdprintf( 111, "failed to connect to port %d on host %s after %d tries\n",
		   port, hostname, NUMTOTRY );
	return( -1 );
    }

    if ( numtriesleft < NUMTOTRY )
	mpdprintf( debug, "network_connect, connected on fd %d after %d %s\n", s,
		   NUMTOTRY + 1 - numtriesleft,
		   NUMTOTRY + 1 - numtriesleft > 1 ? "tries" : "try" );
    return s;
}

int accept_connection( int skt )
{
    struct sockaddr_in from;
    int new_skt, gotit, rc;
    mpd_sockopt_len_t fromlen;
    int optval = 1;

    mpdprintf( 0, "accepting connection on %d\n", skt );
    fromlen = sizeof( from );
    gotit = 0;
    while ( !gotit ) {
	new_skt = accept( skt, ( struct sockaddr * ) &from, &fromlen );
	if ( new_skt == -1 )
	{
	    if ( errno == EINTR )
		continue;
	    else
		error_check( new_skt, "accept_connection accept" );
	}
	else
	    gotit = 1;
    }

    rc = setsockopt( new_skt, IPPROTO_TCP, TCP_NODELAY, (char *) &optval,
		     sizeof( optval ) );
    error_check( rc, "accept_connection, setsockopt" );

    mpdprintf( debug, "accept_connection; new socket = %d\n", new_skt );
    return( new_skt );
}


int accept_unix_connection( int skt )
{
    struct sockaddr_in from;
    int new_skt, gotit;
    mpd_sockopt_len_t fromlen;

    mpdprintf( 0, "accepting unix connection on %d\n", skt );
    fromlen = sizeof( from );
    gotit = 0;
    while ( !gotit ) {
	new_skt = accept( skt, ( struct sockaddr * ) &from, &fromlen );
	if ( new_skt == -1 )
	{
	    if ( errno == EINTR )
		continue;
	    else
		error_check( new_skt, "accept_connection accept" );
	}
	else
	    gotit = 1;
    }

    mpdprintf( debug, "accept_unix_connection; new socket = %d\n", new_skt );
    return( new_skt );
}

int local_connect( char *name )	
{
    int s, rc;
    struct sockaddr_un sa;

    bzero( (void *)&sa, sizeof( sa ) );

    sa.sun_family = AF_UNIX;
    strncpy( sa.sun_path, name, sizeof( sa.sun_path ) - 1 );

    s = socket( AF_UNIX, SOCK_STREAM, 0 );
    error_check( s, "local_connect: socket" );

    rc = connect( s, ( struct sockaddr * ) &sa, sizeof( sa ) );

    if ( rc != -1 ) {
	mpdprintf( debug, "local_connect; socket = %d\n", s );
	return ( s );
    }
    else
	return( rc );
}

void send_msg( int fd, char *buf, int size )	
{
    int n;

    /* maybe should check whether size < MAXLINE? */
    n = write( fd, buf, size );
    if ( n < 0 )
	mpdprintf(1, "error on write; buf=:%s:\n", buf );
    error_check( n, "send_msg write" );
}

int recv_msg( int fd, char *buf, int size )
{
    int n;

    n = read( fd, buf, size );
    error_check( n, "recv_msg read" );
    if ( n == 0 )
	return( RECV_EOF );
    return( RECV_OK );
}

