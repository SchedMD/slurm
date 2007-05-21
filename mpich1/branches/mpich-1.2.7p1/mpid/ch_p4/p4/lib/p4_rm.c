#include "p4.h"
#include "p4_sys.h"

/*
  Defining REDIRECT_OUTPUT allows you to redirect output from
  processes as follows.
  Defining OUT_TO_TERM tty allows you to redirect the
  output from various processes to separate windows.
  */

#if defined(REDIRECT_OUTPUT)
#if defined(OUT_TO_TERM)
#define P4_OUTFILE "/dev/ttyp4"
#else
#define P4_OUTFILE "/tmp/p4out"
#endif
#endif

static int rm_num;
static int rm_flag;

int rm_start( int *argc, char **argv)
{
    int bm_fd, bm_port;
    char *s,*bm_host;
    extern char whoami_p4[];
    struct net_initial_handshake hs;
/*     struct slave_listener_msg lmsg; */
    struct bm_rm_msg msg;
    int type, rc, numslaves;
#if defined(SP1_EUIH)
    int len;
#endif
#if defined(NCUBE)
    int unused_flag;
#endif
#if defined(IPSC860)  ||  defined(CM5)  ||  defined(NCUBE)  ||  defined(SP1_EUI) || defined(SP1_EUIH)
    int i;
#endif
    int conn_retries;
    struct p4_global_data *g;
    char outfile[P4_MAX_PGM_LEN];

    trap_sig_errs(); 		/* Errors can happen any time */

    sprintf(whoami_p4, "rm_%d", (int)getpid());
    p4_dprintfl(20,"remote master starting, my p4 id is %s\n",whoami_p4);

#if defined(REDIRECT_OUTPUT)
    freopen(P4_OUTFILE, "w", stdout);
    freopen(P4_OUTFILE, "w", stderr);
#endif

    conn_retries = 5;
    if (execer_mynodenum)
    {
        bm_host = execer_masthost;
        bm_port = execer_mastport;
	conn_retries = 100;
    }
    else
    {
	if (*argc < 4)
	    p4_error("Invalid arguments to remote_master", *argc);

	bm_host = argv[1];
	bm_port = atoi(argv[2]);
    }

    bm_fd = net_conn_to_listener(bm_host, bm_port, conn_retries);
    if (bm_fd < 0)
	p4_error("rm_start: net_conn_to_listener failed", bm_port);

    net_recv(bm_fd, &hs, sizeof(hs));
    hs.pid = (int) htonl(getpid());
    hs.rm_num = (int) htonl(execer_mynodenum);  /* only used with dqs */
    net_send(bm_fd, &hs, sizeof(hs), P4_FALSE);

#   ifdef SYSV_IPC
    sysv_num_shmids = 0;
    sysv_shmid[0]  = -1;
    sysv_semid0    = -1;
    sysv_semid0 = init_sysv_semset(0);
#   endif

    /* Get the initialization information from the bm */

    rc = net_recv(bm_fd, &msg, sizeof(msg));
    if (rc == PRECV_EOF)
	p4_error("rm_start: got EOF on net_recv", bm_fd);
    type = p4_n_to_i(msg.type);
    if (type != INITIAL_INFO)
	p4_error("rm_start: unknown type, expecting INITIAL_INFO, type=", type);
    if (strcmp(msg.version,P4_PATCHLEVEL) != 0)
    {
	p4_dprintf("my version is %s, received %s as version\n",
		   P4_PATCHLEVEL,msg.version);
	p4_error("version does not match master \n",0);
    }

    /* choose working directory */
    if (strlen(msg.wdir) && !chdir(msg.wdir))
    {
	p4_dprintfl(90, "working directory set to %s\n", msg.wdir);
    }
    else
    {
	if ((s = (char *) rindex(msg.pgm,'/'))  !=  NULL)
	{
	    *s = '\0';  /* chg to directory name only */
	    chdir(msg.pgm);
	}
    }
    globmemsize = p4_n_to_i(msg.memsize);
    logging_flag = p4_n_to_i(msg.logging_flag);
    if (logging_flag)
	ALOG_ENABLE;
    else
	ALOG_DISABLE;

    MD_initmem(globmemsize);
    alloc_global();  /* sets p4_global */
    g = p4_global;
    p4_local = alloc_local_rm();
    g->local_communication_only = P4_FALSE;
    g->num_in_proctable = p4_n_to_i(msg.numinproctab);
    numslaves = p4_n_to_i(msg.numslaves);
    rm_num = p4_n_to_i(msg.rm_num);
    p4_debug_level = p4_n_to_i(msg.debug_level);
    strncpy(outfile, msg.outfile, P4_MAX_PGM_LEN);
    outfile[P4_MAX_PGM_LEN-1] = 0;
    strcpy(p4_global->application_id, msg.application_id);
    p4_dprintfl(90, "got numslaves=%d outfile=%s rm_num=%d dbglvl=%d appid=%s\n",
		numslaves, outfile, rm_num, p4_debug_level, msg.application_id);

    MD_initenv();
    usc_init();
    init_usclock();

    if (*outfile)
    {
	freopen(outfile, "w", stdout);
	freopen(outfile, "w", stderr);
    }

#ifndef THREAD_LISTENER
    SIGNAL_P4(LISTENER_ATTN_SIGNAL, handle_connection_interrupt);
#endif
    p4_lock(&g->slave_lock);
    create_rm_processes(numslaves, bm_fd);
    if (!rm_flag) /* I am not rm; was forked in create_rm_processes */
	return(0);

    /* Grab the whole proc table from the bm */
    p4_dprintfl(90, "receiving proc table\n");
    receive_proc_table(bm_fd);

    /* let local slaves use proctable to identify themselves */
    p4_unlock(&g->slave_lock);

    sprintf(whoami_p4, "rm_%d_%d", rm_num, (int)getpid());
    p4_local->my_id = p4_get_my_id_from_proc();

    p4_global->low_cluster_id = 
	p4_local->my_id - p4_global->proctable[p4_local->my_id].slave_idx;
    p4_global->hi_cluster_id = 
	p4_global->low_cluster_id + p4_global->local_slave_count;

    setup_conntab();

    if (p4_local->conntab[0].type == CONN_REMOTE_SWITCH)
    {
	p4_local->conntab[0].switch_port = p4_global->proctable[0].switch_port;
	p4_local->conntab[0].port = bm_fd;
    }
    else if (p4_local->conntab[0].type == CONN_REMOTE_NON_EST)
    {
	p4_local->conntab[0].type = CONN_REMOTE_EST;
	p4_local->conntab[0].port = bm_fd;
	p4_local->conntab[0].same_data_rep =
	    same_data_representation(p4_local->my_id,0);
    }
    else
    {
	p4_error("rm_start: invalid conn type in conntab ",
		 p4_local->conntab[0].type);
    }

    sprintf(whoami_p4, "p%d_%d", p4_get_my_id(), (int)getpid());


#if defined(IPSC860)  ||  defined(CM5)  ||  defined(NCUBE)  ||  defined(SP1_EUI) || defined(SP1_EUIH)
    for (i = 1; i < numslaves; i++)
    {
#       if defined(IPSC860)
	csend((long) INITIAL_INFO, &msg, (long) sizeof(struct bm_rm_msg), 
              (long) i, (long) NODE_PID);
	csend((long) INITIAL_INFO, p4_global->proctable, 
              (long) sizeof(p4_global->proctable), (long) i, (long) NODE_PID);
#       endif
#       if defined(CM5)
	CMMD_send_noblock(i, INITIAL_INFO,  &msg,sizeof(struct bm_rm_msg));
	CMMD_send_noblock(i, INITIAL_INFO,  p4_global->proctable, 
                          sizeof(p4_global->proctable));
#       endif
#       if defined(NCUBE)
	nwrite(&msg, sizeof(struct bm_rm_msg), i, INITIAL_INFO, &unused_flag);
	nwrite(p4_global->proctable, sizeof(p4_global->proctable), i, INITIAL_INFO, &unused_flag);
#       endif
#       if defined(SP1_EUI)
	mpc_bsend(&msg, sizeof(struct bm_rm_msg), i, INITIAL_INFO);
	mpc_bsend(p4_global->proctable, sizeof(p4_global->proctable), i, INITIAL_INFO);
#       endif
#       if defined(SP1_EUIH)
	len  = sizeof(struct bm_rm_msg);
	type = INITIAL_INFO;
	mp_bsend(&msg, &len, &i, &type);
	len  = sizeof(p4_global->proctable);
	type = INITIAL_INFO;
	mp_bsend(p4_global->proctable, &len, &i, &type);
#       endif
    }
#endif

    /* 
       sync with local slaves thus insuring that they have the proctable before 
       syncing with bm (this keeps bm and its slaves  from interrupting the local 
       processes too early; then re-sync with local slaves (thus permitting them to 
       interrupt remotes)
    */
    p4_barrier(&(p4_global->cluster_barrier),p4_num_cluster_ids());
    
    msg.type = p4_i_to_n(SYNC_MSG);
    net_send(bm_fd, &msg, sizeof(msg), P4_FALSE);
    msg.type = -1;  /* reset to verify type received next */
    rc = net_recv(bm_fd, &msg, sizeof(msg));
    type = p4_n_to_i(msg.type);
    if (type != SYNC_MSG)
	p4_error("rm_start: unknown type, expecting SYNC_MSG, type=", type);
    
    p4_barrier(&(p4_global->cluster_barrier),p4_num_cluster_ids());

    return(0);
}


P4VOID create_rm_processes(int nslaves, int bm_fd)
{
    struct p4_global_data *g = p4_global;
    int end_1, end_2, slave_pid, listener_pid;
    int slave_idx, listener_port, listener_fd;
    char rm_host[100];
    int rm_switch_port;
    struct bm_rm_msg bm_msg;
#   if defined(IPSC860)  ||  defined(CM5)  ||  defined(NCUBE)  ||  defined(SP1_EUI) || defined(SP1_EUIH)
    int i, from, type, len, unused_flag;
#endif
#   if defined(THREAD_LISTENER)
    p4_thread_t trc;
#   endif

#   if !defined(IPSC860)  &&  !defined(CM5)  &&  !defined(NCUBE)  &&  !defined(SP1_EUI) && !defined(SP1_EUIH)
    if (nslaves > P4_MAX_MSG_QUEUES)
	p4_error("create_rm_processes: more slaves than msg queues \n", nslaves);
#   endif

    rm_flag = 0;  /* set below for the remote master */
    /*
     * Allocate the listener's local data area, since this process will
     * eventually become the listener.
     */

    /* nslaves is total number of processes on the remote machine; this
     * is not the case in the big master code, though. */
    listener_info = alloc_listener_info(nslaves);

    net_setup_anon_listener(MAX_P4_CONN_BACKLOG, &listener_port, &listener_fd);
    listener_info->listening_fd = listener_fd;

    p4_dprintfl(70, "created listener on port %d fd %d\n", listener_port,
		listener_fd);

    /* Send off the listener info to the bm */
    bm_msg.type = p4_i_to_n(REMOTE_LISTENER_INFO);
    bm_msg.port = p4_i_to_n(listener_port);
    net_send(bm_fd, &bm_msg, sizeof(struct bm_rm_msg), P4_FALSE);

    rm_host[0] = '\0';
    get_qualified_hostname(rm_host,100);
#ifdef CAN_DO_SWITCH_MSGS
    rm_switch_port = getswport(rm_host);
#else
    rm_switch_port = -1;
#endif
    /* Send my info to the bm */
    bm_msg.type = p4_i_to_n(REMOTE_MASTER_INFO);
    bm_msg.slave_idx = p4_i_to_n(0);
    bm_msg.slave_pid = p4_i_to_n(getpid());
    bm_msg.switch_port = p4_i_to_n(rm_switch_port);
    strncpy(bm_msg.host_name,rm_host,HOSTNAME_LEN);
    strncpy(bm_msg.local_name,g->my_host_name,HOSTNAME_LEN);
    strncpy(bm_msg.machine_type,P4_MACHINE_TYPE,sizeof(bm_msg.machine_type));
    net_send(bm_fd, &bm_msg, sizeof(struct bm_rm_msg), P4_FALSE);

    g->local_slave_count = 0;

#   ifdef TCMP
    tcmp_init(NULL,p4_get_my_cluster_id(),shmem_getclunid());
#   endif

#   if defined(IPSC860)  ||  defined(CM5)  ||  defined(NCUBE)  ||  defined(SP1_EUI) || defined(SP1_EUIH)
    for (slave_idx = 1; slave_idx <= nslaves - 1; slave_idx++)
    {
#       if defined(IPSC860)
	crecv(INITIAL_INFO, &bm_msg, (long) sizeof(struct bm_rm_msg));
#       endif
#       if defined(CM5)
        CMMD_receive(CMMD_ANY_NODE, INITIAL_INFO, (void *) &bm_msg, 
                     sizeof(struct bm_rm_msg));
#       endif
#       if defined(NCUBE)
        from = NCUBE_ANY_NODE;
        type = INITIAL_INFO;
        nread(&bm_msg, sizeof(struct bm_rm_msg), &from,  &type, &unused_flag);
#       endif
#       if defined(SP1_EUI)
        from = ANY_P4TYPE_EUI;
        type = INITIAL_INFO;
        mpc_brecv(&bm_msg, sizeof(struct bm_rm_msg), &from,  &type, &unused_flag);
#       endif
#       if defined(SP1_EUIH)
        from = ANY_P4TYPE_EUIH;
        type = INITIAL_INFO;
	len  = sizeof(struct bm_rm_msg);
        mpc_brecv(&bm_msg, &len, &from,  &type, &unused_flag);
#       endif
	net_send(bm_fd, &bm_msg, sizeof(struct bm_rm_msg), P4_FALSE);
	g->local_slave_count++;
    }
#   else

    for (slave_idx = 1; slave_idx <= nslaves - 1; slave_idx++)
    {
	p4_dprintfl(20,"remote master creating local slave %d\n",slave_idx);
		    
#   if !defined(NO_LISTENER)
	get_pipe(&end_1, &end_2);
	listener_info->slave_fd[slave_idx] = end_2;
#   endif
	slave_pid = fork_p4();
#   if !defined(NO_LISTENER)
	listener_info->slave_pid[slave_idx] = slave_pid;
#   endif
	if (slave_pid)
	    p4_dprintfl(10,"remote master created local slave %d\n",slave_idx);
	if (slave_pid == 0)
	{
	    /* In the slave process */
	    sprintf(whoami_p4, "rm_s_%d_%d_%d", rm_num, slave_idx, (int)getpid());

	    p4_local = alloc_local_slave();

	    /* Check for environment variables that redirect stdin */
	    mpiexec_reopen_stdin();


#   if !defined(NO_LISTENER)
	    {
#ifdef USE_NONBLOCKING_LISTENER_SOCKETS
		int rc = p4_make_socket_nonblocking( end_1 );
		if (rc < 0) {
		    p4_error("create_rm_processes: set listener nonblocking",
		      rc);
		}
#endif /* USE_NONBLOCKING_LISTENER_SOCKETS */
		p4_local->listener_fd = end_1;
#           if !defined(THREAD_LISTENER)
	    close(end_2);
#           endif

	    }
#endif
	    close( listener_fd );


	    /* hang for a valid proctable.  The master holds this lock
	       until the slave processes are created, so this lock/unlock
	       ensures that we wait until the proctable is valid. */
	    p4_lock(&g->slave_lock);
	    p4_unlock(&g->slave_lock);

	    /* Don't enable the interrupt handler until a valid proctable 
	       exists.  To handle the possibility that an interrupt may
	       be lost, the listener will reissue interrupts if the slave
	       does not respond quickly. */
#ifndef THREAD_LISTENER
            SIGNAL_P4(LISTENER_ATTN_SIGNAL, handle_connection_interrupt);
#endif

	    p4_local->my_id = p4_get_my_id_from_proc();
	    sprintf(whoami_p4, "p%d_%d", p4_get_my_id(), (int)getpid());
	    setup_conntab();
	    usc_init();
	    init_usclock();

#           ifdef TCMP
            tcmp_init(NULL,p4_get_my_cluster_id(),shmem_getclunid());
#           endif

	    /* 
	       sync with local master twice: once to make sure all slaves 
	       have got proctable, and second after the master has synced bm
	    */
	    p4_barrier(&(p4_global->cluster_barrier),p4_num_cluster_ids());
	    p4_barrier(&(p4_global->cluster_barrier),p4_num_cluster_ids());

	    p4_dprintfl(20, "remote process starting\n");
            ALOG_SETUP(p4_local->my_id,ALOG_TRUNCATE);
            ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");
	    return;
	} /* if slave_pid == 0 */
	/* else slave_pid != 0, ie., I am the parent */
#   if !defined(NO_LISTENER)
	/* slave holds this end */
	close(end_1);
#   endif
	/* Send off the slave info to the bm */
	bm_msg.type = p4_i_to_n(REMOTE_SLAVE_INFO);
	bm_msg.slave_idx = p4_i_to_n(slave_idx);
	bm_msg.slave_pid = p4_i_to_n(slave_pid);
	bm_msg.switch_port = p4_i_to_n(rm_switch_port);
	strcpy(bm_msg.machine_type,P4_MACHINE_TYPE);
	/* strcpy(bm_msg.slave_hosthame, ); */
	net_send(bm_fd, &bm_msg, sizeof(struct bm_rm_msg), P4_FALSE);

	g->local_slave_count++;
    }
#endif

    /* Send the end message to the bm */
    bm_msg.type = p4_i_to_n(REMOTE_SLAVE_INFO_END);
    net_send(bm_fd, &bm_msg, sizeof(struct bm_rm_msg), P4_FALSE);

    /*
     * Done creating slaves. Now fork off the listener .. we've already
     * created the socket and bound a port to it
     */

    /* setup listener port even in no-listener case, because this process may use it to do
       direct connections
       */
    g->listener_port = listener_port;
    g->listener_fd   = listener_fd;

#   if !defined(IPSC860)  &&  !defined(CM5)  &&  !defined(NCUBE)  &&  !defined(SP1_EUI) && !defined(SP1_EUIH)
    get_pipe(&end_1, &end_2);
    p4_local->listener_fd = end_1;
    listener_info->slave_fd[0] = end_2;
#ifdef USE_NONBLOCKING_LISTENER_SOCKETS
    {
	int rc = p4_make_socket_nonblocking( end_1 );
	if (rc < 0) {
	    p4_error("create_rm_processes: set listener nonblocking",
		     rc);
	}
    }
#endif /* USE_NONBLOCKING_LISTENER_SOCKETS */
#   if !defined(NO_LISTENER) && !defined(THREAD_LISTENER)
    listener_pid = fork_p4();
    if (listener_pid == 0)
    {
	/* Inside listener */
	listener_info->slave_pid[0] = getppid();
	close(end_1);
	sprintf(whoami_p4, "rm_l_%d_%d", rm_num, (int)getpid());
	p4_dprintfl(70, "inside listener pid %d\n", getpid());
	{
	    /* exec external listener process */

	    char *listener_prg = LISTENER_PATHNAME;

	    if (*listener_prg)
	    {
		char dbg_c[10], max_c[10], lfd_c[10], sfd_c[10];

		/*
		p4_error("external listener not supported", 0);
		*/
		sprintf(dbg_c, "%d", p4_debug_level);
		sprintf(max_c, "%d", p4_global->max_connections);
		sprintf(lfd_c, "%d", listener_info->listening_fd);
		sprintf(sfd_c, "%d", listener_info->slave_fd[0]);
		p4_dprintfl(70, "exec %s %s %s %s %s\n",
			    listener_prg, dbg_c, max_c, lfd_c, sfd_c);
		execlp(listener_prg, listener_prg,
		       dbg_c, max_c, lfd_c, sfd_c, NULL);
		p4_dprintfl(70, "exec failed (errno= %d), using buildin\n",
			    errno);
	    }
	}
	listener();
	exit(0);
    }
#   endif
    close(listener_fd);
    close(end_2);

    /* Else we're still in the remote master */
#   if defined(THREAD_LISTENER)
    p4_dprintfl(50,"creating listener thread\n");
    /* This is the Windows NT version (HANDLE trc) */
    /* pthread_mutex_init( &p4_local->conntab_lock, 0 ); */
    p4_create_thread( trc, thread_listener, 66 );
    /* NT version put the last arg of CreateThread into listener_pid */
    p4_dprintfl(50,"created listener thread\n");
#   else
    p4_dprintfl(70, "created listener pid %d\n", listener_pid);
    g->listener_pid = listener_pid;
#   endif

#   endif  /* !IPSC860 etc */
    rm_flag = 1;  /* I am the remote master */
}


P4VOID receive_proc_table(int bm_fd)
{
    P4BOOL done;
    struct bm_rm_msg msg;
    int type;
    int port, unix_id, slave_idx, group_id;
    int switch_port;

    p4_dprintfl(90, "receive_proc_table\n");
    for (done = P4_FALSE; !done;)
    {
	if (net_recv(bm_fd, &msg, sizeof(msg)) == PRECV_EOF)
	    p4_error("recv_proc_table: got net_send_eof", bm_fd);

	type = p4_n_to_i(msg.type);
	switch (type)
	{
	  case PROC_TABLE_ENTRY:
	    group_id = p4_n_to_i(msg.group_id);
	    port = p4_n_to_i(msg.port);
	    unix_id = p4_n_to_i(msg.unix_id);
	    slave_idx = p4_n_to_i(msg.slave_idx);
	    switch_port = p4_n_to_i(msg.switch_port);
	    p4_dprintfl(90, "got entry gid=%d host=%s port=%d unix_id=%d slave_idx=%d switch_port=%d\n",
			group_id,msg.host_name,port,unix_id,slave_idx,switch_port);
	    /* remote master loading proctable from big master */
	    install_in_proctable(group_id, port, unix_id, msg.host_name,
				 msg.local_name, 
				 slave_idx, msg.machine_type, switch_port);
	    break;

	  case PROC_TABLE_END:
	    done = P4_TRUE;
	    break;

	  default:
	    p4_dprintf("receive_proc_table: got invalid message type %d\n", type);
	    break;
	}
    }
}
