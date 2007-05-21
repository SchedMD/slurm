#include "mpd.h"
#include <sys/param.h>
#include "mpdattach.h"	/* Interface to the debugger for process attachment */
#include "merge.h"

#define STDIN_STREAM    0
#define STDOUT_STREAM   1
#define STDERR_STREAM   2
#define CONTROL_STREAM  4
#define LISTEN_STREAM   5
#define TEMP_STREAM     6
#define USER_STDIN      7
#define MAXTOTPROCS  4096  

#define PATSIZE 64
#define TIMEOUTVAL 5

struct fdentry fdtable[MAXFDENTRIES];  /* for external defn */
extern int fdtable_high_water_mark;

/* The following variables need only be present for the debugger to find; they are not
   accessed by mpd or user code. */ 
volatile int MPIR_i_am_starter;	/* Tell the debugger this process is not part
				 * of the MPI world. */
volatile int MPIR_partial_attach_ok; /* Tell the debugger that releasing this (console)
				      process is sufficient to release all processes. */
int cfd, debug = 0;
int tvdebug;
int listener_idx = -1;
int ctl_idx = -1;
int stdin_idx = -1;
int stdout_idx = -1;
int stderr_idx = -1;
int user_stdin_idx = -1;

int jobid, done;
int control_input_closed = 0;
int stdout_input_closed = 0;
int stderr_input_closed = 0;

int iotree     = 1;		   /* default is to prebuild print tree */
int gdb	       = 0;		   /* whether we are running mpigdb or not */
int mpirunning = 0;		   /* whether we are running mpirun or not */
int numprompts = 0;		   /* how many prompts have been received from gdb */
int jobsize;			   /* size of job in mpirun, mpigdb, mpdmpexec */
int mergeprompts;		   /* how many prompts to merge in mpigdb mode */

char myid[IDSIZE];

struct passwd *pwent;

/* Should these be static? */
int mpdhelp( int, char** );
int mpdclean( int, char** );
int mpdcleanup( int, char** );
int mpdtrace( int, char** );
int mpdlistjobs( int, char** );
int mpdkilljob( int, char** );
int mpddump( int, char** );
int mpdmandump( int, char** );
int mpdringtest( int, char** );
int mpdringsize( int, char** );
int mpdmpexec( int, char** );
int mpdexit( int, char** );
int mpdallexit( int, char** );
int mpdshutdown( int, char** );
int mpdbomb( int, char** );
int mpirun( int, char** );
int mpigdb( int, char** );
void con_handle_input_fd( int );
void handle_listen_input( int );
void handle_control_input( int );
void handle_stdout_input( int );
void handle_stderr_input( int );
void handle_temp_input( int );
void handle_user_stdin( int );
void con_sig_handler( int );
int start_mpds( char * );
int squash( char *, char [][PATSIZE] );
void process_buf( char *, char *, int *, int * );
static char * dupstr( const char * );
void * MPIR_Breakpoint( void );
static void sigalrm_handler( int );
char pgmname[128]; 

int main( int argc, char *argv[] )
{
    int rc = 0;
    char buf[MAXLINE];
#   if defined(ROOT_ENABLED)
    int old_uid, old_gid;
#   endif
    char *s, console_name[128];
    struct itimerval timelimit;
    struct timeval tval, tzero;

    strcpy( myid, "mpdcon");

    if ((pwent = getpwuid(getuid())) == NULL) {
        printf( "getpwuid failed\n" );
        exit( 1 );
    }

#   if defined(ROOT_ENABLED)
    old_uid = getuid();
    old_gid = getgid();
    if ( geteuid() != 0 ) {
        printf( "this pgm must run as setuid root\n" );
        exit( 1 );
    }
    setuid(0);  /* temporarily until I connect to unix socket; only possible if euid=0 */
    setgid(0);
#   endif

    /* mpirun must not require -np in the first position (doing so
       keeps the test suite from running */
    if ( strncmp( pgmname, "mpirun", 6 ) == 0) {
	/* Look for -np in the arg list */
	int i = argc, found = 0;
	for (i=1; i<argc; i++) {
	    if (strcmp( argv[i], "-np" ) == 0) { found = 1 ; break; }
	}
	if (!found) 
	{
	    usage_mpirun();
	    exit( 1 );
	}
    }

    if ((s = rindex(argv[0],'/')) == NULL)
	strcpy(pgmname,argv[0]);
    else
	strcpy(pgmname,s+1);
    if ( strcmp( pgmname,"mpdhelp" ) == 0 )
	rc = mpdhelp( argc, argv );
    else if ( strcmp( pgmname,"mpdcleanup" ) == 0 )
	rc = mpdcleanup( argc, argv );
    else
    {
#       if defined(ROOT_ENABLED)
	sprintf( console_name, "%s_%s", CONSOLE_NAME, "root" );
#       else
	sprintf( console_name, "%s_%s", CONSOLE_NAME, pwent->pw_name );
#       endif
	mpdprintf( debug, "connecting to console name :%s:\n", console_name );

        signal(SIGALRM,sigalrm_handler);
        tzero.tv_sec	      = 0;
        tzero.tv_usec	      = 0;
        timelimit.it_interval = tzero;       /* Only one alarm */
        tval.tv_sec	      = TIMEOUTVAL;
        tval.tv_usec	      = 0;
        timelimit.it_value    = tval;
        setitimer(ITIMER_REAL,&timelimit,0);

	cfd = local_connect( console_name );
#       if defined(AUTO_START)
	if ( cfd == -1 ) {  
	    if ( strncmp( pgmname, "mpirun", 6 ) == 0 )
		cfd = start_mpds( console_name );
	}
#       endif
	if ( cfd == -1 ) {
	    mpdprintf( 1, "mpirun for the ch_p4mpd device, and other mpd commands,\n" );
	    mpdprintf( 1, "require an mpd to be running on the local machine\n" );  
	    mpdprintf( 1, "See the Installation and User Guides for how to start mpd's\n" );
	}
	error_check( cfd, "local_connect failed to connect to an mpd: " );

	if ( read_line( cfd, buf, MAXLINE ) != 0 ) {
	    int version;
	    mpd_parse_keyvals( buf );
	    mpd_getval( "version", buf );
	    version = atoi( buf );
	    if ( version != MPD_VERSION ) {
		mpdprintf( 1, "connected to mpd with mismatched version %d; mine is %d\n",
			   version, MPD_VERSION );
		exit( 1 );
	    }
	} 
	else {
	    mpdprintf( 1, "console lost contact with mpd unexpectedly\n" );
	    exit ( 1 );
	}

        tzero.tv_sec	   = 0;
        tzero.tv_usec	   = 0;
        timelimit.it_value = tzero;   /* Turn off timer */
        setitimer(ITIMER_REAL,&timelimit,0);
        signal(SIGALRM,SIG_DFL);

#       if defined(ROOT_ENABLED)
	setuid(old_uid);  /* chg back now that I have the local socket */
	setgid(old_gid);

	/* some commands shouldn't be run by a non-root user if the mpd is 
	   running as root
	*/
	if ( ( old_uid != 0 ) &&
	     ( strcmp( pgmname, "mpdallexit" ) == 0 ||
	       strcmp( pgmname, "mpdexit"    ) == 0 ||
	       strcmp( pgmname, "mpdclean"   ) == 0 ||
	       strcmp( pgmname, "mpdkilljob" ) == 0 ||
	       strcmp( pgmname, "mpdshutdown") == 0 ||
	       strcmp( pgmname, "mpdbomb"    ) == 0 )) {
	    printf( "only root can execute %s\n", pgmname );
	    exit( 1 );
	}

#       endif

	if ( strcmp( pgmname,"mpdringtest" ) == 0 )
	    rc = mpdringtest( argc, argv );
	else if ( strcmp( pgmname,"mpdringsize" ) == 0 )
	    rc = mpdringsize( argc, argv );
	else if ( strcmp( pgmname,"mpdclean" ) == 0 )
	    rc = mpdclean( argc, argv );
	else if ( strcmp( pgmname,"mpdtrace" ) == 0 )
	    rc = mpdtrace( argc, argv );
	else if ( strcmp( pgmname,"mpdlistjobs" ) == 0 )
	    rc = mpdlistjobs( argc, argv );
	else if ( strcmp( pgmname,"mpdkilljob" ) == 0 )
	    rc = mpdkilljob( argc, argv );
	else if ( strcmp( pgmname,"mpddump" ) == 0 )
	    rc = mpddump( argc, argv );
	else if ( strcmp( pgmname,"mpdmandump" ) == 0 )
	    rc = mpdmandump( argc, argv );
	else if ( strcmp( pgmname,"mpdmpexec" ) == 0 )
	    rc = mpdmpexec( argc, argv );
	else if ( strcmp( pgmname,"mpdexit" ) == 0 )
	    rc = mpdexit( argc, argv );
	else if ( strcmp( pgmname,"mpdallexit" ) == 0 )
	    rc = mpdallexit( argc, argv );
	else if ( strcmp( pgmname,"mpdshutdown" ) == 0 )
	    rc = mpdshutdown( argc, argv );
	else if ( strcmp( pgmname,"mpdbomb" ) == 0 )
	    rc = mpdbomb( argc, argv );
	else if ( strncmp( pgmname,"mpirun",6 ) == 0 )
	    rc = mpirun( argc, argv );
	else if ( strcmp( pgmname,"mpigdb" ) == 0 )
	    rc = mpigdb( argc, argv );
	else {
	    printf( "unrecognized pgm name from console \n" );
	    exit( 1 );
	}
    }
    if (rc != 0) {
	/* We've detected some problem but we do not handle it yet. */
	printf( "Unexpected return %d from command\n", rc );
    }
    return 0;
}

int mpdclean( int argc, char *argv[] )
{
    char buf[MAXLINE];

    sprintf( buf, "cmd=clean\n" );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack_from_mpd */
    read_line( cfd, buf, MAXLINE );  /* get clean completed msg */
    printf( "mpdclean: clean completed\n" );
    return(0);
}

int mpdcleanup( int argc, char *argv[] )
{
    char file_name[MAXLINE];
    char cmd[MAXLINE];
    char cmd2[MAXLINE];

    sprintf( cmd2, "mpdallexit");
    /* system( cmd2 ); */  /* prone to failure if there is no mpd */

    sprintf( file_name, "%s_%s", CONSOLE_NAME, pwent->pw_name );
    sprintf( cmd, "/bin/rm -f %s", file_name );
    system( cmd );

    sprintf( file_name, "%s_%s", LOGFILE_NAME, pwent->pw_name );
    sprintf( cmd, "/bin/rm -f %s", file_name );
    system( cmd );

    return(0);
}

int mpdringtest( int argc, char *argv[] )
{
    int  count;
    char buf[MAXLINE];
	
    if (argc < 2)
    {
	printf("usage: mpdringtest count\n" );
	return(0);
    }
    count = atoi( argv[1] );
    if ( count > 0 ) {
        /* send message around ring to self */
        sprintf( buf, "cmd=ringtest laps=%d\n", count );
        send_msg( cfd, buf, strlen( buf ) );
    }
    read_line( cfd, buf, MAXLINE );  /* get ack_from_mpd */
    mpdprintf( debug, "mpdringtest: msg from mpd: %s", buf );
    read_line( cfd, buf, MAXLINE );  /* get ringtest completed msg */
    printf( "mpdringtest: msg from mpd: %s", buf );
    return(0);
}

int mpdringsize( int argc, char *argv[] )
{
    int execonly;
    char buf[MAXLINE];
	
    if ( (argc == 2 ) && (strcmp( argv[1], "-e" ) ) == 0 )
	execonly = 1;
    else
	execonly = 0;
    sprintf( buf, "cmd=ringsize execonly=%d\n", execonly );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack_from_mpd */
    mpdprintf( debug, "mpdringsize: msg from mpd: %s", buf );
    read_line( cfd, buf, MAXLINE );  /* get ringtest completed msg */
    mpd_parse_keyvals( buf );
    mpd_getval( "size", buf );
    /* printf( "mpdringsize=%s\n", buf ); verbose */
    printf( "%s\n", buf );
    return(0);
}

int mpdkilljob( int argc, char *argv[] )
{
    char buf[MAXLINE];
	
    if (argc < 2)
    {
	printf( "usage: mpdkilljob jobid \n" );
	return(0);
    }

    sprintf( buf, "cmd=killjob jobid=%s\n", argv[1] );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack from mpd */
    mpdprintf( debug, "mpdkilljob: msg from mpd: %s", buf );
    return(0);
}

int mpddump(argc,argv)
int argc;
char *argv[];
{
    char buf[MAXLINE];
    char what[80];
	
    if (argc < 2)
	strcpy( what, "all" );
    else
	strcpy( what, argv[1] );

    sprintf( buf, "cmd=dump what=%s\n", what );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack from mpd */
    mpdprintf( debug,"mpddump: msg from mpd: %s", buf );
    return(0);
}

int mpdmandump( argc, argv )
int argc;
char *argv[];
{
    char buf[MAXLINE];
    char what[80];
	
    if ( argc == 4 )
	strcpy( what, argv[3] );
    else if ( argc == 3 )
	strcpy( what, "all" );
    else {
	fprintf( stderr, "Usage: mpdmandump <jobid> <man rank> [<what to dump>]\n" );
	return( -1 );
    }
    fprintf( stderr, "console: dumping from job %d, manager %d\n",
	     atoi( argv[1] ), atoi( argv[2] ) );

    sprintf( buf, "cmd=mandump jobid=%s rank=%s what=%s\n", argv[1], argv[2], what );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack from mpd */
    mpdprintf( 1, "mpdmandump: msg from mpd: %s", buf );
    /* The following code  is only necessary if we route the mandump output back to
       the console.  We are not doing this yet.
    while ( strcmp( buf, "mandump done\n" ) != 0 ) {
	read_line( cfd, buf, MAXLINE );
	if ( strcmp( buf, "mandump done\n" ) != 0 )
	    printf( "mpdmandump: %s", buf );
    }
    */
    return(0);
}

int mpdtrace( int argc, char *argv[] )
{
    char buf[MAXLINE];
    int  execonly;
	
    if ( (argc == 2 ) && (strcmp( argv[1], "-e" ) ) == 0 )
	execonly = 1;
    else
	execonly = 0;
    sprintf( buf, "cmd=trace execonly=%d\n", execonly );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack from mpd */
    mpdprintf( debug, "mpdtrace: msg from mpd: %s", buf );
    while ( strcmp( buf, "trace done\n" ) != 0 ) {
        read_line( cfd, buf, MAXLINE );
	if ( strcmp( buf, "trace done\n" ) != 0 )
	    printf( "mpdtrace: %s", buf );
    }
    return(0);
}

int mpdlistjobs( int argc, char *argv[] )
{
    char buf[MAXLINE];
	
    sprintf( buf, "cmd=listjobs\n" );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack from mpd */
    while ( strcmp( buf, "listjobs done\n" ) != 0 ) {
        read_line( cfd, buf, MAXLINE );
	if ( strcmp( buf, "listjobs done\n" ) != 0 )
	    printf( "mpdlistjobs: %s", buf );
    }
    return(0);
}

int mpdbomb(argc,argv)
int argc;
char *argv[];
{
    char buf[MAXLINE];

    if (argc < 2)
    {
	printf( "usage: mpdbomb mpd_id \n" );
	return(0);
    }
    sprintf( buf, "cmd=bomb mpd_id=%s\n", argv[1] );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack from mpd */
    mpdprintf( debug, "mpdbomb: msg from mpd: %s", buf );
    return(0);
}

int mpdexit(int argc, char *argv[])
{
    char buf[MAXLINE];
    int rc;

    if (argc < 2)
    {
	printf( "usage: mpdexit mpd_id \n" );
	return(0);
    }
    if ( strcmp( argv[1], "me" ) == 0 ) {
	mpdprintf( debug, "killing local mpd\n" );
	sprintf( buf, "cmd=exit mpd_id=self\n" );
    }
    else
	sprintf( buf, "cmd=exit mpd_id=%s\n", argv[1] );
    send_msg( cfd, buf, strlen( buf ) );
    rc = read_line( cfd, buf, MAXLINE );  /* get ack from mpd */
    if ( rc == -1 )
	printf( "lost contact with local mpd\n" );
    mpdprintf( debug, "mpdexit: msg from mpd: %s", buf );
    return(0);
}

int mpdallexit(int argc, char *argv[])
{
    char buf[MAXLINE];

    if (argc != 1)
    {
	printf( "usage: mpdallexit \n" );
	return(0);
    }
    sprintf( buf, "cmd=allexit\n" );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack from mpd */
    mpdprintf(debug,"mpdallexit: msg from mpd: %s",buf);
    return(0);
}

int mpdshutdown(argc,argv)
int argc;
char *argv[];
{
    char buf[MAXLINE];

    if (argc < 2) {
	printf( "usage: mpdshutdown mpd_id \n" );
	return(0);
    }
    sprintf( buf, "cmd=shutdown mpd_id=%s\n", argv[1] );
    send_msg( cfd, buf, strlen( buf ) );
    read_line( cfd, buf, MAXLINE );  /* get ack from mpd */
    mpdprintf( debug, "mpdshutdown: msg from mpd: %s", buf );
    return(0);
}

int mpdhelp(int argc, char *argv[])
{
    printf("\n" );
    printf("mpdhelp\n" );
    printf("  prints this information\n");
    printf("mpdcleanup \n" );
    printf("  deletes unix socket files /tmp/mpd.* if necessary \n");
    printf("mpdtrace\n" );
    printf("  causes each mpd in the ring to respond with \n");
    printf("  a message identifying itself and its neighbors\n");
    printf("mpddump [what]\n" );
    printf("  causes all the mpds to dump data.\n");
    printf("  \"what\" can be \"fdtable\", \"jobtable\", or \"proctable\".\n");
    printf("  It defaults to \"all\".\n");
    printf("mpdmandump jobid manrank [what]\n" );
    printf("  causes the manager given by <jobid> and <manrank> to dump data\n");
    printf("  \"what\" is currently being defined.\n");
    printf("  It defaults to \"all\".\n");
    printf("mpdringtest count\n" );
    printf("  sends a message around the ring \"count\" times\n");
    printf("mpdexit mpd_id \n" );
    printf("  causes the specified mpd_id to exit gracefully;\n");
    printf("  mpd_id is specified as host_portnum or as \"me\" for the local mpd;\n");
    printf("mpdshutdown mpd_id \n" );
    printf("  shuts down the specified mpd; more robust version of mpdexit\n");
    printf("  mpd_id is specified as host_portnum;\n");
    printf("mpdallexit \n" );
    printf("  causes all mpds to exit gracefully;\n");
    printf("mpdbomb mpd_id \n" );
    printf("  for testing: causes the specified mpd_id to \"fail\";\n");
    printf("  mpd_id is specified as host_portnum\n");
    printf("mpdlistjobs \n" );
    printf("  lists active jobs managed by mpds in ring\n");
    printf("mpdkilljob job_id \n" );
    printf("  aborts the specified job\n");
    printf("\n" );
    return(0);
}

/******  This is the console that talks to managers  ****/

int mpdmpexec( int argc, char *argv[] )
{
    int i, argcnt, envcnt, envflag, loccnt, locflag, rc, num_fds, optcount;
    int shmemgrpsize;
    char buf[MAXLINE], argbuf[MAXLINE], stuffed_arg[MAXLINE], wdirname[MAXPATHLEN];
    char path[MAXPATHLEN], executable[MAXPATHLEN], jobinfobuf[8], jobidbuf[8];
    char display[MAXLINE], machinefile[MAXPATHLEN], jobidfile[MAXPATHLEN];
    char myhostname[MAXHOSTNMLEN];
    char *p;
    fd_set readfds, writefds;
    struct timeval tv;
    int path_was_supplied_by_user, line_labels, close_stdin, myrinet_job;
    int first_at_console;  /* run first process on same node as console */
    int whole_lines;
    char hostlist_patterns[128][PATSIZE], tempbuf[128], hostlist_buf[MAXLINE];
    char requested_jobid[10], requested_userid[10];
    char co_program[MAXPATHLEN];
    char mship_port_env[80], mship_fd_env[80], mship_nprocs_env[80];
    int mship_port, mship_fd, mship_pid;
    FILE *jfp;

    machinefile[0]	= '\0';
    display[0]		= '\0';
    requested_jobid[0]	= '\0';
    requested_userid[0]	= '\0';
    jobidfile[0]        = '\0';
    co_program[0]       = '\0';

    if (argc < 3) {
	printf( "usage: mpdmpexec -n numprocs [-l] "
	        "[-g <shmemgrpsize>] [-s] [-m machines_filename] executable"
		" [args] [-MPDENV- env] [-MPDLOC- loc(s)]\n" );
	return(0);
    }

    init_fdtable();

    /* Set up listener port.  This will be used by the manager with rank 0 to connect
       a control stream and streams for stdin, stdout, and stderr. */

    listener_idx		  = allocate_fdentry();
    fdtable[listener_idx].portnum = 0;
    fdtable[listener_idx].fd	  = setup_network_socket( &fdtable[listener_idx].portnum);
    fdtable[listener_idx].read	  = 1;
    fdtable[listener_idx].write	  = 0;
    fdtable[listener_idx].handler = LISTEN_STREAM;
    strcpy( fdtable[listener_idx].name, "listener" );

    optcount = 1;		/* counts argv[0] */
    gethostname( myhostname, MAXHOSTNMLEN );
    getcwd( wdirname, MAXPATHLEN );
    mpdprintf( debug, "current console working directory = %s\n", wdirname );
    strcpy( path, getenv( "PATH" ) ); /* may want to propagate to manager */

    if ( (p = getenv( "DISPLAY" ) ) != NULL )
	strcpy( display, p ); /* For X11 programs */  

    mpdprintf( debug, "current path = %s\n", path );
    line_labels	     = 0;
    whole_lines	     = 0;
    shmemgrpsize     = 1;
    close_stdin	     = 0;
    myrinet_job	     = 0;
    loccnt	     = 0;
    tvdebug          = 0;
    first_at_console = 1;

    while ( optcount < argc  &&  argv[optcount][0] == '-' ) {
	if ( argv[optcount][1] == 'n' ) {
	    for (i=0; i < strlen( argv[optcount+1] ); i++) {
	        if ( ! isdigit( argv[optcount+1][i] ) ) {
		    fprintf( stderr, "invalid jobsize specified: %s\n", argv[optcount+1] );
		    return( -1 );
		}
	    }
	    jobsize = atoi( argv[optcount + 1] );
	    if ( ( jobsize == 0 ) || ( jobsize > MAXTOTPROCS ) ) {
		fprintf( stderr, "jobsize must be > 0 and < %d\n", MAXTOTPROCS );
		return( -1 );
	    }
	    optcount += 2;
	}
	else if ( argv[optcount][1] == 'i' ) {
	    iotree = 0;
	    optcount++;
	}
	else if ( argv[optcount][1] == 'h' ) {
	    usage_mpirun( );
	    optcount++;
	}
	else if ( argv[optcount][1] == 'l' ) {
	    line_labels = 1;
	    optcount++;
	}
	else if ( strcmp( argv[optcount], "whole" ) == 0 ) {
	    whole_lines = 1;
	    optcount++;
	}
	else if ( argv[optcount][1] == '1' ) {
	    first_at_console = 0;
	    optcount++;
	}
	else if ( argv[optcount][1] == 's' ) {
	    close_stdin = 1;
	    optcount++;
	}
	else if ( argv[optcount][1] == 'y' ) {
	    myrinet_job = 1;
	    optcount++;
	}
	else if ( argv[optcount][1] == 'g' ) {
	    for (i=0; i < strlen( argv[optcount+1] ); i++) {
	        if ( ! isdigit( argv[optcount+1][i] ) ) {
		    printf( "invalid groupsize specified\n" );
		    return( -1 );
		}
	    }
	    shmemgrpsize = atoi( argv[optcount + 1] );
	    optcount += 2;
	}
	else if ( strcmp( argv[optcount], "-jid" ) == 0 ) {
	    strncpy( requested_jobid, argv[optcount+1], 10 );
	    optcount += 2;
	}
	else if ( argv[optcount][1] == 'u' ) {
	    strncpy( requested_userid, argv[optcount+1], 10 );
	    optcount += 2;
	}
	else if ( strcmp ( argv[optcount], "-copgm" ) == 0 ) {
	    if ( argv[optcount+1][0] == '-' ) {
		fprintf( stderr, "no co-program specified after -copgm\n" );
		return( -1 );
	    }
	    else {
		strncpy( co_program, argv[optcount+1], MAXPATHLEN );
		optcount += 2;
	    }
	}
	else if ( strcmp( argv[optcount], "-mvhome" ) == 0 )
	    optcount++;		/* ignore this argument */
	else if ( strcmp( argv[optcount], "-mvback" ) == 0 )
	    optcount += 2;	/* ignore this argument and the next */
	else if ( argv[optcount][1] == 'm' ) { /* note potential conflict with above 2,
						handled by having this after them */
	    strcpy( machinefile,argv[optcount+1] );
	    squash( machinefile, hostlist_patterns );
	    optcount += 2;
	    hostlist_buf[0] = '\0';
	    for (i=0; hostlist_patterns[i][0]; i++) {
		loccnt++;
		mpd_stuff_arg(hostlist_patterns[i],stuffed_arg);
		sprintf( tempbuf, " loc%d=%s", loccnt, stuffed_arg );
		strcat( hostlist_buf, tempbuf );
	    }
	}
	else if ( strcmp ( argv[optcount], "-wdir" ) == 0 ) {
	    if ( argv[optcount+1][0] == '-' ) {
		fprintf( stderr, "no working directory specified after -wdir\n" );
		return( -1 );
	    }
	    else {
		strncpy( wdirname, argv[optcount+1], MAXPATHLEN );
		optcount += 2;
	    }
	}
	else if ( strcmp ( argv[optcount], "-jidfile" ) == 0 ) {
	    if ( argv[optcount+1][0] == '-' ) {
		fprintf( stderr, "no file name specified after -jidfile\n" );
		return( -1 );
	    }
	    else {
		strncpy( jobidfile, argv[optcount+1], MAXPATHLEN );
		optcount += 2;
	    }
	}
	else {
	    fprintf( stderr, "Unrecognized argument: %s\n", argv[optcount] );
	    if ( mpirunning )
		usage_mpirun( );
	    else
		fprintf( stderr, "usage: mpdmpexec -n numprocs [-l] "
			"[-g <shmemgrpsize>] [-s] executable"
			" [args] [-MPDENV- env] [-MPDLOC- loc(s)]\n" );
	    return(-1);
	}
    }
    if ( MPIR_being_debugged )
	tvdebug = 1;

    if ( gdb )
        strcpy( executable, "gdb" );
    else {
        if ( optcount >= argc ) {
	    printf( "no executable specified\n" );
	    return( -1 );
	}
        strcpy( executable, argv[optcount++] );
    }

    if ( gdb ) {
	line_labels = 1;
	mergeprompts = jobsize;	   /* initially talking to all gdb's */
    }

    if ( co_program[0] ) {
	mship_fd = setup_network_socket( &mship_port );
	mship_pid = fork();
	if ( mship_pid == 0 ) {
	    sprintf( mship_port_env,"CON_MSHIP_PORT=%d",mship_port );
	    putenv( mship_port_env );
	    sprintf( mship_fd_env,"CON_MSHIP_FD=%d",mship_fd );
	    putenv( mship_fd_env );
	    sprintf( mship_nprocs_env,"CON_MSHIP_NPROCS=%d",jobsize );
	    putenv( mship_nprocs_env );
	    rc = execvp( co_program, NULL );
	    mpdprintf(1, "failed to start mother ship: rc=%d\n", rc );
	    exit(0);  /* just in case */
	}
	close(mship_fd);
    }

    sprintf( buf,
	     "cmd=mpexec hostname=%s portnum=%d iotree=%d numprocs=%d "
	     "executable=%s gdb=%d tvdebug=%d line_labels=%d shmemgrpsize=%d "
             "first_at_console=%d myrinet_job=%d "
             "whole_lines=%d "
             "copgm=%s mship_host=%s mship_port=%d "
             "username=%s requested_jobid=%s requested_userid=%s ",
	     myhostname, fdtable[listener_idx].portnum, iotree, jobsize,
	     executable, gdb, tvdebug, line_labels, shmemgrpsize,
	     first_at_console, myrinet_job, whole_lines,
	     co_program,myhostname,mship_port,
             pwent->pw_name, requested_jobid, requested_userid );
    argcnt  = 0;
    envcnt  = 0;
    envflag = 0;
    locflag = 0;
    path_was_supplied_by_user = 0;

    if ( gdb )
    {
        argcnt++;
	sprintf( argbuf, " arg%d=-q", argcnt );
	strcat( buf,argbuf );
	argcnt++;
	sprintf( argbuf, " arg%d=%s", argcnt, argv[optcount++] );
	strcat( buf,argbuf );
    }

    if (loccnt) {
	if ( ( strlen(buf) + strlen(hostlist_buf) ) < MAXLINE )
            strcat(buf,hostlist_buf);
	else {
	    printf("exiting: squash buffer not large enough to handle host list\n");
	    exit(1);
	}
    }
    if (argc > optcount)
    {
	strcat( buf, " " );  /* extra blank before args */
	for ( i = optcount; i < argc; i++ ) {
	    if (strcmp(argv[i],"-MPDENV-") == 0) {
	        envflag = 1;
	        locflag = 0;
	    }
	    else if (strcmp(argv[i],"-MPDLOC-") == 0) {
	        locflag = 1;
	        envflag = 0;
	    }
	    else {
		mpd_stuff_arg(argv[i],stuffed_arg);
		if (locflag) {
		    loccnt++;
		    sprintf( argbuf, " loc%d=%s", loccnt, stuffed_arg );
		}
		else if (envflag) {
		    envcnt++;
		    sprintf( argbuf, " env%d=%s", envcnt, stuffed_arg );
		    if ( strncmp( argv[i], "PATH=", 5 ) == 0 )
			path_was_supplied_by_user = 1;
		}
		else {
		    argcnt++;
		    sprintf( argbuf, " arg%d=%s", argcnt, stuffed_arg );
		}
		strcat( buf, argbuf );
	    }
	}
    }
    sprintf( argbuf, " argc=%d", argcnt );  
    strcat( buf, argbuf );
    if ( ! path_was_supplied_by_user ) {
	sprintf( argbuf, "PATH=%s", path );
	mpd_stuff_arg(argbuf,stuffed_arg);
	envcnt++;
	sprintf( argbuf, " env%d=%s", envcnt, stuffed_arg );
	strcat( buf, argbuf );
    }

    if ( display[0] != '\0' ) {
	sprintf( argbuf, "DISPLAY=%s", display);
	mpd_stuff_arg(argbuf, stuffed_arg);
	envcnt++;
	sprintf( argbuf, " env%d=%s", envcnt, stuffed_arg );
	strcat( buf, argbuf );
    }

/*  This code should be obsolete now that we handle the Myrinet file differently
    if ( strcmp( machinefile, "" ) != 0 ) { 
	sprintf( argbuf, "GMPI_CONF=%s.myr", machinefile);
	mpd_stuff_arg(argbuf, stuffed_arg);
	envcnt++;
	sprintf( argbuf, " env%d=%s", envcnt, stuffed_arg );
	strcat( buf, argbuf );
    }
*/
    
    sprintf( argbuf, "PWD=%s", wdirname ); 
    mpd_stuff_arg(argbuf,stuffed_arg);
    envcnt++;
    sprintf( argbuf, " env%d=%s", envcnt, stuffed_arg );
    strcat( buf, argbuf );

    sprintf( argbuf, " envc=%d", envcnt );  
    strcat( buf, argbuf );
    sprintf( argbuf, " locc=%d", loccnt );  
    strcat( buf, argbuf );

    strcat( buf, "\n" );
    mpdprintf( debug, "mpdmpexec: sending to mpd :%s:\n", buf );
    send_msg( cfd, buf, strlen( buf ) );

    rc = read_line( cfd, buf, MAXLINE );  /* get ack_from_mpd */
    if ( rc == -1 ) {
	printf( "console lost contact with local mpd\n" );
	exit ( 1 );
    }
    else {
	mpdprintf( debug, "mpdmpexec: msg from mpd: %s", buf );
	if ( strcmp( buf, "cmd=ack_from_mpd\n" ) != 0 ) {
	    printf( "possible invalid cmd from user; invalid response from mpd: %s\n",
		    buf );
	    exit(1);
	}
    }

    /* receive and handle jobinfo msg */
    read_line( cfd, buf, MAXLINE ); /* get jobid from mpd */
    mpdprintf( debug, "mpdmpexec: msg from mpd: %s", buf );
    mpd_parse_keyvals( buf );
    mpd_getval( "cmd", jobinfobuf );
    if ( strcmp( jobinfobuf, "jobinfo" ) != 0 ) {
	mpdprintf( 1, "expecting jobinfo msg; got :%s:\n", jobinfobuf );
	exit(1);
    }
    mpd_getval( "jobid", jobinfobuf );
    jobid = atoi( jobinfobuf );
    mpd_getval( "status", jobinfobuf );
    if ( strcmp( jobinfobuf, "started" ) != 0 ) {
        mpdprintf( 1, "failed to start job %d; \n"
	              "you may have invalid machine names \n"
		      "or the set of mpds you specified may only run root jobs \n"
		      "or mpd may not be able to find mpdman\n",
		   jobid );
	exit(1);
    }
    /* fprintf( stderr, "%s", buf );*/  	/* print job id */
    if ( jobidfile[0] != '\0' ) {
	if ( ( jfp = fopen( jobidfile, "w" ) ) == NULL ) {
	    fprintf( stderr, "could not open file %s to put job id into\n", jobidfile );
	}	
	else {
	    sprintf( jobidbuf, "%d\n", jobid );
	    fputs( jobidbuf, jfp );
	    fclose( jfp );
	}
    }
    
    /* don't close socket to mpd until later when we get ctl stream from mpdman*/
    /* dclose( cfd ); */

    if ( close_stdin ) {
        dclose( 0 );
    }
    else {
        /* put stdin in fdtable */
        user_stdin_idx		        = allocate_fdentry();
        fdtable[user_stdin_idx].fd      = 0;
        fdtable[user_stdin_idx].read    = 0;  /* reset to 1 when recv conn from mgr */
        fdtable[user_stdin_idx].write   = 0;
        fdtable[user_stdin_idx].handler = USER_STDIN;
        strcpy( fdtable[user_stdin_idx].name, "user_stdin" );
    }

    /* Main loop */
    done = 0;
    while ( !done ) {
        FD_ZERO( &readfds );
        FD_ZERO( &writefds );
        for ( i=0; i <= fdtable_high_water_mark; i++ )
            if ( fdtable[i].active && fdtable[i].read ) 
		FD_SET( fdtable[i].fd, &readfds );

        num_fds = FD_SETSIZE;
        tv.tv_sec = 3600;
        tv.tv_usec = 0;

        rc = select( num_fds, &readfds, &writefds, NULL, &tv );

        if ( rc == 0 ) {
            mpdprintf( debug, "select timed out after %ld minutes\n", tv.tv_sec/60 );
            continue;
        } 
        if ( ( rc == -1 ) && ( errno == EINTR ) ) {
            mpdprintf( debug, "select interrupted; continuing\n" );
            continue;
        }
        if ( rc < 0 ) {
            done = 1;
            error_check( rc, "console main loop: select" );
        }

        for ( i=0; i <= fdtable_high_water_mark; i++ ) {
            if ( fdtable[i].active ) {
                if ( FD_ISSET( fdtable[i].fd, &readfds ) )
                    con_handle_input_fd( i );
            }
        }
	mpdprintf( 000, "control_input_closed=%d stdout_input_closed=%d "
	                "stderr_input_closed=%d\n",control_input_closed,
			stdout_input_closed,stderr_input_closed );
	if ( control_input_closed && stdout_input_closed && stderr_input_closed )
	    done = 1;
    }
    
    return(0);
}

/******  This is the mpd version of mpirun; it uses mpdmpexec  ****/

int mpirun( int argc, char *argv[] )
{
    /* mpirun must not require -np in the first position (doing so
       keeps the test suite from running */
    int i = argc, found = 0;
    for (i=1; i<argc; i++) {
	if (strcmp( argv[i], "-np" ) == 0) { found = 1 ; break; }
    }
    if (!found) 
    {
	usage_mpirun();
	exit( 1 );
    }
    argv[i][2] = '\0';		/* replace -np by -n */
    mpirunning = 1;		/* so command-line parsing will do correct err_msg */
    mpdmpexec( argc, argv );
    return( 0 );
}

/******  This is the debugging version of mpirun  ****/

int mpigdb( int argc, char *argv[] )
{
    gdb = 1;	        /* set flag to indicate we are debugging under gdb */
    mpirun( argc, argv );
    return( 0 );
}


void con_handle_input_fd( int idx )
{
    if ( fdtable[idx].handler == NOTSET )
        mpdprintf( debug, "man:  handler not set for port %d\n", idx );
    else if ( fdtable[idx].handler == LISTEN_STREAM )
        handle_listen_input( idx );
    else if ( fdtable[idx].handler == TEMP_STREAM )
        handle_temp_input( idx );
    else if ( fdtable[idx].handler == CONTROL_STREAM )
        handle_control_input( idx );
    else if ( fdtable[idx].handler == STDOUT_STREAM)
        handle_stdout_input( idx );
    else if ( fdtable[idx].handler == STDERR_STREAM )
        handle_stderr_input( idx );
    else if ( fdtable[idx].handler == STDIN_STREAM )
	handle_stdin_input( idx );
    else if ( fdtable[idx].handler == USER_STDIN )
        handle_user_stdin( idx );
    else
        mpdprintf( debug, "invalid handler for fdtable entry %d\n", idx );
}

void handle_listen_input( int idx )
{
    int tmp_idx;

    mpdprintf( debug, "console: handling listen input, accept here\n" ); 
    tmp_idx = allocate_fdentry();
    fdtable[tmp_idx].fd      = accept_connection( fdtable[idx].fd );
    fdtable[tmp_idx].handler = TEMP_STREAM;
    fdtable[tmp_idx].read    = 1;
}

void handle_temp_input( int idx )
{
    int  length;
    char message[MAXLINE], tmpbuf[MAXLINE], cmd[MAXLINE];

    if ( ( length = read_line(fdtable[idx].fd, message, MAXLINE ) ) != 0 )
        mpdprintf( debug, "message from manager to handle = :%s: (read %d)\n",
		   message, length );
    else {
        mpdprintf( 1, "console failed to retrieve msg on conn to listener\n");
	return;
    }
    strcpy( tmpbuf, message );             
    mpd_parse_keyvals( tmpbuf );
    mpd_getval( "cmd", cmd );
    if ( strcmp( cmd, "new_ctl_stream" ) == 0 ) {
        ctl_idx = idx;
	fdtable[ctl_idx].handler = CONTROL_STREAM;
	fdtable[ctl_idx].read = 1;
	strcpy( fdtable[ctl_idx].name, "ctl_stream" );
	/* control connection now open, so set up to pass interrupts to manager */
	mpd_Signal( SIGTSTP,  con_sig_handler );  /* Pass suspension to manager */
	mpd_Signal( SIGCONT,  con_sig_handler );  /* Pass cont to manager  */
	mpd_Signal( SIGINT,   con_sig_handler );  /* Pass kill to manager  */
	dclose( cfd );  /* now that we have a ctl stream from mpdman */
	if ( gdb )
	    write_line( ctl_idx, "cmd=set stdin=all\n" );

	/* can ONLY do con_bnr_put's after we have a valid ctl_idx ( ! -1) */
	/*****
	sprintf( tmpbuf, "cmd=con_bnr_put attr=RALPH val=BUTLER gid=0\n" );
	write_line( ctl_idx, tmpbuf );
	*****/
    }
    else if ( strcmp( cmd, "new_stdin_stream" ) == 0 ) {
        stdin_idx = idx;
	fdtable[stdin_idx].handler = STDIN_STREAM;
	fdtable[stdin_idx].read = 0;
	strcpy( fdtable[stdin_idx].name, "stdin_stream" );
	if ( user_stdin_idx != -1 )
	    fdtable[user_stdin_idx].read = 1;
    }
    else if ( strcmp( cmd, "new_stdout_stream" ) == 0 ) {
        stdout_idx = idx;
	fdtable[stdout_idx].handler = STDOUT_STREAM;
	fdtable[stdout_idx].read = 1;
	strcpy( fdtable[stdout_idx].name, "stdout_stream" );
    }
    else if ( strcmp( cmd, "new_stderr_stream" ) == 0 ) {
        stderr_idx = idx;
	fdtable[stderr_idx].handler = STDERR_STREAM;
	fdtable[stderr_idx].read = 1;
	strcpy( fdtable[stderr_idx].name, "stderr_stream" );
    }
    else {
        mpdprintf( 1, "unrecognized msg to console's listener = :%s:\n",cmd );
    }
}

static void dump_MPIR_proctable ( void )
{
    int i;

    mpdprintf( debug, "Proctable (%d entries)\n",MPIR_proctable_size );
    for (i=0; i<MPIR_proctable_size; i++) {
	mpdprintf( debug, "%4d: %10s %d %s\n",
		   i, MPIR_proctable[i].host_name, MPIR_proctable[i].pid,
		   MPIR_proctable[i].executable_name);
    }
}

void handle_control_input( int idx )
{
    int length;
    char buf[MAXLINE];
    char cmd[80];

    if ( ( length = read_line(fdtable[idx].fd, buf, MAXLINE ) ) > 0 ) {
        mpdprintf( debug, "console received on control from manager: :%s:\n", buf );
	mpd_parse_keyvals( buf );
	mpd_getval( "cmd", cmd );
	if ( strcmp( cmd, "jobdead" ) == 0 ) {
	    mpdprintf( debug, "handle_control_input sending allexit\n");
	    sprintf( buf, "cmd=allexit\n" );
	    write_line( ctl_idx, buf );
	    mpdprintf( debug, "parallel job exited\n" );
	    /* exit( 0 ); */ /* hang around until manager 0 ends */
	}
	else if ( strcmp( cmd, "jobaborted" ) == 0 ) {
	    printf( "job %d aborted with code %d by process %d\n",
	            atoi( mpd_getval( "job", buf ) ),
	            atoi( mpd_getval( "code", buf ) ),
	            atoi( mpd_getval( "rank", buf ) ) );
	    if ( strcmp( mpd_getval( "by", buf ), "mpdman") == 0 ) {
		if ( strcmp( mpd_getval( "reason", buf ), "execvp_failed" ) == 0 ) {
		    printf( "unable to execute program: %s\n",
			    mpd_getval( "info", buf ) );
		}
		else if ( strcmp( mpd_getval( "reason", buf ), "probable_brokenpipe_to_client" ) == 0 ) {
		    printf( "broken pipe to client\n" );
		}
	    }
	    /* exit( 0 ); */ /* hang around until manager 0 ends */
	}
	else if ( strcmp( cmd, "client_info" ) == 0 ) {
	    /* Save information from this message in the global
	     * array, and see if we have all the info we're expecting
	     */
	    static int clients_received;
	    int version;
	    int rank = atoi( mpd_getval( "rank", buf ) );
	    if ( rank < 0 || rank > jobsize ) {
		mpdprintf( 1, "console received client_info from bad rank (%d)\n", rank);
		return;
	    }
	    else
		mpdprintf( debug, "console received client_info from rank %d\n", rank );
	    
	    if ( MPIR_proctable == 0 ) {
		MPIR_proctable = (MPIR_PROCDESC *) calloc(
		    jobsize , sizeof( MPIR_PROCDESC ) );
		if ( MPIR_proctable == 0 ) {
		    mpdprintf( 1, "cannot allocate proctable for %d procs\n",
			   jobsize);
		    return;
		}
	    }
	    
	    MPIR_proctable[rank].pid = atoi( mpd_getval( "pid", buf ) );
	    MPIR_proctable[rank].host_name = dupstr( mpd_getval( "host", buf ) );
	    MPIR_proctable[rank].executable_name = dupstr( mpd_getval( "execname", buf ) );
	    
	    version = atoi( mpd_getval( "version", buf ) );
	    if ( version != MPD_VERSION ) {
		mpdprintf( 1,
			   "client %s, rank %d, on host %s has version %d; mine is %d\n",
			   MPIR_proctable[rank].executable_name, rank,
			   MPIR_proctable[rank].host_name, version, MPD_VERSION );
	    }
	    /* Has everyone checked in yet ? */
	    clients_received ++;
	    if ( clients_received == jobsize ) {
		MPIR_proctable_size = jobsize;
		MPIR_debug_state = MPIR_DEBUG_SPAWNED;
		dump_MPIR_proctable();
		if ( tvdebug ) {
		    MPIR_Breakpoint(); /* Tell the debugger we're ready */
		
		    /* The debugger is happy, so now we can release the clients */
		    mpdprintf( debug, "returned from MPIR_Breakpoint, releasing clients\n");
		    sprintf( buf, "cmd=client_release\n" );
		    write_line( ctl_idx, buf );
		}
	    }

	}
	else if ( strcmp( cmd, "man_ringtest_completed" ) == 0 )
	    printf( "manringtest completed\n" );
	else
	    mpdprintf( 1, "unrecognized message from job manager\n" );
    }
    else if ( length == 0 ) {
	mpdprintf( debug, "eof on cntl input\n" );
	dclose( fdtable[idx].fd );
	deallocate_fdentry( idx );
	control_input_closed = 1;
    }
    else
        mpdprintf( 1,
		   "console failed to retrieve msg from control stream, errno = %d\n",
		   errno );
}

static struct merged *som = NULL; /* The merged output struct for stdout */
void handle_stdout_input( int idx )
{
    int n, promptsfound, len_stripped;
    char buf[STREAMBUFSIZE+1], newbuf[STREAMBUFSIZE];
    static int first_prompts = 1;

    if ( ( n = read( fdtable[idx].fd, buf, STREAMBUFSIZE ) ) > 0 ) {
	buf[n] = '\0';		   /* null terminate for string processing */
	if ( gdb ) {
            if (som == NULL)
                som = merged_create(jobsize, DFLT_NO_LINES, stdout);

            /* fprintf( stderr, "read |%s|\n", buf ); */
	    if (som == NULL || merged_submit( som, buf ) < 0) { 
	        process_buf( buf, newbuf, &promptsfound, &len_stripped );
	        numprompts += promptsfound;
	        mpdprintf( debug, "handle_stdout_input writing %d\n", n - ( len_stripped ) );
	        write( 1, newbuf, n - ( len_stripped ) );
            }

	    if ( ( merged_num_ready(som) >= mergeprompts ) || ( numprompts >= mergeprompts ) ) {
                merged_flush(som);
                if ( !first_prompts )
                    printf( "(mpigdb) " );
		fflush( stdout );
		if ( first_prompts ) {
		    first_prompts = 0;
            write_line( stdin_idx, "set prompt\n" );
            write_line( stdin_idx, "set confirm off\n" );
		    write_line( stdin_idx, "handle SIGUSR1 nostop noprint\n" );
		    write_line( stdin_idx, "handle SIGPIPE nostop noprint\n" );            
            write_line( stdin_idx, "set confirm on\n" );
            write_line( stdin_idx, "set prompt (gdb)\\n\n" );
		}
		numprompts = 0;
	    }
	}
	else 
	    write( 1, buf, n );
    }
    else if ( n == 0 ) {
	mpdprintf( debug, "console received eof on stdout from manager\n" );
	dclose( fdtable[idx].fd );
	deallocate_fdentry( idx );
	stdout_input_closed = 1;
    }
    else
	fprintf( stderr, "console failed to retrieve msg from stdout stream\n" );
}

static struct merged *sem = NULL; /* The merged output struct for stderr */
void handle_stderr_input( int idx )
{
    int n, promptsfound, len_stripped;
    char buf[STREAMBUFSIZE+1], newbuf[STREAMBUFSIZE];

    if ( ( n = read( fdtable[idx].fd, buf, STREAMBUFSIZE ) ) > 0 ) {
	buf[n] = '\0';		   /* null terminate for string processing */
	if ( gdb ) {
            if (sem == NULL)
                sem = merged_create(jobsize, DFLT_NO_LINES, stderr);

            if (sem == NULL || merged_submit( sem, buf ) < 0) {
                process_buf( buf, newbuf, &promptsfound, &len_stripped );
	        numprompts += promptsfound;
	        mpdprintf( debug, "handle_stderr_input writing %d\n", n - ( len_stripped ) );
	        /* write( 2, ".", 1);	 temporarily mark stderr - RL */
	        write( 2, newbuf, n - ( len_stripped ) );
            }

            if ( ( merged_num_ready(sem) >= mergeprompts ) || ( numprompts >= mergeprompts ) ) {
                merged_flush(sem); 
	        fprintf( stderr, "(mpigdb) " );
	        fflush( stderr );
	        numprompts = 0;
	    }
	}
	else {
	    /* write( 2, ".", 1);	temporarily mark stderr - RL */
	    write( 2, buf, n );
	}
    }
    else  if ( n == 0 ) {
	mpdprintf( debug, "console received eof on stderr from manager\n" );
	dclose( fdtable[idx].fd );
	deallocate_fdentry( idx );
	stderr_input_closed = 1;
    }
    else
	fprintf( stderr, "console failed to retrieve msg from stderr stream\n" );
}

void handle_stdin_input( int idx )
{
    int length;
    char buf[STREAMBUFSIZE];

    if ( ( length = read( fdtable[idx].fd, buf, STREAMBUFSIZE ) ) > 0 ) {
	mpdprintf( 1,
		   "console received unexpected input from manager on stdin_out: :%s: (read %d)\n",
		   buf, length );
    }
    else {
	/* manager 0 has closed stdin, so we should not pass stdin through to him */
	dclose( fdtable[idx].fd );
	deallocate_fdentry( idx );
	stdin_idx = -1;
    }
}

void handle_user_stdin( int idx )
{
    int n, target;
    char buf[STREAMBUFSIZE], buf2[STREAMBUFSIZE];
    
    if ( ( n = read_line( fdtable[idx].fd, buf, STREAMBUFSIZE ) ) > 0 ) {
				   /* note n includes newline but not NULL */
	mpdprintf( debug, "handle_user_stdin: stdin_idx=%d got :%s:\n",
	                  stdin_idx, buf ); 
	if ( buf[0] == '_' ) { /* escape character to access cntl */
	    sprintf( buf2, "cmd=%s", buf + 1 );
	    write_line( ctl_idx, buf2 );
	}
	else {
	    if ( stdin_idx != -1 ) {
		if ( gdb ) {	   /* check for 'z' command */
		    if ( strncmp( buf, "z", 1 ) == 0 ) {
			if ( n == 2) {	        /* z only, set stdin target to all */
			    mergeprompts = jobsize;
			    numprompts   = 0;   /* reset number seen */
			    write_line( ctl_idx, "cmd=set stdin=all\n" );
			}
			else {	                /* z <target>, set target */
			    if ( sscanf( buf+1, "%d", &target ) > 0 ) {
				if ( ( target > jobsize - 1 ) || ( target < 0 ) )
				    fprintf( stderr, "target out of range\n" );
				else {
				    mergeprompts = 1;
				    sprintf( buf2, "cmd=set stdin=%d\n", target );
				    write_line( ctl_idx, buf2 );
				}
			    }
			    else
				fprintf( stderr, "Usage: z <target process> OR z\n" );
			}
			printf( "(mpigdb) " ); fflush(stdout);
		    }
		    else {
			mpdprintf( debug, "handle_user_stdin doing send_msg\n");
			send_msg( fdtable[stdin_idx].fd, buf, n );
		    }
		}
		else {
		    mpdprintf( debug, "handle_user_stdin doing send_msg\n");
		    send_msg( fdtable[stdin_idx].fd, buf, n );
		}
	    }
	}
    }
    else if ( n == 0 ) {
	mpdprintf( debug, "console got EOF on its stdin\n" );
	dclose( fdtable[idx].fd ); /* console's own stdin */
	deallocate_fdentry( idx );
	/* close input connections to manager */
	if ( stdin_idx != -1 ) {
	    dclose( fdtable[stdin_idx].fd ); 
	    deallocate_fdentry( stdin_idx );
	    stdin_idx = -1;
	}
    }
    else 
	fprintf( stderr, "console failed to retrieve msg from console's stdin\n" );
}

void con_sig_handler( int signo )
{
    char buf[MAXLINE], buf2[MAXLINE], signame[24];
    int pid;

    unmap_signum( signo, signame );
    mpdprintf( debug, "Console got signal %d (%s)\n", signo, signame );

    if ( signo == SIGTSTP ) {
	mpdprintf( debug, "parallel job suspended\n" );
	fprintf( stderr, "job %d suspended\n", jobid );
	sprintf( buf, "cmd=signal signo=%s\n", "SIGTSTP" );
	write_line( ctl_idx, buf );
	/* suspend self*/
	mpd_Signal( SIGTSTP, SIG_DFL );  /* Set interrupt handler to default */
	pid = getpid();
	kill( pid, SIGTSTP );
    }
    else if ( signo == SIGCONT ) {
	mpdprintf( debug, "parallel job resumed\n" );
	sprintf( buf, "cmd=signal signo=%s\n", "SIGCONT" );
	write_line( ctl_idx, buf );
	mpd_Signal( SIGTSTP, con_sig_handler );   /* Restore this signal handler */
    }
    else if ( signo == SIGINT ) {
    if ( gdb ) {
        char c;
        int i, first_ready, n;

        merged_reset_next_ready(som);
        first_ready = merged_next_ready(som);
        if (first_ready >= 0) {
        while (1) {
            merged_print_status(som);
            printf("\nOptions:\n");
            printf("(1) Switch to the first ready node\n");
            printf("(2) Switch to a specific ready node\n");
            printf("(3) Send a command to all ready nodes\n");
            printf("\n");
            printf("(Q) Quit\n\n");
            printf("Enter your selection: ");
            fflush(stdout);
            if (scanf("%c", &c) != 1)
                continue;
            switch (c) {
                case '1':
				    mergeprompts = 1;
				    sprintf( buf2, "cmd=set stdin=%d\n", first_ready );
				    write_line( ctl_idx, buf2 );
                    printf("(mpigdb) ");
                    fflush(stdout);
                    break;
                case '2':
                    printf("Which node: ");
                    fflush(stdout);
                    scanf("%d", &i);
				    sprintf( buf2, "cmd=set stdin=%d\n", i );
				    write_line( ctl_idx, buf2 );
                    printf("(mpigdb) ");
                    fflush(stdout);
                    break;
                case '3':
                    printf("Enter command to send: ");
                    fflush(stdout);
                    buf[0] = '\0';
                    n = read( STDIN_FILENO, buf, MAXLINE ); 
                    for (i = first_ready; i >= 0; i = merged_next_ready(som)) {
				    sprintf( buf2, "cmd=set stdin=%d\n", i );
				    write_line( ctl_idx, buf2 );
                    send_msg( fdtable[stdin_idx].fd, buf, n );
                    }
                    write_line( ctl_idx, "cmd=set stdin=all\n" );
                    break;
                case 'q':
                    mpdprintf( debug, "parallel job being killed\n" );
                    sprintf( buf, "cmd=signal signo=%s\n", "SIGINT" );
                    write_line( ctl_idx, buf );
                    dclose( fdtable[ctl_idx].fd );
                    exit( -1 );
                default:
                    continue;
            }
            break;
        }
        mpd_Signal( SIGINT, con_sig_handler ); /* Restore this signal handler */
        } else {
        mpdprintf( debug, "parallel job being killed\n" );
        sprintf( buf, "cmd=signal signo=%s\n", "SIGINT" );
        write_line( ctl_idx, buf );
        dclose( fdtable[ctl_idx].fd );
        exit( -1 );
        }
    } else {
        mpdprintf( debug, "parallel job being killed\n" );
        sprintf( buf, "cmd=signal signo=%s\n", "SIGINT" );
        write_line( ctl_idx, buf );
        dclose( fdtable[ctl_idx].fd );
        exit( -1 );
    }
    }
    else {
	mpdprintf( 1, "unknown signal %d (%s)\n", signo, signame );
    }
}

int start_mpds( char *name )
{
    char cmd[50]; 

    sprintf(cmd, "startdaemons 5" );
    system( cmd );

    cfd = local_connect( name );
    return cfd; 
    
}

void process_buf( char *inbuf, char* outbuf, int *promptsfound, int *len_stripped )
/* This routine removes (gdb) prompts from the buffer and counts the number it finds.
   The idea is to enable the caller to issue a (mpigdb) prompt once each of the 
   instances of gdb has been heard from.
*/
{
    char out[STREAMBUFSIZE+1];
    char *p, *q;
    int  num, len, len_to_strip, total_len_stripped = 0;

    /* copy the input and null terminate it just in case it isn't already */
    strcpy( out, inbuf );
    
    len = strlen( out );
    num	= 0;
    p	= out; 
    while ( ( p = strstr( p, ": (gdb) " ) ) != NULL ) {
	num++;
	q = p;
	while ( q != out && *(q - 1) != '\n' && *(q - 1) != ' ' )
	    q--;	/* back up over line label to previous nl or beginning of buffer */
	len_to_strip = p + 8 - q;
	total_len_stripped += len_to_strip;
	len = len - len_to_strip;
	memmove( q, q + len_to_strip, len + 1 ); /* len + 1 to include the \0 */
	p = q;			/* reset for next search */
    }
    *len_stripped = total_len_stripped;
    *promptsfound = num;
    memmove( outbuf, out, len ); 
    /* fprintf( stderr, "old=:\n%s:\nnew=:\n%s:\nlen=%d, len_stripped=%d\n",
       inbuf, outbuf, len, *len_stripped ); */
}

void usage_mpirun()
{
    fprintf( stderr, "Usage: mpirun <args> executable <args_to_executable>\n" );
    fprintf( stderr, "Arguments are:\n" );
    fprintf( stderr, "  [-np num_processes_to_run] (required) \n" );
    fprintf( stderr, "  [-s]  (close stdin; can run in bkgd w/o tty input problems)\n" );
    fprintf( stderr, "  [-h]  print this message\n" );
    fprintf( stderr, "  [-g group_size]  (start group_size procs per mpd)\n" );
    fprintf( stderr, "  [-m machine_file]  (filename for allowed machines)\n" );
    /* fprintf( stderr, "  [-i]  (do not pre-build the I/O tree; faster startup, but may lose some I/O\n" ); unadvertised feature */
    fprintf( stderr, "  [-l]  (line labels; unique id for each process's output\n" );
    fprintf( stderr, "  [-1]  (do NOT start first process locally)\n" );
    fprintf( stderr, "  [-y]  (run as Myrinet job)\n" );
    fprintf( stderr, "  [-whole]  (stdout is guaranteed to stay in whole lines)\n" );
    fprintf( stderr, "  [-wdir dirname] (set working directory for application)\n" );
    /* fprintf( stderr, "  [-jid jobid] (run job with id jobid if possible)\n" ); */
    fprintf( stderr, "  [-jidfile file] (place job id in file file)\n" );
}

#define  MAXMACHINES 2048

void parsename( char *, char *, int *, char * );

int squash( char*  machines_filename, char outstring[][PATSIZE] )
{
    FILE *fp;
    char buf[BUFSIZ], tempnum[16],
	inpat1[MAXMACHINES][32],  inpat2[MAXMACHINES][32],
	outpat1[MAXMACHINES][32], outpat2[MAXMACHINES][32], outnum[MAXMACHINES][32];
    int  i, j, inidx, innum[MAXMACHINES], outidx = 0,
	new_high_found, range_low, range_high;

    if ( ( fp = fopen( machines_filename, "r" ) ) == NULL ) {
	fprintf( stderr, "%s could not be opened\n", machines_filename );
	exit( -1 );
    }

    for ( i = 0; i < MAXMACHINES; i++ ) {
	innum[i] = -1;
	inpat1[i][0] = '\0';
	inpat2[i][0] = '\0';
	outnum[i][0] = '\0';
	outpat1[i][0] = '\0';
	outpat2[i][0] = '\0';
    }

    inidx = 0;
    while ( fgets( buf, BUFSIZ, fp ) != NULL ) {
	if (buf[strlen(buf)-1] == '\n')
	    buf[strlen(buf)-1] = '\0';
	parsename( buf, inpat1[inidx], &innum[inidx], inpat2[inidx] );
	inidx++;
    }

    for (i=0; i < inidx; i++) {
	if ( inpat1[i][0] )  {
	    if ( innum[i] >= 0 ) {
		range_low = innum[i];
		range_high = innum[i];
		for (j=0; j < inidx; j++) {
		    if ( strcmp( inpat1[i],inpat1[j] ) == 0  &&
			 strcmp( inpat2[i],inpat2[j] ) == 0  &&
			 innum[j] == (range_low - 1))
		    {
			break;    /* skip for now */
		    }
		}
		if ( j < inidx )
		    continue;  /* skip for now */
		new_high_found = 1;
		while (new_high_found) {
		    for (j=0; j < inidx; j++) {
			if (j != i  &&  
			    strcmp( inpat1[i],inpat1[j] ) == 0  &&
			    strcmp( inpat2[i],inpat2[j] ) == 0  &&
			    innum[j] == (range_high + 1))
			{
			    range_high = innum[j];
			    new_high_found = 1;
			    inpat1[j][0] = '\0'; /* skip this one from now on */
			    innum[j] = -1;    /* skip this one from now on */
			    break;
			}
			new_high_found = 0;
		    }
		}
		if (range_high > range_low)  {
		    sprintf( outpat1[outidx],inpat1[i] );
		    sprintf( outnum[outidx], "%%d:%d-%d", range_low, range_high );
		    sprintf( outpat2[outidx],inpat2[i] );
		}
		else {
		    strcpy( outpat1[outidx],inpat1[i] );
		    sprintf( tempnum,"%d",innum[i] );
		    strcat( outpat1[outidx],tempnum );
		    strcat( outpat1[outidx],inpat2[i] );
		    inpat1[i][0] = '\0';    /* ignore on subsequent rounds */
		    innum[i] = -1;    /* ignore on subsequent rounds */
		}
	    }
	    else {
		strcpy( outpat1[outidx],inpat1[i] );
	    }
	    outidx++;
	}
    }
    for (i=0; i < outidx; i++) {
	strcpy(outstring[i],outpat1[i]);
	strcat(outstring[i],outnum[i]);
	strcat(outstring[i],outpat2[i]);
    }

    return( 0 );
}
	 
void parsename( char *buf, char *pattern1, int *num, char *pattern2 )
{
    char inpat1[32], inpat2[32], anum[32];
    int le, re;    /* left-end and right-end of right-most digits */

    inpat1[0] = inpat2[0] = anum[0] = '\0';

    re = 0;
    while ( re < strlen(buf)  &&  buf[re] != '.' )
	re++;
    while ( re >= 0  &&  !isdigit(buf[re]) )
	re--;
    if (re < 0)
        strcpy(inpat1,buf);
    else
    {
	strcpy(inpat2,&buf[re+1]);
	le = re;
	while ( le >= 0  &&  isdigit(buf[le]) )
	    le--;
	le++;
	while ( buf[le] == '0' )    /* leading 0 */
	    le++;
	strncpy(inpat1,buf,le);
	inpat1[le] = '\0';
	strncpy(anum,&buf[le],re-le+1);
	anum[re-le+1] = '\0';
    }

    sprintf( pattern1, "%s", inpat1 );
    if (anum[0])
	*num = atoi( anum );
    if (inpat2[0])
	sprintf( pattern2, "%s", inpat2 );
}

static char *dupstr( const char * src )
{
    char * res = 0;

    if (src) {
	size_t l = strlen (src);
	res = malloc (l+1);

	if (res)
	    memmove (res, src, l+1);
    } 

    return res;
}

static void sigalrm_handler(int signo)
{
    printf( "%s timed out after %d seconds waiting for mpd; exiting\n",
	    pgmname, TIMEOUTVAL );
    exit( -1 );
}
