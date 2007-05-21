/*
 *	Process console command from user 
 */ 
#include "mpd.h"

extern struct fdentry fdtable[MAXFDENTRIES];
extern struct procentry proctable[MAXPROCS];
extern char   mydir[MAXLINE];
extern int    listener_idx;
extern int    console_idx;
extern int    console_listen_idx;
extern int    rhs_idx;
extern int    done;     		/* global done flag */
extern char   mynickname[MAXHOSTNMLEN];
extern char   nexthost[MAXHOSTNMLEN];   /* name of next host */ 
extern int    nextport;                 /* port that next host is listening on */
extern char   rhshost[MAXHOSTNMLEN];
extern int    rhsport;              
extern char   prevhost[MAXHOSTNMLEN];   /* name of prev host */
extern int    prevport;                 /* port that prev host is listening on */
extern char   myid[IDSIZE];
extern int    debug;
extern int    allexiting;
extern int    no_execute;

/*
 *	Execute command at multiple nodes, using manager process
 */
void con_mpexec( )
{
    char mpexecbuf[MAXLINE];
    char program[256];
    char buf[MAXLINE], eqbuf[MAXLINE], locid[8], argid[8], envid[8];
    char console_hostname[MAXHOSTNMLEN];
    char username[MAXLINE];
    int  console_portnum, iotree;
    int  numprocs, jid, gdb, tvdebug, line_labels, shmemgrpsize, myrinet_job;
    int  whole_lines;
    int  i, locc, envc, argc, n;
    int  first_at_console;
    char requested_jobid[10], requested_userid[10];
    char co_program[MAXLINE], mship_host[80], mship_port[80];

    mpd_getval( "hostname", console_hostname );
    mpd_getval( "portnum", buf );
    console_portnum = atoi( buf );
    mpd_getval( "iotree", buf );
    iotree = atoi( buf );
    mpd_getval( "gdb", buf );
    gdb = atoi( buf );
    mpd_getval( "tvdebug", buf );
    tvdebug = atoi( buf );
    mpd_getval( "line_labels", buf );
    line_labels = atoi( buf );
    mpd_getval( "whole_lines", buf );
    whole_lines = atoi( buf );
    mpd_getval( "myrinet_job", buf );
    myrinet_job = atoi( buf );
    mpd_getval( "first_at_console", buf );
    first_at_console = atoi( buf );
    mpd_getval( "numprocs", buf );
    numprocs = atoi( buf );
    mpd_getval( "shmemgrpsize", buf );
    shmemgrpsize = atoi( buf );
    mpd_getval( "executable", program );
    mpd_getval( "username", username );
    mpd_getval( "requested_jobid", requested_jobid );
    mpd_getval( "requested_userid", requested_userid );
    mpd_getval( "copgm", co_program );
    mpd_getval( "mship_host", mship_host );
    mpd_getval( "mship_port", mship_port );

    n = sscanf( requested_jobid, "%d", &jid ); /* look for optional request for jid */
    if ( n != 1 )
	jid = allocate_jobid();

    mpdprintf( debug, "con_mpexec: new job id  = %d\n", jid );

    /* optionally overwrite user with requested_userid:  */

#if defined(ROOT_ENABLED)
    if ( ( strcmp( username, "root" ) == 0 ) && ( requested_userid[0] ) ) {
	mpdprintf( debug, "replacing username %s by requested userid %s\n", username,
		   requested_userid );
	strcpy( username, requested_userid );
    }
#endif

    /* hopcount is for checking that an mpexec message has gone around the ring without
       any processes getting started, which indicates a bad machine name in MPDLOC */
    sprintf(mpexecbuf,
	    "cmd=mpexec conhost=%s conport=%d rank=0 src=%s "
	    "iotree=%d dest=anyone job=%d jobsize=%d prog=%s hopcount=0 gdb=%d "
	    "tvdebug=%d line_labels=%d whole_lines=%d "
            "copgm=%s mship_host=%s mship_port=%s "
	    "shmemgrpsize=%d username=%s myrinet_job=%d ",
	    console_hostname, console_portnum, myid, iotree, jid, numprocs, program,
	    gdb, tvdebug, line_labels, whole_lines, 
	    co_program, mship_host, mship_port,
	    shmemgrpsize, username, myrinet_job );

    /* now add other arguments, which are already in key=val form */
    if ( mpd_getval( "locc", buf ) )
	locc = atoi( buf );
    else
	locc = 0;
    sprintf( eqbuf, "locc=%s ", buf );
    strcat( mpexecbuf, eqbuf );
    for ( i = 1; i <= locc; i++ ) {
	sprintf( locid, "loc%d", i );
	mpd_getval( locid, buf );
	sprintf( eqbuf, "loc%d=%s ", i, buf );
	strcat( mpexecbuf, eqbuf );
    }

    if ( mpd_getval( "argc", buf ) )
	argc = atoi( buf );
    else
	argc = 0;
    sprintf( eqbuf, "argc=%s ", buf );
    strcat( mpexecbuf, eqbuf );
    for ( i=1; i <= argc; i++ ) {
	sprintf( argid, "arg%d", i );
	mpd_getval( argid, buf );
	sprintf( eqbuf, "arg%d=%s ", i, buf );
	strcat( mpexecbuf, eqbuf );
    }

    if ( mpd_getval( "envc", buf ) )
	envc = atoi( buf );
    else
	envc = 0;
    sprintf( eqbuf, "envc=%s ", buf );
    strcat( mpexecbuf, eqbuf );
    for ( i=1; i <= envc; i++ ) {
	sprintf( envid, "env%d", i );
	mpd_getval( envid, buf );
	sprintf( eqbuf, "env%d=%s ", i, buf );
	strcat( mpexecbuf, eqbuf );
    }

    mpexecbuf[strlen(mpexecbuf)-1] = '\n';
    mpdprintf( debug, "con_mpexec sending :%s:\n", mpexecbuf );

    if ( first_at_console && !no_execute )
    {
        mpd_parse_keyvals( mpexecbuf );  /* parse my own msg */
        sib_mpexec( );
    }
    else
    {
        write_line( rhs_idx, mpexecbuf );
    }
}

void con_killjob( )
{
    char buf[MAXLINE];
    int  jobid;

    jobid = atoi( mpd_getval( "jobid", buf) );
    sprintf( buf, "src=%s bcast=true cmd=killjob jobid=%d\n", myid, jobid );
    write_line( rhs_idx, buf );
    mpdprintf( debug, "con_killjob: sending killjob jobid=%d\n", jobid );
}

void con_exit( )
{
    char buf[MAXLINE], mpd_id[IDSIZE];

    mpd_getval( "mpd_id", mpd_id );
    if ( strcmp( mpd_id, "self" ) == 0 )
	strncpy( mpd_id, myid, IDSIZE );
    sprintf( buf, "src=%s dest=%s cmd=exit\n", myid, mpd_id );
    write_line( rhs_idx, buf );
}

void con_allexit( )
{
    char buf[MAXLINE];

    allexiting = 1;
    sprintf( buf, "src=%s bcast=true cmd=allexit\n", myid );
    write_line( rhs_idx, buf );
}

void con_shutdown( )
{
    char buf[MAXLINE], mpd_id[IDSIZE];

    mpd_getval( "mpd_id", mpd_id );
    sprintf( buf, "src=%s dest=%s cmd=shutdown\n", myid, mpd_id );
    write_line( rhs_idx, buf );
}

/* RMB: con_addmpd is woefully out of date */
void con_addmpd(command)
char *command;
{
    char *c;
    char rhostname[MAXHOSTNMLEN];
    char mpd_cmd[MAXLINE];
    char rsh_cmd[MAXLINE];

    strcpy( rsh_cmd, "rsh" );		  /* set remote shell command */
    c = strtok( command, "\n "); /* Throw command out */
    c = strtok( NULL, "\n ");
    if ( !c ) {
	mpdprintf( debug, "did not get expected hostname in addmpd command\n" );
	return;
    }
    strcpy( rhostname, c );
    sprintf( mpd_cmd, "%s/mpd", mydir );
    /* rsh another mpd onto specified host */
    {
	int rc;
	char l_port[6];
	sprintf( l_port, "%d", fdtable[listener_idx].portnum );
	rc = fork();	/* fork the rsh, which will run mpd remotely */
	if (rc == 0) {
	    rc = execlp( rsh_cmd, rsh_cmd, rhostname, "-n",
			 mpd_cmd,
			 "-h", mynickname,
			 "-p", l_port,
			 "-w", mydir,
			 NULL );
	    error_check( rc, "mpd: execlp failed" );
	}
	else {
	    mpdprintf( 0, "creating remote mpd on %s\n",rhostname);
	}
    }
}

void con_debug()
{
    char buf[MAXLINE], dest[MAXLINE], src[MAXLINE];
    int  flag;
	
    mpd_getval( "dest", dest );
    flag = atoi( mpd_getval( "flag", buf ) );
    if ( strcmp( dest, myid ) == 0 )  {
        debug = flag;
    }
    else  {
        mpd_getval( "src", src );
        sprintf( buf, "src=%s dest=%s cmd=debug flag=%d\n", myid, dest, flag );
        write_line( rhs_idx, buf );
    }
}

void con_ringtest( )
{
    char ringtestbuf[MAXLINE];
    int  count;
    char buf[MAXLINE];
    double timestamp;

    mpd_getval( "laps", buf );

    if ( buf[0] == '\0' ) {
	sprintf( buf, "must specify count for ringtest\n" );
	write_line( console_idx, buf );
	return;
    }
    count = atoi( buf );
    if ( count > 0 ) {
	/* send message around ring to self */
	timestamp = mpd_timestamp();
	sprintf( ringtestbuf, "src=%s dest=%s cmd=ringtest count=%d starttime=%f\n",
		 myid, myid, count, timestamp );
	write_line( rhs_idx, ringtestbuf );
    }
}

void con_ringsize( )
{
    char buf[MAXLINE], tmpbuf[MAXLINE];

    sprintf( buf, "src=%s dest=anyone cmd=ringsize count=0 execonly=%s\n", 
             myid, mpd_getval( "execonly", tmpbuf ) );
    mpdprintf(debug, "con_ringsize sending to %s_%d\n", rhshost, rhsport);
    write_line( rhs_idx, buf );
}

void con_clean( )
{
    char buf[MAXLINE];
	
    /* send message to next mpd in ring; it will be forwarded all the way around */
    sprintf( buf, "src=%s bcast=true cmd=clean\n", myid );
    write_line( rhs_idx, buf );
}

void con_trace( )
{
    char tracebuf[MAXLINE], tmpbuf[MAXLINE];
	
    /* send message to next mpd in ring; it will be forwarded all the way around */
    sprintf( tracebuf, "src=%s bcast=true cmd=trace execonly=%s\n",
             myid, mpd_getval( "execonly", tmpbuf ) );
    write_line( rhs_idx, tracebuf );
    sprintf( tracebuf, "src=%s bcast=true cmd=trace_trailer\n", myid );
    write_line( rhs_idx, tracebuf );
}

void con_listjobs( )
{
    char listbuf[MAXLINE];
	
    /* send message to next mpd in ring; it will be forwarded all the way around */
    sprintf( listbuf, "con_mpd_id=%s dest=anyone cmd=listjobs\n", myid );
    write_line( rhs_idx, listbuf );
}

void con_dump( )
{
    char dumpbuf[MAXLINE], what[80];

    mpd_getval( "what", what );
    mpdprintf( debug, "conproc sending dump message to rhs, src=%s, what=%s\n",
	       myid, what );
    sprintf( dumpbuf, "src=%s dest=anyone cmd=dump what=%s\n", myid, what );
    write_line( rhs_idx, dumpbuf );
}

void con_mandump( )
{
    char dumpbuf[MAXLINE], buf[MAXLINE], what[80];
    int  jobid, manrank;

    mpd_getval( "jobid", buf );
    jobid = atoi( buf );
    mpd_getval( "rank", buf );
    manrank = atoi( buf );
    mpd_getval( "what", what );
    mpdprintf( debug,
	       "conproc sending mandump message to rhs, src=%s, "
               "jobid=%d manrank=%d what=%s\n",
	       myid, jobid, manrank, what );
    sprintf( dumpbuf,
	     "src=%s dest=anyone cmd=mandump jobid=%d manrank=%d what=%s\n",
	     myid, jobid, manrank, what );
    write_line( rhs_idx, dumpbuf );
}

void con_ping( )
{
    char buf[MAXLINE];
    char *pingee_id;

    pingee_id = mpd_getval( "pingee", buf );
    if ( !pingee_id ) {
	mpdprintf( debug, "did not get expected id to ping\n" );
	return;
    }
    sprintf( buf, "src=%s dest=%s cmd=ping\n", myid, pingee_id );
    write_line( rhs_idx, buf );
}

/* cmd to cause an mpd to "fail" for testing */
void con_bomb( )
{
    char buf[MAXLINE], mpd_id[IDSIZE];

    mpd_getval( "mpd_id", mpd_id );
    sprintf( buf, "src=%s dest=%s cmd=bomb\n", myid, mpd_id );
    write_line( rhs_idx, buf );
}

void con_signaljob( )
{
    char c_signum[32];
    char buf[MAXLINE];
    int  jobid;

    jobid = atoi( mpd_getval( "jobid", buf) );
    strcpy( c_signum, mpd_getval( "signum", buf ) );
    sprintf( buf, "src=%s bcast=true cmd=signaljob jobid=%d signum=%s\n",
             myid, jobid, c_signum );
    write_line( rhs_idx, buf );
    mpdprintf( debug, "con_signaljob: signaling jobid=%d c_signum=%s\n",
               jobid, c_signum );
}

