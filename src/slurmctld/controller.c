/*****************************************************************************\
 *  controller.c - main control machine daemon for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, Kevin Tew <tew1@llnl.gov>, et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <slurm/slurm_errno.h>

#include "src/common/daemonize.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"

#if HAVE_LIBELAN3
#  include "src/common/qsw.h"
#endif

#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#define BUF_SIZE	  1024	/* Temporary buffer size */
#define CRED_LIFE         60	/* Job credential lifetime in seconds */
#define DEFAULT_DAEMONIZE 1	/* Run as daemon by default if set */
#define DEFAULT_RECOVER   1	/* Recover state by default if set */
#define MIN_CHECKIN_TIME  3	/* Nodes have this number of seconds to 
				 * check-in before we ping them */
#define MAX_SERVER_THREADS 20	/* Max threads to service RPCs */
#define MEM_LEAK_TEST	  0	/* Running memory leak test if set */
#define DEFAULT_PIDFILE   "/var/run/slurmctld.pid"

#ifndef MAX
#  define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#endif				/* !MAX */

/* Log to stderr and syslog until becomes a daemon */
log_options_t log_opts = LOG_OPTS_INITIALIZER;

/* Global variables */
slurm_ctl_conf_t slurmctld_conf;

/* Local variables */
static int	daemonize = DEFAULT_DAEMONIZE;
static int	debug_level = 0;
static char	*debug_logfile = NULL;
static int	recover   = DEFAULT_RECOVER;
static bool	resume_backup = false;
static time_t	shutdown_time = (time_t) 0;
static int	server_thread_count = 0;
static pid_t	slurmctld_pid;
static slurm_cred_ctx_t cred_ctx;

#ifdef WITH_PTHREADS
	static pthread_mutex_t thread_count_lock = PTHREAD_MUTEX_INITIALIZER;
	static pthread_t thread_id_main = (pthread_t) 0;
	static pthread_t thread_id_sig  = (pthread_t) 0;
	static pthread_t thread_id_rpc  = (pthread_t) 0;
#else
	static int thread_count_lock = 0;
	static int thread_id_main = 0;
	static int thread_id_sig  = 0;
	static int thread_id_rpc  = 0;
#endif		

static int          _background_process_msg(slurm_msg_t * msg);
static void *       _background_rpc_mgr(void *no_data);
static void *       _background_signal_hand(void *no_data);
static void         _fill_ctld_conf(slurm_ctl_conf_t * build_ptr);
static int          _make_step_cred(struct step_record *step_rec, 
				    slurm_cred_t *slurm_cred);
static void         _parse_commandline(int argc, char *argv[], 
                                       slurm_ctl_conf_t *);
static int          _ping_controller(void);
inline static int   _report_locks_set(void);
static void         _run_backup(void);
inline static void  _save_all_state(void);
static void *       _service_connection(void *arg);
static int          _set_slurmctld_state_loc(void);
inline static void  _slurm_rpc_allocate_resources(slurm_msg_t * msg);
inline static void  _slurm_rpc_allocate_and_run(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_conf(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_nodes(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_partitions(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_jobs(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_step_kill(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_step_complete(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_step_create(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_step_get_info(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_will_run(slurm_msg_t * msg);
inline static void  _slurm_rpc_node_registration(slurm_msg_t * msg);
inline static void  _slurm_rpc_old_job_alloc(slurm_msg_t * msg);
inline static void  _slurm_rpc_ping(slurm_msg_t * msg);
inline static void  _slurm_rpc_reconfigure_controller(slurm_msg_t * msg);
inline static void  _slurm_rpc_shutdown_controller(slurm_msg_t * msg);
inline static void  _slurm_rpc_shutdown_controller_immediate(slurm_msg_t *
							     msg);
inline static void  _slurm_rpc_submit_batch_job(slurm_msg_t * msg);
inline static void  _slurm_rpc_update_job(slurm_msg_t * msg);
inline static void  _slurm_rpc_update_node(slurm_msg_t * msg);
inline static void  _slurm_rpc_update_partition(slurm_msg_t * msg);
static void *       _slurmctld_background(void *no_data);
static void         _slurmctld_req(slurm_msg_t * msg);
static void *       _slurmctld_rpc_mgr(void *no_data);
static void         _init_pidfile(void);
inline static int   _slurmctld_shutdown(void);
static void *       _slurmctld_signal_hand(void *no_data);
inline static void  _update_cred_key(void);
inline static void  _usage(char *prog_name);

typedef struct connection_arg {
	int newsockfd;
} connection_arg_t;

/* main - slurmctld main function, start various threads and process RPCs */
int main(int argc, char *argv[])
{
	int error_code;
	char node_name[MAX_NAME_LEN];
	pthread_attr_t thread_attr_sig, thread_attr_rpc;
	sigset_t set;
	struct rlimit rlim;

	/*
	 * Establish initial configuration
	 */
	log_init(argv[0], log_opts, LOG_DAEMON, NULL);
	thread_id_main = pthread_self();

	slurmctld_pid = getpid();
	slurmctld_conf.slurm_conf = xstrdup(SLURM_CONFIG_FILE);
	_parse_commandline(argc, argv, &slurmctld_conf);
	init_locks();

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_NOFILE, &rlim);
	}
	if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CORE, &rlim);
	}

	if ((error_code = read_slurm_conf(recover))) {
		error("read_slurm_conf error %d reading %s",
		      error_code, SLURM_CONFIG_FILE);
		exit(1);
	}

	if (switch_state_begin(recover)) {
		error("switch_state_begin: %m");
		exit(1);
	}

	/* 
	 * Need to create pidfile here in case we setuid() below
	 * (init_pidfile() exits if it can't initialize pid file)
	 */
	_init_pidfile();

	if ((slurmctld_conf.slurm_user_id) && 
	    (slurmctld_conf.slurm_user_id != getuid()) &&
	    (setuid(slurmctld_conf.slurm_user_id))) {
		error("setuid(%d): %m", slurmctld_conf.slurm_user_id);
		exit(1);
	}

	/* 
	 * Create StateSaveLocation directory if necessary, and chdir() to it.
	 */
	if (_set_slurmctld_state_loc() < 0) {
		error("Unable to initialize StateSaveLocation");
		exit(1);
	}

	if (daemonize) {
		error_code = daemon(1, 1);
		log_alter(log_opts, LOG_DAEMON, 
			  slurmctld_conf.slurmctld_logfile);
		if (error_code)
			error("daemon error %d", error_code);
	}

	if ((error_code = getnodename(node_name, MAX_NAME_LEN)))
		fatal("getnodename error %s", slurm_strerror(error_code));

	/* init job credential stuff */
	cred_ctx = slurm_cred_creator_ctx_create(slurmctld_conf.
						 job_credential_private_key);
	if (!cred_ctx)
		fatal("slurm_cred_creator_ctx_create: %m");


	/* Not used in creator
	 *
	 * slurm_cred_ctx_set(cred_ctx, 
	 *                    SLURM_CRED_OPT_EXPIRY_WINDOW, CRED_LIFE);
	 */

	/* Block SIGALRM everyone not explicitly enabled */
	if (sigemptyset(&set))
		error("sigemptyset error: %m");
	if (sigaddset(&set, SIGALRM))
		error("sigaddset error on SIGALRM: %m");
	if (sigprocmask(SIG_BLOCK, &set, NULL) != 0)
		fatal("sigprocmask error: %m");

	while (1) {
		/* initialization */
		shutdown_time = (time_t) 0;
		resume_backup = false;

		/* start in primary or backup mode */
		if (slurmctld_conf.backup_controller &&
		    (strcmp(node_name,
			    slurmctld_conf.backup_controller) == 0))
			_run_backup();
		else if (slurmctld_conf.control_machine &&
			 (strcmp(node_name, slurmctld_conf.control_machine) 
			  == 0))
			debug3("Running primary controller");
		else {
			error
			    ("this host (%s) not valid controller (%s or %s)",
			     node_name, slurmctld_conf.control_machine,
			     slurmctld_conf.backup_controller);
			exit(0);
		}

		/*
		 * create attached thread for signal handling
		 */
		if (pthread_attr_init(&thread_attr_sig))
			fatal("pthread_attr_init error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		/* we want 1:1 threads if there is a choice */
		if (pthread_attr_setscope
		    (&thread_attr_sig, PTHREAD_SCOPE_SYSTEM))
			error("pthread_attr_setscope error %m");
#endif
		if (pthread_create(&thread_id_sig, &thread_attr_sig,
				   _slurmctld_signal_hand, NULL))
			fatal("pthread_create %m");

		/*
		 * create attached thread to process RPCs
		 */
		slurm_mutex_lock(&thread_count_lock);
		server_thread_count++;
		slurm_mutex_unlock(&thread_count_lock);
		if (pthread_attr_init(&thread_attr_rpc))
			fatal("pthread_attr_init error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		/* we want 1:1 threads if there is a choice */
		if (pthread_attr_setscope
		    (&thread_attr_rpc, PTHREAD_SCOPE_SYSTEM))
			error("pthread_attr_setscope error %m");
#endif
		if (pthread_create(&thread_id_rpc, &thread_attr_rpc,
				   _slurmctld_rpc_mgr, NULL))
			fatal("pthread_create error %m");

		_slurmctld_background(NULL);	/* could run as pthread */
		if (resume_backup == false)
			break;
	}

#if	MEM_LEAK_TEST
	/* This should purge all allocated memory,   *\
	\*   Anything left over represents a leak.   */
	sleep(5);	/* give running agents a chance to complete */
	agent_purge();
	job_fini();
	part_fini();	/* part_fini() must preceed node_fini() */
	node_fini();
	slurm_cred_ctx_destroy(cred_ctx);
	init_slurm_conf(&slurmctld_conf);
	xfree(slurmctld_conf.slurm_conf);
#endif
	log_fini();

	return SLURM_SUCCESS;
}

/* _slurmctld_signal_hand - Process daemon-wide signals */
static void *_slurmctld_signal_hand(void *no_data)
{
	int sig;
	int error_code;
	sigset_t set;
	char *pidfile = "/var/run/slurmctld.pid";
	/* Locks: Write configuration, job, node, and partition */
	slurmctld_lock_t config_write_lock = { WRITE_LOCK, WRITE_LOCK,
		WRITE_LOCK, WRITE_LOCK
	};

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	if (slurmctld_conf.slurmctld_pidfile)
		pidfile = slurmctld_conf.slurmctld_pidfile;
	create_pidfile(pidfile);

	if (sigemptyset(&set))
		error("sigemptyset error: %m");
	if (sigaddset(&set, SIGINT))
		error("sigaddset error on SIGINT: %m");
	if (sigaddset(&set, SIGTERM))
		error("sigaddset error on SIGTERM: %m");
	if (sigaddset(&set, SIGHUP))
		error("sigaddset error on SIGHUP: %m");
	if (sigaddset(&set, SIGABRT))
		error("sigaddset error on SIGABRT: %m");

	if (sigprocmask(SIG_BLOCK, &set, NULL) != 0)
		fatal("sigprocmask error: %m");

	while (1) {
		sigwait(&set, &sig);
		switch (sig) {
		case SIGINT:	/* kill -2  or <CTRL-C> */
		case SIGTERM:	/* kill -15 */
			info("Terminate signal (SIGINT or SIGTERM) received");
			shutdown_time = time(NULL);
			/* send REQUEST_SHUTDOWN_IMMEDIATE RPC */
			_slurmctld_shutdown();
			pthread_join(thread_id_rpc, NULL);
			switch_state_fini();
			return NULL;	/* Normal termination */
			break;
		case SIGHUP:	/* kill -1 */
			info("Reconfigure signal (SIGHUP) received");
			lock_slurmctld(config_write_lock);
			error_code = read_slurm_conf(0);
			unlock_slurmctld(config_write_lock);
			if (error_code)
				error("read_slurm_conf error %s",
				      slurm_strerror(error_code));
			else {
				update_logging();
				_update_cred_key();
			}
			break;
		case SIGABRT:	/* abort */
			fatal("SIGABRT received");
			break;
		default:
			error("Invalid signal (%d) received", sig);
		}
	}

}

/* _slurmctld_rpc_mgr - Read incoming RPCs and create pthread for each */
static void *_slurmctld_rpc_mgr(void *no_data)
{
	slurm_fd newsockfd;
	slurm_fd sockfd;
	slurm_addr cli_addr;
	pthread_t thread_id_rpc_req;
	pthread_attr_t thread_attr_rpc_req;
	int no_thread;
	connection_arg_t *conn_arg;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3("_slurmctld_rpc_mgr pid = %u", getpid());

	/* threads to process individual RPC's are detached */
	if (pthread_attr_init(&thread_attr_rpc_req))
		fatal("pthread_attr_init %m");
	if (pthread_attr_setdetachstate
	    (&thread_attr_rpc_req, PTHREAD_CREATE_DETACHED))
		fatal("pthread_attr_setdetachstate %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	/* we want 1:1 threads if there is a choice */
	if (pthread_attr_setscope
	    (&thread_attr_rpc_req, PTHREAD_SCOPE_SYSTEM))
		error("pthread_attr_setscope error %m");
#endif

	/* initialize port for RPCs */
	if ((sockfd = slurm_init_msg_engine_port(slurmctld_conf.
						 slurmctld_port))
	    == SLURM_SOCKET_ERROR)
		fatal("slurm_init_msg_engine_port error %m");

	/*
	 * Procss incoming RPCs indefinitely
	 */
	while (1) {
		conn_arg = xmalloc(sizeof(connection_arg_t));
		/* accept needed for stream implementation is a no-op in 
		 * message implementation that just passes sockfd to newsockfd
		 */
		if ((newsockfd = slurm_accept_msg_conn(sockfd,
						       &cli_addr)) ==
		    SLURM_SOCKET_ERROR) {
			error("slurm_accept_msg_conn error %m");
			continue;
		}
		conn_arg->newsockfd = newsockfd;
		slurm_mutex_lock(&thread_count_lock);
		server_thread_count++;
		slurm_mutex_unlock(&thread_count_lock);
		if (server_thread_count >= MAX_SERVER_THREADS) {
			info(
			   "Warning: server_thread_count is %d, over system limit", 
			   server_thread_count);
			no_thread = 1;
		} else if (shutdown_time)
			no_thread = 1;
		else if (pthread_create(&thread_id_rpc_req,
					&thread_attr_rpc_req,
					_service_connection,
					(void *) conn_arg)) {
			error("pthread_create error %m");
			no_thread = 1;
		} else
			no_thread = 0;

		if (no_thread) {
			if (_service_connection((void *) conn_arg))
				break;
		}

	}

	debug3("_slurmctld_rpc_mgr shutting down");
	slurm_mutex_lock(&thread_count_lock);
	server_thread_count--;
	slurm_mutex_unlock(&thread_count_lock);
	(void) slurm_shutdown_msg_engine(sockfd);
	pthread_exit((void *) 0);
}

/*
 * _service_connection - service the RPC, return NULL except in case 
 *	of REQUEST_SHUTDOWN_IMMEDIATE 
 * IN/OUT arg - really just the connection's file descriptor, freed
 *	upon completion
 */
static void *_service_connection(void *arg)
{
	slurm_fd newsockfd = ((connection_arg_t *) arg)->newsockfd;
	slurm_msg_t *msg = NULL;
	void *return_code = NULL;

	msg = xmalloc(sizeof(slurm_msg_t));

	if (slurm_receive_msg(newsockfd, msg, 0) < 0) {
		error("slurm_receive_msg (_service_connection) error %m");
	} else {
		if (msg->msg_type == REQUEST_SHUTDOWN_IMMEDIATE)
			return_code = (void *) "fini";
		msg->conn_fd = newsockfd;
		_slurmctld_req (msg);	/* process the request */
	}

	/* close should only be called when the socket implementation is 
	 * being used the following call will be a no-op in a 
	 * message/mongo implementation */
	slurm_close_accepted_conn(newsockfd);	/* close the new socket */

	slurm_free_msg(msg);
	xfree(arg);
	slurm_mutex_lock(&thread_count_lock);
	server_thread_count--;
	slurm_mutex_unlock(&thread_count_lock);
	return return_code;
}

/*
 * _slurmctld_background - process slurmctld background activities
 *	purge defunct job records, save state, schedule jobs, and 
 *	ping other nodes
 */
static void *_slurmctld_background(void *no_data)
{
	static time_t last_sched_time;
	static time_t last_checkpoint_time;
	static time_t last_group_time;
	static time_t last_ping_time;
	static time_t last_rpc_retry_time;
	static time_t last_timelimit_time;
	time_t now;
	/* Locks: Write job, write node, read partition */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK,
		WRITE_LOCK, READ_LOCK
	};
	/* Locks: Write job, write node */
	slurmctld_lock_t node_write_lock = { NO_LOCK, WRITE_LOCK,
		WRITE_LOCK, NO_LOCK
	};
	/* Locks: Write partition */
	slurmctld_lock_t part_write_lock = { 
		NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK };

	/* Let the dust settle before doing work */
	now = time(NULL);
	last_sched_time = last_checkpoint_time = last_group_time = now;
	last_timelimit_time = last_rpc_retry_time = now;
	last_ping_time = now + (time_t)MIN_CHECKIN_TIME -
			 (time_t)slurmctld_conf.heartbeat_interval;
	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3("_slurmctld_background pid = %u", getpid());

	while (shutdown_time == 0) {
		sleep(1);

		now = time(NULL);

		if (difftime(now, last_timelimit_time) > PERIODIC_TIMEOUT) {
			last_timelimit_time = now;
			debug("Performing job time limit check");
			lock_slurmctld(job_write_lock);
			job_time_limit();
			unlock_slurmctld(job_write_lock);
		}

		if (difftime(now, last_ping_time) >=
		    slurmctld_conf.heartbeat_interval) {
			last_ping_time = now;
			debug("Performing node ping");
			lock_slurmctld(node_write_lock);
			ping_nodes();
			unlock_slurmctld(node_write_lock);
		}

		if (difftime(now, last_rpc_retry_time) >= RPC_RETRY_INTERVAL) {
			last_rpc_retry_time = now;
			agent_retry(NULL);
		}

		if (difftime(now, last_group_time) >= PERIODIC_GROUP_CHECK) {
			last_group_time = now;
			lock_slurmctld(part_write_lock);
			load_part_uid_allow_list(0);
			unlock_slurmctld(part_write_lock);
		}

		if (difftime(now, last_sched_time) >= PERIODIC_SCHEDULE) {
			last_sched_time = now;
			debug("Performing purge of old job records");
			lock_slurmctld(job_write_lock);
			purge_old_job();	/* remove defunct job recs */
			unlock_slurmctld(job_write_lock);
			if (schedule())
				last_checkpoint_time = 0;  /* force save */
		}

		if (shutdown_time ||
		    (difftime(now, last_checkpoint_time) >=
		     PERIODIC_CHECKPOINT)) {
			if (shutdown_time) {
				/* wait for any RPC's to complete */
				if (server_thread_count)
					sleep(1);
				if (server_thread_count)
					sleep(1);
				if (server_thread_count)
					info(
					   "warning: shutting down with server_thread_count of %d", 
					   server_thread_count);
				if (_report_locks_set() == 0) {
					last_checkpoint_time = now;
					_save_all_state();
				} else
					error
					    ("unable to save state due to set semaphores");
			} else {
				last_checkpoint_time = now;
				debug("Performing full system state save");
				_save_all_state();
			}
		}

	}
	debug3("_slurmctld_background shutting down");

	return NULL;
}


/* _save_all_state - save entire slurmctld state for later recovery */
static void _save_all_state(void)
{
	clock_t start_time;

	start_time = clock();
	/* Each of these functions lock their own databases */
	(void) dump_all_node_state();
	(void) dump_all_part_state();
	(void) dump_all_job_state();
	info("_save_all_state complete, time=%ld",
	     (long) (clock() - start_time));
}

/* 
 * _report_locks_set - report any slurmctld locks left set 
 * RET count of locks currently set
 */
static int _report_locks_set(void)
{
	slurmctld_lock_flags_t lock_flags;
	char config[4] = "", job[4] = "", node[4] = "", partition[4] = "";
	int lock_count;

	get_lock_values(&lock_flags);

	if (lock_flags.entity[read_lock(CONFIG_LOCK)])
		strcat(config, "R");
	if (lock_flags.entity[write_lock(CONFIG_LOCK)])
		strcat(config, "W");
	if (lock_flags.entity[write_wait_lock(CONFIG_LOCK)])
		strcat(config, "P");

	if (lock_flags.entity[read_lock(JOB_LOCK)])
		strcat(job, "R");
	if (lock_flags.entity[write_lock(JOB_LOCK)])
		strcat(job, "W");
	if (lock_flags.entity[write_wait_lock(JOB_LOCK)])
		strcat(job, "P");

	if (lock_flags.entity[read_lock(NODE_LOCK)])
		strcat(node, "R");
	if (lock_flags.entity[write_lock(NODE_LOCK)])
		strcat(node, "W");
	if (lock_flags.entity[write_wait_lock(NODE_LOCK)])
		strcat(node, "P");

	if (lock_flags.entity[read_lock(PART_LOCK)])
		strcat(partition, "R");
	if (lock_flags.entity[write_lock(PART_LOCK)])
		strcat(partition, "W");
	if (lock_flags.entity[write_wait_lock(PART_LOCK)])
		strcat(partition, "P");

	lock_count = strlen(config) + strlen(job) +
	    strlen(node) + strlen(partition);
	if (lock_count > 0)
		error
		    ("The following locks were left set config:%s, job:%s, node:%s, partition:%s",
		     config, job, node, partition);
	return lock_count;
}

/* _slurmctld_req  - Process an individual RPC request
 * IN/OUT - the request message, data associated with the message is freed
 */
static void _slurmctld_req (slurm_msg_t * msg)
{

	switch (msg->msg_type) {
	case REQUEST_BUILD_INFO:
		_slurm_rpc_dump_conf(msg);
		slurm_free_last_update_msg(msg->data);
		break;
	case REQUEST_NODE_INFO:
		_slurm_rpc_dump_nodes(msg);
		slurm_free_last_update_msg(msg->data);
		break;
	case REQUEST_JOB_INFO:
		_slurm_rpc_dump_jobs(msg);
		slurm_free_job_info_request_msg(msg->data);
		break;
	case REQUEST_PARTITION_INFO:
		_slurm_rpc_dump_partitions(msg);
		slurm_free_last_update_msg(msg->data);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
		_slurm_rpc_allocate_resources(msg);
		slurm_free_job_desc_msg(msg->data);
		break;
	case REQUEST_ALLOCATION_AND_RUN_JOB_STEP:
		_slurm_rpc_allocate_and_run(msg);
		slurm_free_job_desc_msg(msg->data);
		break;
	case REQUEST_OLD_JOB_RESOURCE_ALLOCATION:
		_slurm_rpc_old_job_alloc(msg);
		slurm_free_old_job_alloc_msg(msg->data);
		break;
	case REQUEST_JOB_WILL_RUN:
		_slurm_rpc_job_will_run(msg->data);
		slurm_free_job_desc_msg(msg->data);
		break;
	case REQUEST_CANCEL_JOB_STEP:
		_slurm_rpc_job_step_kill(msg);
		slurm_free_job_step_kill_msg(msg->data);
		break;
	case REQUEST_COMPLETE_JOB_STEP:
		_slurm_rpc_job_step_complete(msg);
		slurm_free_job_complete_msg(msg->data);
		break;
	case REQUEST_SUBMIT_BATCH_JOB:
		_slurm_rpc_submit_batch_job(msg);
		slurm_free_job_desc_msg(msg->data);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		_slurm_rpc_node_registration(msg);
		slurm_free_node_registration_status_msg(msg->data);
		break;
	case REQUEST_RECONFIGURE:
		_slurm_rpc_reconfigure_controller(msg);
		/* No body to free */
		break;
	case REQUEST_CONTROL:
		_slurm_rpc_shutdown_controller(msg);
		/* No body to free */
		break;
	case REQUEST_SHUTDOWN:
		_slurm_rpc_shutdown_controller(msg);
		slurm_free_shutdown_msg(msg->data);
		break;
	case REQUEST_SHUTDOWN_IMMEDIATE:
		_slurm_rpc_shutdown_controller_immediate(msg);
		/* No body to free */
		break;
	case REQUEST_PING:
		_slurm_rpc_ping(msg);
		/* No body to free */
		break;
	case REQUEST_UPDATE_JOB:
		_slurm_rpc_update_job(msg);
		slurm_free_job_desc_msg(msg->data);
		break;
	case REQUEST_UPDATE_NODE:
		_slurm_rpc_update_node(msg);
		slurm_free_update_node_msg(msg->data);
		break;
	case REQUEST_JOB_STEP_CREATE:
		_slurm_rpc_job_step_create(msg);
		slurm_free_job_step_create_request_msg(msg->data);
		break;
	case REQUEST_JOB_STEP_INFO:
		_slurm_rpc_job_step_get_info(msg);
		slurm_free_job_step_info_request_msg(msg->data);
		break;
	case REQUEST_UPDATE_PARTITION:
		_slurm_rpc_update_partition(msg);
		slurm_free_update_part_msg(msg->data);
		break;
	default:
		error("invalid RPC message type %d", msg->msg_type);
		slurm_send_rc_msg(msg, EINVAL);
		break;
	}
}

/* _slurm_rpc_dump_conf - process RPC for Slurm configuration information */
static void _slurm_rpc_dump_conf(slurm_msg_t * msg)
{
	clock_t start_time;
	slurm_msg_t response_msg;
	last_update_msg_t *last_time_msg = (last_update_msg_t *) msg->data;
	slurm_ctl_conf_info_msg_t config_tbl;
	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = { 
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	start_time = clock();
	debug("Processing RPC: REQUEST_BUILD_INFO");
	lock_slurmctld(config_read_lock);

	/* check to see if configuration data has changed */
	if ((last_time_msg->last_update - 1) >= slurmctld_conf.last_update) {
		unlock_slurmctld(config_read_lock);
		info("_slurm_rpc_dump_conf, no change, time=%ld",
		     (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		_fill_ctld_conf(&config_tbl);
		unlock_slurmctld(config_read_lock);

		/* init response_msg structure */
		response_msg.address = msg->address;
		response_msg.msg_type = RESPONSE_BUILD_INFO;
		response_msg.data = &config_tbl;

		/* send message */
		info("_slurm_rpc_dump_conf time=%ld",
		     (long) (clock() - start_time));
		slurm_send_node_msg(msg->conn_fd, &response_msg);
	}
}

/* _slurm_rpc_dump_jobs - process RPC for job state information */
static void _slurm_rpc_dump_jobs(slurm_msg_t * msg)
{
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	job_info_request_msg_t *last_time_msg =
	    (job_info_request_msg_t *) msg->data;
	/* Locks: Read job */
	slurmctld_lock_t job_read_lock = { 
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	start_time = clock();
	debug("Processing RPC: REQUEST_JOB_INFO");
	lock_slurmctld(job_read_lock);

	if ((last_time_msg->last_update - 1) >= last_job_update) {
		unlock_slurmctld(job_read_lock);
		info("_slurm_rpc_dump_jobs, no change, time=%ld",
		     (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		pack_all_jobs(&dump, &dump_size);
		unlock_slurmctld(job_read_lock);

		/* init response_msg structure */
		response_msg.address = msg->address;
		response_msg.msg_type = RESPONSE_JOB_INFO;
		response_msg.data = dump;
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		info("_slurm_rpc_dump_jobs, size=%d, time=%ld",
		     dump_size, (long) (clock() - start_time));
		xfree(dump);
	}
}

/* _slurm_rpc_dump_nodes - process RPC for node state information */
static void _slurm_rpc_dump_nodes(slurm_msg_t * msg)
{
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	last_update_msg_t *last_time_msg = (last_update_msg_t *) msg->data;
	/* Locks: Read node */
	slurmctld_lock_t node_read_lock = { 
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	start_time = clock();
	debug("Processing RPC: REQUEST_NODE_INFO");
	lock_slurmctld(node_read_lock);

	if ((last_time_msg->last_update - 1) >= last_node_update) {
		unlock_slurmctld(node_read_lock);
		info("_slurm_rpc_dump_nodes, no change, time=%ld",
		     (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		pack_all_node(&dump, &dump_size);
		unlock_slurmctld(node_read_lock);

		/* init response_msg structure */
		response_msg.address = msg->address;
		response_msg.msg_type = RESPONSE_NODE_INFO;
		response_msg.data = dump;
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		info("_slurm_rpc_dump_nodes, size=%d, time=%ld",
		     dump_size, (long) (clock() - start_time));
		xfree(dump);
	}
}

/* _slurm_rpc_dump_partitions - process RPC for partition state information */
static void _slurm_rpc_dump_partitions(slurm_msg_t * msg)
{
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	last_update_msg_t *last_time_msg = (last_update_msg_t *) msg->data;
	/* Locks: Read partition */
	slurmctld_lock_t part_read_lock = { 
		NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	start_time = clock();
	debug("Processing RPC: REQUEST_PARTITION_INFO");
	lock_slurmctld(part_read_lock);

	if ((last_time_msg->last_update - 1) >= last_part_update) {
		unlock_slurmctld(part_read_lock);
		info("_slurm_rpc_dump_partitions, no change, time=%ld",
		     (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		pack_all_part(&dump, &dump_size);
		unlock_slurmctld(part_read_lock);

		/* init response_msg structure */
		response_msg.address = msg->address;
		response_msg.msg_type = RESPONSE_PARTITION_INFO;
		response_msg.data = dump;
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		info("_slurm_rpc_dump_partitions, size=%d, time=%ld",
		     dump_size, (long) (clock() - start_time));
		xfree(dump);
	}
}

/* _slurm_rpc_job_step_kill - process RPC to cancel an entire job or 
 * an individual job step */
static void _slurm_rpc_job_step_kill(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	job_step_kill_msg_t *job_step_kill_msg =
	    (job_step_kill_msg_t *) msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: REQUEST_CANCEL_JOB_STEP");
	uid = g_slurm_auth_get_uid(msg->cred);
	lock_slurmctld(job_write_lock);

	/* do RPC call */
	if (job_step_kill_msg->job_step_id == NO_VAL) {
		error_code = job_signal(job_step_kill_msg->job_id, 
					job_step_kill_msg->signal, uid);
		unlock_slurmctld(job_write_lock);

		/* return result */
		if (error_code) {
			info(
			   "_slurm_rpc_job_step_kill JobId=%u, time=%ld, error=%s", 
			   job_step_kill_msg->job_id, 
			   (long) (clock() - start_time),
			   slurm_strerror(error_code));
			slurm_send_rc_msg(msg, error_code);
		} else {
			info(
			   "_slurm_rpc_job_step_kill JobId=%u, time=%ld, success", 
			   job_step_kill_msg->job_id, 
			   (long) (clock() - start_time));
			slurm_send_rc_msg(msg, SLURM_SUCCESS);

			/* Below function provides its own locking */
			(void) dump_all_job_state();

		}
	} else {
		error_code = job_step_signal(job_step_kill_msg->job_id,
					     job_step_kill_msg->job_step_id,
					     job_step_kill_msg->signal,
					     uid);
		unlock_slurmctld(job_write_lock);

		/* return result */
		if (error_code) {
			info(
			   "_slurm_rpc_job_step_kill StepId=%u.%u, time=%ld, error=%s", 
			   job_step_kill_msg->job_id, 
			   job_step_kill_msg->job_step_id, 
			   (long) (clock() - start_time),
			   slurm_strerror(error_code));
			slurm_send_rc_msg(msg, error_code);
		} else {
			info(
			   "_slurm_rpc_job_step_kill StepId=%u.%u, time=%ld, success", 
			   job_step_kill_msg->job_id, 
			   job_step_kill_msg->job_step_id, 
			   (long) (clock() - start_time));
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
		}
	}
}

/* _slurm_rpc_job_step_complete - process RPC to note the completion an  
 *	entire job or an individual job step */
static void _slurm_rpc_job_step_complete(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	complete_job_step_msg_t *complete_job_step_msg =
	    (complete_job_step_msg_t *) msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK,
		WRITE_LOCK, NO_LOCK
	};
	uid_t uid;
	bool job_requeue = false;

	/* init */
	start_time = clock();
	debug("Processing RPC: REQUEST_COMPLETE_JOB_STEP");
	uid = g_slurm_auth_get_uid(msg->cred);
	lock_slurmctld(job_write_lock);

	/* do RPC call */
	/* First set node DOWN if fatal error */
	if (complete_job_step_msg->slurm_rc == ESLURM_ALREADY_DONE) {
		/* race condition on job termination, not a real error */
		info("slurmd error running job %u from node %s: %s",
		      complete_job_step_msg->job_id,
		      complete_job_step_msg->node_name,
		      slurm_strerror(complete_job_step_msg->slurm_rc));
		complete_job_step_msg->slurm_rc = SLURM_SUCCESS;
	}
	if (complete_job_step_msg->slurm_rc != SLURM_SUCCESS) {
		error("Fatal slurmd error running job %u from node %s: %s",
		      complete_job_step_msg->job_id,
		      complete_job_step_msg->node_name,
		      slurm_strerror(complete_job_step_msg->slurm_rc));
		if ((uid != 0) && (uid != getuid())) {
			error_code = ESLURM_USER_ID_MISSING;
			error
			    ("Security violation, uid %u can't set node down",
			     (unsigned int) uid);
		}
		if (error_code == SLURM_SUCCESS) {
			update_node_msg_t update_node_msg;
			update_node_msg.node_names =
			    complete_job_step_msg->node_name;
			update_node_msg.node_state = NODE_STATE_DOWN;
			error_code = update_node(&update_node_msg);
			if (complete_job_step_msg->job_rc != SLURM_SUCCESS)
				job_requeue = true;
		}
	}

	/* Mark job and/or job step complete */
	if (complete_job_step_msg->job_step_id == NO_VAL) {
		error_code = job_complete(complete_job_step_msg->job_id,
					  uid, job_requeue,
					  complete_job_step_msg->job_rc);
		unlock_slurmctld(job_write_lock);

		/* return result */
		if (error_code) {
			info(
			   "_slurm_rpc_job_step_complete JobId=%u, time=%ld, error=%s", 
			   complete_job_step_msg->job_id, 
			   (long) (clock() - start_time), 
			   slurm_strerror(error_code));
			slurm_send_rc_msg(msg, error_code);
		} else {
			info(
			   "_slurm_rpc_job_step_complete JobId=%u, time=%ld, success", 
			   complete_job_step_msg->job_id, 
			   (long) (clock() - start_time));
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			schedule();	/* Has own locking */
			(void) dump_all_job_state();	/* Has own locking */
		}
	} else {
		error_code =
		    job_step_complete(complete_job_step_msg->job_id,
				      complete_job_step_msg->job_step_id,
				      uid, job_requeue,
				      complete_job_step_msg->job_rc);
		unlock_slurmctld(job_write_lock);

		/* return result */
		if (error_code) {
			info(
			   "_slurm_rpc_job_step_complete StepId=%u.%u, time=%ld, error=%s", 
			   complete_job_step_msg->job_id, 
			   complete_job_step_msg->job_step_id, 
			   (long) (clock() - start_time),
			   slurm_strerror(error_code));
			slurm_send_rc_msg(msg, error_code);
		} else {
			info(
			   "_slurm_rpc_job_step_complete StepId=%u.%u, time=%ld, success", 
			   complete_job_step_msg->job_id, 
			   complete_job_step_msg->job_step_id, 
			   (long) (clock() - start_time));
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			(void) dump_all_job_state();	/* Has own locking */
		}
	}
}

/* _slurm_rpc_job_step_get_info - process request for job step info */
static void _slurm_rpc_job_step_get_info(slurm_msg_t * msg)
{
	clock_t start_time;
	void *resp_buffer = NULL;
	int resp_buffer_size = 0;
	int error_code = SLURM_SUCCESS;
	job_step_info_request_msg_t *request =
	    (job_step_info_request_msg_t *) msg->data;
	/* Locks: Read job */
	slurmctld_lock_t job_read_lock = { NO_LOCK, READ_LOCK,
		NO_LOCK, NO_LOCK
	};

	start_time = clock();
	debug("Processing RPC: REQUEST_JOB_STEP_INFO");
	lock_slurmctld(job_read_lock);

	if ((request->last_update - 1) >= last_job_update) {
		unlock_slurmctld(job_read_lock);
		info("_slurm_rpc_job_step_get_info, no change, time=%ld",
		     (long) (clock() - start_time));
		error_code = SLURM_NO_CHANGE_IN_DATA;
	} else {
		Buf buffer;
		buffer = init_buf(BUF_SIZE);
		error_code =
		    pack_ctld_job_step_info_response_msg(request->job_id,
							 request->step_id,
							 buffer);
		unlock_slurmctld(job_read_lock);
		resp_buffer_size = get_buf_offset(buffer);
		resp_buffer = xfer_buf_data(buffer);
		if (error_code == ESLURM_INVALID_JOB_ID)
			info(
			   "_slurm_rpc_job_step_get_info, no such job step %u.%u, time=%ld", 
			   request->job_id, request->step_id, 
			   (long) (clock() - start_time));
		else if (error_code)
			error
			    ("_slurm_rpc_job_step_get_info, time=%ld, error=%s",
			     (long) (clock() - start_time),
			     slurm_strerror(error_code));
	}

	if (error_code)
		slurm_send_rc_msg(msg, error_code);
	else {
		slurm_msg_t response_msg;

		info("_slurm_rpc_job_step_get_info, size=%d, time=%ld",
		     resp_buffer_size, (long) (clock() - start_time));
		response_msg.address = msg->address;
		response_msg.msg_type = RESPONSE_JOB_STEP_INFO;
		response_msg.data = resp_buffer;
		response_msg.data_size = resp_buffer_size;
		slurm_send_node_msg(msg->conn_fd, &response_msg);

	}
}

/* _slurm_rpc_update_job - process RPC to update the configuration of a 
 *	job (e.g. priority) */
static void _slurm_rpc_update_job(slurm_msg_t * msg)
{
	/* init */
	int error_code;
	clock_t start_time;
	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;
	/* Locks: Write job, read node, read partition */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: REQUEST_UPDATE_JOB");
	lock_slurmctld(job_write_lock);
	unlock_slurmctld(job_write_lock);

	/* do RPC call */
	uid = g_slurm_auth_get_uid(msg->cred);
	error_code = update_job(job_desc_msg, uid);

	/* return result */
	if (error_code) {
		error(
		     "_slurm_rpc_update_job JobID=%u, time=%ld, error=%s",
		     job_desc_msg->job_id, (long) (clock() - start_time),
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info(
		   "_slurm_rpc_update_job complete for job id %u, time=%ld", 
		   job_desc_msg->job_id, (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		/* Below functions provide their own locking */
		schedule();
		(void) dump_all_job_state();
	}
}

/* _slurm_rpc_update_node - process RPC to update the configuration of a 
 *	node (e.g. UP/DOWN) */
static void _slurm_rpc_update_node(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	update_node_msg_t *update_node_msg_ptr =
	    			(update_node_msg_t *) msg->data;
	/* Locks: Write node */
	slurmctld_lock_t node_write_lock = { 
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: REQUEST_UPDATE_NODE");
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != 0) && (uid != getuid())) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_NODE RPC from uid %u",
		      (unsigned int) uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = update_node(update_node_msg_ptr);
		unlock_slurmctld(node_write_lock);
	}

	/* return result */
	if (error_code) {
		error("_slurm_rpc_update_node node=%s, time=%ld, error=%s",
		      update_node_msg_ptr->node_names,
		      (long) (clock() - start_time), 
		      slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info(
		   "_slurm_rpc_update_node complete for node %s, time=%ld", 
		   update_node_msg_ptr->node_names, 
		   (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

	/* Below functions provide their own locks */
	if (schedule())
		(void) dump_all_job_state();
	(void) dump_all_node_state();
}

/* _slurm_rpc_update_partition - process RPC to update the configuration 
 *	of a partition (e.g. UP/DOWN) */
static void _slurm_rpc_update_partition(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	update_part_msg_t *part_desc_ptr = (update_part_msg_t *) msg->data;
	/* Locks: Read node, write partition */
	slurmctld_lock_t part_write_lock = { 
		NO_LOCK, NO_LOCK, READ_LOCK, WRITE_LOCK };
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: REQUEST_UPDATE_PARTITION");
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != 0) && (uid != getuid())) {
		error_code = ESLURM_USER_ID_MISSING;
		error
		    ("Security violation, UPDATE_PARTITION RPC from uid %u",
		     (unsigned int) uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(part_write_lock);
		error_code = update_part(part_desc_ptr);
		unlock_slurmctld(part_write_lock);
	}

	/* return result */
	if (error_code) {
		error(
		     "_slurm_rpc_update_partition partition=%s, time=%ld, error=%s",
		     part_desc_ptr->name,
		     (long) (clock() - start_time),
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info(
		   "_slurm_rpc_update_partition complete for partition %s, time=%ld", 
		   part_desc_ptr->name, (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		/* NOTE: These functions provide their own locks */
		(void) dump_all_part_state();
		if (schedule())
			(void) dump_all_job_state();
	}
}

/* _slurm_rpc_submit_batch_job - process RPC to submit a batch job */
static void _slurm_rpc_submit_batch_job(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	uint32_t job_id;
	slurm_msg_t response_msg;
	submit_response_msg_t submit_msg;
	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;
	/* Locks: Write job, read node, read partition */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: REQUEST_SUBMIT_BATCH_JOB");

	/* do RPC call */
	dump_job_desc(job_desc_msg);
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != job_desc_msg->user_id) &&
	    (uid != 0) && (uid != getuid())) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, SUBMIT_JOB from uid %u",
		      (unsigned int) uid);
	}
	if (error_code == SLURM_SUCCESS) {
		lock_slurmctld(job_write_lock);
		error_code = job_allocate(job_desc_msg, &job_id,
					  (char **) NULL,
					  (uint16_t *) NULL,
					  (uint32_t **) NULL,
					  (uint32_t **) NULL, false, false,
					  false, uid, NULL, NULL);
		unlock_slurmctld(job_write_lock);
	}

	/* return result */
	if ((error_code != SLURM_SUCCESS) &&
	    (error_code != ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE)) {
		info("_slurm_rpc_submit_batch_job time=%ld, error=%s",
		     (long) (clock() - start_time),
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info(
		   "_slurm_rpc_submit_batch_job success for id=%u, time=%ld", 
		   job_id, (long) (clock() - start_time));
		/* send job_ID */
		submit_msg.job_id     = job_id;
		submit_msg.error_code = error_code;
		response_msg.msg_type = RESPONSE_SUBMIT_BATCH_JOB;
		response_msg.data = &submit_msg;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		schedule();	/* has own locks */
		(void) dump_all_job_state();	/* has own locks */
	}
}

/* _slurm_rpc_allocate_resources:  process RPC to allocate resources for 
 *	a job */
static void _slurm_rpc_allocate_resources(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	clock_t start_time;
	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;
	char *node_list_ptr = NULL;
	uint16_t num_cpu_groups = 0;
	uint32_t *cpus_per_node = NULL, *cpu_count_reps = NULL;
	uint32_t job_id = 0;
	resource_allocation_response_msg_t alloc_msg;
	/* Locks: Write job, write node, read partition */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	uid_t uid;
	uint16_t node_cnt = 0;
	slurm_addr *node_addr = NULL;
	int immediate = job_desc_msg->immediate;

	start_time = clock();
	debug("Processing RPC: REQUEST_RESOURCE_ALLOCATION");

	/* do RPC call */
	dump_job_desc(job_desc_msg);
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != job_desc_msg->user_id) &&
	    (uid != 0) && (uid != getuid())) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, RESOURCE_ALLOCATE from uid %u",
		      (unsigned int) uid);
	}

	if (error_code == SLURM_SUCCESS) {
		lock_slurmctld(job_write_lock);
		error_code = job_allocate(job_desc_msg, &job_id,
					  &node_list_ptr, &num_cpu_groups,
					  &cpus_per_node, &cpu_count_reps,
					  immediate, false, true, uid,
					  &node_cnt, &node_addr);
		unlock_slurmctld(job_write_lock);
	}

	/* return result */
	if ((error_code == SLURM_SUCCESS) ||
	    ((immediate == 0) && 
	     (error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE))) {
		info(
		   "_slurm_rpc_allocate_resources allocated nodes %s to JobId=%u, time=%ld", 
		   node_list_ptr, job_id, (long) (clock() - start_time));

		/* send job_ID  and node_name_ptr */
		alloc_msg.cpu_count_reps = cpu_count_reps;
		alloc_msg.cpus_per_node  = cpus_per_node;
		alloc_msg.error_code     = error_code;
		alloc_msg.job_id         = job_id;
		alloc_msg.node_addr      = node_addr;
		alloc_msg.node_cnt       = node_cnt;
		alloc_msg.node_list      = node_list_ptr;
		alloc_msg.num_cpu_groups = num_cpu_groups;
		response_msg.msg_type = RESPONSE_RESOURCE_ALLOCATION;
		response_msg.data = &alloc_msg;

		slurm_send_node_msg(msg->conn_fd, &response_msg);
		(void) dump_all_job_state();
	} else {	/* Fatal error */
		info("_slurm_rpc_allocate_resources time=%ld, error=%s ", 
		     (long) (clock() - start_time), 
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	}
}

/* _slurm_rpc_allocate_and_run: process RPC to allocate resources for a job 
 *	and initiate a job step */
static void _slurm_rpc_allocate_and_run(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	clock_t start_time;
	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;
	char *node_list_ptr = NULL;
	uint16_t num_cpu_groups = 0;
	uint32_t *cpus_per_node = NULL, *cpu_count_reps = NULL;
	uint32_t job_id;
	resource_allocation_and_run_response_msg_t alloc_msg;
	struct step_record *step_rec;
	slurm_cred_t slurm_cred;
	job_step_create_request_msg_t req_step_msg;
	/* Locks: Write job, write node, read partition */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	uid_t uid;
	uint16_t node_cnt;
	slurm_addr *node_addr;
	int immediate = true;   /* implicit job_desc_msg->immediate == true */

	start_time = clock();
	debug("Processing RPC: REQUEST_ALLOCATE_AND_RUN_JOB_STEP");

	/* do RPC call */
	dump_job_desc(job_desc_msg);
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != job_desc_msg->user_id) &&
	    (uid != 0) && (uid != getuid())) {
		error("Security violation, ALLOCATE_AND_RUN RPC from uid %u",
		      (unsigned int) uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	lock_slurmctld(job_write_lock);
	error_code = job_allocate(job_desc_msg, &job_id,
				  &node_list_ptr, &num_cpu_groups,
				  &cpus_per_node, &cpu_count_reps,
				  immediate, false, true, uid,
				  &node_cnt, &node_addr);

	/* return result */
	if (error_code) {
		unlock_slurmctld(job_write_lock);
		info("_slurm_rpc_allocate_and_run time=%ld, error=%s", 
		     (long) (clock() - start_time), 
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	req_step_msg.job_id     = job_id;
	req_step_msg.user_id    = job_desc_msg->user_id;
	req_step_msg.node_count = INFINITE;
	req_step_msg.cpu_count  = job_desc_msg->num_procs;
	req_step_msg.num_tasks  = job_desc_msg->num_tasks;
	req_step_msg.task_dist  = job_desc_msg->task_dist;
	error_code = step_create(&req_step_msg, &step_rec, true);
	if (error_code == SLURM_SUCCESS)
		error_code = _make_step_cred(step_rec, &slurm_cred);

	/* note: no need to free step_rec, pointer to global job step record */
	if (error_code) {
		job_complete(job_id, job_desc_msg->user_id, false, 0);
		unlock_slurmctld(job_write_lock);
		info(
		   "_slurm_rpc_allocate_and_run creating job step, time=%ld, error=%s", 
		   (long) (clock() - start_time), slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {

		info(
		   "_slurm_rpc_allocate_and_run allocated nodes %s to JobId=%u, time=%ld", 
		   node_list_ptr, job_id, (long) (clock() - start_time));

		/* send job_ID  and node_name_ptr */
		alloc_msg.job_id         = job_id;
		alloc_msg.node_list      = node_list_ptr;
		alloc_msg.num_cpu_groups = num_cpu_groups;
		alloc_msg.cpus_per_node  = cpus_per_node;
		alloc_msg.cpu_count_reps = cpu_count_reps;
		alloc_msg.job_step_id    = step_rec->step_id;
		alloc_msg.node_cnt       = node_cnt;
		alloc_msg.node_addr      = node_addr;
		alloc_msg.cred           = slurm_cred;
#ifdef HAVE_LIBELAN3
		alloc_msg.qsw_job = qsw_copy_jobinfo(step_rec->qsw_job);
#endif
		unlock_slurmctld(job_write_lock);
		response_msg.msg_type =
				    RESPONSE_ALLOCATION_AND_RUN_JOB_STEP;
		response_msg.data = &alloc_msg;

		slurm_send_node_msg(msg->conn_fd, &response_msg);
		slurm_cred_destroy(slurm_cred);
#ifdef HAVE_LIBELAN3
		qsw_free_jobinfo(alloc_msg.qsw_job);
#endif
		(void) dump_all_job_state();	/* Has its own locks */
	}
}

/* _slurm_rpc_old_job_alloc - process RPC to get details on existing job */
static void _slurm_rpc_old_job_alloc(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	clock_t start_time;
	old_job_alloc_msg_t *job_desc_msg =
	    (old_job_alloc_msg_t *) msg->data;
	char *node_list_ptr = NULL;
	uint16_t num_cpu_groups = 0;
	uint32_t *cpus_per_node = NULL, *cpu_count_reps = NULL;
	resource_allocation_response_msg_t alloc_msg;
	/* Locks: Read job, read node */
	slurmctld_lock_t job_read_lock = { 
		NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };
	uint16_t node_cnt;
	slurm_addr *node_addr;
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: REQUEST_OLD_JOB_RESOURCE_ALLOCATION");

	/* do RPC call */
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != job_desc_msg->uid) && (uid != 0) && (uid != getuid())) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, RESOURCE_ALLOCATE from uid %u",
		      (unsigned int) uid);
	}
	if (error_code == SLURM_SUCCESS) {
		lock_slurmctld(job_read_lock);
		error_code = old_job_info(job_desc_msg->uid,
					  job_desc_msg->job_id,
					  &node_list_ptr, &num_cpu_groups,
					  &cpus_per_node, &cpu_count_reps,
					  &node_cnt, &node_addr);
		unlock_slurmctld(job_read_lock);
	}

	/* return result */
	if (error_code) {
		debug(
		   "_slurm_rpc_old_job_alloc: JobId=%u, uid=%u, time=%ld, error=%s", 
		   job_desc_msg->job_id, job_desc_msg->uid, 
		   (long) (clock() - start_time), 
		   slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info(
		   "_slurm_rpc_old_job_alloc job=%u has nodes %s, time=%ld", 
		   job_desc_msg->job_id, node_list_ptr, 
		   (long) (clock() - start_time));

		/* send job_ID  and node_name_ptr */

		alloc_msg.job_id         = job_desc_msg->job_id;
		alloc_msg.node_list      = node_list_ptr;
		alloc_msg.num_cpu_groups = num_cpu_groups;
		alloc_msg.cpus_per_node  = cpus_per_node;
		alloc_msg.cpu_count_reps = cpu_count_reps;
		alloc_msg.node_cnt       = node_cnt;
		alloc_msg.node_addr      = node_addr;
		response_msg.msg_type    = RESPONSE_RESOURCE_ALLOCATION;
		response_msg.data        = &alloc_msg;

		slurm_send_node_msg(msg->conn_fd, &response_msg);
	}
}

/* _slurm_rpc_job_will_run - process RPC to determine if job with given 
 *	configuration can be initiated */
static void _slurm_rpc_job_will_run(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	uint16_t num_cpu_groups = 0;
	uint32_t *cpus_per_node = NULL, *cpu_count_reps = NULL;
	uint32_t job_id;
	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;
	char *node_list_ptr = NULL;
	/* Locks: Write job, read node, read partition */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: REQUEST_JOB_WILL_RUN");

	/* do RPC call */
	dump_job_desc(job_desc_msg);
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != job_desc_msg->user_id) &&
	    (uid != 0) && (uid != getuid())) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, JOB_WILL_RUN RPC from uid %u",
		      (unsigned int) uid);
	}

	if (error_code == SLURM_SUCCESS) {
		lock_slurmctld(job_write_lock);
		error_code = job_allocate(job_desc_msg, &job_id,
					  &node_list_ptr, &num_cpu_groups,
					  &cpus_per_node, &cpu_count_reps,
					  false, true, true, uid, NULL,
					  NULL);
		unlock_slurmctld(job_write_lock);
	}

	/* return result */
	if (error_code) {
		info("_slurm_rpc_job_will_run time=%ld, error=%s",
		     (long) (clock() - start_time),
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("_slurm_rpc_job_will_run success for , time=%ld",
		     (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
}

/* _slurm_rpc_ping - process ping RPC */
static void _slurm_rpc_ping(slurm_msg_t * msg)
{
	/* We could authenticate here, if desired */

	/* return result */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}


/* _slurm_rpc_reconfigure_controller - process RPC to re-initialize 
 *	slurmctld from configuration file */
static void _slurm_rpc_reconfigure_controller(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	/* Locks: Write configuration, job, node and partition */
	slurmctld_lock_t config_write_lock = { 
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: REQUEST_RECONFIGURE");
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != 0) && (uid != getuid())) {
		error("Security violation, RECONFIGURE RPC from uid %u",
		      (unsigned int) uid);
		error_code = ESLURM_USER_ID_MISSING;
	}

	/* do RPC call */
	if (error_code == SLURM_SUCCESS) {
		lock_slurmctld(config_write_lock);
		error_code = read_slurm_conf(0);
		if (error_code == SLURM_SUCCESS)
			msg_to_slurmd(REQUEST_RECONFIGURE);
		unlock_slurmctld(config_write_lock);
	}
	if (error_code == SLURM_SUCCESS) {  /* Stuff to do after unlock */
		_update_cred_key();
		if (daemonize && chdir(slurmctld_conf.state_save_location) < 0) {
			error("chdir to %s error %m",
			      slurmctld_conf.state_save_location);
		}
	}

	/* return result */
	if (error_code) {
		error(
		     "_slurm_rpc_reconfigure_controller: time=%ld, error=%s",
		     (long) (clock() - start_time),
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info(
		   "_slurm_rpc_reconfigure_controller: completed, time=%ld", 
		   (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		schedule();
		_save_all_state();
	}
}


/* _slurm_rpc_shutdown_controller - process RPC to shutdown slurmctld */
static void _slurm_rpc_shutdown_controller(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS, i;
	uint16_t core_arg = 0;
	shutdown_msg_t *shutdown_msg = (shutdown_msg_t *) msg->data;
	uid_t uid;
	/* Locks: Read node */
	slurmctld_lock_t node_read_lock = { 
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != 0) && (uid != getuid())) {
		error("Security violation, SHUTDOWN RPC from uid %u",
		      (unsigned int) uid);
		error_code = ESLURM_USER_ID_MISSING;
	}
	if (error_code);
	else if (msg->msg_type == REQUEST_CONTROL) {
		info("Performing RPC: REQUEST_CONTROL");
		resume_backup = true;	/* resume backup mode */
	} else {
		debug("Performing RPC: REQUEST_SHUTDOWN");
		core_arg = shutdown_msg->core;
	}

	/* do RPC call */
	if (error_code);
	else if (core_arg)
		info("performing immeditate shutdown without state save");
	else if (shutdown_time)
		debug3("shutdown RPC issued when already in progress");
	else {
		if (msg->msg_type == REQUEST_SHUTDOWN) {
			/* This means (msg->msg_type != REQUEST_CONTROL) */
			lock_slurmctld(node_read_lock);
			msg_to_slurmd(REQUEST_SHUTDOWN);
			unlock_slurmctld(node_read_lock);
		}
		if (thread_id_sig)		/* signal clean-up */
			pthread_kill(thread_id_sig, SIGTERM);
		else {
			error("thread_id_sig undefined, hard shutdown");
			shutdown_time = time(NULL);
			/* send REQUEST_SHUTDOWN_IMMEDIATE RPC */
			_slurmctld_shutdown();
		}
	}

	if (msg->msg_type == REQUEST_CONTROL) {
		/* wait for workload to dry up before sending reply */
		for (i = 0; ((i < 10) && (server_thread_count > 1)); i++)
			sleep(1);
	}
	slurm_send_rc_msg(msg, error_code);
	if ((error_code == SLURM_SUCCESS) && core_arg)
		fatal("Aborting per RPC request");
}

/* _slurm_rpc_shutdown_controller_immediate - process RPC to shutdown 
 *	slurmctld */
static void _slurm_rpc_shutdown_controller_immediate(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	uid_t uid;

	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != 0) && (uid != getuid())) {
		error
		    ("Security violation, SHUTDOWN_IMMEDIATE RPC from uid %u",
		     (unsigned int) uid);
		error_code = ESLURM_USER_ID_MISSING;
	}

	/* do RPC call */
	/* No op: just used to knock loose accept RPC thread */
	if (error_code == SLURM_SUCCESS)
		debug("Performing RPC: REQUEST_SHUTDOWN_IMMEDIATE");
}

/* _slurm_rpc_job_step_create - process RPC to creates/registers a job step 
 *	with the step_mgr */
static void _slurm_rpc_job_step_create(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;

	slurm_msg_t resp;
	struct step_record *step_rec;
	job_step_create_response_msg_t job_step_resp;
	job_step_create_request_msg_t *req_step_msg =
	    (job_step_create_request_msg_t *) msg->data;
	slurm_cred_t slurm_cred;
	/* Locks: Write jobs, read nodes */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: REQUEST_JOB_STEP_CREATE");
	dump_step_desc(req_step_msg);
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != req_step_msg->user_id) &&
	    (uid != 0) && (uid != getuid())) {
		error_code = ESLURM_USER_ID_MISSING;
		error
		    ("Security violation, JOB_STEP_CREATE RPC from uid %u",
		     (unsigned int) uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* issue the RPC */
		lock_slurmctld(job_write_lock);
		error_code = step_create(req_step_msg, &step_rec, false);
	}
	if (error_code == SLURM_SUCCESS)
		error_code = _make_step_cred(step_rec, &slurm_cred);

	/* return result */
	if (error_code) {
		unlock_slurmctld(job_write_lock);
		info("_slurm_rpc_job_step_create: time=%ld error=%s",
		     (long) (clock() - start_time),
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("_slurm_rpc_job_step_create: %u.%u success time=%ld",
		     step_rec->job_ptr->job_id, step_rec->step_id,
		     (long) (clock() - start_time));

		job_step_resp.job_step_id = step_rec->step_id;
		job_step_resp.node_list   = xstrdup(step_rec->step_node_list);
		job_step_resp.cred        = slurm_cred;

#ifdef HAVE_LIBELAN3
		job_step_resp.qsw_job =  qsw_copy_jobinfo(step_rec->qsw_job);
#endif
		unlock_slurmctld(job_write_lock);
		resp.address = msg->address;
		resp.msg_type = RESPONSE_JOB_STEP_CREATE;
		resp.data = &job_step_resp;

		slurm_send_node_msg(msg->conn_fd, &resp);
		xfree(job_step_resp.node_list);
		slurm_cred_destroy(slurm_cred);
#ifdef HAVE_LIBELAN3
		qsw_free_jobinfo(job_step_resp.qsw_job);
#endif
		(void) dump_all_job_state();	/* Sets own locks */
	}
}

/* create a credential for a given job step, return error code */
static int _make_step_cred(struct step_record *step_rec, 
			   slurm_cred_t *slurm_cred)
{
	slurm_cred_arg_t cred_arg;

	cred_arg.jobid    = step_rec->job_ptr->job_id;
	cred_arg.stepid   = step_rec->step_id;
	cred_arg.uid      = step_rec->job_ptr->user_id;
	cred_arg.hostlist = step_rec->step_node_list;

	if ((*slurm_cred = slurm_cred_create(cred_ctx, &cred_arg)) == NULL) {
		error("slurm_cred_create error");
		return ESLURM_INVALID_JOB_CREDENTIAL;
	}

	return SLURM_SUCCESS;
}

/* _slurm_rpc_node_registration - process RPC to determine if a node's 
 *	actual configuration satisfies the configured specification */
static void _slurm_rpc_node_registration(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	slurm_node_registration_status_msg_t *node_reg_stat_msg =
	    (slurm_node_registration_status_msg_t *) msg->data;
	/* Locks: Write job and node */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	uid_t uid;

	start_time = clock();
	debug("Processing RPC: MESSAGE_NODE_REGISTRATION_STATUS");
	uid = g_slurm_auth_get_uid(msg->cred);
	if ((uid != 0) && (uid != getuid())) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation,  NODE_REGISTER RPC from uid %u",
		      (unsigned int) uid);
	}
	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(job_write_lock);
		validate_jobs_on_node(node_reg_stat_msg->node_name,
				      &node_reg_stat_msg->job_count,
				      node_reg_stat_msg->job_id,
				      node_reg_stat_msg->step_id);
		error_code =
		    validate_node_specs(node_reg_stat_msg->node_name,
					node_reg_stat_msg->cpus,
					node_reg_stat_msg->
					real_memory_size,
					node_reg_stat_msg->
					temporary_disk_space,
					node_reg_stat_msg->job_count,
					node_reg_stat_msg->status);
		unlock_slurmctld(job_write_lock);
	}

	/* return result */
	if (error_code) {
		error(
		     "_slurm_rpc_node_registration node=%s, time=%ld, error=%s",
		     node_reg_stat_msg->node_name,
		     (long) (clock() - start_time),
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info(
		   "_slurm_rpc_node_registration complete for %s, time=%ld", 
		   node_reg_stat_msg->node_name, 
		   (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		schedule();	/* has own locks */
	}
}

/*
 * _slurmctld_shutdown - issue RPC to have slurmctld shutdown, knocks
 *	loose an slurm_accept_msg_conn() if we have a thread hung there
 * RET 0 or error code
 */
static int _slurmctld_shutdown(void)
{
	int rc;
	slurm_fd sockfd;
	slurm_msg_t request_msg;
	slurm_addr self;

	/* init message connection for message communication 
	 * with self/controller */
	slurm_set_addr(&self, slurmctld_conf.slurmctld_port, "localhost");
	if ((sockfd = slurm_open_msg_conn(&self)) == SLURM_SOCKET_ERROR) {
		error("_slurmctld_shutdown/slurm_open_msg_conn: %m");
		return SLURM_SOCKET_ERROR;
	}

	/* send request message */
	request_msg.msg_type = REQUEST_SHUTDOWN_IMMEDIATE;

	if ((rc = slurm_send_node_msg(sockfd, &request_msg))
	    == SLURM_SOCKET_ERROR) {
		error("_slurmctld_shutdown/slurm_send_node_msg error: %m");
		return SLURM_SOCKET_ERROR;
	}

	/* no response */

	/* shutdown message connection */
	if ((rc = slurm_shutdown_msg_conn(sockfd))
	    == SLURM_SOCKET_ERROR) {
		error("slurm_shutdown_msg_conn error");
		return SLURM_SOCKET_ERROR;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * _fill_ctld_conf - make a copy of current slurm configuration
 *	this is done with locks set so the data can change at other times
 * OUT conf_ptr - place to copy configuration to
 */
void _fill_ctld_conf(slurm_ctl_conf_t * conf_ptr)
{
	conf_ptr->last_update         = time(NULL);
	conf_ptr->authtype            = slurmctld_conf.authtype;
	conf_ptr->backup_addr         = slurmctld_conf.backup_addr;
	conf_ptr->backup_controller   = slurmctld_conf.backup_controller;
	conf_ptr->control_addr        = slurmctld_conf.control_addr;
	conf_ptr->control_machine     = slurmctld_conf.control_machine;
	conf_ptr->epilog              = slurmctld_conf.epilog;
	conf_ptr->fast_schedule       = slurmctld_conf.fast_schedule;
	conf_ptr->first_job_id        = slurmctld_conf.first_job_id;
	conf_ptr->hash_base           = slurmctld_conf.hash_base;
	conf_ptr->heartbeat_interval  = slurmctld_conf.heartbeat_interval;
	conf_ptr->inactive_limit      = slurmctld_conf.inactive_limit;
	conf_ptr->job_credential_private_key = 
			slurmctld_conf.job_credential_private_key;
	conf_ptr->job_credential_public_certificate = 
			slurmctld_conf.job_credential_public_certificate;
	conf_ptr->kill_wait           = slurmctld_conf.kill_wait;
	conf_ptr->max_job_cnt         = slurmctld_conf.max_job_cnt;
	conf_ptr->min_job_age         = slurmctld_conf.min_job_age;
	conf_ptr->plugindir           = slurmctld_conf.plugindir;
	conf_ptr->prioritize          = slurmctld_conf.prioritize;
	conf_ptr->prolog              = slurmctld_conf.prolog;
	conf_ptr->ret2service         = slurmctld_conf.ret2service;
	conf_ptr->slurm_user_id       = slurmctld_conf.slurm_user_id;
	conf_ptr->slurm_user_name     = slurmctld_conf.slurm_user_name;
	conf_ptr->slurmctld_debug     = slurmctld_conf.slurmctld_debug;
	conf_ptr->slurmctld_logfile   = slurmctld_conf.slurmctld_logfile;
	conf_ptr->slurmctld_pidfile   = slurmctld_conf.slurmctld_pidfile;
	conf_ptr->slurmctld_port      = slurmctld_conf.slurmctld_port;
	conf_ptr->slurmctld_timeout   = slurmctld_conf.slurmctld_timeout;
	conf_ptr->slurmd_debug        = slurmctld_conf.slurmd_debug;
	conf_ptr->slurmd_logfile      = slurmctld_conf.slurmd_logfile;
	conf_ptr->slurmd_pidfile      = slurmctld_conf.slurmd_pidfile;
	conf_ptr->slurmd_port         = slurmctld_conf.slurmd_port;
	conf_ptr->slurmd_spooldir     = slurmctld_conf.slurmd_spooldir;
	conf_ptr->slurmd_timeout      = slurmctld_conf.slurmd_timeout;
	conf_ptr->slurm_conf          = slurmctld_conf.slurm_conf;
	conf_ptr->state_save_location = slurmctld_conf.state_save_location;
	conf_ptr->tmp_fs              = slurmctld_conf.tmp_fs;
	conf_ptr->wait_time           = slurmctld_conf.wait_time;
	return;
}

/* Variables for commandline passing using getopt */
extern char *optarg;
extern int optind, opterr, optopt;

/*
 * _parse_commandline - parse and process any command line arguments
 * IN argc - number of command line arguments
 * IN argv - the command line arguments
 * IN/OUT conf_ptr - pointer to current configuration, update as needed
 */
static void _parse_commandline(int argc, char *argv[], 
			       slurm_ctl_conf_t * conf_ptr)
{
	int c = 0;

	opterr = 0;
	while ((c = getopt(argc, argv, "cdDf:hL:rv")) != -1)
		switch (c) {
		case 'c':
			recover = 0;
			break;
		case 'd':
			daemonize = 1;
			break;
		case 'D':
			daemonize = 0;
			break;
		case 'f':
			slurmctld_conf.slurm_conf = xstrdup(optarg);
			break;
		case 'h':
			_usage(argv[0]);
			exit(0);
			break;
		case 'L':
			debug_logfile = xstrdup(optarg);
			break;
		case 'r':
			recover = 1;
			break;
		case 'v':
			debug_level++;
			break;
		default:
			_usage(argv[0]);
			exit(1);
		}
}

/* _usage - print a message describing the command line arguments of 
 *	slurmctld */
static void _usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s [OPTIONS]\n", prog_name);
#if (DEFAULT_RECOVER != 0)
	fprintf(stderr, "  -c      "
			"\tDo not recover state from last checkpoint.\n");
#endif
#if (DEFAULT_DAEMONIZE == 0)
	fprintf(stderr, "  -d      "
			"\tRun daemon in background.\n");
#endif
#if (DEFAULT_DAEMONIZE != 0)
	fprintf(stderr, "  -D      "
			"\tRun daemon in foreground.\n");
#endif
	fprintf(stderr, "  -f file "
			"\tUse `file' as slurmctld config file.\n");
	fprintf(stderr, "  -h      "
			"\tPrint this help message.\n");
	fprintf(stderr, "  -L logfile "
			"\tLog messages to the file `logfile'\n");
#if (DEFAULT_RECOVER == 0)
	fprintf(stderr, "  -r      "
			"\tRecover state from last checkpoint.\n");
#endif
	fprintf(stderr, "  -v      "
			"\tVerbose mode. Multiple -v's increase verbosity.\n");
}

/* _run_backup - this is the backup controller, it should run in standby 
 *	mode, assuming control when the primary controller stops responding */
static void _run_backup(void)
{
	time_t last_controller_response = time(NULL), last_ping = 0;
	pthread_attr_t thread_attr_sig, thread_attr_rpc;

	info("slurmctld running in background mode");
	resume_backup = false;	/* default: don't resume if shutdown */

	/*
	 * create attached thread for signal handling
	 */
	if (pthread_attr_init(&thread_attr_sig))
		fatal("pthread_attr_init error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	/* we want 1:1 threads if there is a choice */
	if (pthread_attr_setscope(&thread_attr_sig, PTHREAD_SCOPE_SYSTEM))
		error("pthread_attr_setscope error %m");
#endif
	if (pthread_create(&thread_id_sig,
			   &thread_attr_sig, _background_signal_hand, NULL))
		fatal("pthread_create %m");

	/*
	 * create attached thread to process RPCs
	 */
	if (pthread_attr_init(&thread_attr_rpc))
		fatal("pthread_attr_init error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	/* we want 1:1 threads if there is a choice */
	if (pthread_attr_setscope(&thread_attr_rpc, PTHREAD_SCOPE_SYSTEM))
		error("pthread_attr_setscope error %m");
#endif
	if (pthread_create
	    (&thread_id_rpc, &thread_attr_rpc, _background_rpc_mgr, NULL))
		fatal("pthread_create error %m");

	/* repeatedly ping ControlMachine */
	while (shutdown_time == 0) {
		sleep(5);	/* Give the primary slurmctld set-up time */
		if (difftime(time(NULL), last_ping) <
		    slurmctld_conf.heartbeat_interval)
			continue;

		last_ping = time(NULL);
		if (_ping_controller() == 0)
			last_controller_response = time(NULL);
		else if (difftime(time(NULL), last_controller_response) >
			 slurmctld_conf.slurmctld_timeout)
			break;
	}
	if (shutdown_time != 0)
		exit(0);

	error
	    ("ControlMachine %s not responding, BackupController %s taking over",
	     slurmctld_conf.control_machine,
	     slurmctld_conf.backup_controller);
	pthread_kill(thread_id_sig, SIGTERM);
	pthread_join(thread_id_sig, NULL);

	if (read_slurm_conf(1))	/* Recover all state */
		fatal("Unable to recover slurm state");
	shutdown_time = (time_t) 0;
	return;
}

/* _background_signal_hand - Process daemon-wide signals for the 
 *	backup controller */
static void *_background_signal_hand(void *no_data)
{
	int sig;
	sigset_t set;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	info("Send signals to _background_signal_hand, pid = %u", getpid());

	if (sigemptyset(&set))
		error("sigemptyset error: %m");
	if (sigaddset(&set, SIGINT))
		error("sigaddset error on SIGINT: %m");
	if (sigaddset(&set, SIGTERM))
		error("sigaddset error on SIGTERM: %m");
	if (sigaddset(&set, SIGABRT))
		error("sigaddset error on SIGABRT: %m");

	if (sigprocmask(SIG_BLOCK, &set, NULL) != 0)
		fatal("sigprocmask error: %m");

	while (1) {
		sigwait(&set, &sig);
		switch (sig) {
		case SIGINT:	/* kill -2  or <CTRL-C> */
		case SIGTERM:	/* kill -15 */
			info("Terminate signal (SIGINT or SIGTERM) received");
			shutdown_time = time(NULL);
			/* send REQUEST_SHUTDOWN_IMMEDIATE RPC */
			_slurmctld_shutdown();
			pthread_join(thread_id_rpc, NULL);

			return NULL;	/* Normal termination */
			break;
		case SIGABRT:	/* abort */
			fatal("SIGABRT received");
			break;
		default:
			error("Invalid signal (%d) received", sig);
		}
	}
}

/* _background_rpc_mgr - Read and process incoming RPCs to the background 
 *	controller (that's us) */
static void *_background_rpc_mgr(void *no_data)
{
	slurm_fd newsockfd;
	slurm_fd sockfd;
	slurm_addr cli_addr;
	slurm_msg_t *msg = NULL;
	bool done_flag = false;
	int error_code;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3("_background_rpc_mgr pid = %u", getpid());

	/* initialize port for RPCs */
	if ((sockfd =
	     slurm_init_msg_engine_port(slurmctld_conf.slurmctld_port))
	    == SLURM_SOCKET_ERROR) {
		error("slurm_init_msg_engine_port error %m");
		exit(1);
	}

	/*
	 * Procss incoming RPCs indefinitely
	 */
	while (done_flag == false) {
		/* accept needed for stream implementation 
		 * is a no-op in message implementation that just passes 
		 * sockfd to newsockfd */
		if ((newsockfd = slurm_accept_msg_conn(sockfd, &cli_addr))
		    == SLURM_SOCKET_ERROR) {
			error("slurm_accept_msg_conn error %m");
			continue;
		}

		msg = xmalloc(sizeof(slurm_msg_t));
		msg->conn_fd = newsockfd;
		if (slurm_receive_msg(newsockfd, msg, 0) < 0)
			error("slurm_receive_msg error %m");
		else {
			error_code = _background_process_msg(msg);
			if ((error_code == SLURM_SUCCESS) &&
			    (msg->msg_type == REQUEST_SHUTDOWN_IMMEDIATE))
				done_flag = true;
		}
		slurm_free_msg(msg);

		/* close should only be called when the socket 
		 * implementation is being used the following call will 
		 * be a no-op in a message/mongo implementation */
		slurm_close_accepted_conn(newsockfd);	/* close new socket */
	}

	debug3("_background_rpc_mgr shutting down");
	slurm_close_accepted_conn(sockfd);	/* close the main socket */
	pthread_exit((void *) 0);
}

/* _background_process_msg - process an RPC to the backup_controller */
static int _background_process_msg(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;

	if (msg->msg_type != REQUEST_PING) {
		bool super_user = false;
		uid_t uid = g_slurm_auth_get_uid(msg->cred);
		if ((uid == 0) || (uid == getuid()))
			super_user = true;

		if (super_user && 
		    (msg->msg_type == REQUEST_SHUTDOWN_IMMEDIATE)) {
			info("Performing RPC: REQUEST_SHUTDOWN_IMMEDIATE");
		} else if (super_user && 
			   (msg->msg_type == REQUEST_SHUTDOWN)) {
			info("Performing RPC: REQUEST_SHUTDOWN");
			pthread_kill(thread_id_sig, SIGTERM);
		} else if (super_user && 
			   (msg->msg_type == REQUEST_CONTROL)) {
			debug3("Ignoring RPC: REQUEST_CONTROL");
		} else {
			error("Invalid RPC received %d from uid %u", 
			      msg->msg_type, uid);
			error_code = ESLURM_IN_STANDBY_MODE;
		}
	}
	if (msg->msg_type != REQUEST_SHUTDOWN_IMMEDIATE)
		slurm_send_rc_msg(msg, error_code);
	return error_code;
}

/* Ping primary ControlMachine
 * RET 0 if no error */
static int _ping_controller(void)
{
	int rc;
	slurm_msg_t req;

	debug3("pinging slurmctld at %s", slurmctld_conf.control_addr);

	/* 
	 *  Set address of controller to ping
	 */
	slurm_set_addr(&req.address, slurmctld_conf.slurmctld_port, 
	               slurmctld_conf.control_addr);


	req.msg_type = REQUEST_PING;

	if (slurm_send_recv_rc_msg(&req, &rc, 0) < 0) {
		error("_ping_controller/slurm_send_node_msg error: %m");
		return SLURM_ERROR;
	}

	if (rc) {
		error("_ping_controller/response error %d", rc);
		return SLURM_PROTOCOL_ERROR;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * Tell the backup_controller to relinquish control, primary control_machine 
 *	has resumed operation
 * RET 0 or an error code 
 */
int shutdown_backup_controller(void)
{
	int rc;
	slurm_msg_t req;

	if ((slurmctld_conf.backup_addr == NULL) ||
	    (strlen(slurmctld_conf.backup_addr) == 0))
		return SLURM_PROTOCOL_SUCCESS;

	slurm_set_addr(&req.address, slurmctld_conf.slurmctld_port,
		       slurmctld_conf.backup_addr);

	/* send request message */
	req.msg_type = REQUEST_CONTROL;
	req.data = NULL;

	if (slurm_send_recv_rc_msg(&req, &rc, 0) < 0) {
		error("shutdown_backup:send/recv: %m");
		return SLURM_SOCKET_ERROR;
	}

	if (rc) {
		error("shutdown_backup: %s", slurm_strerror(rc));
		return SLURM_ERROR;
	}

	/* FIXME: Ideally the REQUEST_CONTROL RPC does not return until all   
	 * other activity has ceased and the state has been saved. That is   
	 * not presently the case (it returns when no other work is pending,  
	 * so the state save should occur right away). We sleep for a while   
	 * here and give the backup controller time to shutdown */
	sleep(2);

	return SLURM_PROTOCOL_SUCCESS;
}

/* Reset the job credential key based upon configuration parameters */
static void _update_cred_key(void) 
{
	slurm_cred_ctx_key_update(cred_ctx, 
				  slurmctld_conf.job_credential_private_key);
}

/* Reset slurmctld logging based upon configuration parameters
 * uses common slurmctld_conf data structure */
void update_logging(void) 
{
	/* Preserve execute line arguments (if any) */
	if (debug_level) {
		if ((LOG_LEVEL_INFO + debug_level) > LOG_LEVEL_DEBUG3)
			slurmctld_conf.slurmctld_debug = LOG_LEVEL_DEBUG3;
		else
			slurmctld_conf.slurmctld_debug = LOG_LEVEL_INFO + 
							 debug_level;
	} 
	if (slurmctld_conf.slurmctld_debug != (uint16_t) NO_VAL) {
		log_opts.stderr_level  = slurmctld_conf.slurmctld_debug;
		log_opts.logfile_level = slurmctld_conf.slurmctld_debug;
		log_opts.syslog_level  = slurmctld_conf.slurmctld_debug;
	}
	if (debug_logfile) {
		xfree(slurmctld_conf.slurmctld_logfile);
		slurmctld_conf.slurmctld_logfile = xstrdup(debug_logfile);
	}

	if (daemonize) {
		log_opts.stderr_level = LOG_LEVEL_QUIET;
		if (slurmctld_conf.slurmctld_logfile)
			log_opts.syslog_level = LOG_LEVEL_QUIET;
	}

	log_alter(log_opts, SYSLOG_FACILITY_DAEMON,
		  slurmctld_conf.slurmctld_logfile);
}

static void 
_init_pidfile(void)
{
	int   fd      = -1;
	uid_t uid     = slurmctld_conf.slurm_user_id;
	char *pidfile = slurmctld_conf.slurmctld_pidfile;

	pidfile = pidfile ? pidfile : DEFAULT_PIDFILE;

	if ((fd = create_pidfile(pidfile)) < 0) 
		return;

	if (uid && (fchown(fd, uid, -1) < 0))
		error ("Unable to reset owner of pidfile: %m");

	/*
	 * Close fd here, otherwise we'll deadlock since create_pidfile()
	 * flocks the pidfile.
	 */
	close(fd);
}

/*
 * create state directory as needed and "cd" to it 
 */
static int
_set_slurmctld_state_loc(void)
{
	if ((mkdir(slurmctld_conf.state_save_location, 0755) < 0) && 
	    (errno != EEXIST)) {
		error("mkdir(%s): %m", slurmctld_conf.state_save_location);
		return SLURM_ERROR;
	}

	/*
	 * Only chdir() to spool directory if slurmctld will be 
	 * running as a daemon
	 */
	if (daemonize && chdir(slurmctld_conf.state_save_location) < 0) {
		error("chdir(%s): %m", slurmctld_conf.state_save_location);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
