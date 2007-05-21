
/* MPD Client Library */

#include "mpdlib.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

volatile int MPD_global_fence_flag; /* used to implement MPD_Fence */
volatile int MPD_tvdebug_synch_flag = 0;
void (*MPD_user_peer_msg_handler)(char *) = NULL;  /* default */
void mpdlib_sigusr1_handler( int );

static int  mpdlib_myjob, mpdlib_myrank, mpdlib_myjobsize, mpdlib_mpdtvdebug;
static int  mpdlib_man_msgs_fd, mpdlib_peer_listen_fd;
static void mpdlib_getexecname( char *, size_t );
static char mpdlib_myid[MPD_IDSIZE];
static int  mpdlib_debug;

struct mpd_keyval_pairs mpd_keyval_tab[256];
int mpd_keyval_tab_idx;


int MPD_Init( void (*peer_msg_handler)(char *) )
{
    char *p;
    char buf[MPD_MAXLINE];
    char execname[MPD_MAXLINE];
    static int firstcall = 1;

    if ( firstcall )
	firstcall = 0;
    else
	return( 0 );

    setbuf(stdout,NULL);  /* turn off buffering for clients */
    mpdlib_debug = 0;

    MPD_user_peer_msg_handler = peer_msg_handler;
    if ( ( p = getenv( "MPD_TVDEBUG" ) ) )
        mpdlib_mpdtvdebug = atoi( p );
    else
        mpdlib_mpdtvdebug = 0;
    if ( ( p = getenv( "MPD_JID" ) ) )
        mpdlib_myjob = atoi( p );
    else
        mpdlib_myjob = -1;
    if ( ( p = getenv( "MPD_JSIZE" ) ) )
        mpdlib_myjobsize = atoi( p );
    else
        mpdlib_myjobsize = -1;
    if ( ( p = getenv( "MPD_JRANK" ) ) )
        mpdlib_myrank = atoi( p );
    else
        mpdlib_myrank = -1;
    sprintf( mpdlib_myid, "cli_%d", mpdlib_myrank );
    if ( ( p = getenv( "MAN_MSGS_FD" ) ) ) {
        mpdlib_man_msgs_fd = atoi( p );

/* We can only use nonblocking listener sockets for the fd that is used in 
   the P4 code for *READING* only.  Since this fd is used for both reading 
   and writing, we cannot set this socket as nonblocking.

   There remains a potential race condition in the listener code as a 
   result, but we'll need a different solution than this one.
*/
#ifdef USE_NONBLOCKING_LISTENER_SOCKETS	
	{
	    long man_fdflags;
	    if ( ( man_fdflags = fcntl( mpdlib_man_msgs_fd, F_GETFL, 0 ) ) < 0 ) {
		MPD_Printf( 1, "F_GETFL error in MPD_Init\n" );
		exit( -1 );
	    }
	    man_fdflags |= O_NONBLOCK;
	    if ( fcntl( mpdlib_man_msgs_fd , F_SETFL, man_fdflags ) < 0 ) {
		MPD_Printf( 1, "F_SETFL error in MPD_Init\n" );
		exit( -1 );
	    }
	}
#endif /* USE_NONBLOCKING_LISTENER_SOCKETS */	

    }
    else
        mpdlib_man_msgs_fd = -1;

    if ( ( p = getenv( "CLIENT_LISTENER_FD" ) ) )
        mpdlib_peer_listen_fd = atoi( p );
    else
        mpdlib_peer_listen_fd = -1;

    MPD_Printf( mpdlib_debug, "MPD_Init: retrieved  from env rank=%d manfd=%d clifd=%d\n",
               mpdlib_myrank,mpdlib_man_msgs_fd,mpdlib_peer_listen_fd );

    mpdlib_getexecname( execname, sizeof( execname ) );
    sprintf( buf, "cmd=client_ready pid=%d execname=%s version=%d\n",
	     (int)getpid(), execname, MPD_VERSION );
    write( mpdlib_man_msgs_fd, buf, strlen(buf) );
    MPD_Printf( mpdlib_debug, "MPD_Init: sent client_ready to man\n");


    mpd_Signal( SIGUSR1, mpdlib_sigusr1_handler ); /* when poked by manager */
    sprintf( buf, "cmd=accepting_signals pid=%d\n", (int)getpid() );
    write( mpdlib_man_msgs_fd, buf, strlen(buf) );

    if ( mpdlib_mpdtvdebug ) {
	/* wait for synchronization with debugger */
	MPD_Printf( mpdlib_debug, "client about to wait for release by manager\n" );
	while ( !MPD_tvdebug_synch_flag )
	    ;
	MPD_Printf( mpdlib_debug, "client finished waiting for release by manager\n" );
    }

    MPD_Printf( mpdlib_debug, "MPD_Init: returning\n");
    return(0);
}

/* readlink is defined in unistd.h *only* if __USE_BSD is defined */
static void mpdlib_getexecname( char * execname, size_t len )
{
#ifdef __linux
    int rc = readlink( "/proc/self/exe", execname, len );
    if ( rc < 0 || rc == len )
	execname[0] = '\0';
    else
	execname[rc] = '\0';
#else
    execname[0] = '\0';		/* would it be better to return "unknown" */
    MPD_Printf( mpdlib_debug, "mpdlib_getexecname not implemented on non-Linux systems.....yet\n" );
#endif
}

int MPD_Finalize( void )
{
    if (mpdlib_debug==1)
        fprintf(stderr,"MPI Finalize job=%d rank=%d\n", mpdlib_myjob, mpdlib_myrank);
    close( mpdlib_man_msgs_fd );
    /* may need to clean up */
    return(0);
}

int MPD_Job( void )
{
    return(mpdlib_myjob);
}

int MPD_Size( void )
{
    return(mpdlib_myjobsize);
}

int MPD_Rank( void )
{
    return(mpdlib_myrank);
}

int MPD_Peer_listen_fd( void )
{
    return(mpdlib_peer_listen_fd);
}

int MPD_Man_msgs_fd( void )
{
    return(mpdlib_man_msgs_fd);
}

int MPD_Poke_peer( int grpid, int rank, char *msg )
{
    char buf[MPD_MAXLINE];

    sprintf( buf, "cmd=interrupt_peer_with_msg grp=%d torank=%d fromrank=%d msg=%s\n",
             grpid, rank, mpdlib_myrank, msg );
    write( mpdlib_man_msgs_fd, buf, strlen(buf) );
    return(0);
}

void MPD_Abort( int code )
{
    int rank, jobid;
    char buf[MPD_MAXLINE];
    
    rank   = MPD_Rank();
    jobid  = MPD_Job();
    MPD_Printf( mpdlib_debug, "MPD_Abort: process %d aborting with code %d\n", rank, code );

    sprintf( buf, "cmd=abort_job job=%d rank=%d abort_code=%d reason=x by=user\n",
	     jobid, rank, code );
    write( mpdlib_man_msgs_fd, buf, strlen(buf) );

    sleep( 20 );
    MPD_Printf( 1, "MPD_Abort:  exiting after 20 seconds\n" );  fflush( stderr );
    exit( -1 );
}

int MPD_Get_peer_host_and_port( int job, int rank, char *peerhost, int *peerport )
{
    int i;
    char buf[MPD_MAXLINE];
        
    sprintf( buf, "cmd=findclient job=%d rank=%d\n", job, rank );
    write( mpdlib_man_msgs_fd, buf, strlen(buf) );
    i = mpd_read_line( mpdlib_man_msgs_fd, buf, MPD_MAXLINE );  
    MPD_Printf( mpdlib_debug ,"MPDLIB rc=%d reply=>:%s:\n", i, buf );
    mpd_parse_keyvals( buf );
    mpd_getval( "cmd", buf );
    if ( strcmp( "foundclient", buf ) != 0 ) {
        MPD_Printf( 1, "expecting foundclient, got :%s:\n", buf );
        return(-1);
    }
    mpd_getval( "host", peerhost );
    mpd_getval( "port", buf );
    *peerport = atoi( buf );
    if ( *peerport < 0 ) {
        MPD_Printf( 1, "MPD_Get_peer_host_and_port: failed to find client :%d %d:\n", job,rank );
        return(-1);
    }
    MPD_Printf( 1, "LOCATED job=%d rank=%d at peerhost=%s peerport=%d\n",
               job, rank, peerhost, *peerport);
    return(0);        
}

void mpdlib_sigusr1_handler( int signo )
{
    int rc, numfds, done;
    char buf[MPD_MAXLINE];
    struct timeval tv;
    fd_set readfds;

    done = 0;

    while (!done)
    {
        FD_ZERO( &readfds );
        FD_SET( mpdlib_man_msgs_fd, &readfds );
        numfds = mpdlib_man_msgs_fd + 1;
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        rc = select( numfds, &readfds, NULL, NULL, &tv );
        if ( ( rc == -1 ) && ( errno == EINTR ) )
            continue;
        if ( rc < 0 ) {
	    char errmsg[80];
	    sprintf( errmsg, "[%s] mpdlib_sigusr1_handler: select: %d", mpdlib_myid, rc );
	    perror( errmsg );
	    exit( -1 );
	}
        if ( FD_ISSET( mpdlib_man_msgs_fd, &readfds ) )
        {
            rc = mpd_read_line(mpdlib_man_msgs_fd,buf,MPD_MAXLINE);
            MPD_Printf( mpdlib_debug, "mpdlib_sigusr1_handler got buf=:%s:\n",buf );
            MPD_Man_msg_handler( buf );
        }
        else
            done = 1;
    }
    MPD_Printf( mpdlib_debug, "mpdlib_sigusr1_handler exiting\n" );
}

void MPD_Man_msg_handler( char *buf )
{
    MPD_Printf( mpdlib_debug, "MPD_Man_msg_handler got buf=:%s:\n", buf );

    /* Can't use mpd_parse_keyvals since it is not thread-safe */
    if ( strncmp( buf, "cmd=tvdebugsynch", 16 ) == 0 )
	MPD_tvdebug_synch_flag = 1;
    else if ( strncmp( buf, "cmd=client_bnr_fence_out", 24 ) == 0 )
	MPD_global_fence_flag = 1;
    else if ( strncmp( buf, "connect_to_me-", 14 ) == 0 )
	( *MPD_user_peer_msg_handler )( buf );
    else
	MPD_Printf( 1, "MPD_Man_msg_handler received unexpected msg :%s:\n", buf );
}

void MPD_Set_debug( int value )
{
    mpdlib_debug = value;
}

void MPD_Printf( int print_flag, char *fmt, ... )
{
    va_list ap;

    if (print_flag) {
	fprintf( stderr, "[%s]: ", mpdlib_myid );
	va_start( ap, fmt );
	vfprintf( stderr, fmt, ap );
	va_end( ap );
	fflush( stderr );
    }
}


/***** code shared with mpd *****/
/* Note that we sometimes use these in p4 and bnr, but they are
   really only made global for mpdlib and mpd; we just make use
   of them elsewhere because we know they are here.
*/

int mpd_read_line( int fd, char *buf, int maxlen )
{
    int n, rc;
    char c, *ptr;

    ptr = buf;
    for ( n = 1; n < maxlen; n++ ) {
      again:
	if ( ( rc = read( fd, &c, 1 ) ) == 1 ) {
	    *ptr++ = c;
	    if ( c == '\n' )	/* note \n is stored, like in fgets */
		break;
	}
	else if ( rc == 0 ) {
	    if ( n == 1 )
		return( 0 );	/* EOF, no data read */
	    else
		break;		/* EOF, some data read */
	}
	else {
	    if ( errno == EINTR )
		goto again;
	    return ( -1 );	/* error, errno set by read */
	}
    }
    *ptr = 0;			/* null terminate, like fgets */
    return( n );
}

/* from Stevens book */
Sigfunc *mpd_Signal( int signo, Sigfunc func )
{
    struct sigaction act, oact;

    act.sa_handler = func;
    sigemptyset( &act.sa_mask );
    act.sa_flags = 0;
    if ( signo == SIGALRM ) {
#ifdef  SA_INTERRUPT
        act.sa_flags |= SA_INTERRUPT;   /* SunOS 4.x */
#endif
    } else {
#ifdef SA_RESTART
        act.sa_flags |= SA_RESTART;     /* SVR4, 4.4BSD */
#endif
    }
    if ( sigaction( signo,&act, &oact ) < 0 )
        return ( SIG_ERR );
    return( oact.sa_handler );
}

int mpd_parse_keyvals( char *st )
{
    char *p, *keystart, *valstart;

    if ( !st )
	return( -1 );

    mpd_keyval_tab_idx = 0;          
    p = st;
    while ( 1 ) {
	while ( *p == ' ' )
	    p++;
	/* got non-blank */
	if ( *p == '=' ) {
	    MPD_Printf( 1, "mpd_parse_keyvals:  unexpected = at character %d in %s\n",
		       p - st, st );
	    return( -1 );
	}
	if ( *p == '\n' || *p == '\0' )
	    return( 0 );	/* normal exit */
	/* got normal character */
	keystart = p;		/* remember where key started */
	while ( *p != ' ' && *p != '=' && *p != '\n' && *p != '\0' )
	    p++;
	if ( *p == ' ' || *p == '\n' || *p == '\0' ) {
	    MPD_Printf( 1,
		       "mpd_parse_keyvals: unexpected key delimiter at character %d in %s\n",
		       p - st, st );
	    return( -1 );
	}
        strncpy( mpd_keyval_tab[mpd_keyval_tab_idx].key, keystart, p - keystart );
	mpd_keyval_tab[mpd_keyval_tab_idx].key[p - keystart] = '\0'; /* store key */

	valstart = ++p;			/* start of value */
	while ( *p != ' ' && *p != '\n' && *p != '\0' )
	    p++;
        strncpy( mpd_keyval_tab[mpd_keyval_tab_idx].value, valstart, p - valstart );
	mpd_keyval_tab[mpd_keyval_tab_idx].value[p - valstart] = '\0'; /* store value */
	mpd_keyval_tab_idx++;
	if ( *p == ' ' )
	    continue;
	if ( *p == '\n' || *p == '\0' )
	    return( 0 );	/* value has been set to empty */
    }
}
 
void mpd_dump_keyvals( void )
{
    int i;
    for (i=0; i < mpd_keyval_tab_idx; i++) 
	MPD_Printf(1, "  %s=%s\n",mpd_keyval_tab[i].key, mpd_keyval_tab[i].value);
}

char *mpd_getval( char *keystr, char *valstr )
{
    int i;

    for (i=0; i < mpd_keyval_tab_idx; i++) {
       if ( strcmp( keystr, mpd_keyval_tab[i].key ) == 0 ) { 
	    strcpy( valstr, mpd_keyval_tab[i].value );
	    return valstr;
       } 
    }
    valstr[0] = '\0';
    return NULL;
}

void mpd_chgval( char *keystr, char *valstr )
{
    int i;

    for ( i = 0; i < mpd_keyval_tab_idx; i++ ) {
       if ( strcmp( keystr, mpd_keyval_tab[i].key ) == 0 )
	    strcpy( mpd_keyval_tab[i].value, valstr );
    }
}


#define     NL  '\n'
#define ESC_NL  '^'   
#define     END ' '
#define ESC_END '"'
#define     ESC '\\'
#define ESC_ESC '\''

void mpd_stuff_arg(arg,stuffed)
char arg[], stuffed[];
{
    int i,j;

    for (i=0, j=0; i < strlen(arg); i++)
    {
	switch (arg[i]) {
	    case END:
		stuffed[j++] = ESC;
		stuffed[j++] = ESC_END;
		break;
            case NL:	
		stuffed[j++] = ESC;
		stuffed[j++] = ESC_NL;
		break;
	    case ESC:
		stuffed[j++] = ESC;
		stuffed[j++] = ESC_ESC;
		break;
	    default:
		stuffed[j++] = arg[i];
	}
    }
    stuffed[j] = '\0';
}

void mpd_destuff_arg(stuffed,arg)
char stuffed[], arg[];
{
    int i,j;

    i = 0;
    j = 0;
    while (stuffed[i]) {        /* END pulled off in parse */
	switch (stuffed[i]) {
	    case ESC:
		i++;
		switch (stuffed[i]) {
		    case ESC_END:
			arg[j++] = END;
			i++;
			break;
		    case ESC_ESC:
			arg[j++] = ESC;
			i++;
			break;
                    case ESC_NL:
			arg[j++] = NL;
			i++;
			break;
		}
		break;
	    default:
		arg[j++] = stuffed[i++];
	}
    }
    arg[j] = '\0';
}
