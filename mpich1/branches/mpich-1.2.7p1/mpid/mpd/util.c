
/* 
   util.c
   This file contains routines needed by both the mpd daemons and
   their clients, such as consoles and application programs.
   Main routines linking to this should set the string mpdid to identify 
   sources of error messages 
*/

#ifdef FOO
/* to make code below using SA_RESTART/SA_INTERRUPT compile on linux
   we are not sure about portability right now, but it seems fine on 
   linux for now
*/
#define _BSD_SOURCE
#endif

#include "mpd.h"

struct procentry proctable[MAXPROCS];
struct jobentry jobtable[MAXJOBS];

extern struct fdentry fdtable[MAXFDENTRIES];
int fdtable_high_water_mark = -1;

extern int  debug;
extern char myid[IDSIZE];

extern struct mpd_keyval_pairs mpd_keyval_tab[64];
extern int mpd_keyval_tab_idx;

/*
 *	port table routines
 */
void init_fdtable()
{
    int i;
    for ( i = 0; i < MAXFDENTRIES; i++ ) {
	fdtable[i].active = 0;
    }
}

int allocate_fdentry()
{
    int i;

    for ( i = 0; i < MAXFDENTRIES; i++ )
	if ( fdtable[i].active == 0 )
	    break;
    if ( i >= MAXFDENTRIES )
    {
	mpdprintf( 1, "*** WARNING: mpd's fdtable size exceeded\n" );
        return( -1 );
    }
    if ( i > fdtable_high_water_mark )
        fdtable_high_water_mark = i;
    fdtable[i].active	= 1;
    fdtable[i].fd	= -1;
    fdtable[i].read	= 0;
    fdtable[i].write	= 0;
    fdtable[i].portnum	= -1;
    fdtable[i].file	= NULL;
    fdtable[i].handler	= NOTSET;
    strcpy( fdtable[i].name, "" );
    mpdprintf( 0, "allocated fdtable entry %d\n", i );
    return( i );
}

void deallocate_fdentry( idx )
int idx;
{
    fdtable[idx].active = 0;
}

void dump_fdtable( identifier )
char *identifier;
{
    int i;
    
    mpdprintf( 1, "fdtable( %s )\n", identifier );
    for ( i = 0; i < MAXFDENTRIES; i++ ) {
	if ( fdtable[i].active == 1 )
	    mpdprintf( 1,
		"fd[%d]: handler=%s, fd=%d, rd=%d, wr=%d, port=%d, file=%d, name=%s\n",
		i, phandler(fdtable[i].handler), fdtable[i].fd,
		fdtable[i].read, fdtable[i].write,
		fdtable[i].portnum, fdtable[i].file, fdtable[i].name );
    }
}

void init_jobtable()
{
    int i;
    for ( i = 0; i < MAXJOBS; i++ )
	jobtable[i].active = 0;
}

int allocate_jobent()
{
    int i;
    for ( i = 0; i < MAXJOBS; i++ )
	if ( jobtable[i].active == 0 )
	    break;
    if (i >= MAXJOBS) {
	mpdprintf( 1, "could not allocate job table entry; MAXJOBS = %d\n", MAXJOBS );
	return(-1);
    }
    jobtable[i].active = 1;
    jobtable[i].jobid = -1;
    jobtable[i].jobsize = -1;
    jobtable[i].alive_here_sofar = 0;
    jobtable[i].alive_in_job_sofar = 0;
    jobtable[i].added_to_job_sofar = 0;
    jobtable[i].jobsync_is_here = 0;
    return i;
}

int find_jobid_in_jobtable( jobid )
int jobid;
{
    int i;

    for ( i=0; i < MAXJOBS; i++ )
	if ( jobtable[i].active  &&  jobtable[i].jobid == jobid )
	    return(i);
    return(-1);
}

void deallocate_jobent( idx )
int idx;
{
    jobtable[idx].active = 0;
}

void remove_from_jobtable( jobid )
int jobid;
{
    int i;
 
    for ( i = 0; i < MAXJOBS; i++ ) {
        if ( jobtable[i].active && ( jobtable[i].jobid == jobid ) ) {
            deallocate_jobent( i );
            break;
        }
    }
}

void dump_jobtable( flag )
int flag;
{
    int i;

    for ( i = 0; i < MAXJOBS; i++ ) {
	if ( jobtable[i].active )
	    mpdprintf( flag,
	       "job[%d]: jobid=%d jobsize=%d jobsync_is_here=%d\n"
               "    alive_here_sofar=%d alive_in_job_sofar=%d added_to_job_sofar=%d\n",
               i, jobtable[i].jobid, jobtable[i].jobsize, jobtable[i].jobsync_is_here,
               jobtable[i].alive_here_sofar, jobtable[i].alive_in_job_sofar,
               jobtable[i].added_to_job_sofar );	
    }
}

void init_proctable()
{
    int i;
    for ( i = 0; i < MAXPROCS; i++ )
	proctable[i].active = 0;
}

int allocate_procent()
{
    int i, found;

    found = 0;
    for ( i = 0; i < MAXPROCS; i++ )
	if ( proctable[i].active == 0 ) {
	    found = 1;
	    break;
	}
    if (found) {
	proctable[i].active   =  1;
	proctable[i].pid      = -1;
	proctable[i].jobid    = -1;
	proctable[i].jobrank  = -1;
	proctable[i].clientfd = -1;
	proctable[i].lport    = -1;
	proctable[i].state    = CLNOTSET;
	strcpy( proctable[i].name, "none" );
	return i;
    }
    else {
	mpdprintf( 1, "unable to allocate proctable entry, MAXPROCS = %d\n", MAXPROCS );
	return -1;
    }
}

void deallocate_procent( idx )
int idx;
{
    proctable[idx].active = 0;
}

int find_proclisten( job, rank )
int job;
int rank;
{
    int i;
    for ( i = 0; i < MAXPROCS; i++ ) {
	if ( ( proctable[i].active) &&
	     ( job==proctable[i].jobid) &&
	     ( rank==proctable[i].jobrank)) {
	    if ( proctable[i].state == CLALIVE ||
	         proctable[i].state == CLRUNNING )
		return proctable[i].lport;
	    else if ( proctable[i].state == CLSTART )
		return -1;	/* peer client should ask again */
	    else
		mpdprintf( 1,
			   "find_proclisten: invalid state for job=%d rank=%d state=%d\n",
			   job, rank, proctable[i].state );
	}
    }   
    return -2;
}

int find_proclisten_pid( job, rank )
int job;
int rank;
{
    int i;
    for ( i = 0; i < MAXPROCS; i++ ) {
	if ( ( proctable[i].active) &&
	     ( job==proctable[i].jobid) &&
	     ( rank==proctable[i].jobrank)) {
	    return proctable[i].pid;
	}   
    }
    return -2;
}

void remove_from_proctable( int pid )
{
    int i;
 
    for ( i = 0; i < MAXPROCS; i++ ) {
        if ( proctable[i].active && ( proctable[i].pid == pid ) ) {
            deallocate_procent( i );
            break;
        }
    }
}

void kill_rank( int job, int rank, int signum )
{
    int  i;

    for ( i = 0; i < MAXPROCS; i++ )
	if ( ( proctable[i].active ) &&
	     (job == proctable[i].jobid ) &&
	     (rank == proctable[i].jobrank ) )
	    kill(proctable[i].pid,signum);
}

void kill_job( int jobid, int signum )
{
    int  i;

    for ( i = 0; i < MAXPROCS; i++ )
	if ( proctable[i].active  &&  jobid == proctable[i].jobid ) {
	    mpdprintf( debug, "kill_job: killing jobid=%d pid=%d\n",
		       jobid, proctable[i].pid );
	    kill( -proctable[i].pid, signum ); /* -pid means kill process group */
	}
}

void kill_allproc( int signum )
{
    int i, wait_stat;
 
    for ( i = 0; i < MAXPROCS; i++ ) {
        if ( proctable[i].active && proctable[i].pid > 0 ) {
	    mpdprintf( 1, "killing process %d at entry %d\n", proctable[i].pid, i);
	    kill( proctable[i].pid, signum );
	    waitpid( proctable[i].pid, &wait_stat, 0 );
	}
    }
}

void dump_proctable( char *identifier )
{
    int i;

    mpdprintf( 1, "proctable( %s )\n", identifier );
    for ( i = 0; i < MAXPROCS; i++ ) {
	if ( proctable[i].active == 1 )
	    mpdprintf( 1,
		       "proc[%d]: pid=%d, jid=%d, jrank=%d, jfd=%d, lport=%d, "
		       "myrport=%d name=%s, state=%s\n", i, proctable[i].pid,
		       proctable[i].jobid, proctable[i].jobrank, proctable[i].clientfd,
		       proctable[i].lport, proctable[i].myrinet_port,
		       proctable[i].name, pstate( proctable[i].state ) );
    }
}

char *pstate( int state )
{
    if ( state == CLNOTSET )
	return( "NOTSET" );
    else if ( state == CLSTART )
	return( "START" );
    else if ( state == CLALIVE )
	return( "ALIVE" );
    else if ( state == CLRUNNING )
	return( "RUNNING" );
    else if ( state == CLDEAD )
	return( "DEAD" );	      
    else
	return( "UNKNOWN" );
}

char *phandler( handler )
int handler;
{
    if ( handler == NOTSET )
	return "NOTSET";
    else if ( handler == CONSOLE_LISTEN )
	return "CONSOLE_LISTEN";
    else if ( handler == CONSOLE )
	return "CONSOLE";
    else if ( handler == PARENT )
	return "PARENT";
    else if ( handler == LHS )
	return "LHS";
    else if ( handler == RHS )
	return "RHS";
    else if ( handler == CLIENT_LISTEN )
	return "CLIENT_LISTEN";
    else if ( handler == CLIENT )
	return "CLIENT";
    else if ( handler == MPD )
	return "MPD";
    else if ( handler == LISTEN )
	return "LISTEN";
    else if ( handler == STDIN )
	return "STDIN";
    else if ( handler == CONTROL )
	return "CONTROL";
    else if ( handler == DATA )
	return "DATA";
    else if ( handler == MANAGER_LISTEN )
	return "MANAGER_LISTEN";
    else if ( handler == MANAGER )
	return "MANAGER";
    else if ( handler == MAN_LISTEN )
	return "MAN_LISTEN";
    else if ( handler == LHS_MSGS )
	return "LHS_MSGS";
    else if ( handler == RHS_MSGS )
	return "RHS_MSGS";
    else if ( handler == PARENT_MPD_MSGS )
	return "PARENT_MPD_MSGS";
    else if ( handler == CON_STDIN )
	return "CON_STDIN";
    else if ( handler == CON_CNTL )
	return "CON_CNTL";
    else if ( handler == MAN_CLIENT )
	return "MAN_CLIENT";
    else if ( handler == CLIENT_STDOUT )
	return "CLIENT_STDOUT";
    else if ( handler == CLIENT_STDERR )
	return "CLIENT_STDERR";
    else if ( handler == TREE_STDOUT )
	return "TREE_STDOUT";
    else if ( handler == TREE_STDERR )
	return "TREE_STDERR";
    else if ( handler == LOGFILE_OUTPUT )
	return "LOGFILE_OUTPUT";
    else
	return "UNKNOWN";
}

void reconstruct_message_from_keyvals( buf )
char *buf;
{
    int i;
    char tempbuf[MAXLINE];

    buf[0] = '\0';
    for (i=0; i < mpd_keyval_tab_idx; i++) {
	sprintf( tempbuf, "%s=%s ", mpd_keyval_tab[i].key, mpd_keyval_tab[i].value );
	strcat( buf, tempbuf );
    }
    buf[strlen(buf)-1] = '\0';  /* chop off trailing blank */
    strcat( buf, "\n" );
}

void error_check( int val, char *str )	
{
    extern void fatal_error( int, char * );

    if ( val < 0 ) {
	char errmsg[512];
	sprintf( errmsg, "[%s] %s: %d | strerror: %s", myid, str, val, strerror( errno ) );
	perror( errmsg );
	syslog( LOG_INFO, "terminating abnormally, %s", errmsg );
	mpd_cleanup();
	fatal_error( val, str );		
    }
}
/*
 *	Default Fatal exit handling routine 
 */
void def_fatalerror( int val, char *st )
{
    mpdprintf( debug, "error code=%d msg=%s\n",val,st);
    exit(val);
}

void (* _fatal_err)( int, char* ) = def_fatalerror;
/*
 *	invoke fatal error routine 
 */
void fatal_error( int val, char *str )
{
    (* _fatal_err)(val,str);
}
void set_fatalerr_handler( void (*func)(int,char *) ); /* had to prototype this first */
void set_fatalerr_handler( void (*func)(int,char *) )
{
    _fatal_err = func;
}

void usage( st )
char *st;
{
    fprintf( stderr, "Usage: %s  <options>\n", st );
    fprintf( stderr, "Options are:\n" );
    fprintf( stderr, "-h <host to connect to>\n" );
    fprintf( stderr, "-p <port to connect to>\n" );
    fprintf( stderr, "-f <config file>\n" );
    fprintf( stderr, "-n (don't allow console)\n" );
    fprintf( stderr, "-d <debug (0 or 1)>\n" );
    fprintf( stderr, "-w <working directory>\n" );
    fprintf( stderr, "-l <listener port>\n" );
    fprintf( stderr, "-b (background; daemonize)\n" );
    fprintf( stderr, "-e (don't let this mpd start processes, unless root)\n" );
    fprintf( stderr, "-t (echo listener port at startup)\n" );
    exit( 1 );
}

void mpd_cleanup()
{
    int i;

    if ( debug )
	dump_fdtable( "in mpd_cleanup" );
    for ( i = 0; i < MAXFDENTRIES; i++ ) {
        if ( fdtable[i].active )  {
	    mpdprintf( debug, "i=%d name=%s handler=%s\n",
	               i, fdtable[i].name, phandler(fdtable[i].handler) );
	    if ( ( fdtable[i].handler == CONSOLE_LISTEN ) )  {
                mpdprintf( debug, "unlinking  %s\n", fdtable[i].name );
                unlink( fdtable[i].name );
            }
	    else if ( ( fdtable[i].handler == LOGFILE_OUTPUT ) )  {
                mpdprintf( debug, "unlinking  %s\n", fdtable[i].name );
                unlink( fdtable[i].name );
            }
        }
    }
    /* Kill off all child processes by looping thru proctable */
    kill_allproc( SIGINT );	/* SIGKILL seems too violent */
}

double mpd_timestamp()
{
    struct timeval tv;

    gettimeofday( &tv, ( struct timezone * ) 0 );
    return ( tv.tv_sec + (tv.tv_usec / 1000000.0) );
}

int dclose( int fd )		/* version of close for debugging */
{
    int rc;
    
    mpdprintf( debug, "closing fd %d\n", fd );
    if ( ( rc = close( fd ) ) < 0 )
	mpdprintf( 1, "failed to close fd %d\n", fd );
    return rc;
}


int map_signo( char *signo )
{
    if ( strcmp( signo, "SIGTSTP" ) == 0 )
	 return SIGTSTP;
    else if ( strcmp( signo, "SIGCONT" ) == 0 )
	 return SIGCONT;
    else if ( strcmp( signo, "SIGINT" ) == 0 )
	 return SIGINT;
    else 
	return -1;
}

void unmap_signum( int signum, char *signo )
{
    if ( signum == SIGTSTP )
	strcpy( signo, "SIGTSTP" );
    else if ( signum == SIGCONT )
	strcpy( signo, "SIGCONT" );
    else if ( signum == SIGINT )
	strcpy( signo, "SIGINT" );
    else
	strcpy( signo, "UNKNOWN_SIGNUM" );
    return;
}
	    
/* The following two routines are for management of Myrinet ports.
   Just for MPICH-1, where it is needed for having the manager
   (actually, the client-before-exec) write out the Myrinet port
   file before execing the clients.  The client-before-exec will
   use put-fence-get to acquire the information to be written to the
   file.  Currently, this code hands out port numbers from 3 to 7,
   returning -1 if there are no more port numbers.
*/

static int myrinet_port_counter;
static int myrinet_valid_ports[6] = { 1, 2, 4, 5, 6, 7 };

void init_myrinet_port_counter( void )
{
    myrinet_port_counter = 0;
}

int get_next_myrinet_port( void )
{
    int port;

    if ( myrinet_port_counter > 5 )
	port = -1;
    else {
	port = myrinet_valid_ports[myrinet_port_counter];
	myrinet_port_counter++; 
    }
    return ( port );
}

/* The following collection of routines are for detailed, user-friendly error
   messages.  We will add to them incrementally.  The idea is to have detailed
   explanations for errors that users are likely to bring on themselves
   accidentally, not necessary errors that represent bugs in the system and
   require code fixes.  At least until we understand them better, we will use
   one routine per error, with its own arguments, to enable the errors to be
   context sensitive.
*/

void console_setup_failed( char * myhostname )
{

    mpdprintf( 1, "Could not set up unix socket on %s\n", myhostname );
    mpdprintf( 1, "by which the mpd is contacted.  The most likely cause\n" );
    mpdprintf( 1, "is that there is already an mpd running on %s.\n",
	       myhostname );
    mpdprintf( 1, "If you want to start a second mpd in the same ring with\n" );
    mpdprintf( 1, "the first, use the -n option when starting the second\n" );
    mpdprintf( 1, "and subsequent mpd's.  If the already-running mpd is an\n" );
    mpdprintf( 1, "old one and you wish to start a new one in a new ring,\n" );
    mpdprintf( 1, "kill the old ring (with mpdallexit) and then start the new\n" );
    mpdprintf( 1, "mpd.  It may be that there is no mpd running but a former\n" );
    mpdprintf( 1, "mpd left a bad state.  Run mpdcleanup to clean it up.\n" );
}

#ifdef FOO
/* see FOO comments at top of file */
int read_line_with_timeout( int fd, char *buf, int maxlen, int seconds, int useconds)
{
    int rc;

    setup_timeout( seconds, useconds );
    rc = read_line( fd, buf, maxlen );
    if ( check_timeout( ) )
	mpdprintf( 1, "read_line_with_time_out: timed out\n" );
    return( rc );
}
#endif

/* This function reads until it finds a newline character.  It returns the number of
   characters read, including the newline character.  The newline character is stored
   in buf, as in fgets.  It does not supply a string-terminating null character.
*/
int read_line( int fd, char *buf, int maxlen )
{
    int n, rc;
    char c, *ptr;

    ptr = buf;
    for ( n = 1; n < maxlen; n++ ) {
      again:
	rc = read( fd, &c, 1 );
	/* mpdprintf( 111, "read_line rc=%d c=:%c:\n",rc,c ); */
	if ( rc == 1 ) {
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

int write_line( int idx, char *buf )	
{
    int size, n;

    size = strlen( buf );

    if ( size > MAXLINE ) {
	buf[MAXLINE-1] = '\0';
	mpdprintf( 1, "write_line: message string too big: :%s:\n", buf );
    }
    else if ( buf[size-1] != '\n' )  /* error:  no newline at end */
	mpdprintf( 1, "write_line: message string doesn't end in newline: :%s:\n", buf );
    else {
        if ( idx != -1 ) {
	    n = write( fdtable[idx].fd, buf, size );
	    if ( n < 0 ) {
	        mpdprintf( 1, "write_line error; fd=%d buf=:%s:\n", fdtable[idx].fd, buf );
		perror("system msg for write_line failure ");
	        return(-1);
	    }
	    if ( n < size)
	        mpdprintf( 1, "write_line failed to write entire message\n" );
        }
        else
	    mpdprintf( debug, "write_line attempted write to idx -1\n" );
    }
    return 0;
}

void mpdprintf( int print_flag, char *fmt, ... )
{
    va_list ap;

    if (print_flag) {
	fprintf( stderr, "[%s]: ", myid );
	va_start( ap, fmt );
	vfprintf( stderr, fmt, ap );
	va_end( ap );
	fflush( stderr );
    }
}

#ifdef FOO
/* commentd out temporarily; see comments for FOO at top of file also */
/* Note that signal.h is also needed for killpg */
#include <signal.h>

static int sigalrm_flag = 0;
static struct sigaction nact, oact;

static void sigalrm_handler(int signo)
{
    sigalrm_flag = 1;
}

void setup_timeout(int seconds, int microseconds)
{
    struct itimerval timelimit;
    struct timeval tval, tzero;

    /* signal(SIGALRM,sigalrm_handler); */
    nact.sa_handler = sigalrm_handler;
    sigemptyset(&nact.sa_mask);
    /* nact.sa_flags = SA_INTERRUPT; */
    nact.sa_flags &= !(SA_RESTART);    /* turning it OFF */
    if (sigaction(SIGALRM, &nact, &oact) < 0)
    {
	printf("mpd: setup_timeout: sigaction failed\n");
	return;
    }
    tzero.tv_sec	  = 0;
    tzero.tv_usec	  = 0;
    timelimit.it_interval = tzero;       /* Only one alarm */
    tval.tv_sec		  = seconds;
    tval.tv_usec	  = microseconds;
    timelimit.it_value	  = tval;
    setitimer(ITIMER_REAL,&timelimit,0);
}

int check_timeout( void )
{
    struct itimerval timelimit;
    struct timeval tzero;

    if (sigalrm_flag)
    {
        tzero.tv_sec	   = 0;
        tzero.tv_usec	   = 0;
        timelimit.it_value = tzero;   /* Turn off timer */
        setitimer( ITIMER_REAL, &timelimit, 0 );
        /* signal(SIGALRM,SIG_DFL); */
	if (sigaction( SIGALRM, &oact, NULL ) < 0)
        {
	    printf( "mpd: check_timeout: sigaction failed\n" );
	}
        sigalrm_flag = 0;
	return(1);
    }
    return(0);
}
#endif

void strcompress( char *i )
{
    char *initial = i, f[4096], *final;

    mpdprintf( 0, "strcompress: compressing :%s:\n", i );
    final = f;
    while ( *initial ) {
        while ( ( *initial != ' ' ) && *initial ) {
            *final++ = *initial++;
        }
        *final++ = *initial++;
	while ( ( *initial == ' ') && ( *initial++ ) )
	    ;
    }
    *final = '\0';
    strcpy( i, f );
    mpdprintf( 0, "strcompress: returning :%s:\n", i );
}

void datastr_to_xml( char *inbuf, char *src, char *xmlbuf )
{
    char subbuf1[80], subbuf2[80], subbuf3[80], tbuf1[80], buf[MAXLINE];
    char *data[3], *temp, *temp2;
    int i;

    i = 0;
    strcpy( buf, inbuf );
    sprintf( tbuf1, "<node name='%s'>", src );
    strcpy( xmlbuf, tbuf1 );
    data[0] = strtok( buf, "," );
    data[1] = strtok( NULL, "," );
    data[2] = strtok( NULL, "," );
    while ( data[i] && ( i < 3 ) )
    {
	if ( strstr( data[i], "loadavg" ) ) {
	    mpdprintf( debug, "entering loadavg subsection, data[%i] is %s\n", i, data[i] );
	    strcat( xmlbuf, "<loadavg>" );
	    temp = strstr( data[i], "loadavg:" ) + 8;
	    strcat( xmlbuf, temp );
	    strcat( xmlbuf, "</loadavg>" );
	}
	if ( strstr( data[i], "memusage" ) ) {
	    mpdprintf( debug, "entering memusage subsection, data[%i] is %s\n", i, data[i] );
	    strcat( xmlbuf, "<memusage>" );
	    temp = strstr( data[i], "memusage:" ) + 9;
	    temp2 = strtok( temp, "\n" );
	    while ( temp2 ) {
		sscanf( temp2, "%s %s", subbuf1, subbuf2 );
		subbuf1[strlen( subbuf1 ) - 1] = '\0';
		sprintf( subbuf3, "<%s>%s</%s>", subbuf1, subbuf2, subbuf1 );
		strcat( xmlbuf, subbuf3 );
		temp2 = strtok( NULL, "\n" );
	    }
	    strcat( xmlbuf, "</memusage>" );
	}
	if ( strstr( data[i], "myrinfo" ) ) {
	    mpdprintf( debug, "entering myrinfo subsection, data[%i] is %s\n", i, data[i] );
	    strcat( xmlbuf, "<myrinfo>" );
	    temp = strstr( data[i], "myrinfo:" ) + 8;
	    temp2 = strtok( temp, "\n" );
	    mpdprintf( debug, "in myrinfo temp2 after strtokking is %s\n", temp2 );
	    while ( temp2 ) {
		sscanf( temp2, "%s %s", subbuf1, subbuf2 );
		sprintf( subbuf3, "<%s>%s</%s>", subbuf1, subbuf2, subbuf1 );
		strcat( xmlbuf, subbuf3 );
		temp2 = strtok( NULL, "\n" );
	    }
	    strcat( xmlbuf, "</myrinfo>" );
	}
	i++;
    }
    strcat( xmlbuf, "</node>\n" );
}
