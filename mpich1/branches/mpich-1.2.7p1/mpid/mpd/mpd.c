/*

MPID daemon - invoke original one with no args.

Serves three kinds of socket connections:

 a)  A UNIX domain socket to an mpich "console"
 b)  An INET domain socket available for random connections
 c)  A set of INET domain sockets established to other MPICH daemons.
     Currently this set consists of a "next" and "prev" connection, and
     mpd's will be connected in a ring, with next an output port and
     prev an input port.  For the first mpd the output is connected
     to the input
 d)  A possible set of connections to "client" processes on local machine.
*/

#include "mpd.h"

#ifdef NEED_CRYPT_PROTOTYPE
extern char *crypt (const char *, const char *);
#endif

/* for command line argument processing */
extern char *optarg;
extern int  optind;
int         opt;

struct fdentry fdtable[MAXFDENTRIES];
extern int fdtable_high_water_mark;

extern void sigint_handler( int );

static int get_config( char * );
static int use_old_passwd( void );

char mydir[MAXLINE];
char lhshost[MAXHOSTNMLEN];
char orig_lhshost[MAXHOSTNMLEN];
int  lhsport = -1;                
int  orig_lhsport = -1;                
char rhshost[MAXHOSTNMLEN];        
int  rhsport = -1;                
char rhs2host[MAXHOSTNMLEN];        
int  rhs2port = -1;
char myhostname[MAXHOSTNMLEN];
char mynickname[MAXHOSTNMLEN];
int  my_listener_port = 0;	/* might be set on command line, else bind chooses */
char console_name[MAXLINE];
char logfile_name[MAXLINE];
int  generation;

int logfile_idx          = -1;
int listener_idx	 = -1;
int console_listener_idx = -1;   
int console_idx		 = -1;   
int client_listener_idx	 = -1;   
int manager_listener_idx = -1;   
int client_idx		 = -1;   
int lhs_idx		 = -1;
int rhs_idx		 = -1;
int mon_idx              = -1;
int my_listener_fd	 = -1;
int tell_listener_port   = 0; /* echo the port this mpd is listening on for more mpd's */
int done		 = 0;
int debug		 = 0;
int amfirst		 = 1; /* may be overwritten below */
int allexiting		 = 0; /* flag to disable auto reconnect when all mpds are exiting*/
int backgrounded	 = 0; /* flag to indicate I should become a daemon */
int no_execute           = 0; /* set to 1 if this daemon should never run a user job
			         (skipped for mpdrun, mpdmpexec, etc.) */
int pulse_chkr           = 0; /* see if rhs is still alive */
int shutting_down        = 0; /* see if rhs is still alive */

char myid[IDSIZE];
char mylongid[IDSIZE];
char mpd_passwd[PASSWDLEN];

char working_directory[MAXLINE], c_lhs_port[MAXLINE], c_allow_console[MAXLINE];
char c_debug[MAXLINE], c_listener_port[MAXLINE], c_tell_listener_port[MAXLINE];
char c_backgrounded[MAXLINE], c_no_execute[MAXLINE], configfilename[MAXLINE];

/* jobid data */
int first_avail, last_avail, first_pool, last_pool;

extern struct keyval_pairs keyval_tab[64];
extern int keyval_tab_idx;

int main( int argc, char *argv[] )
{
    int  i, allow_console, rc, num_fds;
    char in_buf[MAXLINE], out_buf[MAXLINE], cmd[MAXLINE];
    char *homedir;
    struct timeval tv;
    struct passwd *pwent = 0;

    fd_set readfds, writefds;

    openlog( "MPD", LOG_PID, LOG_USER );

    /* QD_INIT();			   Abhi's profiling package */

    mpd_Signal( SIGINT,  sigint_handler );  /* Cleanup upon SIGINT  */
    mpd_Signal( SIGTERM, sigint_handler );  /* Cleanup upon SIGTERM */
    mpd_Signal( SIGSEGV, sigint_handler );  /* Cleanup upon SIGSEGV */
    mpd_Signal( SIGBUS,  sigint_handler );  /* Cleanup upon SIGBUS */
    mpd_Signal( SIGCHLD, sigchld_handler ); /* Cleanup upon SIGCHLD */
    mpd_Signal( SIGUSR1, sigusr1_handler ); /* Complain upon SIGUSR1 */
    mpd_Signal( SIGPIPE, SIG_IGN ); /* cause return code EPIPE on writes */

#ifdef ROOT_ENABLED
    fprintf( stderr, "mpd configured to run as root\n" );
#endif

    working_directory[0]    = '\0';
    lhshost[0]              = '\0';
    c_lhs_port[0]           = '\0';
    c_allow_console[0]      = '\0';
    c_debug[0]              = '\0';
    c_listener_port[0]      = '\0';
    c_tell_listener_port[0] = '\0';
    c_backgrounded[0]       = '\0';
    c_no_execute[0]         = '\0';

#ifdef ROOT_ENABLED
    strncpy( configfilename, "/etc/mpd.conf", MAXLINE );
#else
    if ( ( homedir = getenv( "HOME" ) ) == NULL ) {
	mpdprintf( 1, "get_config: unable to obtain pathname for home directory\n" );
	return( -1 );
    }
    else
	sprintf( configfilename, "%s/.mpd.conf", homedir );
#endif

    /* overwrite configfilename if it is on command line */
    for ( i = 0; i < argc; i++ ) {
	if ( strncmp( argv[i], "-f", 2 ) == 0 ) {
	    strcpy( configfilename, argv[i+1] );
	    break;
	}
    }

    /* get config info from file */
    if ( get_config( configfilename ) < 0 )
	if ( use_old_passwd( ) < 0 )
	    exit(-1);

    /* overwrite arguments from configfile with command-line arguments */
    while ( ( opt = getopt( argc, argv, "cp:nh:f:?d:w:l:bet" ) ) != EOF ) {
        switch ( opt ) {
        case 'f':
	    /* configfile name extracted above */               break;
        case 'w':
            strncpy( working_directory, optarg, MAXLINE );	break;
        case 'h':
            strncpy( lhshost, optarg, MAXHOSTNMLEN );		break;
        case 'p':
            strncpy( c_lhs_port, optarg, MAXLINE );		break;
        case 'n':
            strncpy( c_allow_console, "no", MAXLINE );		break;
        case 'd':
            strncpy( c_debug, "yes", MAXLINE );      		break;
	case 'l':
	    strncpy( c_listener_port, optarg, MAXLINE );	break;
	case 'b':
	    /* Note that mpd2 uses -d for this, but -d is alread
	       used in mpd1 for debug.  Sigh. */
            strncpy( c_backgrounded, "yes", MAXLINE );          break;
	case 'e':
            strncpy( c_no_execute, "yes", MAXLINE );            break;
	case 't':
            strncpy( c_tell_listener_port, "yes", MAXLINE );    break;
        case '?':
            usage(argv[0]);              		        break;
        default:
            usage(argv[0]);
        }
    }

    if ( lhshost[0] ) {
        amfirst = 0;
    }
    if ( c_lhs_port[0] ) {
        amfirst = 0;
	lhsport = atoi( c_lhs_port );
    }
    if ( amfirst )
	generation = 1;
    else
	generation = 0;
    mpdprintf( debug, "initializing generation number to %d\n", generation );

    allow_console = 1;  /* default */
    if ( strcmp( c_allow_console, "no" ) == 0 )
        allow_console = 0;
    if ( strcmp( c_debug, "yes" ) == 0 )
        debug = 1;
    if ( c_listener_port[0] )
        my_listener_port = atoi( c_listener_port );
    if ( strcmp( c_tell_listener_port, "yes" ) == 0 )
        tell_listener_port = 1;
    if ( strcmp( c_backgrounded, "yes" ) == 0 )
        backgrounded = 1;
    if ( strcmp( c_no_execute, "yes" ) == 0 )
        no_execute = 1;

    /* Record information about self */
    my_listener_fd = setup_network_socket( &my_listener_port );
    if ( tell_listener_port )
	printf( "%d\n", my_listener_port );
    getcwd( mydir, MAXLINE );
    gethostname( myhostname, MAXHOSTNMLEN );
    sprintf( mylongid, "%s_%d", myhostname, my_listener_port );
    strncpy( mynickname, myhostname, MAXHOSTNMLEN );
#ifndef USE_LONG_HOSTNAMES
    strtok( mynickname, "." );
#endif
    sprintf( myid, "%s_%d", mynickname, my_listener_port );

    mpdprintf( 0, "MPD starting\n");  /* first place with a valid id */

    if ( ( !amfirst ) && ( lhsport == -1 || lhshost[0] == '\0' ) ) {
	mpdprintf( 1, "must specify both host and port or else neither\n" );
	exit( -1 );
    }
    
    init_fdtable();
    init_jobtable();
    init_proctable();

    /* set up listener fd */
    listener_idx                  = allocate_fdentry();
    fdtable[listener_idx].read    = 1;
    fdtable[listener_idx].write   = 0;
    fdtable[listener_idx].handler = LISTEN;
    fdtable[listener_idx].fd      = my_listener_fd;
    fdtable[listener_idx].portnum = my_listener_port;
    strncpy( fdtable[listener_idx].name, "listener", MAXSOCKNAMELEN );

    if ((pwent = getpwuid(getuid())) == NULL)
    {
	mpdprintf( 1, "mpd: getpwuid failed" );
	exit( -1 );
    }

    syslog( LOG_INFO, "mpd started by %s", pwent->pw_name );

    /* set up console fd */
    if ( allow_console ) {
        console_listener_idx                  = allocate_fdentry();
        fdtable[console_listener_idx].read    = 1;
        fdtable[console_listener_idx].write   = 0;
        fdtable[console_listener_idx].handler = CONSOLE_LISTEN;
        /* sprintf( console_name, "%s_%d", CONSOLE_NAME, my_listener_port ); */
        sprintf( console_name, "%s_%s", CONSOLE_NAME, pwent->pw_name );
        strncpy( fdtable[console_listener_idx].name, console_name, 
		 MAXSOCKNAMELEN );
        fdtable[console_listener_idx].fd = setup_unix_socket( console_name );  
	if ( fdtable[console_listener_idx].fd < 0 )  {
            console_setup_failed( myhostname );
	    exit( -1 );
	}
    }

    /* first mpd is own lhs */
    if ( amfirst ) {
        strncpy( lhshost, mynickname, MAXHOSTNMLEN );
        lhsport	     = fdtable[listener_idx].portnum;
	orig_lhsport = lhsport;
        init_jobids();		/* protected from executing twice */
    }

    /* set up left-hand side fd */
    lhs_idx                  = allocate_fdentry();
    fdtable[lhs_idx].read    = 1;
    fdtable[lhs_idx].write   = 0;
    fdtable[lhs_idx].handler = LHS;
    fdtable[lhs_idx].fd      = network_connect( lhshost, lhsport );
    fdtable[lhs_idx].portnum = lhsport;
    strncpy( fdtable[lhs_idx].name, lhshost, MAXSOCKNAMELEN );

    /* Send message to lhs, telling him to treat me as his new rhs */
    sprintf( out_buf, "dest=%s_%d cmd=new_rhs_req host=%s port=%d version=%d\n",
             lhshost, lhsport, mynickname, my_listener_port, MPD_VERSION ); 
    mpdprintf( debug, "main: sending to lhs: :%s:\n", out_buf );        
    write_line( lhs_idx, out_buf );
    if ( ! amfirst ) {		/* don't challenge self */
	recv_msg( fdtable[lhs_idx].fd, in_buf, MAXLINE );
	strncpy( out_buf, in_buf, MAXLINE );
	mpd_parse_keyvals( out_buf );
	mpd_getval( "cmd", cmd );
	if ( strcmp( cmd, "challenge" ) != 0 ) {
	    mpdprintf( 1, "expecting challenge, got %s\n", in_buf );
	    exit( -1 );
	}
	newconn_challenge( lhs_idx );
    }

    /* set up right_hand side fd */
    if ( amfirst ) {
        strncpy( rhshost, mynickname, MAXHOSTNMLEN );
        rhsport = my_listener_port;
        /* set up "next-next" */
        strncpy( rhs2host, mynickname, MAXHOSTNMLEN );
        rhs2port = my_listener_port;
        /* accept connection from self, done in "set up lhs fd" above */
        rhs_idx                  = allocate_fdentry();
        fdtable[rhs_idx].read    = 1;
        fdtable[rhs_idx].write   = 0;
        fdtable[rhs_idx].handler = RHS;
        fdtable[rhs_idx].fd      = accept_connection( fdtable[listener_idx].fd );
        fdtable[rhs_idx].portnum = rhsport;
        strncpy( fdtable[rhs_idx].name, rhshost, MAXSOCKNAMELEN );
        read_line( fdtable[rhs_idx].fd, in_buf, MAXLINE );
        /* check that it worked */
	mpdprintf( debug, "test msg received: :%s:\n", in_buf );
        if ( strncmp( in_buf, out_buf, strlen( out_buf ) ) ) {
             mpdprintf( 1, "initial test message to self failed!\n" );
             exit( -1 );
        }
    }
    else
    {
        /* If not first, then rhs will be set up later in response
	 * to a message from our lhs, telling us whom to connect to
	 * on the right.  Get ready for that.
	 */
    }

    strcpy( orig_lhshost, lhshost );
    orig_lhsport = lhsport;

    /* put myself in the background if flag is set */
    if ( backgrounded )
    {
        if ( fork() != 0 )  /* parent exits; child in background */
	    exit( 0 );
	setsid();           /* become session leader; no controlling tty */
	mpd_Signal( SIGHUP, SIG_IGN ); /* make sure no sighup when leader ends */
	/* leader exits; svr4: make sure do not get another controlling tty */
        if ( fork() != 0 )  
	    exit( 0 );
	chdir("/");         /* free up filesys for umount */
	umask(0);

	/* create a logfile entry just for cleanup */
        logfile_idx                  = allocate_fdentry();
        fdtable[logfile_idx].read    = 0;    /* do not select on this for rd or wt */
        fdtable[logfile_idx].write   = 0;    /*   used mostly for cleanup */
        fdtable[logfile_idx].handler = LOGFILE_OUTPUT;
        fdtable[logfile_idx].fd      = 1;   /* stdout */
        sprintf( logfile_name, "%s_%s", LOGFILE_NAME, pwent->pw_name );
        strncpy( fdtable[logfile_idx].name, logfile_name, MAXSOCKNAMELEN );
        freopen( logfile_name, "a", stdout );
        freopen( logfile_name, "a", stderr );
	close( 0 );
    }


    /* Main Loop */
    mpdprintf( debug, "entering main loop\n" );
    while ( !done ) {
        FD_ZERO( &readfds );
        FD_ZERO( &writefds );
        for ( i = 0; i <= fdtable_high_water_mark; i++ ) {
            if ( fdtable[i].active ) {
                mpdprintf( 0, "active fd:%s,fd=%d\n",
                         fdtable[i].name,fdtable[i].fd);
                if ( fdtable[i].read )
		{
                    FD_SET( fdtable[i].fd, &readfds );
		}
		/*****
                if ( fdtable[i].write )
                    FD_SET( fdtable[i].fd, &writefds );
		*****/
            }
        }

        num_fds = FD_SETSIZE;
        tv.tv_sec = 3;
        tv.tv_usec = 0;

/*      Abhi profiling
        {
	    int first_call = 1;
	    if ( first_call )
	        first_call = 0;
	    else 
	        QD_END(HANDLING);
        }
*/

        rc = select( num_fds, &readfds, &writefds, NULL, &tv );

	/* QD_BEGIN(HANDLING); */ 

	if ( pulse_chkr == 1  &&  rhs_idx >= 0 ) {    /* go thru loop once first */
	    sprintf( out_buf, "src=%s dest=%s_%d cmd=pulse\n", 
		     myid, rhshost, rhsport ); 
	    mpdprintf( 0, "sending pulse rhs_idx=%d fd=%d\n",
		       rhs_idx, fdtable[rhs_idx].fd );
	    write_line( rhs_idx, out_buf );
	    pulse_chkr++;
	}
        if ( rc == 0 ) {
            mpdprintf( 0, "select timed out after %ld minutes\n", 
                        tv.tv_sec/60 );
	    pulse_chkr++;  /* gets reset to 0 when rcv pulse_ack */
	    if ( pulse_chkr >= 4 )
	    {
		mpdprintf( 1, "rhs must be dead; no ack from pulse\n" );
		syslog( LOG_INFO, "rhs did not respond to pulse within %d seconds",
			tv.tv_sec );
	        reknit_ring( rhs_idx );
		pulse_chkr = 0;
	    }
            continue;
        } 
        if ( ( rc == -1 ) && ( errno == EINTR ) ) {
            mpdprintf( debug, "select interrupted; continuing\n" );
            continue;
        }
        if ( rc < 0 ) {
            done = 1;
            error_check( rc, "mpd main loop: select" );
        }

        for ( i = 0; i <= fdtable_high_water_mark; i++ ) {
            if ( fdtable[i].active ) {
                if ( FD_ISSET( fdtable[i].fd, &readfds ) )
                    handle_input_fd( i );
		/*****
                if ( FD_ISSET( fdtable[i].fd, &writefds ) )
                   handle_output( i );
		*****/
            }
        }
    }

/*
    QD_END(HANDLING);
    QD_FINALIZE( "QDoutput" );
*/

    syslog( LOG_INFO, "mpd %s terminating normally", myid );

    closelog();

    if ( debug )
	dump_fdtable( "at exit from mpd" );
    mpd_cleanup( );
    return( 0 );
}

void handle_input_fd( idx )
int idx;
{
    mpdprintf( 0, "handle_input_fd: lhs=%s %d rhs=%s %d rhs2=%s %d\n",
            lhshost, lhsport, rhshost, rhsport, rhs2host, rhs2port );
    if ( fdtable[idx].handler == NOTSET )
        mpdprintf( debug, "handler not set for port %d\n", idx );
    else if ( fdtable[idx].handler == CONSOLE_LISTEN )
        handle_console_listener_input( idx );
    else if ( fdtable[idx].handler == MANAGER )
        handle_manager_input( idx );
    else if ( fdtable[idx].handler == CONSOLE )
        handle_console_input( idx );
    else if ( fdtable[idx].handler == LISTEN )
        handle_listener_input( idx );
    else if ( fdtable[idx].handler == NEWCONN )
        handle_newconn_input( idx );
    else if ( fdtable[idx].handler == MONITOR )
	handle_monitor_input( idx );
    else if ( fdtable[idx].handler == LHS )
        handle_lhs_input( idx );
    else if ( fdtable[idx].handler == RHS )
        handle_rhs_input( idx );
    else
        mpdprintf( debug, "invalid handler for fdtable entry %d\n", idx );
}


void init_jobids()
{
    char buf[MAXLINE];
    static int first_call = 1;

    if ( first_call ) {
	if ( amfirst ) {
	    first_avail = 1;
	    last_avail  = CHUNKSIZE;
	    first_pool  = CHUNKSIZE + 1;
	    last_pool   = 2000 * BIGCHUNKSIZE;
	}
	else {
	    first_avail = 0;
	    last_avail  = -1;
	    first_pool  = 0;
	    last_pool   = -1;
	    sprintf( buf, "src=%s dest=anyone cmd=needjobids\n", myid  ); 
	    mpdprintf( 0, "init_jobids: sending needjobids\n" );
	    write_line( rhs_idx, buf );
	}
	first_call = 0;
    }
}

int allocate_jobid()
{
    char buf[MAXLINE];
    int  new_jobid;

    if ( first_avail <= last_avail )   /* ids are available */
	new_jobid = first_avail++;
    else if ( first_pool + CHUNKSIZE - 1 <= last_pool ) { /* avail empty, but not pool */
	first_avail = first_pool;
	last_avail  = first_avail + CHUNKSIZE - 1;
	first_pool  = first_pool + CHUNKSIZE;
	mpdprintf( 0, "after allocating to avail from pool, fp=%d, lp=%d, fa=%d, la=%d\n",
		   first_pool, last_pool, first_avail, last_avail );
	if ( first_pool > last_pool ) {	        /* pool now empty, request more jobids */
	    sprintf( buf, "src=%s dest=anyone cmd=needjobids\n", myid );
	    mpdprintf( 0, "allocate_jobid: sending needjobids\n" );
	    write_line( rhs_idx, buf );
	}
	new_jobid = first_avail++;
    }
    else {
	mpdprintf( 1, "PANIC: could not allocate jobid\n" );
	new_jobid = -1;
    }
    return new_jobid;
}

void add_jobids( int first, int last )
{
    mpdprintf( 0, "received new jobids: first=%d, last=%d\n", first, last );
    first_pool = first;
    last_pool  = last;
}

int steal_jobids( int *first, int *last )
{
    if ( last_pool >= first_pool + 2 * BIGCHUNKSIZE ) {
	*first = first_pool;
	*last  = first_pool + BIGCHUNKSIZE - 1;
	first_pool = first_pool + BIGCHUNKSIZE;
	mpdprintf( 0, "after stealing jobids: first_pool=%d, last_pool=%d\n",
		   first_pool, last_pool );
	return 0;
    }
    else
	return -1;
}

static int get_config( char *filename )
{
    char buf[MAXLINE], inbuf[MAXLINE];
    struct stat statbuf;
    int n, fd;

    /* The configure file can come from the command line, from the user's home directory
       as .mpd.conf, or (if ROOT) from /etc/mpd.conf
    */

    if ( stat( filename, &statbuf ) != 0 ) {
	mpdprintf( 1, "get_config: unable to stat %s\n", filename );
	return( -1 );
    }
    if ( statbuf.st_mode & 00077 ) {  /* if anyone other than owner  can access the file */
	mpdprintf( 1, "get_config: other users can access %s\n", filename );
	return( -1 );
    }
    if ( ( fd = open( configfilename, O_RDONLY ) ) == -1 ) {
	mpdprintf( 1, "get_config: cannot open %s\n", filename );
	return( -1 );
    }
    buf[0] = '\0';
    while ( ( n = read_line( fd, inbuf, MAXLINE ) ) > 0 ) {
	if ( inbuf[0] == '#' )
	    continue;
	if ( inbuf[n-1] == '\n' )
	    inbuf[n-1] = '\0';
	else
	    inbuf[n] = '\0';
	strcat( inbuf, " " );
	strcat( buf, inbuf );
    }
    mpdprintf( debug, "mpd buf=:%s:\n", buf );

    n = mpd_parse_keyvals( buf );
    if ( n < 0 ) {
	mpdprintf( 1, "mpd exiting due to unrecognized values in mpd conf file\n" );
	exit( -1 );
    }
    mpd_getval( "password", mpd_passwd );
    if ( mpd_passwd[0] == '\0' ) {
	mpd_getval( "secretword", mpd_passwd );
	if (mpd_passwd[0] == '\0') {
	    mpdprintf( 1, "get_config: no passwd in config file\n" );
	    return( -1 );
	}
    }
    mpd_getval( "working_directory", working_directory );
    mpd_getval( "lhs_host", lhshost );
    mpd_getval( "lhs_port", c_lhs_port );
    mpd_getval( "allow_console", c_allow_console );
    mpd_getval( "debug", c_debug );
    mpd_getval( "listener_port", c_listener_port );
    mpd_getval( "tell_listener_port", c_tell_listener_port );
    mpd_getval( "background", c_backgrounded );
    mpd_getval( "root_execute_only", c_no_execute );

    return( 0 );
}

static int use_old_passwd( void )
{
    char *homedir, passwd_pathname[MAXLINE];
    struct stat statbuf;
    int fd;

    if ( ( homedir = getenv( "HOME" ) ) == NULL ) {
	mpdprintf( 1, "Looking for file containing MPD password; could"
		      " not find $HOME directory\n" );
	return( -1 );
    }
#ifdef ROOT_ENABLED
    strncpy( passwd_pathname, "/etc/mpdpasswd", MAXLINE );
#else
    sprintf( passwd_pathname, "%s/.mpdpasswd", homedir );
#endif
    if ( stat( passwd_pathname, &statbuf ) != 0 ) {
	mpdprintf( 1, "Looking for file containing MPD password; "
		      "could not find %s\n", passwd_pathname );
	return( -1 );
    }
    if ( statbuf.st_mode & 00077 ) {  /* if anyone other than owner  can access the file */
	mpdprintf( 1, "Password file %s must not be readable by other users\n",
		   passwd_pathname );
	return( -1 );
    }
    if ( ( fd = open( passwd_pathname, O_RDONLY ) ) == -1 ) {
	mpdprintf( 1, "MPD password file %s cannot be opened\n", passwd_pathname );
	return( -1 );
    }
    if ( ( read_line( fd, mpd_passwd, MAXLINE ) ) <= 0 ) {
	/* note mpd_passwd contains the newline at the end of the file if it exists */
	mpdprintf( 1, "Unable to obtain MPD password from %s\n", passwd_pathname );
	return( -1 );
    }

    return( 0 );
}

void enter_ring( void )
{
    int i, lhs_gen, gotit = 0, max_tries = 5;
    char out_buf[MAXLINE], in_buf[MAXLINE], cmd[MAXLINE];
    char c_lhs_gen[10];

    /* sleep( 2 ); */ /* RMB: TEMP */
    /* Send message to lhs, telling him to treat me as his new rhs */
    sprintf( out_buf, "dest=%s_%d cmd=new_rhs_req host=%s port=%d version=%d\n",
             lhshost, lhsport, mynickname, my_listener_port, MPD_VERSION ); 
    for ( i = 0; i < max_tries; i++ ) { 
	mpdprintf( debug, "enter_ring: sending to lhs: %s", out_buf );        
	write_line( lhs_idx, out_buf );
	read_line( fdtable[lhs_idx].fd, in_buf, MAXLINE );
	mpdprintf( debug, "enter_ring: recvd buf=:%s:\n", in_buf );        
	mpd_parse_keyvals( in_buf );
	mpd_getval( "cmd", cmd );
	if ( strcmp( cmd, "challenge" ) != 0 ) {
	    mpdprintf( 1, "enter_ring: expecting challenge, got %s\n", in_buf );
	    exit( -1 );
	}
	else {
	    mpd_getval( "generation", c_lhs_gen );
	    lhs_gen = atoi( c_lhs_gen );
	    if ( lhs_gen > generation ) {
		newconn_challenge( lhs_idx ); /* respond to challenge */
		generation = lhs_gen;         /* set generation to that of lhs */
		gotit = 1;
		mpdprintf( debug, "enter_ring: connected after %d tries\n", i+1 );
		break;
	    }
	    else {
		sleep( 2 ); 
	    }
	}
    }
    if ( ! gotit ) {
	mpdprintf( 1, "enter_ring: exiting; failed to enter the ring after %d tries\n", max_tries );
	exit( -1 );
    }
}

