#include "p4.h"
#include "p4_sys.h"

#ifndef THREAD_LISTENER
/*
 * This listener is based on a rewrite provided by Pete Wykcoff <pw@osc.edu>.
 * His rewrite fixes a number of problems with the multiple slave (comm=shared)
 * version, which could cause the listener to become confused about which 
 * slave it was communicating with.  The fix involves using a separate pipe 
 * for each slave and keeping track of the state of each individual socket.
 * Thanks, Pete!
 */
typedef struct {
    enum { OK, BUSY, DEAD } state;
    int busycount;  /* number of outstanding messages */
} sock_state_t;
static sock_state_t *sock_state = 0;

static void wakeup_slave(int idx);
static void poke_slave(int idx );
static int process_slave_message(int idx);
static int process_connect_request(int listening_fd);

P4VOID listener( void )
{
    P4BOOL done = P4_FALSE;
    int i;

    p4_dprintfl(70, "enter listener, pid %d\n", getpid());
    dump_listener(70);

    sock_state = p4_malloc(listener_info->num * sizeof(*sock_state));
    for (i=0; i<listener_info->num; i++) {
	sock_state[i].state = OK;
	sock_state[i].busycount = 0;
    }

    while (!done) {
	int max_fd, nfds, numbusy;
	fd_set read_fds;
	struct timeval tv, *tvp;

	FD_ZERO(&read_fds);
	FD_SET(listener_info->listening_fd, &read_fds);
	max_fd = listener_info->listening_fd;
	numbusy = 0;
	for (i=0; i<listener_info->num; i++) {
	    if (sock_state[i].state != DEAD) {
		FD_SET(listener_info->slave_fd[i], &read_fds);
		if (listener_info->slave_fd[i] > max_fd)
		    max_fd = listener_info->slave_fd[i];
		if (sock_state[i].state == BUSY)
		    ++numbusy;
	    }
	}

	if (numbusy) {
	    tvp = &tv;
	    tv.tv_sec = 0;
	    tv.tv_usec = 100000;
	} else
	    tvp = 0;
 
 	/* SYSCALL_P4 retries on EINTR; other errors are fatal */
	SYSCALL_P4(nfds, select(max_fd + 1, &read_fds, 0, 0, tvp));
	if (nfds < 0) {
	    p4_error("listener select", nfds);
	}
	if (nfds == 0) {
	    if (tvp) {
		for (i=0; i<listener_info->num; i++)
		    if (sock_state[i].state == BUSY) {
			p4_dprintfl(70, "wakeup slave %d from timeout\n", i);
			/* There is a race condition here.  If the 
			   slave wakes up after having read the 
			   previous message, then there won't be a message
			   to read.  To handle this, instead of
			   simply signaling the slave, we send a 
			   WAKEUP_SLAVE message. */
			/* A downside to this approach is that messages could
			   pile up in some cases, but this is less likely
			   and harmful than sending a signal without a message.
			*/
			poke_slave(i);
		    }
	    } else
		p4_dprintfl(70, "select timeout\n");

	    continue;
	}

	/* We use |= to insure that after the loop, we haven't lost
	   any "done" messages. 
	   There really are some nasty race conditions here, and all
	   this does is cause us to NOT lose a "DIE" message
	*/
	if (FD_ISSET(listener_info->listening_fd, &read_fds)) {
	    p4_dprintfl(70, "input on listening_fd %d\n",
	      listener_info->listening_fd);
	    done |= process_connect_request(listener_info->listening_fd);
	    --nfds;
	}
	for (i=0; nfds && i<listener_info->num; i++) {
	    if (FD_ISSET(listener_info->slave_fd[i], &read_fds)) {
		p4_dprintfl(70, "input on pipe %d, slave_fd = %d, pid = %d\n",
		  i, listener_info->slave_fd[i], listener_info->slave_pid[i]);
		done |= process_slave_message(i);
		--nfds;
	    }
	}
    }

	/*
    close( l->listening_fd );
    close( l->slave_fd );
	*/

    p4_dprintfl(70, "exit listener\n");
    exit(0);
}

/*
 * Return index in array based on incoming pid.
 */
static int
lookup_slave_by_pid(int pid)
{
    int i;

    for (i=0; i<listener_info->num; i++) {
	if (listener_info->slave_pid[i] == pid)
	    return i;
    }
    p4_error("lookup_slave_index_by_pid: pid not found = ", pid);
    return -1;
}

/*
 * Forward a message received from the net to the pipe for
 * the destination slave.
 */
static void
message_to_slave( int idx, struct slave_listener_msg *msg)
{
/*    fprintf (stderr, "send to %d\n", listener_info->slave_fd[idx] ); */
    net_send(listener_info->slave_fd[idx], msg, sizeof(*msg), P4_FALSE);
    sock_state[idx].state = BUSY;
    ++sock_state[idx].busycount;
/*    if (sock_state[idx].busycount > 1) {
 	fprintf( stderr, "busy count to %d = %d on fd %d\n", 
	     idx, sock_state[idx].busycount, listener_info->slave_fd[idx] ) ;
    } 
*/
    wakeup_slave(idx);
    /*  fprintf( stderr, "signal sent\n" );
	fflush( stderr );*/
}

/* 
 * Send a wakeup message to a slave
 */
static void
poke_slave( int idx )
{
    struct slave_listener_msg msg;
    msg.type = p4_i_to_n(WAKEUP_SLAVE);
    net_send(listener_info->slave_fd[idx], &msg, sizeof(msg), P4_FALSE);
    wakeup_slave(idx);
}


/*
 * Send a signal to the process telling him to pay attention to the
 * pipe from the listener.
 * Question: is there a possible race condition here when the 
 * processes are exiting?  We've seen some failures the appear to happen
 * near or during MPI_Finalize.
 *
 * Note that kill doesn't guarantee signal delivery.  Possible alternatives
 * include using sigqueue (the POSIX version of a reliable kill).
 * Also, the order of delivery is not specified for kill and for sa_handler;
 * it may be better to use sa_sigaction instead of sa_handler.
 */
static void
wakeup_slave( int idx )
{
    if (kill(listener_info->slave_pid[idx], LISTENER_ATTN_SIGNAL) == -1) {
	/* might have died on his own, okay */
	p4_dprintf("wakeup_slave: unable to interrupt slave %d pid %d\n",
          idx, listener_info->slave_pid[idx]);
	sock_state[idx].state = DEAD;
    }
}

/*
 * Accept a new socket from the network, deal with it, possibly
 * forwarding the message on to a slave.  The new connection is
 * always closed immediately after receipt of this message.
 */
static P4BOOL process_connect_request(int listening_fd)
{
    struct slave_listener_msg msg;
    int msglen;
    int connection_fd;
    int from, to_pid, idx, type, lport;
    P4BOOL rc = P4_FALSE;

    p4_dprintfl(70, "process_connect_request on %d\n", listening_fd);

    connection_fd = net_accept(listening_fd);

    p4_dprintfl(70, "accepted on connection_fd=%d reading size=%d\n", connection_fd,sizeof(msg));

    /* We originally used net_recv here, but there is the chance that a
       bogus message arrives.  In that case, we read, discard, and close
       the connection.  We detect a bogus message by either a timeout
       or invalid message type (we should switch to a session-specific
       message cookie).  Because we need a timeout, we can't use net_recv.
       Since we don't need a very complex receive message, this isn't
       such a bad thing */
    msglen = net_recv_timeout(connection_fd, &msg, sizeof(msg), 10);
    if (msglen == PRECV_EOF || msglen != sizeof(msg)) {
	p4_dprintf("process_connect_request: bad connect request len %d"
		   " wanted %d\n", msglen, sizeof(msg));
	close(connection_fd);
	return (P4_FALSE);
    }
    close( connection_fd );

    type = p4_n_to_i(msg.type);
    switch (type)
    {
      case IGNORE_THIS:
	p4_dprintfl(70, "got IGNORE_THIS from net\n");
	break;

      case DIE:
	from = p4_n_to_i(msg.from);
	p4_dprintfl(99, "received DIE msg from remote %d\n", from);
	rc = P4_TRUE;
	break;

      case KILL_SLAVE:
        /*
	 * KILL_SLAVE is used by a remote machine to destroy a particular
	 * process here, but not the listener (see DIE).
	 * A KILL_SLAVE message is very strong and causes nearly immediate 
	 * exit by the slave.  */
	from = p4_n_to_i(msg.from);
	to_pid = p4_n_to_i(msg.to_pid);
	idx = lookup_slave_by_pid( to_pid );
	p4_dprintfl(10, "received msg for %d: kill_slave from %d to_pid %d\n",
	  idx, from, to_pid);
	message_to_slave( idx, &msg );
#ifdef FOO
	slave_fd = listener_info->slave_fd;

	if (kill(to_pid, LISTENER_ATTN_SIGNAL) == -1)
	{
	    p4_dprintf("Listener: Unable to interrupt client pid=%d.\n", to_pid);
	    break;
	}

	net_send(slave_fd, &msg, sizeof(msg), P4_FALSE);
	/* wait for msg from slave indicating it got connected */
	/*
	 * do not accept any more connections for slave until it has fully
	 * completed this one, i.e. do not want to interrupt it until it has
	 * handled this interrupt
	 */
	p4_dprintfl(70, "waiting for slave to handle interrupt\n");
	net_recv(slave_fd, &msg, sizeof(msg));
	/* Check that we get a valid message; for now (see p4_sock_conn/
	   handle_connection_interrupt) this is just IGNORE_THIS */
	if (p4_i_to_n(msg.type) != IGNORE_THIS) {
	    p4_dprintf("received incorrect handshake message type=%d\n", 
		       p4_i_to_n(msg.type) );
	    p4_error("slave_listener_msg: broken handshake", 
		     p4_i_to_n(msg.type));
	    }
	p4_dprintfl(70, "back from slave handling interrupt\n");
#endif
	break;

      case CONNECTION_REQUEST:
	from = p4_n_to_i(msg.from);
	to_pid = p4_n_to_i(msg.to_pid);
	idx = lookup_slave_by_pid(to_pid);
/*	to = p4_n_to_i(msg.to); */
	lport = p4_n_to_i(msg.lport); 
	p4_dprintfl(70, "process_connect_request: to slave %d pid %d from %d port %d\n", idx, to_pid, from, lport );
	message_to_slave( idx, &msg );
#ifdef FOO
	slave_fd = listener_info->slave_fd;

	if (kill(to_pid, LISTENER_ATTN_SIGNAL) == -1)
	{
	    p4_dprintf("Listener: Unable to interrupt client pid=%d.\n", to_pid);
	    break;
	}

	net_send(slave_fd, &msg, sizeof(msg), P4_FALSE);
	/* wait for msg from slave indicating it got connected */
	/*
	 * do not accept any more connections for slave until it has fully
	 * completed this one, i.e. do not want to interrupt it until it has
	 * handled this interrupt
	 */
	p4_dprintfl(70, "waiting for slave to handle interrupt\n");
	net_recv(slave_fd, &msg, sizeof(msg));
	/* Check that we get a valid message; for now (see p4_sock_conn/
	   handle_connection_interrupt) this is just IGNORE_THIS */
	if (p4_i_to_n(msg.type) != IGNORE_THIS) {
	    p4_dprintf("received incorrect handshake message type=%d\n", 
		       p4_i_to_n(msg.type) );
	    p4_error("slave_listener_msg: broken handshake", 
		     p4_i_to_n(msg.type));
	    }
	p4_dprintfl(70, "back from slave handling interrupt\n");
#endif
	break;

      default:
	p4_dprintf("invalid type %d in process_connect_request\n", type);
	break;
    }
    close(connection_fd);
    return (rc);
}

static P4BOOL process_slave_message(int idx)
{
    struct slave_listener_msg msg;
    P4BOOL rc = P4_FALSE;
    int type, from, cc;

    /*
     * An EOF will happen naturally if the slave process exits.  Do
     * not force an error.  In fact, don't even use net_recv() since
     * this is a local pipe.  Just read it.
     */
    cc = read(listener_info->slave_fd[idx], &msg, sizeof(msg));
    if (cc == 0 || (cc < 0 && errno == ECONNRESET)) {
	/* ECONNRESET means there was still data on the connection, but
	 * it can be ignored since the slave already exited.
	 */
	sock_state[idx].state = DEAD;
	close(listener_info->slave_fd[idx]);
	return rc;
    }
    if (cc < 0) {
	p4_dprintf("process_slave_message: idx %d fd %d pid %d cc %d"
	  " errno %d\n",
	  idx, listener_info->slave_fd[idx], listener_info->slave_pid[idx],
	  cc, errno);
	p4_error("process_slave_message: read pipe", cc);
    }
    if (cc != sizeof(msg))
	p4_error("process_slave_message: short read from pipe", 0);

    type = p4_n_to_i(msg.type);
    from = p4_n_to_i(msg.from);

    switch (type)
    {
      case IGNORE_THIS:
	/* response to forwarded message, clear his busy flag */
	if (sock_state[idx].state == BUSY) {
	    p4_dprintfl(20, "process_slave_message: slave %d busy was %d\n",
	      idx, sock_state[idx].busycount);
	    --sock_state[idx].busycount;
	    if (sock_state[idx].busycount == 0)
		sock_state[idx].state = OK;
	} else {
	    p4_dprintf("process_slave_message: ignoring IGNORE_THIS for %d",
	      idx);
	}
	break;

      case DIE:
	/* see DIE from remote above, just quit the listener */
	p4_dprintfl(70, "received die msg from slave %d\n", from);
	rc = P4_TRUE;
	break;

      default:
	p4_dprintf("received unknown message type=%d from=%d\n", type, from);
	p4_error("slave_listener_msg: unknown message type", type);
	break;
    }

    return (rc);
}

#else /* def THREAD_LISTENER */
/* 
 * The thread listener logic is quite different from the process listener
 * logic.  This takes advantage of the fact that the thread is in the same
 * process.  The algorithm is this:
 *   Let L be the listener thread and P be the "process"/user thread
 *   To connect, P sends a message to its OWN listener, using the pipe
 *   between them (this allows L to use a select to wait for work to do).
 *   P then waits for a message back down the pipe that indicates that the
 *   connection is ready.  It may get messages about other connections
 *   becoming ready while it is waiting.
 *
 *   L selects on the pipe to P and the external connection socket.
 *   If it gets a request from P, it checks the connection table; if
 *   the connection has already been made, it ignores the request (since
 *   the request-ready message is already in the pipe).  Otherwise, it
 *   creates a new socket and contacts the remote listener.
 *   
 *   If the rank of L is LOWER than the rank of the remote L, this is the
 *   socket that will be used for the connection.  Once the remote listener
 *   accepts the connection, BOTH listeners (local and remote) transfer the
 *   socket fd into the connection tables, set the connection to EST, and
 *   send a message down the pipe to P.
 *
 *   If the rank of L is higher than the rank of the remote L, a message is
 *   sent asking the remote (lower rank) L to establish a connection.
 *   The socket used for this request is closed when the connection is 
 *   established.  This is the only time a socket is created and later closed.
 *
 *   Because this is so different from the process listener, there is a 
 *   separate establish_connection routine.
 *
 *   Why choose the lower rank to establish the connection?  Because the
 *   first round of connections is from the master, at rank 0.  Additional
 *   connections as part of the initial distribution tree are also from low
 *   to high rank.  This reduces the number of connections that are made.
 */
P4VOID thread_listener( void )
{
    struct slave_listener_msg msg;
    int type;
    int connection_fd;
    int from, lport, to_pid, to;
/*
    int do_conn;
    P4BOOL rc = P4_FALSE;
    struct proc_info *from_pi;
 */
    fd_set read_fds;
    int    nfds, nfds_in;
    int    msglen;

  p4_dprintfl(70,"TL: thread listener starting\n");
  while(1)
  {
      p4_dprintfl(70, "TL: thread listener starting select on fd=%d port=%d\n",
		  p4_global->listener_fd,p4_global->listener_port);
      
      FD_ZERO(&read_fds);
      FD_SET(p4_global->listener_fd, &read_fds);
      /* This version only works with single-slaves */ 
      if (listener_info->num > 1) {
	  p4_error( "Threaded listener does not support multiple slaves", 
		    listener_info->num );
      }
      FD_SET(listener_info->slave_fd[0], &read_fds);

      nfds_in = p4_global->listener_fd;
      if (listener_info->slave_fd[0] > nfds_in) nfds_in = listener_info->slave_fd[0]; 
      nfds_in++;
      SYSCALL_P4(nfds, select(nfds_in, &read_fds, 0, 0, 0));
      if (nfds < 0)
	  p4_error("listener select", nfds);
      if (nfds == 0) {
	  p4_dprintfl(70, "TL: select timeout\n");
	  continue;
      }

      /* Process remote connection requests first */
      if (FD_ISSET(p4_global->listener_fd, &read_fds)) {
	  /* Accept connection, get message */
	  p4_dprintfl( 70, "TL: starting accept\n" );
	  connection_fd = net_accept(p4_global->listener_fd);
	  p4_dprintfl(70, 
		  "TL: thread listener accepted on %d, got connection_fd=%d\n",
		      p4_global->listener_fd, connection_fd);
	  
	  if ((msglen = net_recv_timeout(connection_fd, &msg, sizeof(msg),10)) == PRECV_EOF)
	  {
	      p4_dprintf("TL: thread listener detected EOF on fd=%d\n",
			 connection_fd);
	      p4_error("thread listener detected EOF",-1);
	  }
	  if (msglen != sizeof(msg)) {
	      p4_dprintf("TL: message was wrong size (%d)\n", msglen );
	      close(connection_fd);
	  }
	  type = p4_n_to_i(msg.type);
	  switch (type) 
	  {
	  case IGNORE_THIS:
	      p4_dprintfl(70, "TL: got IGNORE_THIS\n");
	      break;
	  case CONNECTION_REQUEST:
	      from   = p4_n_to_i(msg.from);
	      to_pid = p4_n_to_i(msg.to_pid);
	      to     = p4_n_to_i(msg.to);
	      lport  = p4_n_to_i(msg.lport);
	      if (lport != -1) {
		  /* Message from non-threaded listener! */
	      }
	      p4_dprintfl(70, 
	      "TL: got connection_request: from=%d lport=%d to_pid=%d to=%d\n",
			  from, lport, to_pid, to);

	      if (p4_local->conntab[from].type == CONN_REMOTE_NON_EST) {
		  /* Establish the connection */
		  /* p4_local->conntab[from].type = CONN_REMOTE_OPENING; */
		  p4_dprintfl(70, "TL: connection now opening for %d\n", 
			      from );
		  
		  if (p4_local->my_id < from) 
		  {
		      int new_connection_fd;
		      p4_dprintfl(90,"TL: myid < from, myid = %d, from = %d\n",
				  p4_local->my_id,from);
		      /* Create a connection back to "from".  We could use
		       the same socket, but using the same request code 
		       is easier */
		      new_connection_fd = request_connection(from);
		      if (new_connection_fd < 0) {
			  p4_error( "Could not create new connection", 
				    new_connection_fd );
		      }
		      close( connection_fd );
		      connection_fd = new_connection_fd;
		      /* We now have the socket for the connection */
		  }
		  /* This is the new socket.  Just keep it. */
		  p4_local->conntab[from].port = connection_fd;
		  p4_local->conntab[from].same_data_rep =
		      same_data_representation(p4_local->my_id,from);
		  /* Requires write ordering in the thread */
		  p4_local->conntab[from].type = CONN_REMOTE_EST;
		  
		  /* Send dummy message to P */
		  p4_dprintfl(70,"TL: sending dummy msg on fd=%d\n",
			      listener_info->slave_fd[0]);
		  net_send(listener_info->slave_fd[0], &msg, sizeof(msg), 
			   P4_FALSE);
		  p4_dprintfl(70,"TL: sent dummy msg on fd=%d\n",
			      listener_info->slave_fd[0]);
	      }
	      else {
		  /* If the connection is in any other state, we've already
		     connected it and have nothing to do (connections passed) 
		   */
		  close( connection_fd );
	      }
	      break;

	  default:
	      p4_dprintf("TL: invalid type %d in process_connect_request\n", 
			 type);
	      break;
	  }
      }
      if (FD_ISSET(listener_info->slave_fd[0], &read_fds)) {
	  p4_dprintfl( 70, "TL: connection request from slave\n" );
	  /* Read this message */
	  net_recv( listener_info->slave_fd[0], &msg, sizeof(msg) );
	  from   = p4_n_to_i(msg.from);
	  to_pid = p4_n_to_i(msg.to_pid);
	  to     = p4_n_to_i(msg.to);
	  lport  = p4_n_to_i(msg.lport);
	  /* We may have established this connection while the slave
	     was sending this request */
	  if (p4_local->conntab[to].type == CONN_REMOTE_EST) {
	      /* Nothing to do - we've already sent the est message */
	      continue;
	  }
	  else {
	      /* Send request connection */
	      p4_dprintfl( 70, "TL: Slave requests a connection to %d\n", to );
	      connection_fd = request_connection( to );
	      if (connection_fd < 0) {
		  p4_error( "Unable to get connection fd", connection_fd );
	      }
	      p4_dprintfl( 70, "TL: connection ready on fd=%d\n", 
			   connection_fd );
	      /* Send request ready */
	      if (p4_local->my_id < to) {
		  /* This is the new socket.  Just keep it. */
		  p4_local->conntab[to].port = connection_fd;
		  p4_local->conntab[to].same_data_rep =
		      same_data_representation(p4_local->my_id,to);
		  /* Requires write ordering in the thread */
		  p4_local->conntab[to].type = CONN_REMOTE_EST;
	      
		  /* Send dummy message to P */
		  p4_dprintfl(70,"TL: sending dummy msg on fd=%d\n",
			      listener_info->slave_fd[0]);
		  net_send(listener_info->slave_fd[0], &msg, sizeof(msg), 
			   P4_FALSE);
		  p4_dprintfl(70,"TL: sent dummy msg on fd=%d\n",
			      listener_info->slave_fd[0]);
	      }
	      else {
		  /* Otherwise, we need to wait for the connection to come
		     from the other end. This connection is no longer needed */
		  close( connection_fd );
	      }
	  }
      }
  }
}

/* 
 * This routine should only be called when the connection is not 
 * established.  It is called ONLY by the process, which is waiting for the
 * listener thread to complete.
 */
int establish_connection(int dest_id)
{
    int                       myid = p4_get_my_id();
    struct slave_listener_msg msg;
    struct proc_info          *dest_pi;

    p4_dprintfl( 80, 
	     "TL: Sending request to listener to open connection with %d\n", 
		 dest_id );
    /* 
     * Send message to local listener requesting connection to dest_id
     * (process listener code uses p4_global->dest_id[myid] = dest_id/-1
     * to lock/unlock around the request.  We don't need to do that.
     */
    dest_pi    = get_proc_info(dest_id);

    msg.type   = p4_i_to_n(CONNECTION_REQUEST);
    msg.from   = p4_i_to_n(myid);
    msg.lport  = p4_i_to_n(-1);
    msg.to     = p4_i_to_n(dest_id);
    msg.to_pid = p4_i_to_n(dest_pi->unix_id);

    net_send( p4_local->listener_fd, &msg, sizeof(msg), P4_FALSE );

    /* Wait for thread to complete the request */
    while (p4_local->conntab[dest_id].type == CONN_REMOTE_NON_EST) {
        p4_dprintfl( 80, "TL: Waiting for message from listener thread\n" );
        net_recv( p4_local->listener_fd, &msg, sizeof(msg) );
        /* Just discard message */
    }

    p4_dprintfl(70, "TL :Connection established\n");
    return (P4_TRUE);
}

/* 
 * Send a connection request from one listener to another listener.  Return 
 * the socket created for the request.
 */
int request_connection(dest_id)
int dest_id;
{
    struct proc_info *my_pi, *dest_pi;
    char *my_host, *dest_host;
    int my_id;
    struct slave_listener_msg msg;
    int dest_listener_con_fd;
    int my_listener, dest_listener;
    int num_tries;

    /* Get some initial information */
    my_id	  = p4_get_my_id();
    my_pi	  = get_proc_info(my_id);
    my_host	  = my_pi->host_name;
    my_listener	  = my_pi->port;

    dest_pi	  = get_proc_info(dest_id);
    dest_host	  = dest_pi->host_name;
    dest_listener = dest_pi->port;

    p4_dprintfl(70, "TL :request_connection: my_id=%d my_host=%s my_listener=%d dest_id=%d dest_host=%s dest_listener=%d\n",
	    my_id, my_host, my_listener, dest_id, dest_host, dest_listener);

    /* Have we already connected?? */
    if (p4_local->conntab[dest_id].type != CONN_REMOTE_NON_EST /* &&
	p4_local->conntab[dest_id].type != CONN_REMOTE_OPENING */)
    {
	/* This should never happen */
	p4_dprintfl(70,"TL: request_connection %d: already connected!\n", 
		    dest_id);
	return -2;
    }

    p4_dprintfl(70, "TL: enter loop to connect to dest listener %s\n",
		dest_host);
    /* Connect to dest listener */
    num_tries = 1;
    p4_has_timedout( 0 );
    while((dest_listener_con_fd = 
	   net_conn_to_listener(dest_host,dest_listener,1)) == -1) {
	num_tries++;
	if (p4_has_timedout( 1 )) {
	    p4_error( "Timeout in establishing connection to remote process", 
		      0 );
	    }
	}
    p4_dprintfl(70, 
 "TL: conn_to_proc_contd: connected after %d tries, dest_listener_con_fd=%d\n",
		num_tries, dest_listener_con_fd);

    /* Construct a connection request message */
    msg.type   = p4_i_to_n(CONNECTION_REQUEST);
    msg.from   = p4_i_to_n(my_id);
    msg.lport  = p4_i_to_n(-1);
    msg.to     = p4_i_to_n(dest_id);
    msg.to_pid = p4_i_to_n(dest_pi->unix_id);

    /* Send it to dest_id's listener */
    p4_dprintfl(70, 
"TL: request_connection: sending CONNECTION_REQUEST to %d on fd=%d size=%d\n",
		dest_id,dest_listener_con_fd,sizeof(msg));
    net_send(dest_listener_con_fd, &msg, sizeof(msg), P4_FALSE);
    p4_dprintfl(70, 
	"TL: request_connection: sent CONNECTION_REQUEST to dest_listener\n");

    return dest_listener_con_fd;
}
#endif

/* This is net_recv, except simplified for short messages and with an 
   explicit timeout */
int net_recv_timeout(int fd, P4VOID *in_buf, int size, int secs)
{
    int recvd = 0;
    int n;
    int read_counter = 0;
    int block_counter = 0;
    int eof_counter = 0;
    char *buf = (char *)in_buf;
    time_t start_time, cur_time;

    start_time = time( (time_t) 0 );

    p4_dprintfl( 99, "Beginning net_recv_timeout of %d on fd %d\n", size, fd );
    while (recvd < size)
    {
	read_counter++;

	SYSCALL_P4(n, read(fd, buf + recvd, size - recvd));

	cur_time = time( (time_t) 0 );
	if (cur_time - start_time >= secs) {
	    if (n > 0) recvd += n;
	    return recvd;
	}

	if (n == 0)		/* maybe EOF, maybe not */
#if defined(P4SYSV) && !defined(NONBLOCKING_READ_WORKS)
	{
	    int n1 = 0;
	    struct timeval tv;
	    fd_set read_fds;
	    int rc;
	    char tempbuf[1];

	    eof_counter++;

	    tv.tv_sec = 5;
	    tv.tv_usec = 0;
	    FD_ZERO(&read_fds);
	    FD_SET(fd, &read_fds);
	    SYSCALL_P4(n1, select(fd+1, &read_fds, 0, 0, &tv));
	    if (n1 == 1  &&  FD_ISSET(fd, &read_fds))
	    {
		rc = recv(fd, tempbuf, 1, MSG_PEEK);
		if (rc == -1)
		{
		    /* -1 indicates ewouldblock (eagain) (check errno) */
		    p4_error("net_recv_timeout recv:  got -1", -1);
		}
		if (rc == 0)	/* if eof */
		{
		    /* eof; a process has closed its socket; may have died */
		    p4_error("net_recv_timeout recv:  EOF on socket", read_counter);
		}
		else
		    continue;
	    }
	    sleep(1);
	    if (eof_counter < 5)
		continue;
	    else
		p4_error("net_recv_timeout read:  probable EOF on socket", read_counter);
	}
#else
	{
	    /* Except on SYSV, n == 0 is EOF */
	    /* Should we ignore EOFs during shutdown? */
	    p4_error("net_recv_timeout read:  probable EOF on socket", read_counter);
        }
#endif
	if (n < 0)
	{
	    /* EAGAIN is really POSIX, so we check for either EAGAIN 
	       or EWOULDBLOCK.  Note some systems set EAGAIN == EWOULDBLOCK
	     */
	    /* Solaris 2.5 occasionally sets n == -1 and errno == 0 (!!).
	       since n == -1 and errno == 0 is invalid (i.e., a bug in read),
	       it should be safe to treat it as EAGAIN and to try the
	       read once more 
	     */
	    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == 0)
	    {
		struct timeval tv;
		fd_set         read_fds;
		int            n1;
		block_counter++;
		/* Wait here for more data for no more than the timeout
		   period */
		tv.tv_sec = secs - (cur_time - start_time);
		tv.tv_usec = 0;
		FD_ZERO(&read_fds);
		FD_SET(fd, &read_fds);
		SYSCALL_P4(n1, select(fd+1, &read_fds, 0, 0, &tv));
		continue;
	    }
	    else {
		/* A closed socket can cause this to happen. */
		printf( "net_recv_timeout failed for fd = %d\n", fd );
		p4_error("net_recv_timeout read, errno = ", errno);
	    }
	}
	recvd += n;
    }
    p4_dprintfl( 99, 
		"Ending net_recv_timeout of %d on fd %d (eof_c = %d, block = %d)\n", 
		 size, fd, eof_counter, block_counter );
    return (recvd);
}

