#include "p4.h"
#include "p4_sys.h"

/* extern struct fdentry fdtable[MAXFDENTRIES]; */

void p4_peer_msg_handler(char *);

int bm_start(int *argc, char **argv)
{
    int bm_switch_port;
    char *s, pgmname[P4_MAX_PGM_LEN];
    int rc, myjsize, myrank;
    BNR_Group mygroup;

    setbuf( stdout, NULL );  /* turn off buffering for clients */

    alloc_global();  /* sets p4_global */
    p4_global->local_communication_only = P4_FALSE;  /* hard code for mpd */
    p4_local = alloc_local_bm();
    if (p4_local == NULL)
	p4_error("p4_initenv: alloc_local_bm failed\n", 0);

    rc = BNR_Pre_init( p4_peer_msg_handler );  /* specific to mpich-1 */
    rc = BNR_Init( );
    rc = BNR_Get_group( &mygroup );
    rc = BNR_Get_rank( mygroup, &myrank );
    rc = BNR_Get_size( mygroup, &myjsize );

    p4_dprintfl(10,"IGNORING SIGPIPE\n");
    SIGNAL_P4(SIGPIPE,SIG_IGN);
    
    sprintf(whoami_p4, "p%d_%d", myrank, (int)getpid());
    p4_global->num_in_proctable = myjsize; /* there really isn't any proctable  */

    p4_local->my_id	    = myrank;
    p4_local->my_job	    = BNR_Get_group_id( mygroup );  /* default jobid for now */

    setup_conntab();

    /* get fd for talking to the manager from the environment, where he put it after
       acquiring it. */
    rc = BNR_Man_msgs_fd( &(p4_local->parent_man_fd) );

#if 0
    /**********   This is done for us in the p4mpd device  **********/

    /* choose a working directory */
    if (strlen(p4_wd) && !chdir(p4_wd))
    {
	p4_dprintfl(90, "working directory for %d set to %s\n", p4_local->my_id, p4_wd);
    }
    else
    {
	strncpy(pgmname,argv[0],P4_MAX_PGM_LEN);
	pgmname[P4_MAX_PGM_LEN-1] = 0;
	if ((s = (char *) rindex(pgmname,'/'))  !=  NULL)
	{
	    *s = '\0';  /* chg to directory name only */
	    chdir(pgmname);
	    /* p4_dprintf("RMB bm_start: CHGING DIR TO %s\n",pgmname); */
	}
    }
#endif

#   ifdef SYSV_IPC
    sysv_num_shmids = 0;
    sysv_shmid[0]  = -1;
    sysv_semid0    = -1;
    sysv_semid0 = init_sysv_semset(0);
#   endif

    MD_initmem(globmemsize);

    if (*bm_outfile)
    {
	freopen(bm_outfile, "w", stdout);
	freopen(bm_outfile, "w", stderr);
    }

    MD_initenv();
    bm_switch_port = getswport(p4_global->my_host_name);
    usc_init();
    init_usclock();

    /* big master installing himself */
    install_in_proctable(0, (-1), getpid(), p4_global->my_host_name, 
			 p4_global->my_host_name, 
			 0, P4_MACHINE_TYPE, bm_switch_port);

    BNR_Fence( mygroup ); /* to make sure p4 data structures are set before interrupts
			     can occur */
    return (0);
}

void p4_peer_msg_handler( msg )
char *msg;
{
    char cmd[32] /*, tohostname[MAXLINE]*/;
    char c_torank[32],c_toport[32],c_toipaddr[32];
    int flags,torank,toport,connection_fd,num_tries,rc,connected,optval;
    int myid = p4_get_my_id();
    unsigned int toipaddr;
    struct sockaddr_in sa;

    p4_dprintfl(077,"p4_peer_msg_handler entered with msg :%s:\n",msg );
    if (strncmp(msg,"connect_to_me-",14) != 0)
    {
	p4_dprintf("invalid msg in p4_peer_msg_handler :%s:\n",msg);
	return;
    }
    sscanf(msg,"%[^-]-%[^-]-%[^-]-%s",cmd,c_torank,c_toipaddr,c_toport);
    torank = atoi(c_torank);
    toport = atoi(c_toport);
    toipaddr = inet_addr(c_toipaddr);
    bzero((void *)&sa, sizeof(struct sockaddr_in));
    bcopy((void *)&toipaddr, (void *)&sa.sin_addr, sizeof(unsigned int));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(toport);

    if (p4_local->conntab[torank].type == CONN_REMOTE_EST)
    {
	p4_dprintfl(077, "p4_peer_msg_handler: already conn'd to %d\n", torank);
	return;
    }
    if (p4_global->dest_id[myid] == torank)  /* already connecting */
    {
	if (myid > torank)
	{
	    p4_dprintfl(077, "p4_peer_msg_handler: already making conn to %d\n", torank);
	    return;
	}
    }

    connected = 0;
    num_tries = 3;
    while (!connected && num_tries) {
	SYSCALL_P4(connection_fd, socket(AF_INET, SOCK_STREAM, 0));
	if (connection_fd < 0)
	    p4_error("p4_peer_msg_handler socket", connection_fd);
	SYSCALL_P4(rc, connect(connection_fd, (struct sockaddr *) &sa,
			       sizeof(struct sockaddr_in)));
	if (rc < 0)
	{
	    close(connection_fd);
	    p4_dprintfl( 077, "Connect failed; closed socket %d\n", connection_fd );
	    if (--num_tries)
	    {
		p4_dprintfl(077,"p4_peer_msg_handler: connect to %s failed; will try %d more times \n",c_toipaddr,num_tries);
		sleep(1);
	    }
	}
	else
	{
	    connected = 1;
	    p4_dprintfl(077,"p4_peer_msg_handler: connected to %s\n",c_toipaddr);
	}

    }
    if ( ! connected)
    {
        p4_dprintf("p4_peer_msg_handler: failed connect to %s\n",c_toipaddr);
        p4_error("failed to connnect",-1);
    }

    flags = fcntl(connection_fd, F_GETFL, 0);
    if (flags < 0)
        p4_error("p4_bm fcntl1", flags); 
#   if defined(HP)
    flags |= O_NONBLOCK;
#   else
    flags |= O_NDELAY;
    p4_dprintfl(90, "p4_bm: setting ndelay for %d\n",connection_fd);
#   endif   
#   if defined(RS6000)
    flags |= O_NONBLOCK;
#   endif   
    flags = fcntl(connection_fd, F_SETFL, flags); 
    if (flags < 0)
        p4_error("p4_bm fcntl2", flags); 
    optval = 1;
    SYSCALL_P4(rc, setsockopt(connection_fd,IPPROTO_TCP,TCP_NODELAY,(char *) &optval,sizeof(optval)));

    p4_local->conntab[torank].type = CONN_REMOTE_EST;
    p4_local->conntab[torank].port = connection_fd;
    p4_local->conntab[torank].same_data_rep = P4_TRUE;
/*    p4_dprintfl(077, "p4_peer_msg_handler: connected after %d tries, connection_fd=%d host = %s\n",
      num_tries, connection_fd, tohostname); */
    p4_dprintfl(077, "p4_peer_msg_handler: connected after %d tries, connection_fd=%d\n",
		num_tries, connection_fd);

    /* We're connected, so we can add this connection to the table */
    p4_dprintfl(077, "marked as established fd=%d torank=%d\n",
		connection_fd, torank);
    p4_dprintfl(077,"p4_peer_msg_handler done\n" );
}


int p4_startup(pg)
struct p4_procgroup *pg;
{
    int nslaves;
    int listener_port, listener_fd;

    p4_dprintfl(90,"entering p4_startup\n");

    if (p4_global == NULL)
	p4_error("p4 not initialized; perhaps p4_initenv not called",0);

/* On some systems (SGI IRIX 6), process exit sometimes kills all processes
   in the process GROUP.  This code attempts to fix that.  
   We DON'T do it if stdin (0) is connected to a terminal, because that
   disconnects the process from the terminal.
 */
#if defined(HAVE_SETSID) && defined(HAVE_ISATTY) && defined(SET_NEW_PGRP)
if (!isatty(0)) {
    pid_t rc;
    rc = setsid();
    if (rc < 0) {
	p4_dprintfl( 90, "Could not create new process group\n" );
	}
    else {
	p4_dprintfl( 80, "Created new process group %d\n", rc );
	}
    }
else {
	p4_dprintfl( 80, 
         "Did not created new process group because isatty returned true\n" );
    }
#endif



    procgroup_to_proctable(pg);
    if (pg->num_entries > 1)
	p4_global->local_communication_only = P4_FALSE;

#   ifdef CAN_DO_SOCKET_MSGS
    if (!p4_global->local_communication_only)
    {
	net_setup_anon_listener(10, &listener_port, &listener_fd);
	p4_global->listener_port = listener_port;
	p4_global->listener_fd = listener_fd;
	p4_dprintfl(90, "setup listener on port %d fd %d\n",
		    listener_port, listener_fd);
	p4_global->proctable[0].port = listener_port;
#ifndef THREAD_LISTENER
	SIGNAL_P4(LISTENER_ATTN_SIGNAL, handle_connection_interrupt);
#endif
    }
    else
	p4_global->listener_fd = -1;
#   endif

    setup_conntab();

    p4_lock(&p4_global->slave_lock);
    if ((nslaves = create_bm_processes(pg)) < 0)
	return (-1);
    if (!p4_am_i_cluster_master())  /* I was forked in create_bm_processes */
	return(0);

#   ifdef CAN_DO_SOCKET_MSGS
    if (create_remote_processes(pg) < 0)
	return (-1);
#   endif

    /* let local slaves use proc table to identify themselves */
    p4_unlock(&p4_global->slave_lock); 

    send_proc_table();  /* to remote masters */

#   if defined(IPSC860)  ||  defined(CM5)     ||  defined(NCUBE)  \
                         ||  defined(SP1_EUI) ||  defined(SP1_EUIH)
     {
    struct bm_rm_msg bm_msg;
    struct p4_procgroup_entry *local_pg;
    int len, to, type;
    int i, unused_flag;

    /* send initial info and proctable to local slaves */
    /* must use p4_i_to_n procs because node slave 
       does not know if the msg is forwarded from bm */
    local_pg = &(pg->entries[0]);
    bm_msg.type = p4_i_to_n(INITIAL_INFO);
    bm_msg.numinproctab = p4_i_to_n(p4_global->num_in_proctable);
    bm_msg.numslaves = p4_i_to_n(local_pg->numslaves_in_group);
    bm_msg.debug_level = p4_i_to_n(p4_remote_debug_level);
    bm_msg.memsize = p4_i_to_n(globmemsize);
    bm_msg.logging_flag = p4_i_to_n(logging_flag);
    strcpy(bm_msg.application_id, p4_global->application_id);
    strcpy(bm_msg.version, P4_PATCHLEVEL);
    if (strlen( local_pg->slave_full_pathname ) >= P4_MAX_PGM_LEN) {
	p4_error("Program name is too long, must be less than", 
		 P4_MAX_PGM_LEN);
    }
    strcpy(bm_msg.pgm, local_pg->slave_full_pathname);
    strcpy(bm_msg.wdir, p4_wd);
    for (i = 1; i <= nslaves; i++)
    {
	p4_dprintfl(90,"sending initinfo to slave %d of %d\n",i,nslaves);
#       if defined(IPSC860)
	csend((long) INITIAL_INFO, &bm_msg, (long) sizeof(struct bm_rm_msg), 
	      (long) i, (long) NODE_PID);
	csend((long) INITIAL_INFO, p4_global->proctable, 
	      (long) sizeof(p4_global->proctable), (long) i, (long) NODE_PID);
#       endif
#       if defined(CM5)
	CMMD_send_noblock(i, INITIAL_INFO, &bm_msg, sizeof(struct bm_rm_msg));
	CMMD_send_noblock(i, INITIAL_INFO, p4_global->proctable, sizeof(p4_global->proctable));
#       endif
#       if defined(NCUBE)
	nwrite(&bm_msg, sizeof(struct bm_rm_msg), i, INITIAL_INFO, &unused_flag);
	nwrite(p4_global->proctable, sizeof(p4_global->proctable), i, INITIAL_INFO, &unused_flag);
#       endif
#       if defined(SP1_EUI)
	mpc_bsend(&bm_msg, sizeof(struct bm_rm_msg), i, INITIAL_INFO);
	mpc_bsend(p4_global->proctable, sizeof(p4_global->proctable), i, INITIAL_INFO);
#       endif
#       if defined(SP1_EUIH)
	len  = sizeof(struct bm_rm_msg);
	to   = i;
	type = INITIAL_INFO; 
	mp_bsend(&bm_msg, &len, &to, &type); 
	len  = sizeof(p4_global->proctable);
	mp_bsend(p4_global->proctable, &len, &to, &type);
#       endif
	p4_dprintfl(90,"sent initinfo to slave %d of %d\n",i,nslaves);
    }
     }   /* End of local declarations */
#   endif

    p4_global->low_cluster_id = 
	p4_local->my_id - p4_global->proctable[p4_local->my_id].slave_idx;
    p4_global->hi_cluster_id = 
	p4_global->low_cluster_id + p4_global->local_slave_count + 1;

    /* 
       sync with local slaves thus insuring that they have the proctable before 
       syncing with remotes (this keeps remotes from interrupting the local 
       processes too early; then re-sync with local slaves (thus permitting them
       to interrupt remotes)
    */

    p4_barrier(&(p4_global->cluster_barrier),p4_num_cluster_ids());
    /* 
       NEED A SYNC WITH LOCALS THAT DOES A BARRIER WITH PROCS THAT SHARE
       MEMORY AND MP BARRIER WITH OTHER "LOCAL" PROCESSES 
    */
    sync_with_remotes();
    p4_barrier(&(p4_global->cluster_barrier),p4_num_cluster_ids());


    return (0);
}

int create_bm_processes(pg)
struct p4_procgroup *pg;
{
    struct p4_procgroup_entry *local_pg;
    struct listener_data *l = NULL;
    int nslaves, end_1, end_2;
    int slave_pid, listener_pid = -1;
    int slave_idx, listener_fd = -1;
#   if defined(IPSC860)  ||  defined(CM5)  ||  defined(NCUBE)  ||  defined(SP1_EUI) || defined(SP1_EUIH)
    /* Message passing systems require additional information */
    struct bm_rm_msg bm_msg;
    int i;
    int port, switch_port, type, len, from, unused_flag
#endif
#   if defined(THREAD_LISTENER)
    p4_thread_t trc;
#   endif

    p4_dprintfl(90,"entering create_bm_processes\n");
    local_pg = &(pg->entries[0]);

    nslaves = local_pg->numslaves_in_group;
#   if !defined(IPSC860)  &&  !defined(CM5)  &&  !defined(NCUBE)  &&  !defined(SP1_EUI) && !defined(SP1_EUIH)
    if (nslaves > P4_MAX_MSG_QUEUES)
	p4_error("more slaves than msg queues \n", nslaves);
#   endif

/* alloc listener local data since this proc will eventually become listener */
#   if defined(CAN_DO_SOCKET_MSGS)  &&  !defined(NO_LISTENER)
    if (!(p4_global->local_communication_only))
    {
	listener_fd = p4_global->listener_fd;
	listener_info = alloc_listener_info(1);
	l = listener_info;
	get_pipe(&end_1, &end_2); /* used even by thread listener */
	l->slave_fd[0] = end_2;
    }
#   endif

#   ifdef TCMP
    tcmp_init(NULL,p4_get_my_cluster_id(),shmem_getclunid());
#   endif

#   if defined(IPSC860)  ||  defined(CM5)  ||  defined(NCUBE)  ||  defined(SP1_EUI) || defined(SP1_EUIH)
    for (i = 1; i <= nslaves; i++)
    {
	p4_dprintfl(90,"doing initial sync with local slave %d\n",i);
#       if defined(IPSC860)
	csend((long) SYNC_MSG, &bm_msg, (long) sizeof(struct bm_rm_msg), 
	      (long) i, (long) NODE_PID);
	crecv(INITIAL_INFO, &bm_msg, (long) sizeof(struct bm_rm_msg));
#       endif
#       if defined(CM5)
	CMMD_send_noblock(i, SYNC_MSG, &bm_msg, sizeof(struct bm_rm_msg));
        CMMD_receive(CMMD_ANY_NODE, INITIAL_INFO, (void *) &bm_msg, sizeof(struct bm_rm_msg));
#       endif
#       if defined(NCUBE)
	nwrite(&bm_msg, sizeof(struct bm_rm_msg), i, SYNC_MSG, &unused_flag);
        from = NCUBE_ANY_NODE;
        type = INITIAL_INFO;
        nread(&bm_msg, sizeof(struct bm_rm_msg), &from, &type, &unused_flag);
#       endif
#       if defined(SP1_EUI)
	mpc_bsend(&bm_msg, sizeof(struct bm_rm_msg), i, SYNC_MSG);
        from = ANY_P4TYPE_EUI;
        type = INITIAL_INFO;
        mpc_brecv(&bm_msg, sizeof(struct bm_rm_msg), &from, &type, &unused_flag);
#       endif
#       if defined(SP1_EUIH)
	len = sizeof(struct bm_rm_msg);
	type = SYNC_MSG;
	mp_bsend(&bm_msg, &len, &i, &type);
        from = ANY_P4TYPE_EUIH;
        type = INITIAL_INFO;
	len  = sizeof(struct bm_rm_msg);
        mp_brecv(&bm_msg, &len, &from, &type, &unused_flag);
#       endif
	port = p4_n_to_i(bm_msg.port);
	slave_idx = p4_n_to_i(bm_msg.slave_idx);
	slave_pid = p4_n_to_i(bm_msg.slave_pid);
	switch_port = p4_n_to_i(bm_msg.switch_port);
	/* big master installing local slaves */
	install_in_proctable(0, port, slave_pid, bm_msg.host_name, 
			     bm_msg.local_name, 
			     slave_idx, P4_MACHINE_TYPE, switch_port);
	p4_global->local_slave_count++;
    }
#   else
#if defined(SUN_SOLARIS)
/*****  Shyam code, removed by RL
    { processorid_t proc = 0;
      if(p_online(proc,P_STATUS) != P_ONLINE)
	printf("Could not bind parent to processor 0\n");
      else
	{
	  processor_bind(P_PID,P_MYID,proc, &proc);
	  printf("Bound parent to processor 0 , previous binding was %d\n",
		 proc);
	}
    }
*****/
#endif
    for (slave_idx = 1; slave_idx <= nslaves; slave_idx++)
    {
	p4_dprintfl(20, "creating local slave %d of %d\n",slave_idx,nslaves);
	slave_pid = fork_p4();
	if (slave_pid < 0)
	    p4_error("create_bm_processes fork", slave_pid);
	else
	    if (slave_pid)
		p4_dprintfl(10, "created local slave %d\n", slave_idx);
	if (slave_pid == 0)	/* At this point, we are the slave. */
	{
	    sprintf(whoami_p4, "bm_slave_%d_%d", slave_idx, (int)getpid());

	    p4_free(p4_local);	/* Doesn't work for weird memory model. */
	    p4_local = alloc_local_slave();

#           ifdef CAN_DO_SOCKET_MSGS
	    if (!(p4_global->local_communication_only))
	    {
		p4_local->listener_fd = end_1;
#               if !defined(THREAD_LISTENER)
		close(end_2);
#               endif
		close(listener_fd);
	    }
#ifndef THREAD_LISTENER
	    SIGNAL_P4(LISTENER_ATTN_SIGNAL, handle_connection_interrupt);
#endif
#           endif

	    /* hang for a valid proctable */
	    p4_lock(&p4_global->slave_lock);
	    p4_unlock(&p4_global->slave_lock);

	    p4_local->my_id = p4_get_my_id_from_proc();
#if defined(SUN_SOLARIS)
/*****  Shyam code, removed by RL
	    {
	      int no_processors;
	      processorid_t bindproc;
	      no_processors = sysconf(_SC_NPROCESSORS_ONLN);
	      bindproc = (p4_local->my_id) % no_processors;
	      if(p_online(bindproc,P_STATUS) != P_ONLINE)
		 {
		   printf("could not bind slave %d to processor %d",
		            p4_local->my_id, bindproc);
		 }
	      else
		 {
		   printf("Bound slave %d to processor %d\n",
			  p4_local->my_id,bindproc);
		   processor_bind(P_PID,P_MYID,bindproc, &bindproc);
                   printf("previous binding was %d\n",bindproc);
		 }
	    }
*****/
#endif
	    setup_conntab();
	    sprintf(whoami_p4, "p%d_%d", p4_local->my_id, (int)getpid());
	    usc_init();
	    init_usclock();

#           ifdef TCMP
            tcmp_init(NULL,p4_get_my_cluster_id(),shmem_getclunid());
#           endif

	    /* 
	       sync with master twice: once to make sure all slaves have 
	       got proctable, and second after the master has synced with the 
	       remote processes 
	    */
	    p4_barrier(&(p4_global->cluster_barrier),p4_num_cluster_ids());
	    p4_barrier(&(p4_global->cluster_barrier),p4_num_cluster_ids());

	    p4_dprintfl(20, "local slave starting\n");
	    ALOG_SETUP(p4_local->my_id,ALOG_TRUNCATE);
	    ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");
	    return (0);
	}

	/* master installing local slaves */
	install_in_proctable(0, p4_global->listener_port, slave_pid,
			     p4_global->my_host_name, 
			     p4_global->my_host_name, 
			     slave_idx, P4_MACHINE_TYPE,
			     p4_global->proctable[0].switch_port);
	p4_global->local_slave_count++;
    }
#   endif

#   if defined(CM5)
    for (i=nslaves+1; i < CMMD_partition_size(); i++)
        CMMD_send_noblock(i, DIE, &bm_msg, sizeof(struct bm_rm_msg));
#   endif
#   if defined(NCUBE)
    for (i=nslaves+1; i < ncubesize(); i++)
	nwrite(&bm_msg, sizeof(struct bm_rm_msg), i, DIE, &unused_flag);
#   endif
#   if defined(SP1_EUI)
    for (i=nslaves+1; i < eui_numtasks; i++)
	mpc_bsend(&bm_msg, sizeof(struct bm_rm_msg), i, DIE);
#   endif
#   if defined(SP1_EUIH)
    for (i=nslaves+1; i < euih_numtasks; i++)
    {
	len = sizeof(struct bm_rm_msg);
	type = DIE;
	mp_bsend(&bm_msg, &len, &i, &type);
    }
#   endif


    /* Done creating slaves. Now fork off the listener */

#   if !defined(IPSC860)  &&  !defined(CM5)  &&  !defined(NCUBE)  &&  !defined(SP1_EUI) && !defined(SP1_EUIH)

#   if defined(CAN_DO_SOCKET_MSGS)  &&  !defined(NO_LISTENER) && !defined(THREAD_LISTENER)
    if (!(p4_global->local_communication_only))
    {
	listener_pid = fork_p4();
	if (listener_pid < 0)
	    p4_error("create_bm_processes listener fork", listener_pid);
	if (listener_pid == 0)
	{
	    sprintf(whoami_p4, "bm_list_%d", (int)getpid());
	    /* Inside listener */
	    p4_local = alloc_local_listener();
	    l->listening_fd = listener_fd;
	    l->slave_fd[0] = end_2;
	    close(end_1);
	    {
		/* exec external listener process */

		char *listener_prg = LISTENER_PATHNAME;

		if (*listener_prg)
		{
		    char dbg_c[10], max_c[10], lfd_c[10], sfd_c[10];

		    sprintf(dbg_c, "%d", p4_debug_level);
		    sprintf(max_c, "%d", p4_global->max_connections);
		    sprintf(lfd_c, "%d", l->listening_fd);
		    sprintf(sfd_c, "%d", l->slave_fd[0]);
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
    }
#   endif

    /* Else we're still in the big master */
    sprintf(whoami_p4, "p0_%d", (int)getpid());

#   if defined(THREAD_LISTENER)
    /* 
       If there is only one process, then we will not have created a 
       listener port or corresponding fd.  In that case, we don't need
       the listener (it just does an while(1) {accept(listener_fd);...},
       so there isn't anything to do if only a single process is running).
     */
    if (p4_global->listener_fd >= 0) {
	p4_dprintfl(50,"creating listener thread\n");
	/* pthread_mutex_init( &p4_local->conntab_lock, 0 ); */
	p4_create_thread( trc, thread_listener, 66 );
	p4_dprintfl(50,"created listener thread\n");
    }
    /* else
	trc = 0; */
    /* NT version put the last arg of CreateThread into listener_pid */
#   endif

    /* We need to close the fds from the listener setup */
#   if defined(CAN_DO_SOCKET_MSGS)  &&  !defined(NO_LISTENER) 
    if (!(p4_global->local_communication_only))
    {
	p4_local->listener_fd = end_1;
#       if !defined(THREAD_LISTENER)
	close(listener_fd);
	close(end_2);
#       endif
	p4_global->listener_pid = listener_pid;
    }
#   endif

#   endif

    dump_global(80);
    p4_dprintfl(90, "create_bm_processes: exiting\n");
    return (nslaves);
}


P4VOID procgroup_to_proctable(pg)
struct p4_procgroup *pg;
{
    int i, j, ptidx;
    struct p4_procgroup_entry *pe;

    if (strcmp(pg->entries[0].host_name,"local") == 0)
    {
	strcpy(p4_global->proctable[0].host_name,p4_global->my_host_name);
    }
    else
    {
	p4_dprintfl(10,"hostname in first line of procgroup is %s\n",
		    pg->entries[0].host_name);
	strcpy(p4_global->my_host_name,pg->entries[0].host_name);
	strcpy(p4_global->proctable[0].host_name,pg->entries[0].host_name);
    }
    get_qualified_hostname(p4_global->proctable[0].host_name, HOSTNAME_LEN);
    p4_dprintfl(10,"hostname for first entry in proctable is %s\n",
		p4_global->proctable[0].host_name);
    p4_global->proctable[0].group_id = 0;
    ptidx = 1;
    for (i=0, pe=pg->entries; i < pg->num_entries; i++, pe++)
    {
	for (j=0; j < pe->numslaves_in_group; j++)
	{
	    if (i == 0)
		strcpy(p4_global->proctable[ptidx].host_name,
		       p4_global->proctable[0].host_name);
	    else
		strcpy(p4_global->proctable[ptidx].host_name,pe->host_name);
	    get_qualified_hostname(p4_global->proctable[ptidx].host_name,
				   HOSTNAME_LEN);
	    p4_global->proctable[ptidx].group_id = i;
#           ifdef CAN_DO_SOCKET_MSGS
	    {
	    struct hostent *hp = 
		gethostbyname_p4(p4_global->proctable[ptidx].host_name);
	    struct sockaddr_in *listener_sin = 
		&p4_global->proctable[ptidx].sockaddr;
	    bzero( (P4VOID*) listener_sin, sizeof(struct sockaddr_in) );
	    bcopy((P4VOID *) hp->h_addr, (P4VOID *)&listener_sin->sin_addr, 
		  hp->h_length);
	    listener_sin->sin_family = hp->h_addrtype;
	    /* Set a dummy port so that we can detect that the field
	       has been initialized */
	    listener_sin->sin_port = 1;
	    }
#           endif
	    ptidx++;
	}
	p4_global->num_in_proctable = ptidx;
    }
}

P4VOID sync_with_remotes()
{
    struct bm_rm_msg msg;
    int i, fd, node, num_rms, rm[P4_MAXPROCS];

    p4_dprintfl(90, "sync_with_remotes: starting\n");

#   ifdef CAN_DO_SOCKET_MSGS
    p4_get_cluster_masters(&num_rms, rm);
    for (i = 1; i < num_rms; i++)
    {
	node = rm[i];
	fd = p4_local->conntab[node].port;
	net_recv(fd, &msg, sizeof(msg));
	msg.type = p4_n_to_i(msg.type);
	if (msg.type != SYNC_MSG)
	    p4_error("sync_with_remotes: bad type rcvd\n",msg.type);
    }
    for (i = 1; i < num_rms; i++)
    {
	node = rm[i];
	fd = p4_local->conntab[node].port;
	msg.type = p4_i_to_n(SYNC_MSG);
	net_send(fd, &msg, sizeof(msg), P4_FALSE);
    }
#   endif
}

P4VOID send_proc_table()
{
    int slave_idx, ent;
    int fd;
    struct bm_rm_msg msg;
    struct proc_info *pe;

    p4_dprintfl(90, "send_proc_table: starting\n");

#   ifdef CAN_DO_SOCKET_MSGS
    for (slave_idx = 1; slave_idx < p4_global->num_in_proctable; slave_idx++)
    {
	if (p4_global->proctable[slave_idx].slave_idx != 0)
	    continue;

	fd = p4_local->conntab[slave_idx].port;

	p4_dprintfl(90, "sending proctable to slave %d on %d:\n", slave_idx, fd);
	if (fd < 0)
	    p4_error("send_proc_table: rm entry doesn't have valid fd", fd);

	for (ent = 0, pe = p4_global->proctable;
	     ent < p4_global->num_in_proctable; ent++, pe++)
	{
	    msg.type = p4_i_to_n(PROC_TABLE_ENTRY);
	    msg.port = p4_i_to_n(pe->port);
	    msg.unix_id = p4_i_to_n(pe->unix_id);
	    msg.slave_idx = p4_i_to_n(pe->slave_idx);
	    msg.group_id = p4_i_to_n(pe->group_id);
	    strcpy(msg.host_name, pe->host_name);
	    strcpy(msg.machine_type,pe->machine_type);
	    msg.switch_port = p4_i_to_n(pe->switch_port);
	    net_send(fd, &msg, sizeof(msg), P4_FALSE);
	    p4_dprintfl(90, "%s sent proctable entry to slave %d: %s \n", 
                        p4_global->proctable[0].host_name,
                        ent+1, pe->host_name);
	}
	p4_dprintfl(90, "  sending end_of_proc_table\n");
	msg.type = p4_i_to_n(PROC_TABLE_END);
	net_send(fd, &msg, sizeof(msg), P4_FALSE);
    }
#   endif
}
