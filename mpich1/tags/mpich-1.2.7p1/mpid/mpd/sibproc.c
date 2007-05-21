/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpd.h"
#if defined(ROOT_ENABLED)
#if !defined(__USE_BSD)
#define __USE_BSD
#endif
#include <grp.h>
#endif

extern struct fdentry fdtable[MAXFDENTRIES];
extern struct procentry proctable[MAXPROCS];
extern struct jobentry jobtable[MAXJOBS];
extern int    amfirst;
extern int    console_idx;
extern int    rhs_idx;
extern int    lhs_idx;
extern int    mon_idx;
extern int    prev_sibling_idx;
extern int    listener_idx;
extern int    done;                     /* global done flag */
extern char   mynickname[MAXHOSTNMLEN];
extern char   myhostname[MAXHOSTNMLEN];
extern int    my_listener_port;
extern char   rhshost[MAXHOSTNMLEN];
extern int    rhsport;
extern char   rhs2host[MAXHOSTNMLEN];
extern int    rhs2port;
extern char   myid[IDSIZE];
extern char   lhshost[MAXHOSTNMLEN];
extern int    lhsport;
extern int    debug;
extern int    allexiting;
extern struct keyval_pairs keyval_tab[64];
extern int    keyval_tab_idx;
extern char   mpd_passwd[PASSWDLEN];
extern int    no_execute;
extern int    shutting_down;
extern int    generation;

/* Forward reference for an internal routine */
static const char *mpdGetManager( const char [], const char [], 
				  const char [], const char [] );

void sib_reconnect_rhs( int idx ) 
{
    int  newport;
    char buf[MAXLINE], new_rhs[MAXHOSTNMLEN], parse_buf[MAXLINE], cmd[MAXLINE];

    mpd_getval("rhshost",new_rhs);
    mpd_getval("rhsport",buf);
    newport = atoi( buf );
    mpdprintf( debug, "got cmd=reconnect_rhs host=%s port=%d\n",new_rhs,newport);
    strcpy(rhshost,new_rhs);
    rhsport = newport;
    mpd_getval("rhs2host",buf);
    strcpy( rhs2host, buf );
    mpd_getval("rhs2port",buf);
    rhs2port = atoi(buf);

    if ( rhs_idx == -1 )
	rhs_idx = allocate_fdentry();
    else
        dclose( fdtable[rhs_idx].fd );
    fdtable[rhs_idx].fd = network_connect(new_rhs,newport);
    fdtable[rhs_idx].active = 1;  /* in case a new one */
    fdtable[rhs_idx].read = 1;
    fdtable[rhs_idx].write   = 0;
    fdtable[rhs_idx].handler = RHS;
    fdtable[rhs_idx].portnum = newport;
    strcpy( fdtable[rhs_idx].name, new_rhs );
    sprintf( buf, "src=%s dest=%s_%d cmd=new_lhs_req host=%s port=%d\n",
             myid, rhshost, rhsport, mynickname, my_listener_port );
    write_line( rhs_idx, buf );
    recv_msg( fdtable[rhs_idx].fd, buf, MAXLINE );
    strcpy( parse_buf, buf );
    mpd_parse_keyvals( parse_buf );
    mpd_getval( "cmd", cmd );
    if ( strcmp( cmd, "challenge" ) != 0 ) {
	mpdprintf( 1, "reconnect_rhs: expecting challenge, got %s\n", buf );
	exit( -1 );
    }
    newconn_challenge( rhs_idx );
    /* Now that we have an rhs, we can initialize the jobid pool, which might require
       sending messages.
    */
    init_jobids();		/* protected from executing twice */
}

void sib_rhs2info( int idx )
{
    char buf[10];

    mpd_getval("rhs2host", rhs2host );
    rhs2port = atoi( mpd_getval( "rhs2port", buf ) );
}

void sib_killjob( void )
{ 
    int  jobid;
    char buf[MAXLINE];

    mpd_getval( "jobid", buf );
    mpdprintf( debug,"sib_killjob: killing jobid=%s\n",buf);
    jobid = atoi( buf );
    kill_job( jobid, SIGKILL );
}

void sib_signaljob( void )
{ 
    int  pidx, jobid;
    char c_signum[32], buf[MAXLINE];

    mpd_getval( "jobid", buf );
    jobid = atoi( buf );
    mpd_getval( "signum", c_signum);
    for ( pidx=0; pidx < MAXPROCS; pidx++ )
    {
	if ( proctable[pidx].active  &&  proctable[pidx].jobid == jobid )  {
            sprintf( buf, "cmd=signaljob signo=%s\n", c_signum );
	    write( proctable[pidx].clientfd, buf, strlen(buf) );
	}
    }
}

void sib_bomb( void )
{
    mpdprintf( debug, "%s bombing\n", myid );
    exit(1);  /* not graceful; mimic machine dying etc. */
}

void sib_exit( void )
{
    done = 1;
}

void sib_allexit( void )
{
    allexiting = 1;
    done       = 1;
}

void sib_shutdown( void )
{
    char buf[MAXLINE];
    char toid[IDSIZE];

    if ( strcmp( mynickname, lhshost ) == 0  &&  my_listener_port == lhsport )
    {
        done = 1;
	return;
    }
    shutting_down = 1;
    sprintf( toid, "%s_%d", lhshost, lhsport );
    mpdprintf( debug, "sib_shutdown sending req to lhs\n" );
    sprintf( buf, "cmd=req_perm_to_shutdown dest=%s src=%s\n", toid, myid );
    write_line( rhs_idx, buf );
}

void sib_req_perm_to_shutdown( void )
{
    char buf[MAXLINE];

    mpdprintf( debug, "sib_req_perm_to_shutdown: enter \n" );
    if ( ! shutting_down )
    {
	mpdprintf( debug, "sending perm_to_shutdown\n" );
	sprintf( buf, "src=%s dest=%s_%d cmd=perm_to_shutdown\n",
		 myid, rhshost, rhsport );
	write_line( rhs_idx, buf );
	reknit_ring( rhs_idx );
    }
}

void sib_perm_to_shutdown( void )
{
    mpdprintf( debug, "sib_perm_to_shutdown: setting done = 1\n" );
    done = 1;
}

void sib_debug( void )
{
    char buf[MAXLINE];

    mpd_getval( "flag", buf );
    debug = atoi( buf );
    mpdprintf( debug, "[%s] debugging set to %d\n", myid, debug );
}

void sib_mpexec( void )		/* designed to work with process managers */
{
    char buf[MAXLINE], fwdbuf[MAXLINE], 
	 temparg[MAXLINE], argid[MAXLINE],
	 tempenv[MAXLINE], envid[MAXLINE],
	 temploc[MAXLINE], locid[MAXLINE];
    char src[MAXLINE], program[MAXLINE], username[80];
    int  pid, i, saved_i, j, rc, argc, envc, locc, gdb, tvdebug;
    int  line_labels, whole_lines, shmemgrpsize;
    char *argv[25];
    char *env[50];
    int  jobrank, jobid, jobsize, jidx, cid, myrinet_job;
    char conhost[MAXHOSTNMLEN];
    int  conport;
    int  man_mpd_socket[2];
    int  man_idx;
    char env_mpd_fd[80], env_job[80], env_rank[80], env_size[80];
    char env_man_pgm[256], env_man_listener_fd[80], env_man_prevhost[MAXHOSTNMLEN];
    char env_man_prevport[80], env_man_host0[MAXHOSTNMLEN], env_man_port0[80];
    char env_man_conport[80], env_man_conhost[MAXHOSTNMLEN], env_man_debug[80];
    char env_man_prebuildprinttree[80], env_gdb[80], env_tvdebug[80];
    char env_line_labels[80], env_whole_lines[80];
    char env_co_program[80], env_mship_host[80], env_mship_port[80];
    char env_myrinet_port[80], env_version[80];
    char env_shmemkey[80], env_shmemgrpsize[80], env_shmemgrprank[80];
    int  man_listener_fd, last_man_listener_fd, man_listener_port, last_man_listener_port;
    int  first_man_listener_port = -1, first_man_listener_fd;
    char host0[MAXHOSTNMLEN], prevhost[MAXHOSTNMLEN];
    int  port0, prevport;
    char host0_next_mpd[MAXHOSTNMLEN];
    int  port0_next_mpd;
    int  hopcount, iotree, do_mpexec_here;
    char co_program[80], mship_host[80];
    int  mship_port;
    const char *manager_pathname;
#if defined(ROOT_ENABLED)
    struct passwd *pwent;
#endif

    mpdprintf( debug, "sib_mpexec: entering\n");
    
    mpd_getval( "job", buf );
    jobid = atoi( buf );
    mpd_getval( "jobsize", buf );
    jobsize = atoi( buf );
    mpd_getval( "prog", program );
    mpd_getval( "rank", buf );
    jobrank = atoi( buf );
    mpd_getval( "conhost", conhost ); 
    mpd_getval( "conport", buf );
    conport = atoi( buf );
    mpd_getval( "src", src );
    mpd_getval( "hopcount", buf );
    hopcount = atoi( buf );
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
    mpd_getval( "shmemgrpsize", buf );
    shmemgrpsize = atoi( buf );
    mpd_getval( "username", username );
    mpd_getval( "myrinet_job", buf );
    myrinet_job = atoi( buf );
    mpd_getval( "copgm", co_program );
    mpd_getval( "mship_host", mship_host );
    mpd_getval( "mship_port", buf );
    mship_port = atoi( buf );

    if ( jobrank >= jobsize ) {
	mpdprintf( debug, "mpexec jobstarted, jobrank=%d, jobsize=%d\n",
		   jobrank, jobsize );
	sprintf( buf, "dest=%s cmd=jobstarted jobid=%d status=started\n", src, jobid ); 
	write_line( rhs_idx, buf );
	return;			/* all processes already forked upstream */
    }

    do_mpexec_here = 0;

    if ( mpd_getval( "locc", buf ) )
	locc = atoi( buf );
    else
	locc = 0;

    if ( locc == 0 )
	do_mpexec_here = 1;
    else {
	for (i=1; i <= locc; i++) {
	    sprintf(locid,"loc%d",i);
	    mpd_getval(locid,buf);
	    mpd_destuff_arg(buf,temploc);
	    if ( my_hostname_is_in_pattern( temploc ) )  {
		do_mpexec_here = 1;
		break;
	    }
	}
    }

    mpd_getval( "username", buf );	/* get userid of user who started job */
    if ( no_execute && strcmp( buf, "root" ) != 0 )   /* don't run job here if mpd started
							 with -e, except for root */
	do_mpexec_here = 0;

    /* This is to stop an infinite loop when the user has specified only invalid
       machines in -MPDLOC- */
    if ( ( hopcount > 1 ) &&
	 ( jobrank == 0 ) &&
	 ( strcmp( src, myid ) == 0 ) &&
	 ( !do_mpexec_here ) ) 
    {
	mpdprintf( 1, 
		   "did not start any processes for job %d; \n"
		   "    user may have specified invalid machine names\n",
		   jobid );
	/* notify console */
	sprintf( buf, "cmd=jobinfo jobid=%d status=failed\n",jobid );
	write_line( console_idx, buf );
	dclose( fdtable[console_idx].fd ); /* without this we get "Broken Pipe" */
	deallocate_fdentry( console_idx );
	console_idx = -1;
	return;
    } 

    if ( ! do_mpexec_here ) {
	sprintf( buf, "%d", hopcount + 1 );
	mpd_chgval( "hopcount", buf );
	reconstruct_message_from_keyvals( fwdbuf );
	mpdprintf( debug, "fwding mpexec cmd instead of execing it; fwdbuf=%s\n", fwdbuf );
	write_line( rhs_idx, fwdbuf );
	return;
    }

    mpdprintf( debug, "executing mpexec here\n" );

#if defined(ROOT_ENABLED)
    if ((pwent = getpwnam( username )) == NULL)
    {
	mpdprintf( 1, "mpd: getpwnam failed" );
	exit( -1 );
    }
#endif

    /* First acquire a socket to be used by the last manager to be forked *here*, to
       send to the next mpd. 
       This will be the general-purpose listener port for the manager.  It is acquired now
       so that the manager will have it ready before the next manager to the right, on
       the next mpd, if there is one, attempts to connect on it.
       */

    last_man_listener_port = 0;
    last_man_listener_fd = setup_network_socket( &last_man_listener_port );
    if ( shmemgrpsize > 1 ) {
	first_man_listener_port = 0;
	first_man_listener_fd = setup_network_socket( &first_man_listener_port );
    }
    else {
	first_man_listener_fd = last_man_listener_fd;
	first_man_listener_port = last_man_listener_port;
    }

    mpdprintf( debug, "last_man_fd=%d, last_man_listener_port=%d, "
	       "first_man_fd=%d, first_man_listener_port=%d\n",
	       last_man_listener_fd, last_man_listener_port,
	       first_man_listener_fd, first_man_listener_port); 

    /* For rank 0, the incoming mpexec command formulated by conproc does not have
       (host0, port0) (since it doesn't know), or (prevhost,prevport) (since they
       don't exist). */

    if ( jobrank == 0 ) {	/* I am the mpd that is starting the first manager */
	strcpy( host0_next_mpd, myhostname );
	if ( shmemgrpsize == 1 )
	    port0_next_mpd = last_man_listener_port;
	else
	    port0_next_mpd = first_man_listener_port;
    }
    else {			
        strcpy( host0_next_mpd, mpd_getval( "host0", buf ) );
	port0_next_mpd = atoi( mpd_getval( "port0", buf ) );
    }

    mpdprintf( debug, "before sending:  port0_next_mpd=%d, prevport=%d\n",
	       port0_next_mpd, last_man_listener_port );

    sprintf(fwdbuf,
	    "cmd=mpexec conhost=%s conport=%d host0=%s port0=%d prevhost=%s prevport=%d "
	    "iotree=%d rank=%d src=%s dest=anyone job=%d jobsize=%d prog=%s hopcount=%d "
	    "gdb=%d tvdebug=%d line_labels=%d whole_lines=%d "
            "copgm=%s mship_host=%s mship_port=%d "
	    "shmemgrpsize=%d username=%s myrinet_job=%d ",
	    conhost, conport, host0_next_mpd, port0_next_mpd, myhostname,
	    last_man_listener_port, iotree, jobrank + shmemgrpsize, src, jobid, jobsize,
	    program, hopcount + 1, gdb, tvdebug, line_labels, whole_lines,
	    co_program, mship_host, mship_port,
	    shmemgrpsize, username, myrinet_job );
    /* no newline in above buffer because we are not finished adding things to it */

    /* set up locations for fwded message; locc already parsed above */

    if (locc > 0) {
	sprintf( buf, " locc=%d", locc );
	strcat( fwdbuf, buf);
	for ( i=1; i <= locc; i++ ) {
	    sprintf( locid, "loc%d", i );
	    mpd_getval( locid, temploc);
	    sprintf(buf," loc%d=%s", i, temploc );
	    strcat( fwdbuf, buf );
	}
    }
    
    /* Find the manager to use */
    manager_pathname = mpdGetManager( MANAGER_PATH, MANAGER_NAME, 
				      MANAGER_ENVPATH, MANAGER_ENVNAME );

    if (!manager_pathname) {
      mpdprintf( 1, "Could not find mpd manger; aborting\n" );
      exit(1);
    }
    argv[0] = (char *)manager_pathname;
    env[0]  = NULL;       /* in case there are no env strings */

    mpd_getval("argc",buf);
    argc = atoi(buf);
    if (argc > 0) {
	strcat(fwdbuf," argc=");
	strcat(fwdbuf,buf);
    }
    for (i=1; i <= argc; i++) {
	sprintf(argid,"arg%d",i);
	mpd_getval(argid,temparg);
	sprintf(buf," arg%d=%s",i,temparg);
	strcat(fwdbuf,buf);
        argv[i] = (char *) malloc(MAXLINE);
	mpd_destuff_arg(temparg,argv[i]);
    }
    argv[i] = NULL;

    mpd_getval( "envc", buf );
    envc = atoi( buf );
    if (envc > 0) {
	strcat(fwdbuf," envc=");
	strcat(fwdbuf,buf);
    }
    for (i=0; i < envc; i++) {
	sprintf(envid,"env%d",i+1);
	mpd_getval(envid,tempenv);
	sprintf(buf," env%d=%s",i+1,tempenv);
	strcat(fwdbuf,buf);
	env[i] = (char *) malloc(MAXLINE);
	mpd_destuff_arg(tempenv,env[i]);
    }
    /* set   env[i] = NULL;    below */
    saved_i = i;

    strcat( fwdbuf, "\n" );
    mpdprintf( debug, "sib_mpexec: sending to rhs: :%s:\n", fwdbuf );
    write_line( rhs_idx, fwdbuf );

    /* We have now forwarded the appropriate mpexec command to the next mpd, so we
       now proceed to create shmemgrpsize number of managers at this mpd. */ 

    for ( j = 0; j < shmemgrpsize && jobrank < jobsize; j++ ) {

	i = saved_i;
	if ( ( jidx = find_jobid_in_jobtable(jobid) ) < 0 ) {
	    if ( ( jidx = allocate_jobent() ) < 0 ) {
		mpdprintf( 1, "sib_mpexec: could not find empty slot in jobtable\n" );
		exit(-1);
	    }
	    if ( myrinet_job )
		init_myrinet_port_counter( );
	}
	jobtable[jidx].jobid = jobid;
	jobtable[jidx].jobsize = jobsize;
	strncpy( jobtable[jidx].program, program, MAXLINE );
	strncpy( jobtable[jidx].username, username, 80 );
	mpdprintf( debug, "sib_mpexec: jobid=%d in jobtable at jidx=%d: \n",jobid,jidx );

	/* set up socket for mpd-manager communication */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, man_mpd_socket) < 0) 
	    error_check( -1, "could not create socketpair to manager" );
	else {
	    man_idx			 = allocate_fdentry();
	    fdtable[man_idx].fd	         = man_mpd_socket[0];
	    fdtable[man_idx].read	 = 1;
	    fdtable[man_idx].write	 = 0;
	    fdtable[man_idx].handler = MANAGER;
	    sprintf( fdtable[man_idx].name, "manager_%d", jobrank );
	}
	mpdprintf( debug, "socketpair for manager %d is %d and %d\n",
		   jobrank, man_mpd_socket[0], man_mpd_socket[1] );

	/* plant environment variables for client process */
	cid = allocate_procent();
	sprintf(env_job,  "MPD_JID=%d", jobid );
	env[i++] = env_job; 
	sprintf(env_rank, "MPD_JRANK=%d", jobrank );
	env[i++] = env_rank; 
	sprintf(env_size, "MPD_JSIZE=%d", jobsize );
	env[i++] = env_size; 
	sprintf(env_shmemkey, "MPD_SHMEMKEY=%d",
		generate_shmemkey( my_listener_port, jobrank / shmemgrpsize, jobid ) );
	env[i++] = env_shmemkey; 
	sprintf(env_shmemgrpsize, "MPD_SHMEMGRPSIZE=%d", shmemgrpsize );
	env[i++] = env_shmemgrpsize; 
	sprintf(env_shmemgrprank, "MPD_SHMEMGRPRANK=%d", j );
	env[i++] = env_shmemgrprank; 
	/* plant environment variables for manager process */
	sprintf( env_man_pgm, "MAN_CLIENT_PGM=%s", program );
	env[i++] = env_man_pgm;
	sprintf( env_mpd_fd, "MAN_MPD_FD=%d", man_mpd_socket[1] );
	env[i++] = env_mpd_fd; 
	/* acquire next available myrinet port and put in environment for manager */
	if ( myrinet_job ) {
	    int mrc;
	    mrc = get_next_myrinet_port( );
	    if ( mrc < 0 ) {
		mpdprintf( 1, "mpexec: could not acquire myrinet port\n" );
		syslog( LOG_INFO, "could not get myrinet port for job %d; user=%s pgm=%s",
			jobid, username, program );
	        sprintf( buf, "src=%s dest=%s cmd=jobstarted job=%d status=failed\n",
		         myid, src, jobid );
	        write_line( rhs_idx, buf );
	    }
	    else {
		sprintf( env_myrinet_port, "MPD_MYRINET_PORT=%d", mrc );
		env[i++] = env_myrinet_port;
	    }
	}
	if ( jobrank == 0 ) {
	    strcpy( prevhost, DUMMYHOSTNAME );
	    prevport = DUMMYPORTNUM;
	}
	else {
	    if ( j == 0 ) {	/* I am setting up first manager on this mpd, but not
				 the very first manager */
		strcpy( prevhost, mpd_getval( "prevhost", buf ) );
		prevport = atoi( mpd_getval( "prevport", buf ) );
	    }
	    else {
		strcpy( prevhost, myhostname );
		prevport = man_listener_port; /* from previous iteration of loop, below */
	    }
	}
	sprintf( env_man_prevhost, "MAN_PREVHOST=%s", prevhost );
	env[i++] = env_man_prevhost;
	sprintf( env_man_prevport, "MAN_PREVPORT=%d", prevport );
	env[i++] = env_man_prevport;

	if ( jobrank != jobsize - 1 ) {	         /* Not the globally last manager */
	    strcpy( host0, DUMMYHOSTNAME );
	    port0 = DUMMYPORTNUM;
	}
	else {
	    if ( jobrank >= shmemgrpsize ) {     /* So there is host0,port0 in
						    incoming message */
		strcpy( host0, mpd_getval( "host0", buf ) );
		port0 = atoi( mpd_getval( "port0", buf ) );
	    }
	    else {		/* We are on first mpd, so no
				   host0,port0 in incoming message */
		strcpy( host0, myhostname );
		if ( j != 0 )
		    port0 = first_man_listener_port;
		else		/* globally first manager (first mpd, j = 0) */
		    port0 = last_man_listener_port;
	    }
	}
	sprintf( env_man_host0, "MAN_HOST0=%s", host0 );
	env[i++] = env_man_host0;
	sprintf( env_man_port0, "MAN_PORT0=%d", port0 );
	env[i++] = env_man_port0;

	if ( j == shmemgrpsize - 1 || jobrank == jobsize - 1 ) { /* last man on this mpd */
	    man_listener_fd = last_man_listener_fd;
	    man_listener_port = last_man_listener_port;
	}
	else
	    if ( j == 0 ) {
		man_listener_fd = first_man_listener_fd; /* acquired at top */
		man_listener_port = first_man_listener_port;
	    }
	    else {
		man_listener_port = 0;
		man_listener_fd = setup_network_socket( &man_listener_port );
	    }
	sprintf( env_man_listener_fd, "MAN_LISTENER_FD=%d", man_listener_fd );
	env[i++] = env_man_listener_fd;

	sprintf( env_man_conhost, "MAN_CONHOST=%s", conhost );
	env[i++] = env_man_conhost;
	sprintf( env_man_conport, "MAN_CONPORT=%d", conport );
	env[i++] = env_man_conport;
	sprintf( env_man_debug, "MAN_DEBUG=%d", debug );
	env[i++] = env_man_debug;
	sprintf( env_man_prebuildprinttree, "MAN_PREBUILD_PRINT_TREE=%d", iotree );
	env[i++] = env_man_prebuildprinttree;
	sprintf( env_gdb, "MAN_GDB=%d", gdb );
	env[i++] = env_gdb;
	sprintf( env_tvdebug, "MAN_TVDEBUG=%d", tvdebug );
	env[i++] = env_tvdebug;
	sprintf( env_version, "MPD_VERSION=%d", MPD_VERSION );
	env[i++] = env_version;
	sprintf( env_line_labels, "MAN_LINE_LABELS=%d", line_labels );
	env[i++] = env_line_labels;
	sprintf( env_whole_lines, "MAN_WHOLE_LINES=%d", whole_lines );
	env[i++] = env_whole_lines;
	sprintf( env_co_program, "MAN_CLI_COPGM=%s", co_program );
	env[i++] = env_co_program;
	sprintf( env_mship_host, "MAN_CLI_MSHIP_HOST=%s", mship_host );
	env[i++] = env_mship_host;
	sprintf( env_mship_port, "MAN_CLI_MSHIP_PORT=%d", mship_port );
	env[i++] = env_mship_port;
	env[i] = NULL;

	proctable[cid].jobid    = jobid;
	proctable[cid].jobrank  = jobrank;
	proctable[cid].state    = CLSTART; /* not running yet */
	proctable[cid].clientfd = man_mpd_socket[0];
	strcpy( proctable[cid].name, manager_pathname );

	mpd_Signal( SIGCHLD, sigchld_handler );
	mpdprintf( debug, "starting program %s\n", manager_pathname );
        syslog( LOG_INFO, "starting job %d; user=%s pgm=%s", jobid, username, program );
	pid = fork();
	proctable[cid].pid = pid;
	if ( pid < 0 ) {
	    mpdprintf( 1, "could not fork manager\n" );
	    deallocate_procent( cid );
	}
	else if ( pid == 0 ) {                  /* child manager */
	    sprintf( myid, "man_%d_before_exec", jobrank );
	    mpdprintf( debug, "manager before exec closing fd %d\n", man_mpd_socket[0] );
	    dclose( man_mpd_socket[0] );
	    setpgid(0,0);	/* set process group id of manager to pid of manager */
#if defined(ROOT_ENABLED)
	    /* set group membership here */
	    initgroups( username, pwent->pw_gid ); 
	    setgid( pwent->pw_gid );
	    setuid( pwent->pw_uid );
#endif
	    rc = execve( manager_pathname, argv, env );
	    if ( rc < 0 ) {
	        sprintf( buf, "src=%s dest=%s cmd=jobstarted job=%d status=failed\n",
		         myid, src, jobid );
	        mpdprintf( debug, "mpexec: sending jobstarted-failed: job=%d dest=%s manager pathname=%s\n", jobid, src, manager_pathname );
	        write_line( rhs_idx, buf );
	    }
	    dclose( rhs_idx );
	    exit(-1);  /* exit if I got thru the execve (with an error) */
	}
	/* parent mpd */
	dclose( man_listener_fd );	/* close listener fd setup on behalf of manager */
	dclose( man_mpd_socket[1] );

	jobrank++;
    }                                   /* end of loop through creation of managers */

    i = 1;  /* argv[0] wasn't malloc'd */
    while (argv[i])
	free(argv[i++]);
    for (i=0; i < envc; i++)
	free(env[i]);
}

void sib_jobsync( void )
{
    char buf[MAXLINE], src[MAXLINE];
    int i, n, jobid, jobsize, sofar, jidx, num_here_in_job;

    mpd_getval( "job", buf );
    jobid = atoi( buf );
    mpd_getval( "jobsize", buf );
    jobsize = atoi( buf );
    mpd_getval( "sofar", buf );
    sofar = atoi( buf );
    mpd_getval( "src", src );

    mpdprintf( debug, "sib_jobsync: entering with jobid=%d, jobsize=%d, sofar=%d\n",
	       jobid, jobsize, sofar );

    if ( sofar == jobsize ) {
	sprintf( buf, "src=%s bcast=true cmd=jobgo job=%d\n", myid, jobid ); 
	mpdprintf( debug, "sib_jobsync: sending jobgo! job=%d\n", jobid );
	write_line( rhs_idx, buf );
	return;
    }

    for ( jidx = 0; jidx < MAXJOBS; jidx++ )
    {
	if ( jobtable[jidx].active  &&  jobtable[jidx].jobid == jobid )
	    break;
    }
    if ( jidx >= MAXJOBS ) {
	mpdprintf( 1, "sib_jobsync: could not find jobid=%d in table\n",jobid );
	exit(-1);
    }

    mpdprintf( debug,"sib_jobsync: setting jobsync_is_here for jobid=%d at jidx=%d \n",
	       jobid, jidx );
    jobtable[jidx].jobsync_is_here = 1;
    for ( i = 0, num_here_in_job = 0; i < MAXPROCS; i++ ) {
        if ( proctable[i].active && proctable[i].jobid == jobid ) {
	    num_here_in_job++;
	}
    }
    jobtable[jidx].alive_in_job_sofar = sofar;
    if (num_here_in_job == jobtable[jidx].alive_here_sofar)
    {
	n = jobtable[jidx].alive_here_sofar - jobtable[jidx].added_to_job_sofar;
	sofar += n;
	jobtable[jidx].added_to_job_sofar += n;
	sprintf( buf, "src=%s dest=anyone cmd=jobsync job=%d jobsize=%d sofar=%d\n",
		 myid, jobid, jobsize, sofar );
	mpdprintf( debug, "sib_jobsync: sending jobsync: job=%d sofar=%d\n", jobid,sofar );
	write_line( rhs_idx, buf );
	jobtable[jidx].jobsync_is_here = 0;
    }
}


void sib_jobgo( void )
{
    char buf[MAXLINE];
    int i, jobid;

    mpd_getval( "job", buf );
    jobid = atoi( buf );
    for ( i = 0; i < MAXPROCS; i++ ) {
        if ( proctable[i].active && ( proctable[i].jobid == jobid ) ) {
	    proctable[i].state = CLRUNNING;
	    mpdprintf( 1, "sib_jobgo: sending go to client for job=%d, rank=%d\n",
		       jobid, proctable[i].jobrank );
	    sprintf( buf, "cmd=go\n" );
	    send_msg( proctable[i].clientfd, buf, strlen( buf ) );
	}
    }
}

void sib_jobstarted( void )
{
    char buf[MAXLINE], statusbuf[64];
    int  jobid;

    mpd_getval( "jobid", buf );
    jobid = atoi( buf );
    mpd_getval( "status", statusbuf );
    sprintf( buf, "cmd=jobinfo jobid=%d status=%s\n", jobid, statusbuf );
    write_line( console_idx, buf );
}

void sib_ringtest( void )
{
    int  count;
    char buf[MAXLINE];
    char srcid[IDSIZE];
    char destid[IDSIZE];
    char timestamp[80];
    double time1, time2;

    mpd_getval( "count", buf );                 
    count = atoi( buf );
    mpd_getval( "src", srcid );
    mpd_getval( "dest", destid );
    mpd_getval( "starttime", timestamp );

    mpdprintf( debug, "ringtest myid=%s count=%d starttime=%s\n",
	       myid, count, timestamp ); 
    if ( strcmp( destid, myid ) == 0 ) {
        count--;
        if ( count <= 0 ) {
	    time2 = mpd_timestamp();
	    time1 = atof( timestamp );
            sprintf( buf, "ringtest completed in %f seconds\n", time2 - time1 );
            write_line( console_idx, buf );
        }
    }
    if ( count > 0 ) {
        sprintf( buf,"src=%s dest=%s cmd=ringtest count=%d starttime=%s\n",
                srcid, destid, count, timestamp );
        write_line( rhs_idx, buf );
    }
}

void sib_ringsize( void )
{
    int count, execonly;
    char srcid[IDSIZE], buf[MAXLINE];

    mpd_getval( "src", srcid );
    execonly = atoi( mpd_getval( "execonly", buf ) );
    count = atoi( mpd_getval( "count", buf ) );

    mpdprintf( debug, "ringsize received count=%d execonly=%d; my no_exec=%d\n", 
               count, execonly, no_execute ); 
    if ( ! execonly  ||  ! no_execute )
	count++;
    if ( strcmp( srcid, myid ) == 0 ) {
	sprintf( buf, "cmd=ringsize_completed size=%d\n", count );
	write_line( console_idx, buf );
    }
    else {
        sprintf( buf,"src=%s dest=anyone cmd=ringsize count=%d execonly=%d\n",
	         srcid, count, execonly );
        write_line( rhs_idx, buf );
    }
}

void sib_clean( void )
{
    int jidx;
    char buf[MAXLINE], srcid[IDSIZE];

    for ( jidx = 0; jidx < MAXJOBS; jidx++ )
    {
	if ( jobtable[jidx].active )
	    kill_job( jobtable[jidx].jobid, SIGKILL );
    }
    mpd_getval( "src", srcid );
    if ( strcmp( srcid, myid ) == 0 ) {
        sprintf( buf, "cmd=clean_complete\n" );
        write_line( console_idx, buf );
    }
}

void sib_trace( void )
{
    int execonly;
    char buf[MAXLINE], srcid[IDSIZE];

    execonly = atoi( mpd_getval( "execonly", buf ) );
    if ( execonly  &&  no_execute )
        return;
    mpd_getval( "src", srcid );
    if ( strcmp( srcid, myid ) == 0 ) {
        sprintf( buf, "%s:  lhs=%s_%d  rhs=%s_%d  rhs2=%s_%d gen=%d\n",
                 myid, lhshost, lhsport, rhshost, rhsport, rhs2host, rhs2port, generation);
        write_line( console_idx, buf );
    }
    else {
	mpdprintf(debug,"sending my trace info to %s\n",srcid);
	sprintf( buf,
		 "src=%s dest=%s cmd=trace_info lhs=%s_%d rhs=%s_%d rhs2=%s_%d gen=%d\n",
		 myid, srcid, lhshost, lhsport, rhshost, rhsport,
		 rhs2host, rhs2port, generation );
	write_line( rhs_idx, buf );
    }
}

void sib_trace_trailer( void )
{
    char buf[MAXLINE];
    char srcid[IDSIZE];

    mpd_getval( "src", srcid );
    if ( strcmp( srcid, myid ) == 0 ) {
        sprintf( buf, "trace done\n" );
        write_line( console_idx, buf );
    }
    else {
        sprintf( buf, "cmd=trace_trailer src=%s\n", srcid );
        write_line( rhs_idx, buf );
    }
}

void sib_trace_info( void )
{
    char buf[MAXLINE];
    char srcid[IDSIZE], lhsid[IDSIZE], rhsid[IDSIZE], rhs2id[IDSIZE], gen[8];

    mpd_getval( "src", srcid );
    mpd_getval( "lhs", lhsid );
    mpd_getval( "rhs", rhsid );
    mpd_getval( "rhs2", rhs2id );
    mpd_getval( "gen", gen );
    sprintf( buf, "%s:  lhs=%s  rhs=%s  rhs2=%s gen=%s\n",
             srcid, lhsid, rhsid, rhs2id, gen );
    write_line( console_idx, buf );
}

void sib_listjobs( void )
{
    int i;
    char buf[MAXLINE];
    char con_mpd_id[IDSIZE];

    mpd_getval( "con_mpd_id", con_mpd_id );   
    mpdprintf( debug, "got listjobs con_mpd_id=%s\n", con_mpd_id );
    if ( strcmp( con_mpd_id,myid ) != 0 ) {
	reconstruct_message_from_keyvals( buf );
	write_line( rhs_idx, buf );
    }
    for (i=0; i < MAXJOBS; i++) {
	if ( jobtable[i].active ) {
	    sprintf( buf,
		     "con_mpd_id=%s cmd=listjobs_info dest=anyone info_src=%s jobid=%d "
		     "user=%s program=%s\n",
		     con_mpd_id, myid, jobtable[i].jobid,
		     jobtable[i].username, jobtable[i].program );
	    write_line( rhs_idx, buf );
	}
    }
    if ( strcmp( con_mpd_id, myid ) == 0 ) {
	sprintf( buf, "con_mpd_id=%s dest=anyone cmd=listjobs_trailer\n", myid );
	write_line( rhs_idx, buf );
    }
}

void sib_listjobs_trailer( void )
{
    char buf[MAXLINE];
    char con_mpd_id[IDSIZE];

    mpd_getval( "con_mpd_id", con_mpd_id );
    mpdprintf( debug, "sibproc got trailer from %s\n", con_mpd_id );
    if ( strcmp( con_mpd_id, myid ) == 0 ) {
        sprintf( buf, "listjobs done\n" );
        write_line( console_idx, buf );
    }
    else {
        sprintf( buf, "cmd=listjobs_trailer dest=anyone con_mpd_id=%s\n", con_mpd_id );
        write_line( rhs_idx, buf );
    }
}

void sib_listjobs_info( void )
{
    char buf[MAXLINE];
    char con_mpd_id[IDSIZE], info_src[IDSIZE], jobid[IDSIZE], username[80], program[MAXLINE];

    mpd_getval( "con_mpd_id", con_mpd_id );
    mpd_getval( "info_src", info_src );
    mpd_getval( "jobid", jobid );
    mpd_getval( "user", username );
    mpd_getval( "program", program );
    mpdprintf( debug, "sibproc got listjobs_info from info_src=%s con_mpd_id=%s\n",
	       info_src, con_mpd_id );
    if ( strcmp( con_mpd_id, myid ) == 0 ) {
        sprintf( buf, "%s: running jobid=%s user=%s program=%s\n",
		 info_src, jobid, username, program );
        write_line( console_idx, buf );
    }
    else {
	reconstruct_message_from_keyvals( buf );
        write_line( rhs_idx, buf );
    }
}

void sib_dump( void )
{
    char buf[MAXLINE];
    char srcid[IDSIZE], what[80];

    mpd_getval( "src", srcid );
    mpd_getval( "what", what );

    if ( strcmp( what, "jobtable") == 0 ||
	 strcmp( what, "all"     ) == 0 )
	dump_jobtable( 1 );
    if ( strcmp( what, "proctable") == 0 ||
	 strcmp( what, "all"      ) == 0 )
	dump_proctable( "procentries" );
    if ( strcmp( what, "fdtable") == 0 ||
	 strcmp( what, "all"      ) == 0 )
	dump_fdtable( "fdentries" );

    if ( strcmp( srcid, myid ) != 0 ) {
        sprintf( buf, "src=%s dest=anyone cmd=dump what=%s\n", srcid, what );
        write_line( rhs_idx, buf );
    }
}

void sib_mandump( void )
{
    int  i, jobid, manrank;
    char buf[MAXLINE];
    char srcid[IDSIZE], what[80], manager[8];

    mpd_getval( "src", srcid );
    mpd_getval( "jobid", buf );
    jobid = atoi( buf );
    mpd_getval( "manrank", manager );
    manrank = atoi( manager );
    mpd_getval( "what", what );

    mpdprintf(1, "got mandump command for jobid=%d manrank=%d what=%s\n",
	      jobid, manrank, what );

    /* next step:  look up to see if I have a client with that rank */
    for ( i = 0; i < MAXPROCS; i++ ) {
        if ( proctable[i].active &&
	   ( proctable[i].jobrank == manrank) &&
	   ( proctable[i].jobid == jobid ) ) {
	    mpdprintf( 1, "sib_mandump: job=%d, rank=%d what=%s\n",
		       jobid, manrank, what );
	    sprintf( buf, "cmd=mandump what=%s\n", what );
	    send_msg( proctable[i].clientfd, buf, strlen( buf ) );
	}
    }

    if ( strcmp( srcid, myid ) != 0 ) {
        sprintf( buf,
		 "src=%s dest=anyone cmd=mandump jobid=%d manrank=%d what=%s\n",
		 srcid, jobid, manrank, what );
        write_line( rhs_idx, buf );
    }
}

void sib_ping_ack( void )
{
    char buf[MAXLINE];
    char fromid[IDSIZE];

    mpd_getval( "src", buf );
    strcpy( fromid, buf );
    sprintf( buf, "%s is alive\n", fromid );
    write_line( console_idx, buf );
}

void sib_ping( void )
{
    char buf[MAXLINE];
    char fromid[IDSIZE];

    mpd_getval( "src", fromid );
    sprintf( buf, "src=%s dest=%s cmd=ping_ack\n", myid, fromid );
    write_line( rhs_idx, buf );
}

void sib_needjobids( void )
{
    char buf[MAXLINE], srcbuf[MAXLINE];
    int first, last;

    mpd_getval( "src", srcbuf );
    if ( steal_jobids( &first, &last ) == 0 ) {
	sprintf( buf, "cmd=newjobids dest=%s first=%d last=%d\n", srcbuf, first, last );
	mpdprintf( 0, "sending newids, first=%d, last=%d to %s\n", first, last, srcbuf );
	write_line( rhs_idx, buf );
    }
    else {
	sprintf( buf, "src=%s dest=anyone cmd=needjobids\n", srcbuf );
	mpdprintf( 0, "forwarding needjobids message\n" );
	write_line( rhs_idx, buf );
    }
}

void sib_newjobids( void )
{
    char buf[MAXLINE];
    int first, last;

    mpd_getval( "first", buf );
    first = atoi( buf );
    mpd_getval( "last", buf );
    last  = atoi( buf );

    mpdprintf( 0, "accepting new jobids first=%d, last=%d\n", first, last );
    add_jobids( first, last );
}

void sib_pulse( void )
{
    char buf[MAXLINE];
    char fromid[IDSIZE];

    mpd_getval( "src", fromid );
    mpdprintf( 0, "responding to pulse\n" );
    sprintf( buf, "src=%s dest=%s cmd=pulse_ack\n", myid, fromid );
    write_line( lhs_idx, buf );
}

/* This handles cmd=moninfo_reqest */
void sib_moninfo( void )
{
    char fromid[IDSIZE], reqtype[80], buf[MAXLINE], databuf[MAXLINE], stuffedbuf[MAXLINE];
    char xmlbuf[2*MAXLINE], monwhat[MAXLINE];
    int  jobid, rc, get_data_here = 0;

    mpd_getval( "src", fromid );
    mpd_getval( "vals", reqtype );
    mpd_getval( "monwhat", monwhat ); /* monwhat is "all" or int jobid */

    mpdprintf( debug, "sib_moninfo got request from %s of type %s\n", fromid, reqtype );

    if ( strcmp( monwhat, "all" ) == 0 )
	get_data_here = 1;
    else {			 /* monwhat is job id; is job running here? */
	jobid = atoi( monwhat ); /* should use strtol */
	rc = find_jobid_in_jobtable( jobid );
	if ( rc >= 0 )
	    get_data_here = 1;
    }

    if ( strcmp( fromid, myid ) != 0 ) {    /* most mpd's forward data to the right */
	/* forward data */
	if ( get_data_here ) {
	    get_mon_data( reqtype, databuf );
	    mpd_stuff_arg( databuf, stuffedbuf );
	    sprintf( buf, "cmd=moninfo_data dest=%s src=%s data=%s\n",
		     fromid, myid, stuffedbuf );
	    /* mpdprintf( 111, "sending data to rhs, buf=:%s:\n", buf ); */
	    write_line( rhs_idx, buf );
	}
	/* forward request */
	sprintf( buf, "cmd=moninfo_req dest=anyone src=%s monwhat=%s vals=%s\n",
		 fromid, monwhat, reqtype );
	mpdprintf( debug, "sending req to rhs, buf=:%s:\n", buf );
	write_line( rhs_idx, buf );
    }
    else {			/* mpd in contact with monitor sends directly  */
	if ( get_data_here ) {
	    get_mon_data( reqtype, databuf );
	    mpdprintf( debug, "databuf before xml, a, = :%s:\n", databuf );
	    datastr_to_xml( databuf, myid, xmlbuf );
	    mpdprintf( debug, "sending data to monitor, a, buf=:%s:\n", xmlbuf );
	    write_line( mon_idx, xmlbuf );
	}
	else {
	    sprintf( xmlbuf, "<node name='%s'>trailer</node>\n", myid ); /* even if no
									   such job
									   on this mpd*/
	    write_line( mon_idx, xmlbuf );
	}
    }
}

void sib_moninfo_data( void )
{
    char data[MAXLINE], src[IDSIZE], xmlbuf[2*MAXLINE], unstuffed[MAXLINE];

    mpd_getval( "src", src );
    mpd_getval( "data", data );
    mpd_destuff_arg( data, unstuffed );
    mpdprintf( debug, "databuf before xml, b, = :%s:\n", unstuffed );
    datastr_to_xml( unstuffed, src, xmlbuf );
    mpdprintf( debug, "sending data to monitor, b, buf=:%s:\n", xmlbuf );
    write_line( mon_idx, xmlbuf );
}

void sigchld_handler( int signo )
{
    pid_t pid;
    int i, wait_stat, jidx;

    /* pid = wait( &wait_stat ); */
    while ( ( pid = waitpid( -1, &wait_stat, WNOHANG ) ) > 0 ) {
        for ( i = 0; i < MAXPROCS; i++ ) {
	    if (proctable[i].active && proctable[i].pid == pid) {
                jidx = find_jobid_in_jobtable(proctable[i].jobid);
		if ( jidx >=0 ) {
		    jobtable[jidx].alive_here_sofar--;
		    if (jobtable[jidx].alive_here_sofar <= 0)
			remove_from_jobtable( jobtable[jidx].jobid );
		}
	    }
	}
        mpdprintf( debug, "child %d terminated\n", (int) pid );
        remove_from_proctable( (int) pid );
	dump_jobtable(0);
    }
    return;
}

void sigusr1_handler( int signo )
{
    mpdprintf( 1, "mpd got SIGUSR1\n" );
}

extern void mpd_cleanup( void );

void sigint_handler( int signo )
{
    char buf[MAXLINE];

    mpdprintf( debug, "\n MPD exit on SIGINT\n");
    if (amfirst) { /* for master , kill all */
       sprintf(buf,"src=%s dest=%s bcast=true cmd=bomb\n",
               myid,myid);
       write_line( rhs_idx, buf );
    }
    mpdprintf( 1, "calling mpd_cleanup from sigint_handler;sig=%d\n", signo );
    mpd_cleanup();
    exit(1);
}

/* returns a key that is shared by processes in same cluster in same job, but no others */
int generate_shmemkey( int portid, int clusterid, int jobid )
{
    int newportid, newclusterid, newjobid, shmemkey;

    newportid	 = portid % ( 1 << 16 );
    newclusterid = clusterid % ( 1 << 8 );
    newjobid	 = jobid % ( 1 << 8 );

    shmemkey = ( newportid << 16 ) + ( newclusterid << 8 ) + ( newjobid );

    mpdprintf( debug, "shmemkey = 0x%x = %d\n", shmemkey, shmemkey );

    return shmemkey;

}

int parse_groups( char *groups, gid_t gids[], int *numgids )
{
    int i;
    char *c, groupstring[5*MAXGIDS];

    mpdprintf( debug, "group string in parse_groups = :%s:\n", groups );
    strcpy( groupstring, groups );
    c = strtok( groupstring, ", " );
    i = 0;
    while ( c ) {
	mpdprintf( 0, "group = %s\n", c );
	gids[i] = atoi( c );
	c = strtok( NULL, ", " );
	i++;
    }
    *numgids = i;
    return 0;
}    

int my_hostname_is_in_pattern( char *hostlist_pattern )
{
    /* hostlist_pattern is if form:  ccn%s-my:1-2,4,7-9
       where the list of machines is:  ccn1-my  ccn2-my  ccn4-my  ccn7-my  ccn8-my  ccn9-my
    */

    char *c, *d, *e, hostname_pattern[64], range1_pattern[64], range2_pattern[64], temphostname[128];
    int  i, len, range1, range2;

    if ( ( c = strchr( hostlist_pattern, ':' ) ) == NULL ) {
        if ( strcmp( hostlist_pattern, myhostname ) == 0  ||
             strcmp( hostlist_pattern, mynickname ) == 0)
	{
	    return( 1 );
	}
	else
	{
	    return( 0 );
	}
    }

    len = c - hostlist_pattern;
    memcpy( hostname_pattern, hostlist_pattern, len );
    hostname_pattern[len] = '\0';

    c++;
    while (1)
    {
	d = strchr( c, ',' );
	if ( d != NULL )
	    *d = '\0';

	e = strchr( c, '-' );
	if ( e != NULL )  {
	    strncpy( range1_pattern, c, e-c );
	    /* Need to insure a null at the end of range1_pattern */
	    range1_pattern[e-c] = 0;
	    strcpy( range2_pattern, e+1 );
	}
	else {
	    strcpy( range1_pattern, c );
	    range2_pattern[0] = '\0';
	}

	range1 = atoi(range1_pattern);
	if (range2_pattern[0])
	    range2 = atoi(range2_pattern);
	else
	    range2 = range1;
	
	for ( i=range1; i <= range2; i++ )  {
	    sprintf( temphostname, hostname_pattern, i );
	    if ( strcmp( temphostname, myhostname ) == 0 ||
	         strcmp( temphostname, mynickname ) == 0 )
	    {
	        return( 1 );
	    }
	}

	c += strlen(c) + 1;
	if ( d == NULL )
	    break;
    }

    return( 0 );
}

void get_mon_data( char *vals, char *buf )
{
    char getbuf[MAXLINE], tempbuf[MAXLINE], dbuf[MAXLINE];

    dbuf[0] = '\0';

    /* This should be made more robust by checking lengths, using strncat, etc. */
    if ( strstr( vals, "loadavg" ) ) {
	strcat( dbuf, "loadavg:" );
	get_mon_data_load( getbuf );
	sprintf( tempbuf, "%s,", getbuf );
	strcat( dbuf, tempbuf );
    }
    if ( strstr( vals, "memusage" ) ) {
	strcat( dbuf, "memusage:" );
	get_mon_data_mem( getbuf );
	sprintf( tempbuf, "%s,", getbuf );
	strcat( dbuf, tempbuf );
    }
    if ( strstr( vals, "myrinfo" ) ) {
	strcat( dbuf, "myrinfo:" );
	get_mon_data_myr( getbuf );
	sprintf( tempbuf, "%s,", getbuf );
	strcat( dbuf, tempbuf );
    }
    strcpy( buf, dbuf );
}

void get_mon_data_load( char *retbuf )
{
    FILE *pstream;
    char buf[MAXLINE], *p;
    char loadavg[10];

    mpdprintf( debug, "get_mon_data_load: starting...\n" );
    pstream = popen( "uptime", "r" );
    mpdprintf( debug, "get_mon_data_load: uptime has been popen()ed\n" );
    if ( pstream == NULL ) {
	mpdprintf( 1, "newconn_moninfo: could not popen uptime\n" );
	retbuf[0] = '\0';
    }
    else {
      again:
	p = fgets( buf, MAXLINE, pstream );
	if ( !p ) {
	    if ( errno == EINTR )
		goto again;
	    retbuf[0] = '\0';
	}
	else {   
	    mpdprintf( debug, "get_mon_data_load: buf is :%s:\n", buf );
	    p = strstr( buf, "load average:" );
	    sscanf( p, "load average: %s", loadavg );
	    loadavg[strlen( loadavg ) - 1] = '\0';	/* destroy trailing comma */
	    strcpy( retbuf, loadavg );
	    mpdprintf( debug, "get_mon_data_load: returning :%s:\n", loadavg );
	}
	pclose( pstream );
    }
}

void get_mon_data_mem( char *retbuf )
{
    FILE *pstream;
    char buf[MAXLINE], *p, tempbuf[MAXLINE];

    buf[0] = '\0';
    mpdprintf( debug, "get_mon_data_mem: starting...\n" );
    pstream = popen( "cat /proc/meminfo", "r" );
    mpdprintf( debug,
	      "get_mon_data_mem: cat /proc/meminfo has been popen()ed\n" );
    if ( pstream == NULL ) {
	mpdprintf( 1, "get_mon_data_info: could not open cat /proc/meminfo\n" );
	retbuf[0] = '\0';
    }
    else {
	while ( 1 ) {
	  again:
	    p = fgets( tempbuf, MAXLINE, pstream );
	    if ( p ) {
		mpdprintf( 0, "get_mon_data_mem: read :%s:\n", tempbuf );
		strcat( buf, tempbuf );
	    }
	    else {
		if ( errno == EINTR ) {
		    errno = 0;	/* Seems to be necessary; it isn't reset autmatically */
		    goto again;
		}
		else
		    break;
	    }
	}

	if ( buf[0] == '\0' ) {
	    mpdprintf( debug, "get_mon_data_mem: received no lines\n" );
	    retbuf[0] = '\0';
	}
	else {
	    mpdprintf( 0, "get_mon_data_mem: buf is :%s:\n", buf );
	    p = strstr( buf, "MemTotal:" );
	    if ( p ) {
		strcpy( buf, p );
		strcompress( buf );
		strcpy( retbuf, buf );
		mpdprintf( 0, "get_mon_data_mem: returning :%s:\n", retbuf );
	    }
	    else
		retbuf[0] = '\0';
	}
	pclose( pstream );
    }
}

/* This routine is dependent on the output of Myricom's gm_counters function.
   It depends on a) first line can be ignored, b) no commas in data
*/
void get_mon_data_myr( char *retbuf )
{
    FILE *pstream;
    char buf[MAXLINE], *p, tempbuf[MAXLINE];

    buf[0] = '\0';
    mpdprintf( debug, "get_mon_data_myr: starting...\n" );
    pstream = popen( "/my/bin/gm_counters", "r" );
    mpdprintf( debug, "gm_counters has been popen()ed\n" );
    if ( pstream == NULL ) {
	mpdprintf( 1, "get_mon_data_myr: could not popen gm_counters\n" );
	retbuf[0] = '\0';
    }
    else {
	while ( 1 ) {
	  again:
	    p = fgets( tempbuf, MAXLINE, pstream );
	    if ( p ) {
		mpdprintf( 0, "get_mon_data_myr: read :%s:\n", tempbuf );
		strcat( buf, tempbuf );
	    }
	    else {
		if ( errno == EINTR ) {
		    errno = 0;	/* Seems  necessary; it isn't reset automatically */
		    goto again;
		}
		else
		    break;
	    }
	}

	if ( buf[0] == '\0' || !strstr( buf, "_cnt" ) ) {
	    mpdprintf( debug, "get_mon_data_myr: received no valid lines\n" );
	    retbuf[0] = '\0';
	}
	else {
	    pclose( pstream );
	    mpdprintf( 0, "get_mon_data_myr: buf is :%s:\n", buf );
	    p = strstr( buf, "\n" ); /* skip first line */
	    p++;
	    strcpy( buf, p );
	    strcompress( buf );
	    strcpy( retbuf, buf );
	    mpdprintf( 0, "get_mon_data_myr: returning :%s:\n", buf );
	}
	pclose( pstream );
    }
}

/*
 * Find the manager.  Look for the name in the path, unless envname
 * is in the environment *and* that program exists.  envpath allows
 * an alternate search path to be specified by the environment variable
 * with that name.
 * 
 * The test for executability is crude and should be refined, but this
 * should be sufficient for the needs of this routine.
 *
 * If mpd is running as root, we may not want to allow a general
 * manager to run (though that should be ok, since it runs as a user
 * process and since the manager will immediately run a user program.
 * In that case, this routine should just return MANAGER_PATHNAME instead.
 */
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif
static const char *mpdGetManager( const char path[], const char name[], 
				  const char envpath[], const char envname[] ) 
{
    static char fullname[MAXPATHLEN];
    const char *envfullname;
    struct stat filestatus;
    const char *(paths[2]);
    int         err, len, i;

    /* First, check the environment */
    if (envname) {
	envfullname = getenv( envname );
	if (envfullname) {
	    err = stat( envfullname, &filestatus );
	    if (err == 0 && 
		(filestatus.st_mode & (S_IXGRP | S_IXOTH | S_IXUSR))) {
		strcpy( fullname, envfullname );
		return (const char *)fullname;
	    }
	}
    }

    /* Get the array of possible path values */
    paths[0] = 0;
    if (envpath) {
	paths[0] = getenv(envpath);
    }
    if (paths[0]) 
	paths[1] = path;
    else {
	paths[0] = path;
	paths[1] = 0;
    }

    /* Now, run through the search paths */
    for (i=0; i<2 && paths[i]; i++) {
	path = paths[i];
	while (path) {
	    char *next_path;
	    /* Get next path member */
	    next_path = strchr( path, ':' );
	    if (next_path) 
		len = next_path - path;
	    else
		len = strlen(path);
	    
	    /* Copy path into the file name */
	    strncpy( fullname, path, len );
	    
	    fullname[len]   = '/';
	    fullname[len+1] = 0;
	    
	    /* Construct the final path name */
	    strcat( fullname, name ); 
	    
	    if (debug) 
		printf( "Attempting to stat %s\n", fullname );
	    
	    err = stat( fullname, &filestatus );
	    if (err == 0 && 
		(filestatus.st_mode & (S_IXGRP | S_IXOTH | S_IXUSR))) {
		return (const char *)fullname;
	    }
	    
	    if (next_path) 
		path = next_path + 1;
	    else
		path = 0;
	}
	
    }
    /* ! Could not find a manager.  Return null */
    return 0;
}
