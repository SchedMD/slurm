/*****************************************************************************\
 *  ll_msg_thread.c - Used to respond to pings from slurmctld. 
 * 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Danny Auble <da@llnl.gov>
 * 
 *  This file is part of slurm_ll_api, a collection of LoadLeveler-compatable
 *  interfaces to Simple Linux Utility for Resource Managment (SLURM).  These 
 *  interfaces are used by POE (IBM's Parallel Operating Environment) to 
 *  initiated SLURM jobs. For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  This notice is required to be provided under our contract with the U.S.
 *  Department of Energy (DOE).  This work was produced at the University
 *  of California, Lawrence Livermore National Laboratory under Contract
 *  No. W-7405-ENG-48 with the DOE.
 * 
 *  Neither the United States Government nor the University of California
 *  nor any of their employees, makes any warranty, express or implied, or
 *  assumes any liability or responsibility for the accuracy, completeness,
 *  or usefulness of any information, apparatus, product, or process
 *  disclosed, or represents that its use would not infringe
 *  privately-owned rights.
 *
 *  Also, reference herein to any specific commercial products, process, or
 *  services by trade name, trademark, manufacturer or otherwise does not
 *  necessarily constitute or imply its endorsement, recommendation, or
 *  favoring by the United States Government or the University of
 *  California.  The views and opinions of authors expressed herein do not
 *  necessarily state or reflect those of the United States Government or
 *  the University of California, and shall not be used for advertising or
 *  product endorsement purposes.
 * 
 *  The precise terms and conditions for copying, distribution and
 *  modification are specified in the file "COPYING".
\*****************************************************************************/

#include "ll_msg_thread.h"

slurmctld_comm_addr_t slurmctld_comm_addr;

static slurm_fd slurmctld_fd   = (slurm_fd) NULL;
static int message_thread = 0;
		
static slurm_fd _slurmctld_msg_init(void);
static int _do_poll(struct pollfd *fds, int timeout);
static void _msg_thr_poll(forked_msg_t *forked_msg);
static int _job_msg_done(forked_msg_t *forked_msg);
//static void _do_poll_timeout(forked_msg_t *forked_msg);
static int _get_next_timeout(forked_msg_t *forked_msg);
static void _accept_msg_connection(forked_msg_t *forked_msg);
static void _handle_msg(forked_msg_t *forked_msg, slurm_msg_t *msg);

void * msg_thr(void *arg)
{
	forked_msg_t *forked_msg = (forked_msg_t *) arg;
	forked_msg_pipe_t *par_msg = forked_msg->par_msg;

	VERBOSE("msg thread pid = %lu\n", (unsigned long) getpid());

	_msg_thr_poll(forked_msg);

	close(par_msg->msg_pipe[1]); // close excess fildes
	VERBOSE("msg thread done\n");	
	return (void *)1;
}

void * par_thr(void *arg)
{
	forked_msg_t *forked_msg = (forked_msg_t *) arg;
	forked_msg_pipe_t *par_msg = forked_msg->par_msg;
	forked_msg_pipe_t *msg_par = forked_msg->msg_par;
	int c;
	pipe_enum_t type=0;
	int tid=-1;
	//int nodeid=-1;
	int status;
	VERBOSE("par thread pid = %lu\n", (unsigned long) getpid());

	close(msg_par->msg_pipe[0]); // close read end of pipe
	close(par_msg->msg_pipe[1]); // close write end of pipe 
	while(read(par_msg->msg_pipe[0],&c,sizeof(int))>0) {
		// getting info from msg thread
		if(type == PIPE_NONE) {
			//debug2("got type %d\n",c);
			type = c;
			continue;
		} 

		if(type == PIPE_JOB_STATE) {
			//update_job_state(job, c);
		} else if(type == PIPE_TASK_STATE) {
/*  			if(tid == -1) { */
/*  				tid = c; */
/*  				continue; */
/*  			} */
/*  			slurm_mutex_lock(&job->task_mutex); */
/*  			job->task_state[tid] = c; */
/*  			if(c == SRUN_TASK_FAILED) */
/*  				tasks_exited++; */
/*  			slurm_mutex_unlock(&job->task_mutex); */
			
/*  			tid = -1; */
		} else if(type == PIPE_HOST_STATE) {
			if(tid == -1) {
				tid = c;
				continue;
			}
	/*  		slurm_mutex_lock(&job->task_mutex); */
/*  			job->host_state[tid] = c; */
/*  			slurm_mutex_unlock(&job->task_mutex); */
/*  			tid = -1; */
		} else if(type == PIPE_SIGNALED) {
/*  			slurm_mutex_lock(&job->state_mutex); */
/*  			job->signaled = c; */
/*  			slurm_mutex_unlock(&job->state_mutex); */
		} else if(type == PIPE_MPIR_PROCTABLE_SIZE) {
/*  			if(MPIR_proctable_size == 0) { */
/*  				MPIR_proctable_size = c; */
/*  				MPIR_proctable =  */
/*  					xmalloc(sizeof(MPIR_PROCDESC) * c); */
/*  			}		 */
		} else if(type == PIPE_MPIR_TOTALVIEW_JOBID) {
/*  			totalview_jobid = NULL; */
/*  			xstrfmtcat(totalview_jobid, "%lu", c); */
		} else if(type == PIPE_MPIR_PROCDESC) {
/*  			if(tid == -1) { */
/*  				tid = c; */
/*  				continue; */
/*  			} */
/*  			if(nodeid == -1) { */
/*  				nodeid = c; */
/*  				continue; */
/*  			} */
/*  			MPIR_PROCDESC *tv   = &MPIR_proctable[tid]; */
/*  			tv->host_name       = job->host[nodeid]; */
/*  			tv->executable_name = remote_argv[0]; */
/*  			tv->pid             = c; */
/*  			tid = -1; */
/*  			nodeid = -1; */
		} else if(type == PIPE_MPIR_DEBUG_STATE) {
/*  			MPIR_debug_state = c; */
/*  			MPIR_Breakpoint(); */
		}
		type = PIPE_NONE;
		
	}
	close(par_msg->msg_pipe[0]); // close excess fildes    
	close(msg_par->msg_pipe[1]); // close excess fildes
	if(waitpid(par_msg->pid,&status,0)<0) // wait for pid to finish
		return (void *)0;// there was an error
	VERBOSE("par thread done\n");
	return (void *)1;
}

extern int msg_thr_create(forked_msg_t *forked_msg)
{
	int c;
	
	forked_msg->par_msg = malloc(sizeof(forked_msg_pipe_t));
	forked_msg->msg_par = malloc(sizeof(forked_msg_pipe_t));
	forked_msg_pipe_t *par_msg = forked_msg->par_msg;
	forked_msg_pipe_t *msg_par = forked_msg->msg_par;
	pthread_t thread_agent;
	pthread_attr_t attr;
	
	/* Set up slurmctld message handler */
	_slurmctld_msg_init();

	if (pipe(par_msg->msg_pipe) == -1) 
		return SLURM_ERROR; // there was an error
	if (pipe(msg_par->msg_pipe) == -1) 
		return SLURM_ERROR; // there was an error
	VERBOSE("created the pipes for communication\n");
	if((par_msg->pid = fork()) == -1)   
		return SLURM_ERROR; // there was an error
	else if (par_msg->pid == 0) 
	{                       // child:                       
		setsid();  
		message_thread = 1;
		close(par_msg->msg_pipe[0]); // close read end of pipe
		close(msg_par->msg_pipe[1]); // close write end of pipe
		
		if (pthread_attr_init(&attr)) 
			exit(0);
		if (pthread_attr_setstacksize(&attr, 1024*1024))
			ERROR("pthread_attr_setstacksize: %m"); 
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if ((errno = pthread_create(&thread_agent, &attr, msg_thr,
					    (void *)forked_msg))) {
			ERROR("FATAL: "
			      "Unable to start msg to parent thread: %m");
			exit(0);
		}
		//debug("Started msg to parent server thread (%lu)", 
			//(unsigned long) job->jtid);
		
		while(read(msg_par->msg_pipe[0],&c,sizeof(int))>0)
			; // make sure my parent doesn't leave me hangin
		
		close(msg_par->msg_pipe[0]); // close excess fildes    
		free(forked_msg);	
		free(par_msg);	
		free(msg_par);	
		_exit(0);
	}
	else 
	{ // parent:   
		
		if (pthread_attr_init(&attr)) 
			exit(0);
		if (pthread_attr_setstacksize(&attr, 1024*1024))
			ERROR("pthread_attr_setstacksize: %m");
		
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if ((errno = pthread_create(&thread_agent, &attr, par_thr, 
					    (void *)forked_msg))) {
			ERROR("FATAL: "
			      "Unable to start parent to msg thread: %m");
			exit(0);
		}
		//debug("Started parent to msg server thread (%lu)", 
			//(unsigned long) job->jtid);
	}

	
	return SLURM_SUCCESS;
}

/* Set up port to handle messages from slurmctld */
static slurm_fd _slurmctld_msg_init(void)
{
	slurm_addr slurm_address;
	char hostname[64];
	uint16_t port;

	if (slurmctld_fd)	/* May set early for queued job allocation */
		return slurmctld_fd;

	slurmctld_fd = -1;
	slurmctld_comm_addr.hostname = NULL;
	slurmctld_comm_addr.port = 0;

	if ((slurmctld_fd = slurm_init_msg_engine_port(0)) < 0) {
		ERROR("FATAL: "
		      "slurm_init_msg_engine_port error %m");
		exit(0);
	}
	if (slurm_get_stream_addr(slurmctld_fd, &slurm_address) < 0) {
		ERROR("FATAL: "
		      "slurm_get_stream_addr error %m");
		exit(0);
	}
	fd_set_nonblocking(slurmctld_fd);
	/* hostname is not set, so slurm_get_addr fails
	slurm_get_addr(&slurm_address, &port, hostname, sizeof(hostname)); */
	port = slurm_address.sin_port;
	getnodename(hostname, sizeof(hostname));
	slurmctld_comm_addr.hostname = strdup(hostname);
	slurmctld_comm_addr.port     = ntohs(port);
	VERBOSE("slurmctld messasges to host=%s,port=%u\n", 
	 		slurmctld_comm_addr.hostname,
 			slurmctld_comm_addr.port);

	return slurmctld_fd;
}

/*
 *  Call poll() with a timeout. (timeout argument is in seconds)
 * NOTE: One extra FD for incoming slurmctld messages
 */
static int _do_poll(struct pollfd *fds, int timeout)
{
	nfds_t nfds = 1;
	int rc, to;

	if (timeout > 0)
		to = timeout * 1000;
	else
		to = timeout;
	
	while ((rc = poll(fds, nfds, to)) < 0) {
		switch (errno) {
		case EAGAIN:
		case EINTR:  continue;
		case ENOMEM:
		case EINVAL:
		case EFAULT: ERROR("FATAL: poll: %m\n");
			exit(0);
			break;
		default:     ERROR("poll: %m. Continuing...\n");
			     continue;
		}
	}

	return rc;
}

/* NOTE: One extra FD for incoming slurmctld messages */
static void _msg_thr_poll(forked_msg_t *forked_msg)
{
	struct pollfd *fds;
	int i = 0;
	int count = 0;
	fds = malloc(sizeof(*fds));
	_poll_set_rd(fds[i], slurmctld_fd);
	 
	while (!_job_msg_done(forked_msg)) {
		count++;
		if (_do_poll(fds, _get_next_timeout(forked_msg)) == 0) {
			//_do_poll_timeout(forked_msg);
			//continue;
		}
	
		unsigned short revents = fds[i].revents;
		
		if ((revents & POLLERR) || 
		    (revents & POLLHUP) ||
		    (revents & POLLNVAL))
			ERROR("poll error on jfd %d: %m\n", (int)fds[i].fd);
		else if (revents & POLLIN) 
			_accept_msg_connection(forked_msg);
	}
	free(fds);	/* if we were to break out of while loop */
}

static int _job_msg_done(forked_msg_t *forked_msg)
{
	return (*forked_msg->job_state >= JOB_COMPLETE);
}

/*
 *  Handle the two poll timeout cases:
 *    1. Job launch timed out
 *    2. Exit timeout has expired (either print a message or kill job)
 */
/*  static void _do_poll_timeout(slurm_job_init_t *job) */
/*  { */
/*  	time_t now = time(NULL); */

/*  	if ((job->etimeout > 0) && (job->etimeout <= now)) { */
/*  		//report_task_status(job); */
/*  		//update_job_state(job, JOB_FAILED); */
/*  		job->etimeout = 0; */
/*  	} */
/*  } */

/*
 *  Get the next timeout in seconds from now.
 */
 static int _get_next_timeout(forked_msg_t *forked_msg)
 {
 	int timeout = 10;

 	/* if (!forked_msg->ltimeout && !forked_msg->etimeout) */
/*  		return -1; */

/*  	if (!forked_msg->ltimeout) */
/*  		timeout = forked_msg->etimeout - time(NULL); */
/*  	else if (!forked_msg->etimeout) */
/*  		timeout = forked_msg->ltimeout - time(NULL); */
/*  	else */
/*  		timeout = forked_msg->ltimeout < forked_msg->etimeout ? */
/*  			  forked_msg->ltimeout - time(NULL) : */
/*  			  forked_msg->etimeout - time(NULL); */

 	return timeout;
 }

/* NOTE: One extra FD for incoming slurmctld messages */
static void _accept_msg_connection(forked_msg_t *forked_msg)
{
	slurm_fd     fd = (slurm_fd) NULL;
	slurm_msg_t *msg = NULL;
	slurm_addr   cli_addr;
	unsigned char *uc;
	short        port;
	int          timeout = 0;	/* slurm default value */

	
	fd = slurm_accept_msg_conn(slurmctld_fd, &cli_addr);
	
	if (fd < 0) {
		ERROR("Unable to accept connection: %m");
		return;
	}

	/* Should not call slurm_get_addr() because the IP may not be
	   in /etc/hosts. */
	uc = (unsigned char *)&cli_addr.sin_addr.s_addr;
	port = cli_addr.sin_port;
	//debug2("got message connection from %u.%u.%u.%u:%d",
	//uc[0], uc[1], uc[2], uc[3], ntohs(port));

	msg = malloc(sizeof(*msg));

again:
	if (slurm_receive_msg(fd, msg, timeout) < 0) {
		if (errno == EINTR)
			goto again;
		ERROR("slurm_receive_msg[%u.%u.%u.%u]: %m",
		      uc[0],uc[1],uc[2],uc[3]);
		free(msg);
	} else {

		msg->conn_fd = fd;
		_handle_msg(forked_msg, msg); /* handle_msg frees msg */
	}

	slurm_close_accepted_conn(fd);
	return;
}

static void _handle_msg(forked_msg_t *forked_msg, slurm_msg_t *msg)
{
/*  	srun_timeout_msg_t *to; */
/*  	srun_node_fail_msg_t *nf; */

	switch (msg->msg_type) {
	case RESPONSE_LAUNCH_TASKS:
		VERBOSE("recvd lauch tasks response\n");
		slurm_free_launch_tasks_response_msg(msg->data);
		break;
	case MESSAGE_TASK_EXIT:
		VERBOSE("recvd message task exit\n");
		slurm_free_task_exit_msg(msg->data);
		break;
	case RESPONSE_REATTACH_TASKS:
		VERBOSE("recvd reattach response\n");
		//_reattach_handler(job, msg);
		slurm_free_reattach_tasks_response_msg(msg->data);
		break;
	case SRUN_PING:
		VERBOSE("slurmctld ping received\n");
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		//free(msg->data);
 			
		slurm_free_srun_ping_msg(msg->data);
		break;
	case SRUN_TIMEOUT:
		VERBOSE("slurmctld timeout received\n");
		/* to = msg->data; */
/*  			_timeout_handler(to->timeout); */
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		slurm_free_srun_timeout_msg(msg->data);
		break;
	case SRUN_NODE_FAIL:
		VERBOSE("slurmctld node fail received\n");
		/* nf = msg->data; */
/*  			_node_fail_handler(nf->nodelist, job); */
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		slurm_free_srun_node_fail_msg(msg->data);
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		VERBOSE("resource allocation response received\n");
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		slurm_free_resource_allocation_response_msg(msg->data);
		break;
	default:
		ERROR("received spurious message type: %d\n",
		      msg->msg_type);
		break;
	}
	g_slurm_auth_destroy(msg->cred);
	free(msg);
	//slurm_free_msg(msg);
	return;
}
