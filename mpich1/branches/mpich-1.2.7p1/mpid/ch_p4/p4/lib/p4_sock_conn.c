#include "p4.h"
#include "p4_sys.h"

#ifdef P4_WITH_MPD
#if defined(USE_STDARG)
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#ifdef HAVE_ARPA_INET_H
/* prototype for inet_ntoa() */
#include <arpa/inet.h>
#endif

#ifdef P4_WITH_MPD
/* declarations for routines needed to process messages from an MPD manager */
void p4_printf( int, char *, ... );
int  p4_read_line( int, char *, int );
int  p4_parse_keyvals( char * );
void p4_dump_keyvals( void );
char *p4_getval( char *, char * );
void p4_chgval( char *, char * );
void p4_stuff_arg( char *, char * );
void p4_destuff_arg( char *, char * );
#endif

#define P4_MAXLINE 4096

/* data structures for parsing messages from MPD manager */
struct p4_keyval_pairs
{
    char key[32];
    char value[P4_MAXLINE];	
};

struct p4_keyval_pairs p4_keyval_tab[64];
int p4_keyval_tab_idx;
#endif /* P4_WITH_MPD */

/* This routine gies us a way to timeout a loop */
#include <sys/time.h>
#ifndef TIMEOUT_VALUE 
#define TIMEOUT_VALUE 300
#endif
static int p4_timeout_value = TIMEOUT_VALUE;

int p4_has_timedout( flag )
int flag;
{
    static time_t start_time;
    time_t curtime;
    curtime = time((time_t)0);
    if (flag) {
	if (curtime - start_time > p4_timeout_value) return 1;
    }
    else
	start_time = curtime;
    return 0;
}

int p4_establish_all_conns()
{
    int myid = p4_get_my_id();
    int i;

    for (i = 0; i < p4_global->num_in_proctable; i++) {
	if ( i > myid ) {
	    if (!in_same_cluster(i,myid)) {
		if (p4_local->conntab[i].type == CONN_REMOTE_NON_EST) {
		    p4_dprintfl(20,"establishing early connection to %d\n",i);
		    establish_connection(i);
		}
	    }
	}
    }
    return 0;
}

/* see p4_sock_list.c for the thread version */
#ifndef THREAD_LISTENER

#ifdef P4_WITH_MPD
int establish_connection(int dest_id)
{
    int myid = p4_get_my_id();
    int new_listener_port, new_listener_fd, connection_fd;
    char buf[1024];
    struct timeval tv;
    fd_set readfds;
    int numfds, rc;
    struct hostent *hp;
    struct in_addr in;
    char *c_inetaddr;

    p4_dprintfl(077, "p4's estab_connection: trying dest_id=%d my_id=%d\n", dest_id, myid);

    p4_global->dest_id[myid] = dest_id;  /* block interrupt handler */
    /* if already done by interrupt handler */
    if (p4_local->conntab[dest_id].type == CONN_REMOTE_EST)
    {
	p4_global->dest_id[myid] = (-1);
	return(P4_TRUE);
    }

    net_setup_anon_listener(1, &new_listener_port, &new_listener_fd);
    hp = gethostbyname(p4_global->my_host_name);
    if (hp == NULL)
    {
	/* printf("connect_to_server: gethostbyname %s: %s -- exiting\n",
	   p4_global->my_host_name, sys_errlist[errno]); */
	exit(99);
    }
    
    memcpy( &in.s_addr, hp->h_addr, sizeof(in.s_addr) );
    c_inetaddr = (char *)inet_ntoa( in );
    /* c_inetaddr = (char *)inet_ntoa( (int) *((int*)(hp->h_addr)) ); */

    /* mpdman adds a newline to this msg before passing it down; id is rank */ 
    sprintf(buf,"connect_to_me-%d-%s-%d",p4_local->my_id,c_inetaddr,new_listener_port);

    p4_dprintfl(077, "calling p4_poke_client; destid=%d\n",dest_id);
    rc = BNR_Poke_peer( p4_local->my_job,dest_id,buf );  /* job is groupid for now */
    while (1)
    {
        FD_ZERO( &readfds );
        FD_SET( new_listener_fd, &readfds );
        numfds = new_listener_fd + 1;
	if (p4_local->conntab[dest_id].type == CONN_REMOTE_EST)
	{
	    p4_dprintfl(077,"p4's estab_conn: return pt 1; already conn'd\n");
	    p4_global->dest_id[myid] = (-1);
	    return(P4_TRUE);
	}
        tv.tv_sec = 0;
        tv.tv_usec = 10000;
	p4_dprintfl(077,"p4's estab_conn: trying select\n");
        rc = select( numfds, &readfds, NULL, NULL, &tv );
	p4_dprintfl(077,"p4's estab_conn: past select rc=%d\n",rc);
        if ( ( rc == -1 ) && ( errno == EINTR ) )
	{
            continue;
	}
        else if ( rc < 0 )
	{
	    char errmsg[80];
	    sprintf( errmsg, "[%d] establish_connection: select: %d", p4_get_my_id(), rc );
	    perror( errmsg );
	    exit( -1 );
	}
        else if ( rc == 0 )    /* if timed out */
	{
	    p4_dprintfl(077, "select timed out after %ld useconds\n", tv.tv_usec );
	    /* if already done by interrupt handler */
	    if (p4_local->conntab[dest_id].type == CONN_REMOTE_EST)
	    {
		p4_dprintfl(077,"p4's estab_conn: return pt 2; already conn'd\n");
		p4_global->dest_id[myid] = (-1);
		return(P4_TRUE);
	    }
	    else
	    {
		continue;
	    }
	}
        else if ( FD_ISSET( new_listener_fd, &readfds ) )
        {
	    connection_fd = net_accept(new_listener_fd);
	    break;
        }
    }

    p4_local->conntab[dest_id].type = CONN_REMOTE_EST;
    p4_local->conntab[dest_id].port = connection_fd;
    p4_local->conntab[dest_id].same_data_rep = P4_TRUE;
    p4_global->dest_id[myid] = (-1);
    p4_dprintfl(077, "p4's estab_connection: got  dest_id=%d my_id=%d port=%d\n",
		 dest_id, myid, p4_local->conntab[dest_id].port);
    return(P4_TRUE);
}
/*
 * This routine may be called by the thread listener after marking the 
 * connection as opening.  The second argument is used to indicate this
 * case.
 */
P4VOID request_connection(int dest_id)
{
    int my_id;
    struct slave_listener_msg msg;
    int connection_fd;
    int dest_listener_con_fd;
    int new_listener_port, new_listener_fd;
    int i, num_tries;
    char buf[P4_MAXLINE], charport[8], charpid[8], cmd[32], host[MAXHOSTNAMELEN];
    int pid, port, status;
    P4_BLOCK_SIG_DECL;

    p4_dprintfl( 50, "entering req_conn; dest_id=%d\n", dest_id );

    P4_BLOCK_SIG(LISTENER_ATTN_SIGNAL);

    /* Get some initial information */
    my_id = p4_get_my_id();

    /* Have we already connected?? */
    if (p4_local->conntab[dest_id].type == CONN_REMOTE_EST)
    {
	p4_dprintfl(70,"request_connection %d: already connected\n", dest_id);
	P4_RELEASE_SIG(LISTENER_ATTN_SIGNAL);
	return;
    }

    /* find destination listener from our parent mpdman */

    for (i=0; i < 5; i++) {
        p4_dprintfl( 70, "%d: Tell parent I need to talk to %d\n", my_id, dest_id );
	sprintf( buf, "cmd=findclient job=%d rank=%d\n",
		 p4_local->my_job, dest_id );
	write( p4_local->parent_man_fd, buf, strlen(buf) );
	status = p4_read_line( p4_local->parent_man_fd, buf, 256 ); /* Note client hanging */

	p4_dprintfl( 70, "%d: Reply from parent mpdman, buf=:%s:, status=%d\n",
		     my_id, buf, status );
	if (status <= 0)
	{
	    p4_dprintf( "request_conn: invalid status from parent; status=%d \n", status );
	    p4_error( "request_conn: invalid status from read_file for msg from mpdman", -1 );
	}
	p4_parse_keyvals( buf );
	p4_getval( "cmd", cmd );
        if ( strcmp( cmd, "foundclient" ) != 0 )
	{
	    p4_dprintf("recvd :%s: when expecting foundclient\n",cmd);
	    p4_error( "invalid msg from mpdman", -1 );
	}
	p4_getval( "host", host );
	p4_getval( "port", charport );
	port = atoi( charport );
	if ( port > 0 )
	    break;
	else {
	    sleep(1);
	    p4_dprintfl( 70, "Trying again to get port for destination listener\n" );
	}
    }
    if (port < 0)
	    p4_error( "couldn't get port for destination listener", port );
    p4_getval( "pid", charpid );
    pid = atoi( charpid );

    p4_dprintfl( 70, "located job=%d rank=%d at host=%s port=%d pid=%d\n",
	       p4_local->my_job, p4_local->my_id, host, port, pid);

    p4_dprintfl(70, "enter loop to connect to dest listener %s\n",host);
    /* Connect to dest listener */
    num_tries = 1;
    p4_has_timedout( 0 );
    while((dest_listener_con_fd = net_conn_to_listener(host,port,1)) == -1) {
	num_tries++;
	if (p4_has_timedout( 1 )) {
	    p4_error( "Timeout in establishing connection to remote process", 0 );
	}
    }
    p4_dprintfl(70,
		"request_connection: connected after %d tries, dest_listener_con_fd=%d\n",
		num_tries, dest_listener_con_fd);

    /* Setup a listener to accept the connection to dest_id */
    net_setup_anon_listener(1, &new_listener_port, &new_listener_fd);

    /* Construct a connection request message */
    msg.type = p4_i_to_n(CONNECTION_REQUEST);
    msg.from = p4_i_to_n(my_id);
    msg.lport = p4_i_to_n(new_listener_port);
    msg.to = p4_i_to_n(dest_id);
    msg.to_pid = p4_i_to_n(pid);
    strcpy(msg.hostname,p4_global->my_host_name);

    /* Send it to dest_id's listener */
    p4_dprintfl(70,
		"request_connection: sending CONNECTION_REQUEST to %d on fd=%d size=%d\n",
		dest_id,dest_listener_con_fd,sizeof(msg));
    net_send(dest_listener_con_fd, &msg, sizeof(msg), P4_FALSE);
    p4_dprintfl(70, "request_connection: sent CONNECTION_REQUEST to dest_listener\n");

    if (my_id < dest_id)
    {
	/* Wait for the remote process to connect to me */
	p4_dprintfl(70,
		    "request_connection: waiting for accept from %d on fd=%d, port=%d\n",
		    dest_id, new_listener_fd, new_listener_port);

	/* This needs a timeout? ???  */
	connection_fd = net_accept(new_listener_fd);
	p4_dprintfl(70, "request_connection: accepted from %d on %d\n", dest_id, connection_fd);

	/* Add the connection to the table */
	p4_local->conntab[dest_id].port = connection_fd;
	p4_local->conntab[dest_id].same_data_rep = P4_TRUE;
        /***** out for mpd
	p4_local->conntab[dest_id].same_data_rep = 
	    same_data_representation(p4_local->my_id,dest_id);
        ****/
	/* Requires write ordering in the thread */
	p4_local->conntab[dest_id].type = CONN_REMOTE_EST;
    }

    close(dest_listener_con_fd);
    /* Now release the listener connections */
    close(new_listener_fd);

    P4_RELEASE_SIG(LISTENER_ATTN_SIGNAL);

    p4_dprintfl(70, "request_connection: finished connecting\n");
    return;
}

/* sig isn't used except to match the function prototypes for POSIX
   signal handlers */
P4VOID handle_connection_interrupt( int sig )
{
    struct slave_listener_msg msg;
    int type;
    int listener_fd;
    int to, to_pid, from, lport;
    int connection_fd;
    int myid = p4_get_my_id();
    int num_tries;

    p4_dprintfl(70, "Inside handle_connection_interrupt fd=%d\n",p4_local->listener_fd);
    listener_fd = p4_local->listener_fd;

#ifdef USE_NONBLOCKING_LISTENER_SOCKETS
    /* This parameter gives the number of attempts to read before 
       deciding that something has gone wrong */
#    define MAX_DRY_ITERATIONS 1000000
    /*
     * Must read non-blocking due to race conditions with using
     * signals as IPC mechanism.  See the fcntl near get_pipe where
     * these are created.
     *
     * However, this should not loop endlessly.  If a signal has been 
     * delivered, the listener is trying to talk to us.  To catch
     * failures in the listener or other logic (for example, not all of the
     * listener_fd's were properly set in the 1.2.3 release of MPICH).
     */
    { 
	int it_count = 0;
	for (;;) {
	    int cc = read(listener_fd, &msg, sizeof(msg));
	    if (cc == 0)
		p4_error("handle_connection_interrupt: EOF from listener", 0);
	    if (cc < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
		    it_count ++;
		    if (it_count > MAX_DRY_ITERATIONS) {
			p4_error("handled_connection_interrupt: listener is not sending", -1 );
		    }
		    continue;
		}
		p4_error("handle_connection_interrupt: read listener", cc);
	    }
	    /* these should be atomic: AF_UNIX, AF_STREAM */
	    if (cc != sizeof(msg))
		p4_error("handle_connection_interrupt: short read from listener", 0);
	    break;
	}
    }
#else
    if (net_recv(listener_fd, &msg, sizeof(msg)) == PRECV_EOF)
    {
	p4_dprintf("OOPS: got eof in handle_connection_interrupt\n");
	return;
    }
#endif /* USE_NONBLOCKING_LISTENER_SOCKETS */
    type = p4_n_to_i(msg.type);
    if (type != CONNECTION_REQUEST)
    {
	p4_dprintf("handle_connection_interrupt: invalid type %d\n", type);
	return;
    }

    to = p4_n_to_i(msg.to);
    from = p4_n_to_i(msg.from);
    to_pid = p4_n_to_i(msg.to_pid);
    lport = p4_n_to_i(msg.lport);

    p4_dprintfl(70, "handle_connection_interrupt: msg contents: to=%d from=%d to_pid=%d lport=%d\n",
		to, from, to_pid, lport);

    /* If we're already connected, forget about the interrupt. */
    if (p4_local->conntab[from].type != CONN_REMOTE_EST)
    {
	if (myid < from)
	{
	    /* see if I have already started this connection */
	    p4_dprintfl(90,"myid < from, myid = %d, from = %d\n",myid,from);
	    if (p4_global->dest_id[myid] != from)
		request_connection(from);
	}
	else
	{
	    /* Get the information for the process we're connecting to */

	    /* Connect to the waiting process */
	    p4_dprintfl(70, "connecting to port...\n");
	    num_tries = 1;
	    /* connect to the requesting process, who is listening */
	    p4_dprintfl(70,"handling connection interrupt: connecting to %s port=%d\n",
			msg.hostname,lport);
	    p4_has_timedout( 0 );
	    while ((connection_fd = net_conn_to_listener(msg.hostname,lport,1)) == -1) {
		num_tries++;
		if (p4_has_timedout( 1 )) {
		    p4_error( "Timeout in establishing connection to remote process", 0 );
		    }
		}

	    p4_dprintfl(70, "handling connection interrupt: connected after %d tries, connection_fd=%d host = %s\n",
			num_tries, connection_fd, msg.hostname);

	    /* We're connected, so we can add this connection to the table */
	    p4_local->conntab[from].port = connection_fd;
	    p4_local->conntab[from].same_data_rep = P4_TRUE;
	    /***
	    p4_local->conntab[from].same_data_rep = 
		same_data_representation(p4_local->my_id,from);
		***/
	    /* Note that this requires write ordering in the threads */
	    p4_local->conntab[from].type = CONN_REMOTE_EST;
	    p4_dprintfl(70, "marked as established fd=%d from=%d\n",
			connection_fd, from);
	}
    }
    else
    {
	p4_dprintfl(70,"ignoring interrupt from %d\n",from);
    }

    msg.type = p4_i_to_n(IGNORE_THIS);
    p4_dprintfl(70, "handle_connection_interrupt: sending IGNORE_THIS to my_listener\n");
    /* send msg to listener indicating I made the connection */
    net_send(listener_fd, &msg, sizeof(msg), P4_FALSE);
    p4_dprintfl(70, "handle_connection_interrupt: exiting handling intr from %d\n",from);
    
    /* If the return from this is SIG_DFL, then there is a problem ... */
    SIGNAL_P4(LISTENER_ATTN_SIGNAL, handle_connection_interrupt);
}

#else /* P4_WITH_MPD */
/* This is the p4 *without* mpd branch */
int establish_connection(int dest_id)
{
    int myid = p4_get_my_id();

    p4_global->dest_id[myid] = dest_id;
    request_connection(dest_id);
    p4_global->dest_id[myid] = (-1);

    if (myid > dest_id)
    {
	/* following should not spin long */
        p4_has_timedout( 0 );
	/* If threaded, we should wait for the message from the thread,
	   rather than spin here */
	p4_dprintfl(70, "waiting for interrupt handler to do its job\n");
	while (p4_local->conntab[dest_id].type != CONN_REMOTE_EST) {
	    p4_dprintfl(111, "waiting in loop for interrupt handler to do its job\n");
	    if (p4_has_timedout( 1 )) {
		p4_error( "Timeout in establishing connection to remote process", 0 );
		}
	    }
	p4_dprintfl(70, "interrupt handler succeeded\n");
    }
    return (P4_TRUE);
}
/*
 * This routine may be called by the thread listener after marking the 
 * connection as opening.  The second argument is used to indicate this
 * case.
 */
P4VOID request_connection(int dest_id)
{
    struct proc_info *my_pi, *dest_pi;
    char *my_host, *dest_host;
    int my_id;
    struct slave_listener_msg msg;
    int connection_fd;
    int dest_listener_con_fd;
    int my_listener, dest_listener;
    int new_listener_port, new_listener_fd;
    int num_tries;
    P4_BLOCK_SIG_DECL;

    P4_BLOCK_SIG(LISTENER_ATTN_SIGNAL);

    /* Get some initial information */
    my_id = p4_get_my_id();
    my_pi = get_proc_info(my_id);
    my_host = my_pi->host_name;
    my_listener = my_pi->port;

    dest_pi = get_proc_info(dest_id);
    dest_host = dest_pi->host_name;
    dest_listener = dest_pi->port;

    p4_dprintfl(70, "request_connection: my_id=%d my_host=%s my_listener=%d dest_id=%d dest_host=%s dest_listener=%d\n",
	    my_id, my_host, my_listener, dest_id, dest_host, dest_listener);

    /* Have we already connected?? */
    if (p4_local->conntab[dest_id].type == CONN_REMOTE_EST)
    {
	p4_dprintfl(70,"request_connection %d: already connected\n", dest_id);
	P4_RELEASE_SIG(LISTENER_ATTN_SIGNAL);
	return;
    }

    p4_dprintfl(70, "enter loop to connect to dest listener %s\n",dest_host);
    /* Connect to dest listener */
    num_tries = 1;
    p4_has_timedout( 0 );
    while((dest_listener_con_fd = net_conn_to_listener(dest_host,dest_listener,1)) == -1) {
	num_tries++;
	if (p4_has_timedout( 1 )) {
	    p4_error( "Timeout in establishing connection to remote process", 0 );
	    }
	}
    p4_dprintfl(70, "conn_to_proc_contd: connected after %d tries, dest_listener_con_fd=%d\n",num_tries, dest_listener_con_fd);


    /* Setup a listener to accept the connection to dest_id */
    net_setup_anon_listener(1, &new_listener_port, &new_listener_fd);

    /* Construct a connection request message */
    msg.type = p4_i_to_n(CONNECTION_REQUEST);
    msg.from = p4_i_to_n(my_id);
    msg.lport = p4_i_to_n(new_listener_port);
    msg.to = p4_i_to_n(dest_id);
    msg.to_pid = p4_i_to_n(dest_pi->unix_id);

    /* Send it to dest_id's listener */
    p4_dprintfl(70, "request_connection: sending CONNECTION_REQUEST to %d on fd=%d size=%d\n",
		dest_id,dest_listener_con_fd,sizeof(msg));
    net_send(dest_listener_con_fd, &msg, sizeof(msg), P4_FALSE);
    p4_dprintfl(70, "request_connection: sent CONNECTION_REQUEST for %d (pid %d) to dest_listener on fd %d\n", dest_id, dest_pi->unix_id, dest_listener_con_fd);

    if (my_id < dest_id)
    {
	/* Wait for the remote process to connect to me */
	p4_dprintfl(70, "request_connection: waiting for accept from %d on fd=%d, port=%d\n",
		    dest_id, new_listener_fd, new_listener_port);

	/* This needs a timeout? ???  */
	connection_fd = net_accept(new_listener_fd);
	p4_dprintfl(70, "request_connection: accepted from %d on %d\n", dest_id, connection_fd);

	/* Add the connection to the table */
	p4_local->conntab[dest_id].port = connection_fd;
	p4_local->conntab[dest_id].same_data_rep =
	    same_data_representation(p4_local->my_id,dest_id);
	/* Requires write ordering in the thread */
	p4_local->conntab[dest_id].type = CONN_REMOTE_EST;
    }

    close(dest_listener_con_fd);
    /* Now release the listener connections */
    close(new_listener_fd);

    P4_RELEASE_SIG(LISTENER_ATTN_SIGNAL);

    p4_dprintfl(70, "request_connection: finished connecting\n");
    return;
}

/* sig isn't used except to match the function prototypes for POSIX
   signal handlers */
P4VOID handle_connection_interrupt( int sig )
{
    struct slave_listener_msg msg;
    int type;
    int listener_fd;
    int to, to_pid, from, lport;
    int connection_fd;
    struct proc_info *from_pi;
    int myid = p4_get_my_id();
    int num_tries;
    static int in_handler = 0;

    /* There is a small chance that we'll be in the handler when another 
       signal is delivered.  Since the listener will send a signal 
       every .1 seconds until there is a response, we can simply return.
       This test does have a race condition, but it is very small, and
       we're going to ignore it.

       In fact, if signal blocking works correctly, we should *never* 
       enter the handler while we are already in it.  Perhaps this
       test should abort if in_handler?
    */
    if (in_handler) return;
    in_handler = 1;

    listener_fd = p4_local->listener_fd;
    p4_dprintfl(70, "Inside handle_connection_interrupt, listener_fd=%d\n",
		listener_fd);

#ifdef USE_NONBLOCKING_LISTENER_SOCKETS
    /* This parameter gives the number of attempts to read before 
       deciding that something has gone wrong */
#    define MAX_DRY_ITERATIONS 1000000
    /*
     * Must read non-blocking due to race conditions with using
     * signals as IPC mechanism.  See the fcntl near get_pipe where
     * these are created.
     *
     * However, this should not loop endlessly.  If a signal has been 
     * delivered, the listener is trying to talk to us.  To catch
     * failures in the listener or other logic (for example, not all of the
     * listener_fd's were properly set in the 1.2.3 release of MPICH).
     */
    { 
	int it_count = 0;
	for (;;) {
	    int cc = read(listener_fd, &msg, sizeof(msg));
	    if (cc == 0)
		p4_error("handle_connection_interrupt: EOF from listener", 0);
	    if (cc < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
		    it_count ++;
		    if (it_count > MAX_DRY_ITERATIONS) {
			/* Temporary */
			/* 
			 printf( "zillion iterations, resetting\n"); 
			 it_count = 0; 
 			p4_error("handled_connection_interrupt: listener is not sending", -1 );   
			*/
			/* give up; the listener will try again */
			in_handler = 0;
			return;
			
		    }
		    continue;
		}
		p4_error("handle_connection_interrupt: read listener", cc);
	    }
	    /* these should be atomic: AF_UNIX, AF_STREAM */
	    if (cc != sizeof(msg))
		p4_error("handle_connection_interrupt: short read from listener", 0);
	    break;
	}
    }
#else
    if (net_recv(listener_fd, &msg, sizeof(msg)) == PRECV_EOF)
    {
	p4_dprintf("OOPS: got eof in handle_connection_interrupt\n");
	in_handler = 0;
	return;
    }
#endif 

    type = p4_n_to_i(msg.type);

    if (type == WAKEUP_SLAVE) {
	/* Ignore and return.  This may be a poke for a message that
	   we've already processed.  In case these wakeups have
	   piled up, we try to read again, and then only if there is
	   nothing, do we return */
#ifdef USE_NONBLOCKING_LISTENER_SOCKETS
	int cc;
	while ((cc = read(listener_fd, &msg, sizeof(msg))) > 0) {
	    if (cc != sizeof(msg))
		p4_error("handle_connection_interrupt: short read from listener", 0);
	    type = p4_n_to_i(msg.type);
	    if (type != WAKEUP_SLAVE) break;
	}
	if (cc <= 0) {
	    in_handler = 0;
	    return;
	}
	/* Otherwise, drop through with the new message */
#else
	in_handler = 0;
	return;
#endif
    }

    if (type == KILL_SLAVE) {
        msg.type = p4_i_to_n(IGNORE_THIS);
        p4_dprintfl(70, "handle_connection_interrupt: sending IGNORE_THIS to my_listener\n");
        /* send msg to listener indicating I made the connection */
        net_send(listener_fd, &msg, sizeof(msg), P4_FALSE);
	p4_dprintfl(99, "handle_connection_interrupt: exiting due to DIE msg\n");
	/* Try to clean up first, then exit.  This tries to handle the
	   cases that can leave SYSV IPC's around, along with 
	   ensuring that sockets are shut down */
	/* shutdown(sock,2), close(sock) all sockets */
#	ifdef CAN_DO_SOCKET_MSGS
	shutdown_p4_socks();
#	endif

#	ifdef SYSV_IPC
	remove_sysv_ipc();
#	endif

#	if defined(SGI)  &&  defined(VENDOR_IPC)
	unlink(p4_sgi_shared_arena_filename);
#	endif
	p4_clean_execer_port();
	exit(0);
    }

    if (type != CONNECTION_REQUEST)
    {
	p4_dprintf("handle_connection_interrupt: invalid type %d\n", type);
	in_handler = 0;
	return;
    }

    to = p4_n_to_i(msg.to);
    from = p4_n_to_i(msg.from);
    to_pid = p4_n_to_i(msg.to_pid);
    lport = p4_n_to_i(msg.lport);

    p4_dprintfl(70, "handle_connection_interrupt: msg contents: to=%d from=%d to_pid=%d lport=%d\n",
		to, from, to_pid, lport);

    /* If we're already connected, forget about the interrupt. */
    if (p4_local->conntab[from].type != CONN_REMOTE_EST)
    {
	if (myid < from)
	{
	    /* see if I have already started this connection */
	    p4_dprintfl(90,"myid < from, myid = %d, from = %d\n",myid,from);
	    if (p4_global->dest_id[myid] != from)
		request_connection(from);
	}
	else
	{
	    /* Get the information for the process we're connecting to */
	    from_pi = &(p4_global->proctable[from]);

	    /* Connect to the waiting process */
	    p4_dprintfl(70, "connecting to port...\n");
	    num_tries = 1;
	    /* connect to the requesting process, who is listening */
	    p4_dprintfl(70,"handling connection interrupt: connecting to %s\n",from_pi->host_name);
	    p4_has_timedout( 0 );
	    while ((connection_fd = net_conn_to_listener(from_pi->host_name,lport,1)) == -1) {
		num_tries++;
		if (p4_has_timedout( 1 )) {
		    p4_error( "Timeout in establishing connection to remote process", 0 );
		    }
		}

	    p4_dprintfl(70, "handling connection interrupt: connected after %d tries, connection_fd=%d host = %s\n",
			num_tries, connection_fd, from_pi->host_name);

	    /* We're connected, so we can add this connection to the table */
	    p4_local->conntab[from].port = connection_fd;
	    p4_local->conntab[from].same_data_rep =
		same_data_representation(p4_local->my_id,from);
	    /* Note that this requires write ordering in the threads */
	    p4_local->conntab[from].type = CONN_REMOTE_EST;
	    p4_dprintfl(70, "marked as established fd=%d from=%d\n",
			connection_fd, from);
	}
    }
    else
    {
	p4_dprintfl(70,"ignoring interrupt from %d\n",from);
    }

    msg.type = p4_i_to_n(IGNORE_THIS);
    p4_dprintfl(70, "handle_connection_interrupt: sending IGNORE_THIS to my_listener\n");
    /* send msg to listener indicating I made the connection */
    net_send(listener_fd, &msg, sizeof(msg), P4_FALSE);
    p4_dprintfl(70, "handle_connection_interrupt: exiting handling intr from %d\n",from);
    
    /* If the return from this is SIG_DFL, then there is a problem ... */
    /* The following re-establishes the signal handler, which is needed on
       systems where the handler is reset to SIG_DFL when it is triggered.
       Such systems are *broken*, since there is no good way to avoid the
       resulting race conditions.  Unfortunately, we must work with those 
       systems.  

       We could reset the signal handler only on systems that require it.
       However, this should be safe for all cases. 
    */
    SIGNAL_P4(LISTENER_ATTN_SIGNAL, handle_connection_interrupt);

    in_handler = 0;
}
#endif

#endif /* THREAD_LISTENER */

#ifdef P4_WITH_MPD
/* routines copied from MPD (mpdlib.c) and renamed as p4 routines.  These are
   all that is necessary to communicate with the manager */
/* FIXME: These should be part of the BNR library, not P4. */


void p4_printf( int print_flag, char *fmt, ... )
{
    va_list ap;

    if (print_flag) {
	fprintf( stderr, "[%s]: ", whoami_p4 );
	va_start( ap, fmt );
	vfprintf( stderr, fmt, ap );
	va_end( ap );
	fflush( stderr );
    }
}

int p4_read_line( int fd, char *buf, int maxlen )
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


int p4_parse_keyvals( char *st )
{
    char *p, *keystart, *valstart;

    if ( !st )
	return( -1 );

    p4_keyval_tab_idx = 0;          
    p = st;
    while ( 1 ) {
	while ( *p == ' ' )
	    p++;
	/* got non-blank */
	if ( *p == '=' ) {
	    p4_printf( 1, "p4_parse_keyvals:  unexpected = at character %d in %s\n",
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
	    p4_printf( 1,
		       "p4_parse_keyvals: unexpected key delimiter at character %d in %s\n",
		       p - st, st );
	    return( -1 );
	}
        strncpy( p4_keyval_tab[p4_keyval_tab_idx].key, keystart, p - keystart );
	p4_keyval_tab[p4_keyval_tab_idx].key[p - keystart] = '\0'; /* store key */

	valstart = ++p;			/* start of value */
	while ( *p != ' ' && *p != '\n' && *p != '\0' )
	    p++;
        strncpy( p4_keyval_tab[p4_keyval_tab_idx].value, valstart, p - valstart );
	p4_keyval_tab[p4_keyval_tab_idx].value[p - valstart] = '\0'; /* store value */
	p4_keyval_tab_idx++;
	if ( *p == ' ' )
	    continue;
	if ( *p == '\n' || *p == '\0' )
	    return( 0 );	/* value has been set to empty */
    }
}
 
void p4_dump_keyvals()
{
    int i;
    for (i=0; i < p4_keyval_tab_idx; i++) 
	p4_printf(1, "  %s=%s\n",p4_keyval_tab[i].key, p4_keyval_tab[i].value);
}

char *p4_getval( char *keystr, char *valstr )
{
    int i;

    for (i=0; i < p4_keyval_tab_idx; i++) {
       if ( strcmp( keystr, p4_keyval_tab[i].key ) == 0 ) { 
	    strcpy( valstr, p4_keyval_tab[i].value );
	    return valstr;
       } 
    }
    valstr[0] = '\0';
    return NULL;
}

void p4_chgval( char *keystr, char *valstr )
{
    int i;

    for ( i = 0; i < p4_keyval_tab_idx; i++ ) {
       if ( strcmp( keystr, p4_keyval_tab[i].key ) == 0 )
	    strcpy( p4_keyval_tab[i].value, valstr );
    }
}


#define     END ' '
#define ESC_END '"'
#define     ESC '\\'
#define ESC_ESC '\''

void p4_stuff_arg( char arg[], char stuffed[])
{
    int i,j;

    for (i=0, j=0; i < strlen(arg); i++)
    {
	switch (arg[i]) {
	    case END:
		stuffed[j++] = ESC;
		stuffed[j++] = ESC_END;
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

void p4_destuff_arg(char stuffed[], char arg[])
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
		}
		break;
	    default:
		arg[j++] = stuffed[i++];
	}
    }
    arg[j] = '\0';
}
#endif
