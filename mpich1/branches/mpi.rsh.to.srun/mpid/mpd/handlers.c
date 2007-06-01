/*
 *        handlers.c
 *        Handle incoming messages
 */
#include "mpd.h"

extern struct fdentry fdtable[MAXFDENTRIES];
extern int    console_idx;
extern int    client_idx;
extern int    listener_idx;
extern char   lhshost[MAXHOSTNMLEN];
extern char   orig_lhshost[MAXHOSTNMLEN];
extern int    lhsport;
extern int    orig_lhsport;
extern char   rhshost[MAXHOSTNMLEN];
extern int    rhsport;
extern char   rhs2host[MAXHOSTNMLEN];
extern int    rhs2port;
extern int    debug;
extern int    allexiting;
extern char   myid[IDSIZE];
extern int    my_listener_port;
extern char   mynickname[MAXHOSTNMLEN];
extern int    rhs_idx;
extern int    lhs_idx;
extern int    mon_idx;
extern struct keyval_pairs keyval_tab[64];
extern int    keyval_tab_idx;
extern char   mpd_passwd[PASSWDLEN];
extern int    pulse_chkr;
extern int    shutting_down;
extern int    generation;
extern int    amfirst;

int connecting_to_self_as_lhs = 0;

void handle_lhs_input( int idx )
{
    int  fwd, length, exec;
    char message[MAXLINE];
    char srcid[MAXLINE];
    char destid[MAXLINE];
    char bcastval[MAXLINE];
    char cmdval[MAXLINE];
    char fwdbuf[MAXLINE];

    mpdprintf( 0, "handling lhs input\n" );
    if ( (length = read_line(fdtable[idx].fd, message, MAXLINE ) ) != 0 ) {
        mpdprintf( 0, "message from lhs to handle =:%s: (read %d)\n", 
		   message, length );
	/* parse whole message */ 
	strcpy( fwdbuf, message );             
	mpd_parse_keyvals( message );
	/* dump_keyvals(); */
	mpd_getval( "src", srcid );
	mpd_getval( "dest", destid );
	mpd_getval( "bcast", bcastval );
	mpd_getval( "cmd", cmdval );
        if ( strlen(cmdval) == 0 ) 
	{
            mpdprintf( debug, "no command specified in msg\n" );
            return;
        }

	fwd = 0;
	if ( strcmp( bcastval, "true" ) == 0  &&  strcmp( srcid, myid ) != 0 )
	{
	    fwd = 1;
	}
	else if ( strcmp( destid, "anyone" ) != 0  && 
		  strcmp( destid, myid )     != 0  &&
	          strcmp( srcid, myid )      != 0 )
	{
	    fwd = 1;
	}
	if ( fwd )  {
	    mpdprintf( debug, "forwarding :%s: to :%s_%d:\n", fwdbuf, rhshost,rhsport );
	    write_line( rhs_idx, fwdbuf );
	}

	exec = 0;
	if ( strcmp( bcastval, "true" ) == 0 )
	    exec = 1;
	else if ( strcmp( destid, myid ) == 0 || strcmp( destid, "anyone" ) == 0 )
	     exec = 1;
	if (!exec)
	    return;

	if ( strcmp( cmdval, "ping" ) == 0 )
	    sib_ping();
	else if ( strcmp( cmdval, "ping_ack" ) == 0 )
	    sib_ping_ack();
	else if ( strcmp( cmdval, "ringtest" ) == 0 )
	    sib_ringtest();
	else if ( strcmp( cmdval, "ringsize" ) == 0 )
	    sib_ringsize();
	else if ( strcmp( cmdval, "clean" ) == 0 )
	    sib_clean();
	else if ( strcmp( cmdval, "trace" ) == 0 )
	    sib_trace();
	else if ( strcmp( cmdval, "trace_info" ) == 0 )
	    sib_trace_info();
	else if ( strcmp( cmdval, "trace_trailer" ) == 0 )
	    sib_trace_trailer();
	else if ( strcmp( cmdval, "dump" ) == 0 )
	    sib_dump();
	else if ( strcmp( cmdval, "mandump" ) == 0 )
	    sib_mandump();
	else if ( strcmp( cmdval, "rhs2info" ) == 0 )
	    sib_rhs2info( idx );
	else if ( strcmp( cmdval, "reconnect_rhs" ) == 0 )
	    sib_reconnect_rhs(idx);
	else if ( strcmp( cmdval, "listjobs" ) == 0 )
	    sib_listjobs();
	else if ( strcmp( cmdval, "listjobs_info" ) == 0 )
	    sib_listjobs_info();
	else if ( strcmp( cmdval, "listjobs_trailer" ) == 0 )
	    sib_listjobs_trailer();
	else if ( strcmp( cmdval, "signaljob" ) == 0 )
	    sib_signaljob();
	else if ( strcmp( cmdval, "killjob" ) == 0 )
	    sib_killjob();
	else if ( strcmp( cmdval, "exit" ) == 0 )
	    sib_exit();
	else if ( strcmp( cmdval, "allexit" ) == 0 )
	    sib_allexit();
	else if ( strcmp( cmdval, "shutdown" ) == 0 )
	    sib_shutdown();
	else if ( strcmp( cmdval, "req_perm_to_shutdown" ) == 0 )
            sib_req_perm_to_shutdown();
	else if ( strcmp( cmdval, "perm_to_shutdown" ) == 0 )
            sib_perm_to_shutdown();
	else if ( strcmp( cmdval, "mpexec" ) == 0 )
	    sib_mpexec();
	else if ( strcmp( cmdval, "jobstarted" ) == 0 )
	    sib_jobstarted();
	else if ( strcmp( cmdval, "jobsync" ) == 0 )
	    sib_jobsync();
	else if ( strcmp( cmdval, "jobgo" ) == 0 )
	    sib_jobgo();
	else if ( strcmp( cmdval, "bomb" ) == 0 )
	    sib_bomb();
	else if ( strcmp( cmdval, "debug" ) == 0 )
	    sib_debug();
	else if ( strcmp( cmdval, "needjobids" ) == 0 )
	    sib_needjobids();
	else if ( strcmp( cmdval, "newjobids" ) == 0 )
	    sib_newjobids();
	else if ( strcmp( cmdval, "pulse" ) == 0 )
	    sib_pulse();
	else if ( strcmp( cmdval, "moninfo_req" ) == 0 )
	    sib_moninfo();
	else if ( strcmp( cmdval, "moninfo_data" ) == 0 )
	    sib_moninfo_data();
	else
	    mpdprintf( 1, "invalid msg string from lhs = :%s:\n", fwdbuf );
        return;        
    }
    else { /* sibling gone away */
	mpdprintf( debug, "lost contact with sibling idx=%d fd=%d\n",
		   idx,fdtable[idx].fd); 
        dclose( fdtable[idx].fd ); 
        deallocate_fdentry( idx );
	if ( idx == lhs_idx)
	    lhs_idx = -1;
    }
}

/*
 *        Handler for console input
 */
void handle_console_input( idx )
int idx;
{
    int rc;
    char buf[MAXLINE];
    char parsebuf[MAXLINE];
    char errbuf[MAXLINE];
    char cmd[MAXLINE];

    mpdprintf( 0, "handling console input\n" );
    if ( read_line( fdtable[idx].fd, buf, MAXLINE ) != 0 ) {
        mpdprintf( debug, "mpd received :%s: from console\n", 
		   buf );
        /* get first word and branch accordingly, but pass whole buf */
	strcpy( parsebuf, buf );
	rc = mpd_parse_keyvals( parsebuf );
	if ( rc < 0 ) {
	    sprintf( errbuf,
		     "cmd=jobstarted status=failed reason=invalid_msg_from_console\n" );
	    write_line( console_idx, errbuf );
	    return;
	}
	mpd_getval( "cmd", cmd );
	if ( cmd[0] ) {
	    if ( strcmp( cmd, "mpexec" ) == 0 )
		con_mpexec( );
	    else if ( strcmp( cmd, "ringtest" ) == 0 )
		con_ringtest( );
	    else if ( strcmp( cmd, "ringsize" ) == 0 )
		con_ringsize( );
	    else if ( strcmp( cmd, "debug" ) == 0 )
		con_debug( );
	    else if ( strcmp( cmd, "clean" ) == 0 )
		con_clean( );
	    else if ( strcmp( cmd, "trace" ) == 0 )
		con_trace( );
	    else if ( strcmp( cmd, "dump" ) == 0 )
		con_dump( );
	    else if ( strcmp( cmd, "mandump" ) == 0 )
		con_mandump( );
	    else if ( strcmp( cmd, "ping" ) == 0 )
		con_ping( );
	    else if ( strcmp( cmd, "bomb" ) == 0 )
		con_bomb( );
	    else if ( strcmp( cmd, "exit" ) == 0 )
		con_exit( );
	    else if ( strcmp( cmd, "allexit" ) == 0 )
		con_allexit( );
	    else if ( strcmp( cmd, "shutdown" ) == 0 )
		con_shutdown( );
	    else if ( strcmp( cmd, "listjobs" ) == 0 )
		con_listjobs( );
	    else if ( strcmp( cmd, "signaljob" ) == 0 )
		con_signaljob( );
	    else if ( strcmp( cmd, "killjob" ) == 0 )
		con_killjob( );
	    else if ( strcmp( cmd, "addmpd" ) == 0 )
		con_addmpd( buf );  /* RMB: eliminate need for buf */
	    else {
		if ( strlen( buf ) > 1 ) {    /* newline already there */
		    sprintf( errbuf, "%s: %s", "invalid console buf\n", buf );
		    write_line( console_idx, errbuf );
		}
	    }
            sprintf( buf, "cmd=ack_from_mpd\n" );
            write_line( console_idx, buf );
	}
    }
    else {                        /* console gone away */
        mpdprintf( 0,
		   "eof on console fd; closing console fd %d idx=%d console_idx=%d\n",
		   fdtable[console_idx].fd, idx, console_idx ); 
	dclose( fdtable[idx].fd );
	deallocate_fdentry( idx ); 
	console_idx = -1;
    }
}

void handle_listener_input( int idx )
{
    int new_idx;
    
    mpdprintf( debug, "handling listener input, accept here\n" ); 
    new_idx = allocate_fdentry();
    fdtable[new_idx].fd      = accept_connection( fdtable[idx].fd );
    fdtable[new_idx].read    = 1;
    if ( connecting_to_self_as_lhs ) {
        strcpy( fdtable[new_idx].name, "lhs" );
        fdtable[new_idx].handler = LHS;
        strcpy( lhshost, mynickname );
	lhsport = my_listener_port;
	lhs_idx = new_idx;
	mpdprintf( debug, "set newconn as LHS\n" );
	connecting_to_self_as_lhs = 0;
	pulse_chkr = 0;
    }
    else {
        strcpy( fdtable[new_idx].name, "temp" );
        fdtable[new_idx].handler = NEWCONN;
    }
    mpdprintf( debug, "accepted new tmp connection on %d\n", fdtable[new_idx].fd ); 
}

void handle_console_listener_input( int idx )
{
    int new_idx;
    char buf[MAXLINE];

    mpdprintf( debug, "handling console listener input\n" );
    if ( console_idx == -1 ) {
	new_idx = allocate_fdentry();
	strcpy( fdtable[new_idx].name, "console" );
	fdtable[new_idx].fd      = accept_unix_connection( fdtable[idx].fd );
	fdtable[new_idx].read    = 1;
	fdtable[new_idx].write   = 0;
	fdtable[new_idx].handler = CONSOLE;
	mpdprintf( 0, "accepted new console connection on %d\n", fdtable[new_idx].fd );
	console_idx = new_idx;
	sprintf( buf, "cmd=version_check version=%d\n", MPD_VERSION );
	write_line( console_idx, buf );
    }
    else 
	mpdprintf( 0, "delaying new console connection\n" );
}

void handle_rhs_input( int idx )
{
    int n;
    char buf[MAXLINE], parse_buf[MAXLINE], cmd[MAXLINE];
 
    if ( allexiting )
	mpdprintf( debug, "ignoring eof on rhs since all mpd's are exiting\n" );
    else {
	buf[0] = '\0';
	n = read_line( fdtable[idx].fd, buf, MAXLINE );
	if ( n == 0 || (n == -1 && errno == EPIPE ) ) { /* EOF, next sib died */
	    mpdprintf( 0000, "next sibling died; reknitting ring: n=%d strerror=:%s:\n",
	               n, strerror(errno) );
	    syslog( LOG_INFO, "unexpected EOF on rhs" );
	    reknit_ring( idx );
	}
	else {
	    strcpy( parse_buf, buf );
	    mpd_parse_keyvals( parse_buf );
	    mpd_getval( "cmd", cmd );
	    if ( strcmp( cmd, "pulse_ack" ) == 0 )
	        pulse_chkr = 0;
	    else if ( strcmp( cmd, "rebuilding_the_ring" ) == 0 ) {
		mpdprintf( 0000, "received rebuilding message from rhs\n" );
		reknit_ring( idx );
	    }
	    else
		mpdprintf( 1, "handle_rhs_input: got n=%d unexpected msg=:%s:\n", n, buf );
	}
    }
}

void handle_monitor_input( int idx )
{
    char buf[MAXLINE], cmdval[MAXLINE], typeval[MAXLINE], monwhat[MAXLINE];

    mpdprintf( debug, "handling monitor input\n" );
    if ( read_line( fdtable[idx].fd, buf, MAXLINE ) != 0 ) {
	mpdprintf( debug, "got monitor information request :%s:\n", buf );
	mpd_parse_keyvals( buf );
	mpd_getval( "cmd", cmdval );
	if ( strcmp( cmdval, "moninfo_req" ) == 0 ) {
	    mpd_getval( "vals", typeval );
	    mpd_getval( "monwhat", monwhat );
	    sprintf( buf, "cmd=moninfo_req dest=anyone src=%s monwhat=%s vals=%s\n",
		     myid, monwhat, typeval );
	    write_line( rhs_idx, buf );
	}
	else if ( strcmp( cmdval, "moninfo_conn_close" ) == 0 ) {
	    dclose( fdtable[idx].fd );
	    deallocate_fdentry( idx );
	}
	else {
	    mpdprintf( 1, "unexpected monitor request = :%s:\n", cmdval );
	}
    }
    else {
        mpdprintf( 1, "lost contact with monitor\n" );
        dclose( fdtable[idx].fd );
        deallocate_fdentry( idx );
    }
}

void handle_manager_input( int idx )
{
    int jobid;
    char buf[MAXLINE], cmdval[MAXLINE];

    mpdprintf( debug, "handling manager input\n" );
    if ( read_line( fdtable[idx].fd, buf, MAXLINE ) != 0 ) {
        mpdprintf( debug, "mpd handling msg from manager :%s\n", buf );
	mpd_parse_keyvals( buf );
	mpd_getval( "cmd", cmdval );
	/* handle msg from manager */
	if (strcmp(cmdval,"killjob") == 0)
	{
	    jobid = atoi( mpd_getval( "jobid", buf ) );
	    mpdprintf( debug, "handle_manager_input:  sending killjob jobid=%d\n", jobid );
            sprintf( buf, "src=%s bcast=true cmd=killjob jobid=%d\n", myid, jobid );
            write_line( rhs_idx, buf );
	}
	else if (strcmp(cmdval,"terminating") == 0)
	{
	    jobid = atoi( mpd_getval( "jobid", buf ) );
	    mpdprintf( debug, "handle_manager_input:  got terminating from jobid=%d\n",
		       jobid );
            syslog( LOG_INFO, "job %d is terminating", jobid );
	}
	else if (strcmp(cmdval,"mandump_output") == 0)
	{
	    mpdprintf( 1, "mpd:  mandump_output not yet implemented\n" );
	}
	else
	    mpdprintf( 1, "mpd received unknown msg from manager :%s\n", buf );
    }
    else {                        /* manager gone away */
        mpdprintf( debug, "lost contact with manager %s\n", fdtable[idx].name );
        dclose( fdtable[idx].fd );
        deallocate_fdentry( idx );
    }
}

void handle_newconn_input( int idx )
{
    int n;
    char buf[MAXLINE], parse_buf[MAXLINE], cmdval[MAXLINE];
    
    buf[0] = '\0';
    n = read_line( fdtable[idx].fd, buf, MAXLINE );
    if ( n == 0 ) {
	mpdprintf( debug, "newconn died\n" );
	dclose( fdtable[idx].fd );
	deallocate_fdentry( idx ); 
	return;
    }
    mpdprintf( debug, "handling newconn msg=:%s:\n", buf ); 
    strcpy( parse_buf, buf );             
    mpd_parse_keyvals( parse_buf );
    mpd_getval( "cmd", cmdval );
    if ( strcmp( cmdval, "new_rhs_req" ) == 0 )
	newconn_new_rhs_req( idx );
    else if (strcmp( cmdval, "new_rhs" ) == 0 )
	newconn_new_rhs( idx );
    else if ( strcmp( cmdval, "new_lhs_req" ) == 0 )
	newconn_new_lhs_req( idx );
    else if ( strcmp( cmdval, "new_lhs" ) == 0 )
	newconn_new_lhs( idx );
    else if ( strcmp( cmdval, "challenge" ) == 0 )
	newconn_challenge( idx );
    else if ( strcmp( cmdval, "moninfo_conn_req" ) == 0 )
	newconn_moninfo_conn_req( idx );
    else if ( strcmp( cmdval, "new_moninfo_conn" ) == 0 )
	newconn_moninfo_conn( idx );
    else
	mpdprintf( 1, "invalid msg from newconn: msg=:%s:\n", buf );
}

void newconn_moninfo_conn_req( int idx )
{
    int version;
    struct timeval tv;
    char challenge_buf[MAXLINE], buf[MAXLINE];

    /* don't validate peer host, not doing it in newconn_new_rhs_req anyway */
    mpd_getval( "version", buf );
    version = atoi( buf );

    if ( version != MPD_VERSION ) {
	mpdprintf( 1, "got request for new monitoring connection, "
		   "with mismatched version %d, my version is %d\n",
		   version, MPD_VERSION ); 
    }
    else {
	mpdprintf( debug, "got cmd=moninfo_conn_req\n" ); 
	gettimeofday( &tv, ( struct timezone * ) 0 );
	srand( tv.tv_usec * 167.5 );
	fdtable[idx].rn = rand( );
	sprintf( challenge_buf,
		 "cmd=challenge dest=anyone rand=%d type=new_moninfo generation=%d\n",
		 fdtable[idx].rn, generation );
	write_line( idx, challenge_buf );
    }
}

void newconn_moninfo_conn( int idx )
{
    char buf[MAXLINE], encoded_num[16];

    mpd_getval( "encoded_num", encoded_num );
    /* validate response */
    encode_num( fdtable[idx].rn, buf );
    if ( strcmp( buf, encoded_num ) != 0 ) {
	/* response did not meet challenge */
	mpdprintf( 1, "response did not match challenge in newconn_moninfo_conn\n" );
	dclose( fdtable[idx].fd );
	deallocate_fdentry( idx );
	return;
    }
    /* make this port our monitoring port */ 
    mpdprintf( debug, "new monitoring connection successfully set up\n" );
    fdtable[idx].handler = MONITOR;
    mon_idx = idx;
    strncpy( fdtable[idx].name, "monitor", strlen( "monitor" ) + 1 );
    sprintf( buf, "cmd=moninfo_conn_ok\n" );
    write_line( idx, buf );
}

void newconn_challenge( int idx )
{
    char buf[MAXLINE], encoded_num[16], type[MAXLINE];
    int  challenge_num;

    mpd_getval( "rand", buf );
    mpd_getval( "type", type );
    challenge_num = atoi( buf );
    mpd_getval( "generation", buf );
    generation = atoi( buf );
    mpdprintf( debug, "setting generation to %d\n", generation );
    encode_num( challenge_num, encoded_num );
    sprintf( buf, "cmd=%s dest=anyone encoded_num=%s host=%s port=%d\n",
	     type, encoded_num, mynickname, my_listener_port );
    write_line( idx, buf );
    mpdprintf( debug, "newconn_challenge: sent response=:%s:\n",buf );
}

/* A new mpd enters the ring by connecting to the listener port of an existing mpd and
   sending it a new_rhs_req message.
*/
void newconn_new_rhs_req( int idx )
{
    /* validate new mpd attempting to enter the ring */
    int newport, version;
    mpd_sockopt_len_t salen;
    struct timeval tv;
    struct hostent *hp;
    struct sockaddr_in sa;
    char buf[MAXLINE], challenge_buf[MAXLINE], newhost[MAXHOSTNMLEN], *fromhost;

    mpd_getval( "port", buf ); 
    newport = atoi( buf );
    mpd_getval( "host", newhost ); 
    mpd_getval( "version", buf );
    version = atoi( buf );

    /* validate remote host */
    salen = sizeof( sa );
    /* AIX wants namelen to be size_t */
    if ( getpeername( fdtable[idx].fd, (struct sockaddr *) &sa, &salen ) != 0 ) {
	mpdprintf( 1, "getpeername failed: %s\n", strerror( errno ) );
    }
    fromhost = inet_ntoa( sa.sin_addr );
    hp = gethostbyaddr( (char *) &sa.sin_addr,sizeof( sa.sin_addr ),(int) sa.sin_family );
    if (hp == NULL)
	mpdprintf( 1, "Cannot get host info for %s", fromhost );
    else {
	fromhost = hp->h_name;
	mpdprintf( debug, "accepted connection from %s\n", fromhost );
    }
    /* Someday, check this host name or its address against list of approved hosts */

    if ( version != MPD_VERSION ) {
	mpdprintf( 1, "got request to enter ring from host %s, "
		   "with mismatched version %d, my version is %d\n",
		   fromhost, version, MPD_VERSION ); 
    }
    else {
	mpdprintf( debug, "got cmd=new_rhs_req host=%s port=%d\n", newhost, newport ); 
	gettimeofday( &tv, ( struct timezone * ) 0 );
	srand( tv.tv_usec * 167.5 );
	fdtable[idx].rn = rand( );
	sprintf( challenge_buf,
		 "cmd=challenge dest=anyone rand=%d type=new_rhs generation=%d\n",
		 fdtable[idx].rn, generation );
	write_line( idx, challenge_buf );
    }
}

void newconn_new_lhs_req( int idx )
{
    /* validate new mpd attempting to enter the ring */
    int newport;
    mpd_sockopt_len_t salen;
    struct timeval tv;
    struct hostent *hp;
    struct sockaddr_in sa;
    char buf[MAXLINE], challenge_buf[MAXLINE], newhost[MAXHOSTNMLEN], *fromhost;

    mpd_getval( "port", buf ); 
    newport = atoi( buf );
    mpd_getval( "host", newhost ); 

    /* validate remote host */
    salen = sizeof( sa );
    if ( getpeername( fdtable[idx].fd, (struct sockaddr *) &sa, &salen ) != 0 ) {
	mpdprintf( 1, "getpeername failed: %s\n", strerror( errno ) );
    }
    fromhost = inet_ntoa( sa.sin_addr );
    hp = gethostbyaddr( (char *) &sa.sin_addr,sizeof( sa.sin_addr ),(int) sa.sin_family );
    if (hp == NULL)
	mpdprintf( 1, "Cannot get host info for %s", fromhost );
    else {
	fromhost = hp->h_name;
	mpdprintf( debug, "accepted connection from %s\n", fromhost );
    }
    /* Someday, check this host name or its address against list of approved hosts */

    mpdprintf( debug, "got cmd=new_lhs_req host=%s port=%d\n", newhost, newport ); 
    gettimeofday( &tv, ( struct timezone * ) 0 );
    srand( tv.tv_usec * 167.5 );
    fdtable[idx].rn = rand( );
    sprintf( challenge_buf,
	     "cmd=challenge dest=anyone rand=%d type=new_lhs generation=%d\n",
	     fdtable[idx].rn, generation );
    write_line( idx, challenge_buf );
}

void newconn_new_rhs( int idx ) 
{
    int  newport;
    char buf[MAXLINE], new_rhs[MAXHOSTNMLEN], encoded_num[16];

    mpd_getval( "host", new_rhs ); 
    mpd_getval( "port", buf ); 
    newport = atoi( buf );
    mpd_getval( "encoded_num", encoded_num );
    mpdprintf( debug, "newconn_new_rhs: host=%s port=%d, encoded_num=%s\n",
	       new_rhs, newport, encoded_num ); 
    /* validate response */
    encode_num( fdtable[idx].rn, buf );
    if ( strcmp( buf, encoded_num ) != 0 ) {
	/* response did not meet challenge */
	mpdprintf( debug, "newconn_new_rhs:  rejecting new rhs connection\n" );
	dclose( fdtable[idx].fd );
	deallocate_fdentry( idx );
	return;
    }

    /* make this port our next sibling port */
    if ( rhs_idx != -1 ) {
	dclose( fdtable[rhs_idx].fd );
	deallocate_fdentry( rhs_idx );  /* dealloc old one */
    }

    rhs_idx = idx;                /* new one already alloced */
    fdtable[rhs_idx].portnum = newport;
    fdtable[rhs_idx].handler = RHS;
    strcpy(fdtable[rhs_idx].name,"next");
    fdtable[rhs_idx].read = 1;  /* in case of EOF, if he goes away */
    if ( strcmp( lhshost, mynickname ) == 0  &&  lhsport == my_listener_port ) {
        sprintf( buf, "src=%s dest=%s_%d cmd=reconnect_rhs rhshost=%s rhsport=%d rhs2host=%s rhs2port=%d\n",
                 myid, new_rhs, newport, rhshost, rhsport, new_rhs, newport );
    }
    else {
        sprintf( buf, "src=%s dest=%s_%d cmd=reconnect_rhs rhshost=%s rhsport=%d rhs2host=%s rhs2port=%d\n",
                 myid, new_rhs, newport, rhshost, rhsport, rhs2host, rhs2port );
    }
    write_line( rhs_idx, buf );
    strcpy( rhs2host, rhshost );   /* old rhs becomes rhs2 */
    rhs2port = rhsport;
    strcpy( rhshost, new_rhs );    /* install the new mpd */
    rhsport = newport;
    /***** next block is special case logic *****/
    if ( strcmp( lhshost, mynickname ) != 0  ||
         lhsport != my_listener_port )
    {
        sprintf( buf, "src=%s dest=%s_%d cmd=rhs2info rhs2host=%s rhs2port=%d\n",
                 myid, lhshost, lhsport, rhshost, rhsport );
        write_line( rhs_idx, buf );
    }
    /* Now that we have an rhs, we can initialize the jobid pool,
       which might require sending messages.
    */
    init_jobids();		/* protected from executing twice */
}

void newconn_new_lhs( idx )
int idx;
{
    int  newport;
    char buf[MAXLINE], new_lhs[MAXHOSTNMLEN], encoded_num[16];

    mpd_getval( "host", new_lhs ); 
    newport = atoi( mpd_getval( "port", buf ) ); 
    mpd_getval( "encoded_num", encoded_num );
    mpdprintf( debug, "got cmd=new_lhs host=%s port=%d, encoded_num=%s\n",
	       new_lhs, newport, encoded_num ); 
    /* validate response */
    encode_num( fdtable[idx].rn, buf );
    if ( strcmp( buf, encoded_num ) != 0 ) {
	/* response did not meet challenge */
	mpdprintf( debug, "newconn_new_lhs:  rejecting new lhs connection\n" );
	dclose( fdtable[idx].fd );
	deallocate_fdentry( idx );
	return;
    }

    if ( lhs_idx != -1 ) {
	dclose( fdtable[lhs_idx].fd );
	deallocate_fdentry( lhs_idx );  /* dealloc old one */
    }
    lhs_idx = idx;                /* new one already alloced */
    fdtable[lhs_idx].portnum = newport;
    fdtable[lhs_idx].handler = LHS;
    strcpy(fdtable[lhs_idx].name,"prev");
    fdtable[lhs_idx].read = 1;
    strcpy( lhshost,new_lhs );
    lhsport = newport;
    sprintf( buf, "src=%s dest=%s_%d cmd=rhs2info rhs2host=%s rhs2port=%d\n",
	     myid, lhshost, lhsport, rhshost, rhsport );
    write_line( rhs_idx, buf );
    if ( shutting_down ) {
        sprintf( buf, "cmd=req_perm_to_shutdown\n" );
        write_line( lhs_idx, buf );
    }
}

/* we come here because the "old" rhs has disappeared */
void reknit_ring( int old_rhs_idx )
{
    char in_buf[MAXLINE], out_buf[MAXLINE];
    int temp_port, temp_fd;

    mpdprintf( debug, "inside reknit_ring\n" );
    /*****
    if (chg_rhs_to_rhs2( old_rhs_idx ) == 0) {
	mpdprintf( debug, "successfully connected to rhs2\n" );
	return;
    }
    *****/
    dclose( fdtable[old_rhs_idx].fd );  /* RMB: only while above commented */
    deallocate_fdentry( old_rhs_idx );  /* RMB: only while above commented */

    mpdprintf( 0000, "reknit_ring: checking to see if should notify lhs\n" );
    if ( lhs_idx >= 0 )
    {
	/* send msg to current lhs telling him we need to rebuild the ring */
	mpdprintf( 0000, "sending first rebuilding message to lhs, lhs_idx=%d fd=%d\n",
		   lhs_idx, fdtable[lhs_idx].fd );
	write_line( lhs_idx, "cmd=rebuilding_the_ring\n" );	/* might fail! - RL */ 
	mpdprintf( 0000, "sent first rebuilding message to lhs, lhs_idx = %d fd=%d\n",
		   lhs_idx, fdtable[lhs_idx].fd );
	dclose( fdtable[lhs_idx].fd );
	deallocate_fdentry( lhs_idx );
	lhs_idx = -1;
    }
    pulse_chkr = 0;  /* useful ? */
    strcpy( lhshost, orig_lhshost );
    lhsport = orig_lhsport;
    if ( amfirst ) {
	/* reconnect to myself */
	temp_fd = setup_network_socket( &temp_port );
	mpdprintf( debug, "reconnecting to self at host=%s port=%d\n", lhshost, temp_port );
	lhs_idx                  = allocate_fdentry( );
	fdtable[lhs_idx].read    = 1;
	fdtable[lhs_idx].write   = 0;
	fdtable[lhs_idx].handler = LHS;
	fdtable[lhs_idx].fd      = network_connect( lhshost, temp_port );
	mpdprintf( debug, "connected to self at host=%s port=%d\n", lhshost, temp_port );
	fdtable[lhs_idx].portnum = lhsport;
	strncpy( fdtable[lhs_idx].name, lhshost, MAXSOCKNAMELEN );

        strncpy( rhshost, mynickname, MAXHOSTNMLEN );
        rhsport = my_listener_port;
        strncpy( rhs2host, mynickname, MAXHOSTNMLEN );
        rhs2port = my_listener_port;

	/* Send message to lhs, telling him to treat me as his new rhs */
	sprintf( out_buf, "dest=%s_%d cmd=new_rhs_req host=%s port=%d version=%d\n",
		 lhshost, lhsport, mynickname, my_listener_port, MPD_VERSION ); 
	mpdprintf( debug, "sending test message to self outbuf=:%s:", out_buf );        
	write_line( lhs_idx, out_buf );

        /* accept connection from self, done in "set up lhs fd" above */
        rhs_idx                  = allocate_fdentry();
        fdtable[rhs_idx].read    = 1;
        fdtable[rhs_idx].write   = 0;
        fdtable[rhs_idx].handler = RHS;
        fdtable[rhs_idx].fd      = accept_connection( temp_fd );
	mpdprintf( debug, "accepted connection from self rhs_idx=%d fd=%d\n",rhs_idx,fdtable[rhs_idx].fd );

        fdtable[rhs_idx].portnum = rhsport;
        strncpy( fdtable[rhs_idx].name, rhshost, MAXSOCKNAMELEN );
        read_line( fdtable[rhs_idx].fd, in_buf, MAXLINE );
	mpdprintf( debug, "received test message from self in_buf=:%s:\n", in_buf );
        /* check that it worked */
        if ( strncmp( in_buf, out_buf, strlen( out_buf ) ) ) {
             mpdprintf( 1, "reknit_ring: initial test message to self failed!\n" );
             exit( -1 );
        }
	generation++;
	mpdprintf( debug, "first mpd incrementing generation number to %d\n", generation );
	close(temp_fd);
    }
    else {
	/* connect to my original ring entry point as my new lhs */
	mpdprintf( debug, "connecting to original lhs\n" );
	lhs_idx                  = allocate_fdentry();
	fdtable[lhs_idx].read    = 1;
	fdtable[lhs_idx].write   = 0;
	fdtable[lhs_idx].handler = LHS;
	fdtable[lhs_idx].fd      = network_connect( lhshost, lhsport );
	fdtable[lhs_idx].portnum = lhsport;
	strncpy( fdtable[lhs_idx].name, lhshost, MAXSOCKNAMELEN );
	enter_ring( );  /* enter a new generation of the ring */
    }
    mpdprintf( debug, "exiting reknit_ring\n" );
}

int chg_rhs_to_rhs2( int idx )
{
    char buf[MAXLINE], parse_buf[MAXLINE], cmd[MAXLINE];

    syslog( LOG_INFO, "connecting around mpd on host %s, port %d", rhshost, rhsport );
    dclose( fdtable[idx].fd );
    mpdprintf( debug,"reconnecting to: %s_%d\n",rhs2host,rhs2port );
    if ( strcmp( rhs2host, mynickname ) == 0 && rhs2port == my_listener_port )
        connecting_to_self_as_lhs = 1;  /* reset in handler */
    fdtable[idx].fd      = network_connect( rhs2host, rhs2port );
    if ( fdtable[idx].fd == -1 ) {
        deallocate_fdentry( idx );
	return( -1 );
    }
    fdtable[idx].read    = 1;
    fdtable[idx].write   = 0;
    fdtable[idx].handler = RHS;
    strcpy( fdtable[idx].name, "rhs" );
    if ( connecting_to_self_as_lhs ) {
        strcpy( rhshost, rhs2host );
	rhsport = rhs2port;
	rhs_idx = idx;
	mpdprintf( debug, "set RHS to myself\n" );
    }
    else {
        sprintf( buf, "src=%s dest=%s_%d cmd=new_lhs_req host=%s port=%d\n",
	         myid, rhs2host, rhs2port, mynickname, my_listener_port );
        write_line( idx, buf );
	if ( read_line( fdtable[idx].fd, buf, MAXLINE ) < 0 )
	    return(-1);
        strcpy( parse_buf, buf );
        mpd_parse_keyvals( parse_buf );
        mpd_getval( "cmd", cmd );
        if ( strcmp( cmd, "challenge" ) != 0 ) {
	    mpdprintf( 1, "handle_rhs_input: expecting challenge, got %s\n", buf );
	    exit( -1 );
        }
        newconn_challenge( idx );
        /* special case logic */
        if ( strcmp( lhshost, rhshost ) != 0  ||  lhsport != rhsport ) {
	    sprintf( buf, "src=%s dest=%s_%d cmd=rhs2info rhs2host=%s rhs2port=%d\n",
		     myid, lhshost, lhsport, rhs2host, rhs2port );
	    if ( write_line( idx, buf ) < 0 )
		return(-1);
        }
        strcpy( rhshost, rhs2host );
        rhsport = rhs2port;
        rhs_idx = idx;
    }
    pulse_chkr = 0;
    return( 0 );
}

#ifdef NEED_CRYPT_PROTOTYPE
extern char *crypt (const char *, const char *);
#endif

void encode_num( int rn, char *buf )
{
    char tempbuf[PASSWDLEN+32];

    sprintf( tempbuf, "%s%d", mpd_passwd, rn );
    strcpy( buf, crypt( tempbuf, "el" ) );
}
