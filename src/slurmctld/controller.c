/*****************************************************************************\
 *  controller.c - main control machine daemon for slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, Kevin Tew <tew1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <errno.h>
#include <getopt.h>
#include <grp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/daemonize.h"
#include "src/common/extra_constraints.h"
#include "src/common/fd.h"
#include "src/common/group_cache.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/port_mgr.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/run_command.h"
#include "src/common/sluid.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_socket.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/timers.h"
#include "src/common/track_script.h"
#include "src/common/uid.h"
#include "src/common/util-net.h"
#include "src/common/xstring.h"
#include "src/common/xsystemd.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/acct_gather_profile.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/certmgr.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/hash.h"
#include "src/interfaces/job_submit.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/jobcomp.h"
#include "src/interfaces/mcs.h"
#include "src/interfaces/mpi.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/preempt.h"
#include "src/interfaces/prep.h"
#include "src/interfaces/priority.h"
#include "src/interfaces/sched_plugin.h"
#include "src/interfaces/select.h"
#include "src/interfaces/serializer.h"
#include "src/interfaces/site_factor.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/tls.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/heartbeat.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/rate_limit.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/rpc_queue.h"
#include "src/slurmctld/sackd_mgr.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"

#include "src/stepmgr/srun_comm.h"
#include "src/stepmgr/stepmgr.h"

decl_static_data(usage_txt);

#ifdef MEMORY_LEAK_DEBUG
#define SLURMCTLD_CONMGR_DEFAULT_THREADS 8
#else
#define SLURMCTLD_CONMGR_DEFAULT_THREADS 64
#endif
#define SLURMCTLD_CONMGR_DEFAULT_MAX_CONNECTIONS 50
#define MIN_CHECKIN_TIME  3	/* Nodes have this number of seconds to
				 * check-in before we ping them */
#define SHUTDOWN_WAIT     2	/* Time to wait for backup server shutdown */
#define JOB_COUNT_INTERVAL 30   /* Time to update running job count */

#define DEV_TTY_PATH "/dev/tty"
#define DEV_NULL_PATH "/dev/null"

/**************************************************************************\
 * To test for memory leaks, set MEMORY_LEAK_DEBUG to 1 using
 * "configure --enable-memory-leak-debug" then execute
 *
 * $ valgrind --tool=memcheck --leak-check=yes --num-callers=40 \
 *   --leak-resolution=high ./slurmctld -Dc >valg.ctld.out 2>&1
 *
 * Then exercise the slurmctld functionality before executing
 * > scontrol shutdown
 *
 * Note that --enable-memory-leak-debug will cause the daemon to
 * unload the shared objects at exit thus preventing valgrind
 * to display the stack where the eventual leaks may be.
 * It is always best to test with and without --enable-memory-leak-debug.
 *
 * On some systems _keyvalue_regex_init() will generate two blocks "definitely
 *    lost", both of size zero.
 * On some systems dlopen() will generate a small number of "definitely
 *    lost" blocks that are not cleared by dlclose().
 * On some systems, pthread_create() will generated a small number of
 *    "possibly lost" blocks.
 * Otherwise the report should be free of errors. Remember to reset
 *    MEMORY_LEAK_DEBUG to 0 for production use (non-seamless backup
 *    controller use).
\**************************************************************************/

/* Log to stderr and syslog until becomes a daemon */
log_options_t log_opts = LOG_OPTS_INITIALIZER;
/* Scheduler Log options */
log_options_t sched_log_opts = SCHEDLOG_OPTS_INITIALIZER;

/* Global variables */
bool    preempt_send_user_signal = false;
uint16_t accounting_enforce = 0;
void *	acct_db_conn = NULL;
int	backup_inx;
int	batch_sched_delay = 3;
bool cloud_dns = false;
uint32_t cluster_cpus = 0;
time_t	control_time = 0;
bool disable_remote_singleton = false;
int max_depend_depth = 10;
time_t	last_proc_req_start = 0;
uint32_t max_powered_nodes = NO_VAL;
bool	ping_nodes_now = false;
pthread_cond_t purge_thread_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t purge_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t check_bf_running_lock = PTHREAD_MUTEX_INITIALIZER;
int	sched_interval = 60;
slurmctld_config_t slurmctld_config = {0};
diag_stats_t slurmctld_diag_stats;
bool slurmctld_primary = true;
bool	want_nodes_reboot = true;
int   slurmctld_tres_cnt = 0;
slurmdb_cluster_rec_t *response_cluster_rec = NULL;
uint16_t running_cache = RUNNING_CACHE_STATE_NOTRUNNING;
pthread_mutex_t assoc_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t assoc_cache_cond = PTHREAD_COND_INITIALIZER;

/* Local variables */
static pthread_t assoc_cache_thread = (pthread_t) 0;
static char binary[PATH_MAX];
static int	bu_rc = SLURM_SUCCESS;
static int	bu_thread_cnt = 0;
static pthread_cond_t bu_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t bu_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool daemonize = true;
static bool setwd = false;
static int	debug_level = 0;
static char *	debug_logfile = NULL;
static bool	dump_core = false;
static int      job_sched_cnt = 0;
static int main_argc = 0;
static char **main_argv = NULL;
static uint32_t max_server_threads = MAX_SERVER_THREADS;
static time_t	next_stats_reset = 0;
static int	new_nice = 0;
static bool original = true;
static int pidfd = -1;
/*
 * 0 = use no saved state information
 * 1 = recover saved job state,
 *     node DOWN/DRAIN state & reason information
 * 2 = recover state saved from last shutdown
 */
static int recover = 1;
static pthread_mutex_t sched_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static char *	slurm_conf_filename;
static int reconfig_rc = SLURM_SUCCESS;
static bool reconfig = false;
static list_t *reconfig_reqs = NULL;
static pthread_mutex_t shutdown_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shutdown_cond = PTHREAD_COND_INITIALIZER;
static bool under_systemd = false;

/* Array of listening sockets */
static struct {
	pthread_mutex_t mutex;
	int count;
	int *fd;
	conmgr_fd_t **cons;
	bool standby_mode;
	bool quiesced;
} listeners = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.quiesced = true,
};

typedef struct primary_thread_arg {
	pid_t cpid;
	char *prog_type;
} primary_thread_arg_t;

static int          _accounting_cluster_ready();
static int          _accounting_mark_all_nodes_down(char *reason);
static void *       _assoc_cache_mgr(void *no_data);
static int          _controller_index(void);
static void         _create_clustername_file(void);
static void _flush_rpcs(void);
static void         _get_fed_updates();
static void         _init_config(void);
static void         _init_pidfile(void);
static int          _init_tres(void);
static void         _kill_old_slurmctld(void);
static void _open_ports(void);
static void         _parse_commandline(int argc, char **argv);
static void _post_reconfig(void);
static void *       _purge_files_thread(void *no_data);
static void *_acct_update_thread(void *no_data);
static void         _remove_assoc(slurmdb_assoc_rec_t *rec);
static void         _remove_qos(slurmdb_qos_rec_t *rec);
static void         _restore_job_dependencies(void);
static void         _run_primary_prog(bool primary_on);
static void         _send_future_cloud_to_db();
static void _service_connection(conmgr_callback_args_t conmgr_args,
				int input_fd, int output_fd, void *arg);
static void         _set_work_dir(void);
static int          _shutdown_backup_controller(void);
static void *       _slurmctld_background(void *no_data);
static void         _test_thread_limit(void);
static int _try_to_reconfig(void);
static void         _update_assoc(slurmdb_assoc_rec_t *rec);
static void         _update_diag_job_state_counts(void);
static void         _update_cluster_tres(void);
static void         _update_nice(void);
static void _update_pidfile(void);
static void         _update_qos(slurmdb_qos_rec_t *rec);
static void _usage(void);
static void _verify_clustername(void);
static void *       _wait_primary_prog(void *arg);

static void _send_reconfig_replies(void)
{
	slurm_msg_t *msg = NULL;

	while ((msg = list_pop(reconfig_reqs))) {
		/* Must avoid sending reply via msg->conmgr_fd */
		xassert((msg->conn_fd >= 0) || msg->conn);

		(void) slurm_send_rc_msg(msg, reconfig_rc);
		fd_close(&msg->conn_fd);
		slurm_free_msg(msg);
	}
}

static void _attempt_reconfig(void)
{
	info("Attempting to reconfigure");

	/*
	 * Reconfigure requires all connections to fully processed before
	 * continuing as the file descriptors will be closed during fork() and
	 * the parent process will call _exit() instead of finishing their
	 * processing if the new slurmctld process starts successfully.
	 */
	conmgr_quiesce(__func__);

	/*
	 * Send RC to requestors in foreground mode now as slurmctld is about
	 * to call exec() which will close connections.
	 */
	if (!daemonize && !under_systemd)
		_send_reconfig_replies();

	reconfig_rc = _try_to_reconfig();

	_send_reconfig_replies();

	if (!reconfig_rc) {
		info("Relinquishing control to new child");
		_exit(0);
	}

	recover = 2;

	/*
	 * Reconfigure failed which means this process needs start again
	 * processing connections.
	 */
	conmgr_unquiesce(__func__);
}

static void _on_sigint(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Terminate signal SIGINT received");
	slurmctld_shutdown();
}

static void _on_sigterm(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Terminate signal SIGTERM received");
	slurmctld_shutdown();
}

static void _on_sigchld(conmgr_callback_args_t conmgr_args, void *arg)
{
	debug5("Caught SIGCHLD. Ignoring");
}

static void _on_sigquit(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Terminate signal SIGQUIT received");
	slurmctld_shutdown();
}

static void _on_sigtstp(conmgr_callback_args_t conmgr_args, void *arg)
{
	debug5("Caught SIGTSTP. Ignoring");
}

static void _on_sighup(conmgr_callback_args_t conmgr_args, void *arg)
{
	bool standby_mode;

	info("Reconfigure signal (SIGHUP) received");

	slurm_mutex_lock(&listeners.mutex);
	standby_mode = listeners.standby_mode;
	slurm_mutex_unlock(&listeners.mutex);

	if (standby_mode) {
		backup_on_sighup();
		return;
	}

	reconfig = true;
	slurmctld_shutdown();
}

static void _on_sigusr1(conmgr_callback_args_t conmgr_args, void *arg)
{
	debug5("Caught SIGUSR1. Ignoring.");
}

static void _on_sigusr2(conmgr_callback_args_t conmgr_args, void *arg)
{
	static const slurmctld_lock_t conf_write_lock = {
		.conf = WRITE_LOCK,
	};

	info("Logrotate signal (SIGUSR2) received");

	lock_slurmctld(conf_write_lock);
	update_logging();
	if (slurmctld_primary)
		slurmscriptd_update_log_level(slurm_conf.slurmctld_debug, true);
	unlock_slurmctld(conf_write_lock);

	if (slurmctld_primary && jobcomp_g_set_location())
		error("%s: JobComp set location operation failed on SIGUSR2",
		      __func__);
}

static void _on_sigpipe(conmgr_callback_args_t conmgr_args, void *arg)
{
	debug5("Caught SIGPIPE. Ignoring.");
}

static void _on_sigxcpu(conmgr_callback_args_t conmgr_args, void *arg)
{
	debug5("Caught SIGXCPU. Ignoring.");
}

static void _on_sigabrt(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("SIGABRT received");
	slurmctld_shutdown();
	dump_core = true;
}

static void _on_sigalrm(conmgr_callback_args_t conmgr_args, void *arg)
{
	debug5("Caught SIGALRM. Ignoring.");
}

static void _register_signal_handlers(conmgr_callback_args_t conmgr_args,
				      void *arg)
{
	conmgr_add_work_signal(SIGINT, _on_sigint, NULL);
	conmgr_add_work_signal(SIGTERM, _on_sigterm, NULL);
	conmgr_add_work_signal(SIGCHLD, _on_sigchld, NULL);
	conmgr_add_work_signal(SIGQUIT, _on_sigquit, NULL);
	conmgr_add_work_signal(SIGTSTP, _on_sigtstp, NULL);
	conmgr_add_work_signal(SIGHUP, _on_sighup, NULL);
	conmgr_add_work_signal(SIGUSR1, _on_sigusr1, NULL);
	conmgr_add_work_signal(SIGUSR2, _on_sigusr2, NULL);
	conmgr_add_work_signal(SIGPIPE, _on_sigpipe, NULL);
	conmgr_add_work_signal(SIGXCPU, _on_sigxcpu, NULL);
	conmgr_add_work_signal(SIGABRT, _on_sigabrt, NULL);
	conmgr_add_work_signal(SIGALRM, _on_sigalrm, NULL);
}

static void _reopen_stdio(void)
{
	int devnull = -1;

	if ((devnull = open(DEV_NULL_PATH, O_RDWR)) < 0)
		fatal_abort("Unable to open %s: %m", DEV_NULL_PATH);

	dup2(devnull, STDIN_FILENO);
	dup2(devnull, STDOUT_FILENO);
	dup2(devnull, STDERR_FILENO);

	if (devnull > STDERR_FILENO)
		fd_close(&devnull);

#ifdef __linux__
	if (isatty(STDOUT_FILENO) && !daemonize) {
		int tty = -1;

		if ((tty = open(DEV_TTY_PATH, O_WRONLY)) > 0 && isatty(tty)) {
			dup2(tty, STDOUT_FILENO);
			dup2(tty, STDERR_FILENO);
		}

		if (tty > STDERR_FILENO)
			fd_close(&tty);
	}
#endif /* __linux__ */
}

static void _init_db_conn(void)
{
	int rc;

	/*
	 * errno is used to get an error when establishing the persistent
	 * connection. Set errno to 0 here to avoid a previous value for
	 * errno causing us to think there was a problem.
	 * FIXME: Stop using errno for control flow here.
	 */
	errno = 0;

	if (acct_db_conn)
		acct_storage_g_close_connection(&acct_db_conn);

	acct_db_conn = acct_storage_g_get_connection(
		0, NULL, false, slurm_conf.cluster_name);
	rc = clusteracct_storage_g_register_ctld(acct_db_conn,
						 slurm_conf.slurmctld_port);

	if (rc & RC_AS_CLUSTER_ID) {
		uint16_t id = rc & ~RC_AS_CLUSTER_ID;
		if (slurm_conf.cluster_id && (id != slurm_conf.cluster_id)) {
			fatal("CLUSTER ID MISMATCH.\n"
			      "slurmctld has been started with \"ClusterID=%u\"  from the state files in StateSaveLocation, but the DBD thinks it should be \"%u\".\n"
			      "Running multiple clusters from a shared StateSaveLocation WILL CAUSE CORRUPTION.\n"
			      "Remove %s/clustername to override this safety check if this is intentional.",
			      slurm_conf.cluster_id, id,
			      slurm_conf.state_save_location);
		} else if (!slurm_conf.cluster_id) {
			slurm_conf.cluster_id = id;
			_create_clustername_file();
		}
	}
}

/*
 * Retry connecting to the dbd and initializing assoc_mgr until success, or
 * fatal on shutdown.
 */
static void _retry_init_db_conn(assoc_init_args_t *args)
{
	while (true) {
		struct timespec ts = timespec_now();
		ts.tv_sec += 2;

		slurm_mutex_lock(&shutdown_mutex);
		slurm_cond_timedwait(&shutdown_cond, &shutdown_mutex, &ts);
		slurm_mutex_unlock(&shutdown_mutex);

		if (slurmctld_config.shutdown_time)
			fatal("slurmdbd must be up at slurmctld start time");

		error("Retrying initial connection to slurmdbd");
		_init_db_conn();
		if (!slurm_conf.cluster_id) {
			error("Still don't know my ClusterID");
			continue;
		}
		if (!assoc_mgr_init(acct_db_conn, args, errno))
			break;
	}
}

/* main - slurmctld main function, start various threads and process RPCs */
int main(int argc, char **argv)
{
	int error_code;
	struct timeval start, now;
	struct stat stat_buf;
	struct rlimit rlim;
	/* Locks: Write configuration, job, node, and partition */
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	prep_callbacks_t prep_callbacks = {
		.prolog_slurmctld = prep_prolog_slurmctld_callback,
		.epilog_slurmctld = prep_epilog_slurmctld_callback,
	};
	bool backup_has_control = false;
	bool slurmscriptd_mode = false;
	char *conf_file;
	stepmgr_ops_t stepmgr_ops = {0};

	stepmgr_ops.agent_queue_request = agent_queue_request;
	stepmgr_ops.find_front_end_record = find_front_end_record;
	stepmgr_ops.find_job_array_rec = find_job_array_rec;
	stepmgr_ops.find_job_record = find_job_record;
	stepmgr_ops.job_config_fini = job_config_fini;
	stepmgr_ops.last_job_update = &last_job_update;
	stepmgr_init(&stepmgr_ops);

	main_argc = argc;
	main_argv = argv;

	if (getenv("SLURMCTLD_RECONF"))
		original = false;
	if (getenv(SLURMSCRIPTD_MODE_ENV))
		slurmscriptd_mode = true;

	/*
	 * Make sure we have no extra open files which
	 * would be propagated to spawned tasks.
	 */
	if (original || slurmscriptd_mode) {
		closeall(slurmscriptd_mode ? SLURMSCRIPT_CLOSEALL :
			 (STDERR_FILENO + 1));
	}

	if (slurmscriptd_mode)
		_reopen_stdio();

	/*
	 * Establish initial configuration
	 */
	_init_config();
	_parse_commandline(argc, argv);
	log_init(argv[0], log_opts, LOG_DAEMON, NULL);
	sched_log_init(argv[0], sched_log_opts, LOG_DAEMON, NULL);
	/*
	 * Must pass in an explicit filename to slurm_conf_init() to avoid
	 * the "configless" mode of operation kicking in if no file is
	 * currently available.
	 */
	if (!(conf_file = slurm_conf_filename))
		if (!(conf_file = getenv("SLURM_CONF")))
			conf_file = default_slurm_config_file;
	slurm_conf_init(conf_file);

	lock_slurmctld(config_write_lock);
	update_logging();
	unlock_slurmctld(config_write_lock);

	if (slurmscriptd_mode) {
		/* Cleanup env */
		(void) unsetenv(SLURMSCRIPTD_MODE_ENV);
		/* Execute in slurmscriptd mode. */
		become_slurm_user();
		slurmscriptd_run_slurmscriptd(argc, argv, binary);
	}

	memset(&slurmctld_diag_stats, 0, sizeof(slurmctld_diag_stats));
	/*
	 * Calculate speed of gettimeofday() for sdiag.
	 * Large delays indicate the Linux vDSO is not in use, which
	 * will lead to significant scheduler performance issues.
	 */
	gettimeofday(&start, NULL);
	for (int i = 0; i < 1000; i++)
		gettimeofday(&now, NULL);

	slurmctld_diag_stats.latency  = (now.tv_sec  - start.tv_sec) * 1000000;
	slurmctld_diag_stats.latency +=  now.tv_usec - start.tv_usec;

	if (slurmctld_diag_stats.latency > 200)
		error("High latency for 1000 calls to gettimeofday(): %d microseconds",
		      slurmctld_diag_stats.latency);

	/*
	 * Verify clustername from conf matches value in spool dir
	 * exit if inconsistent to protect state files from corruption.
	 * This needs to be done before we kill the old one just in case we
	 * fail.
	 */
	_verify_clustername();

	_update_nice();
	if (original)
		_kill_old_slurmctld();

	for (int i = 0; i < 3; i++)
		fd_set_close_on_exec(i);

	if (original && daemonize) {
		if (xdaemon())
			error("daemon(): %m");
		sched_debug("slurmctld starting");
	}

	if (slurm_conf.slurmctld_params)
		conmgr_set_params(slurm_conf.slurmctld_params);

	conmgr_init(SLURMCTLD_CONMGR_DEFAULT_THREADS,
		    SLURMCTLD_CONMGR_DEFAULT_MAX_CONNECTIONS,
		    (conmgr_callbacks_t) {0});

	conmgr_add_work_fifo(_register_signal_handlers, NULL);

	conmgr_run(false);

	if (auth_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize auth plugin");
	if (hash_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize hash plugin");
	if (tls_g_init() != SLURM_SUCCESS)
		fatal("Failed to initialize tls plugin");
	if (certmgr_g_init() != SLURM_SUCCESS)
		fatal("Failed to initialize certmgr plugin");

	if (original && !under_systemd) {
		/*
		 * Need to create pidfile here in case we setuid() below
		 * (init_pidfile() exits if it can't initialize pid file).
		 * On Linux we also need to make this setuid job explicitly
		 * able to write a core dump.
		 */
		_init_pidfile();
		become_slurm_user();
	}

	reconfig_reqs = list_create(NULL);

	rate_limit_init();
	rpc_queue_init();

	/* open ports must happen after become_slurm_user() */
	 _open_ports();

	/*
	 * Create StateSaveLocation directory if necessary.
	 */
	set_slurmctld_state_loc();

	if (daemonize || setwd)
		_set_work_dir();

	if (stat(slurm_conf.mail_prog, &stat_buf) != 0)
		error("Configured MailProg is invalid");

	if (!slurm_conf.accounting_storage_type) {
		if (slurm_conf.job_acct_gather_type)
			error("Job accounting information gathered, but not stored");
	} else if (!slurm_conf.job_acct_gather_type)
		info("Job accounting information stored, but details not gathered");

	if (license_init(slurm_conf.licenses) != SLURM_SUCCESS)
		fatal("Invalid Licenses value: %s", slurm_conf.licenses);

#ifdef PR_SET_DUMPABLE
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#endif /* PR_SET_DUMPABLE */

	/* Warn if the stack size is not unlimited */
	if ((getrlimit(RLIMIT_STACK, &rlim) == 0) &&
	    (rlim.rlim_cur != RLIM_INFINITY))
		info("Stack size set to %ld", rlim.rlim_max);

	test_core_limit();
	_test_thread_limit();


	/*
	 * This creates a thread to listen to slurmscriptd, so this needs to
	 * happen after we block signals so that thread doesn't catch any
	 * signals.
	 */
	slurmscriptd_init(argv, binary);
	if ((run_command_init(argc, argv, binary) != SLURM_SUCCESS) &&
	    binary[0])
		fatal("%s: Unable to reliably execute %s", __func__, binary);

	accounting_enforce = slurm_conf.accounting_storage_enforce;
	if (slurm_with_slurmdbd()) {
		/* we need job_list not to be NULL */
		init_job_conf();
	}

	if (accounting_enforce && !slurm_with_slurmdbd()) {
		accounting_enforce = 0;
		slurm_conf.conf_flags &= (~CONF_FLAG_WCKEY);
		slurm_conf.accounting_storage_enforce = 0;

		error("You can not have AccountingStorageEnforce set for AccountingStorageType='%s'",
		      slurm_conf.accounting_storage_type);
	}

	info("%s version %s started on cluster %s(%u)",
	     slurm_prog_name, SLURM_VERSION_STRING, slurm_conf.cluster_name,
	     slurm_conf.cluster_id);
	if ((error_code = gethostname_short(slurmctld_config.node_name_short,
					    HOST_NAME_MAX)))
		fatal("getnodename_short error %s", slurm_strerror(error_code));
	if ((error_code = gethostname(slurmctld_config.node_name_long,
				      HOST_NAME_MAX)))
		fatal("getnodename error %s", slurm_strerror(error_code));

	/* init job credential stuff */
	if (cred_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize cred plugin");

	/* Must set before plugins are loaded. */
	backup_inx = _controller_index();
	if (backup_inx == -1) {
		error("This host (%s/%s) not a valid controller",
		      slurmctld_config.node_name_short,
		      slurmctld_config.node_name_long);
		exit(1);
	}

	if (backup_inx > 0) {
		slurmctld_primary = false;

		if (xstrcasestr(slurm_conf.sched_params,
		                "no_backup_scheduling"))
			slurmctld_config.scheduling_disabled = true;
	}

	if (!original && !slurmctld_primary) {
		info("Restarted while operating as primary, resuming operation as primary.");
		backup_has_control = true;
	}

	/*
	 * Initialize plugins.
	 * If running configuration test, report ALL failures.
	 */
	if (select_g_init(0) != SLURM_SUCCESS)
		fatal("failed to initialize node selection plugin");
	/* gres_init() must follow select_g_init() */
	if (gres_init() != SLURM_SUCCESS)
		fatal("failed to initialize gres plugin");
	if (preempt_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize preempt plugin");
	if (acct_gather_conf_init() != SLURM_SUCCESS)
		fatal("failed to initialize acct_gather plugins");
	if (jobacct_gather_init() != SLURM_SUCCESS)
		fatal("failed to initialize jobacct_gather plugin");
	if (job_submit_g_init(false) != SLURM_SUCCESS)
		fatal("failed to initialize job_submit plugin");
	if (prep_g_init(&prep_callbacks) != SLURM_SUCCESS)
		fatal("failed to initialize prep plugin");
	if (node_features_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize node_features plugin");
	if (mpi_g_daemon_init() != SLURM_SUCCESS)
		fatal("Failed to initialize MPI plugins.");
	/* Fatal if we use extra_constraints without json serializer */
	if (extra_constraints_enabled() &&
	    serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))
		fatal("Extra constraints feature requires a json serializer.");
	if (serializer_g_init(NULL, NULL))
		fatal("Failed to initialize serialization plugins.");
	if (switch_g_init(true) != SLURM_SUCCESS)
		fatal("Failed to initialize switch plugin");

	if (original && under_systemd)
		xsystemd_change_mainpid(getpid());

	while (1) {
		bool reconfiguring = reconfig;
		/* initialization for each primary<->backup switch */
		slurmctld_config.shutdown_time = (time_t) 0;
		slurmctld_config.resume_backup = false;
		control_time = 0;
		reconfig = false;
		reconfig_rc = SLURM_SUCCESS;

		agent_init();

		/* start in primary or backup mode */
		if (!slurmctld_primary && !backup_has_control) {
			controller_fini_scheduling(); /* make sure shutdown */
			_run_primary_prog(false);
			if (acct_storage_g_init() != SLURM_SUCCESS)
				fatal("failed to initialize accounting_storage plugin");
			if (bb_g_init() != SLURM_SUCCESS)
				fatal("failed to initialize burst buffer plugin");

			slurm_mutex_lock(&listeners.mutex);
			listeners.standby_mode = true;
			slurm_mutex_unlock(&listeners.mutex);

			/*
			 * run_backup() will never return unless it is time for
			 * standby to take control as backup controller
			 */
			run_backup();

			slurm_mutex_lock(&listeners.mutex);
			listeners.standby_mode = false;
			slurm_mutex_unlock(&listeners.mutex);

			(void) _shutdown_backup_controller();
		} else {
			if (acct_storage_g_init() != SLURM_SUCCESS)
				fatal("failed to initialize accounting_storage plugin");
			(void) _shutdown_backup_controller();
			trigger_primary_ctld_res_ctrl();
			ctld_assoc_mgr_init();
			/*
			 * read_slurm_conf() will load the burst buffer state,
			 * init the burst buffer plugin early.
			 */
			if (bb_g_init() != SLURM_SUCCESS)
				fatal("failed to initialize burst_buffer plugin");
			/* Now recover the remaining state information */
			lock_slurmctld(config_write_lock);
			if (switch_g_restore(recover))
				fatal("failed to initialize switch plugin");
		}

		/*
		 * priority_g_init() needs to be called after assoc_mgr_init()
		 * and before read_slurm_conf() because jobs could be killed
		 * during read_slurm_conf() and call priority_g_job_end().
		 */
		if (priority_g_init() != SLURM_SUCCESS)
			fatal("failed to initialize priority plugin");

		if ((slurmctld_primary || backup_has_control) &&
		    !reconfiguring) {
			if ((error_code = read_slurm_conf(recover))) {
				fatal("read_slurm_conf reading %s: %s",
				      slurm_conf.slurm_conf,
				      slurm_strerror(error_code));
			}
			configless_update();
			if (conf_includes_list) {
				/*
				 * clear included files so that subsequent conf
				 * parsings refill it with updated information.
				 */
				list_flush(conf_includes_list);
			}
		}

		priority_g_thread_start();

		if (slurmctld_primary || backup_has_control) {
			select_g_select_nodeinfo_set_all();
			unlock_slurmctld(config_write_lock);

			if (recover == 0) {
				slurmctld_init_db = 1;
				_accounting_mark_all_nodes_down("cold-start");
			}
		}

		slurm_persist_conn_recv_server_init();
		info("Running as primary controller");
		if (!reconfiguring) {
			_run_primary_prog(true);
			control_time = time(NULL);
			heartbeat_start();
			if (!slurmctld_config.resume_backup && slurmctld_primary)
				trigger_primary_ctld_res_op();
		}

		/* Set pointers after they have been set */
		stepmgr_ops.acct_db_conn = acct_db_conn;
		stepmgr_ops.job_list = job_list;
		stepmgr_ops.up_node_bitmap = up_node_bitmap;

		_accounting_cluster_ready();
		_send_future_cloud_to_db();

		/*
		 * call after registering so that the current cluster's
		 * control_host and control_port will be filled in.
		 */
		fed_mgr_init(acct_db_conn);

		_restore_job_dependencies();

		sync_job_priorities();

		if (mcs_g_init() != SLURM_SUCCESS)
			fatal("failed to initialize mcs plugin");

		/*
		 * create attached thread for state save
		 */
		slurm_thread_create(&slurmctld_config.thread_id_save,
				    slurmctld_state_save, NULL);

		/*
		 * create attached thread for node power management
  		 */
		power_save_init();

		/*
		 * create attached thread for purging completed job files
		 */
		slurm_thread_create(&slurmctld_config.thread_id_purge_files,
				    _purge_files_thread, NULL);

		/*
		 * create attached thread for purging completed job files
		 */
		slurm_thread_create(&slurmctld_config.thread_id_acct_update,
				    _acct_update_thread, NULL);

		/*
		 * If reconfiguring, we need to restart the gang scheduler.
		 * Otherwise, gang scheduling was already started by
		 * read_slurm_conf().
		 */
		if (controller_init_scheduling(reconfiguring) != SLURM_SUCCESS)
			fatal("Failed to initialize the various schedulers");

		if (!original && !reconfiguring) {
			notify_parent_of_success();
			if (!under_systemd)
				_update_pidfile();
			_post_reconfig();
		}

		/*
		 * process slurm background activities, could run as pthread
		 */
		_slurmctld_background(NULL);

		controller_fini_scheduling(); /* Stop all scheduling */
		agent_fini();

		/* termination of controller */
		switch_g_save();
		priority_g_fini();
		shutdown_state_save();
		slurm_mutex_lock(&purge_thread_lock);
		slurm_cond_signal(&purge_thread_cond); /* wake up last time */
		slurm_mutex_unlock(&purge_thread_lock);
		slurm_thread_join(slurmctld_config.thread_id_purge_files);
		slurm_thread_join(slurmctld_config.thread_id_save);
		slurm_mutex_lock(&slurmctld_config.acct_update_lock);
		slurm_cond_broadcast(&slurmctld_config.acct_update_cond);
		slurm_mutex_unlock(&slurmctld_config.acct_update_lock);
		slurm_thread_join(slurmctld_config.thread_id_acct_update);

		/* kill all scripts running by the slurmctld */
		track_script_flush();
		slurmscriptd_flush();
		run_command_shutdown();

		bb_g_fini();
		mcs_g_fini();
		fed_mgr_fini();

		ctld_assoc_mgr_fini();

		/* Save any pending state save RPCs */
		acct_storage_g_close_connection(&acct_db_conn);
		acct_storage_g_fini();

		slurm_persist_conn_recv_server_fini();
		power_save_fini();

		/* attempt reconfig here */
		if (reconfig) {
			_attempt_reconfig();
			continue;
		}

		config_power_mgr_fini();

		/* stop the heartbeat last */
		heartbeat_stop();

		/*
		 * Run SlurmctldPrimaryOffProg only if we are the primary
		 * (backup_inx == 0). The backup controllers (backup_inx > 0)
		 * already run it when dropping to standby mode.
		 */
		if (slurmctld_primary)
			_run_primary_prog(false);

		if (slurmctld_config.resume_backup == false)
			break;

		/* primary controller doesn't resume backup mode */
		if (slurmctld_config.resume_backup && slurmctld_primary)
			break;

		/* The backup is now meant to relinquish control */
		if (slurmctld_config.resume_backup && !slurmctld_primary)
			backup_has_control = false;

		recover = 2;

		/*
		 * We need to re-initialize run_command after
		 * run_command_shutdown() was called. Pass NULL since we do
		 * not want to change the script launcher location.
		 */
		(void) run_command_init(0, NULL, NULL);
	}

	slurmscriptd_fini();
	jobcomp_g_fini();

	/*
	 * Since pidfile is created as user root (its owner is
	 *   changed to SlurmUser) SlurmUser may not be able to
	 *   remove it, so this is not necessarily an error.
	 */
	if (!under_systemd && (unlink(slurm_conf.slurmctld_pidfile) < 0)) {
		verbose("Unable to remove pidfile '%s': %m",
			slurm_conf.slurmctld_pidfile);
	}


#ifdef MEMORY_LEAK_DEBUG
{
	/*
	 * This should purge all allocated memory.
	 *  Anything left over represents a leak.
	 */

	xassert(list_is_empty(reconfig_reqs));
	FREE_NULL_LIST(reconfig_reqs);
	agent_purge();

	/* Purge our local data structures */
	configless_clear();
	job_fini();
	part_fini();	/* part_fini() must precede node_fini() */
	node_fini();
	mpi_fini();
	node_features_g_fini();
	purge_front_end_state();
	resv_fini();
	trigger_fini();
	assoc_mgr_fini(1);
	reserve_port_config(NULL, NULL);

	/* Some plugins are needed to purge job/node data structures,
	 * unplug after other data structures are purged */
	gres_fini();
	job_submit_g_fini(false);
	prep_g_fini();
	preempt_g_fini();
	jobacct_gather_fini();
	acct_gather_conf_destroy();
	select_g_fini();
	topology_g_fini();
	auth_g_fini();
	hash_g_fini();
	tls_g_fini();
	certmgr_g_fini();
	switch_g_fini();
	site_factor_g_fini();

	/* purge remaining data structures */
	group_cache_purge();
	getnameinfo_cache_purge();
	license_free();
	FREE_NULL_LIST(slurmctld_config.acct_update_list);
	cred_g_fini();
	slurm_conf_destroy();
	cluster_rec_free();
	track_script_fini();
	cgroup_conf_destroy();
	usleep(500000);
	serializer_g_fini();
	bit_cache_fini();
}
#endif

	conmgr_request_shutdown();
	conmgr_fini();

	rate_limit_shutdown();
	rpc_queue_shutdown();
	log_fini();
	sched_log_fini();

	if (dump_core)
		abort();
	else
		exit(0);
}

static int _find_node_event(void *x, void *key)
{
	slurmdb_event_rec_t *event = x;
	char *node_name = key;

	return !xstrcmp(event->node_name, node_name);
}

/*
 * Create db down events for FUTURE and CLOUD+POWERED_DOWN nodes
 */
static void _send_future_cloud_to_db()
{
	time_t now = time(NULL);
	slurmdb_event_rec_t *event = NULL;
	list_t *event_list = NULL;
	bool check_db = !running_cache;
	node_record_t *node_ptr;

	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		if (!IS_NODE_FUTURE(node_ptr) &&
		    !IS_NODE_POWERED_DOWN(node_ptr))
			continue;

		/*
		 * If the DBD is up, then try to avoid making duplicate
		 * g_node_down() calls by reconciling with the db. If it's not
		 * up, just send the down events to preserve the startup time
		 * stamps.
		 */
		if (check_db && !event_list) {
			slurmdb_event_cond_t event_cond = {0};
			event_cond.event_type = SLURMDB_EVENT_NODE;
			event_cond.cond_flags = SLURMDB_EVENT_COND_OPEN;

			event_cond.cluster_list = list_create(xfree_ptr);
			list_append(event_cond.cluster_list,
				    xstrdup(slurm_conf.cluster_name));

			event_cond.format_list = list_create(NULL);
			list_append(event_cond.format_list, "node_name");

			event_cond.state_list = list_create(xfree_ptr);
			list_append(event_cond.state_list,
				    xstrdup_printf("%u", NODE_STATE_FUTURE));
			list_append(event_cond.state_list,
				    xstrdup_printf("%"PRIu64,
						   NODE_STATE_POWERED_DOWN));

			event_list = acct_storage_g_get_events(acct_db_conn,
							       getuid(),
							       &event_cond);
			if (!event_list)
				check_db = false;

			FREE_NULL_LIST(event_cond.cluster_list);
			FREE_NULL_LIST(event_cond.format_list);
			FREE_NULL_LIST(event_cond.state_list);
		}

		if (event_list &&
		    (event = list_find_first(event_list, _find_node_event,
					     node_ptr->name))) {
			/* Open event record already exists, don't send again */
			continue;
		}

		clusteracct_storage_g_node_down(
			acct_db_conn, node_ptr, now,
			IS_NODE_FUTURE(node_ptr) ? "Future" : "Powered down",
			slurm_conf.slurm_user_id);
	}

	FREE_NULL_LIST(event_list);
}

/* initialization of common slurmctld configuration */
static void  _init_config(void)
{
	struct rlimit rlim;

	rlimits_use_max_nofile();
	if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		(void) setrlimit(RLIMIT_CORE, &rlim);
	}
	if (getrlimit(RLIMIT_STACK, &rlim) == 0) {
		/* slurmctld can spawn lots of pthreads.
		 * Set the (per thread) stack size to a
		 * more "reasonable" value to avoid running
		 * out of virtual memory and dying */
		rlim.rlim_cur = rlim.rlim_max;
		(void) setrlimit(RLIMIT_STACK, &rlim);
	}
	if (getrlimit(RLIMIT_DATA, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		(void) setrlimit(RLIMIT_DATA, &rlim);
	}

	memset(&slurmctld_config, 0, sizeof(slurmctld_config_t));
	FREE_NULL_LIST(slurmctld_config.acct_update_list);
	slurmctld_config.acct_update_list =
		list_create(slurmdb_destroy_update_object);
	slurm_mutex_init(&slurmctld_config.acct_update_lock);
	slurm_cond_init(&slurmctld_config.acct_update_cond, NULL);
	slurm_cond_init(&slurmctld_config.backup_finish_cond, NULL);
	slurm_mutex_init(&slurmctld_config.backup_finish_lock);
	slurmctld_config.boot_time      = time(NULL);
	slurmctld_config.resume_backup  = false;
	slurmctld_config.server_thread_count = 0;
	slurmctld_config.shutdown_time  = (time_t) 0;
	slurmctld_config.thread_id_main = pthread_self();
	slurmctld_config.scheduling_disabled  = false;
	slurmctld_config.submissions_disabled = false;
	track_script_init();
	slurm_mutex_init(&slurmctld_config.thread_count_lock);
	slurm_cond_init(&slurmctld_config.thread_count_cond, NULL);
	slurmctld_config.thread_id_main    = (pthread_t) 0;
}

static int _try_to_reconfig(void)
{
	extern char **environ;
	char **child_env;
	pid_t pid;
	int to_parent[2] = {-1, -1};
	int *skip_close = NULL, skip_index = 0, auth_fd = -1;

	child_env = env_array_copy((const char **) environ);
	setenvf(&child_env, "SLURMCTLD_RECONF", "1");
	if (pidfd != -1) {
		setenvf(&child_env, "SLURMCTLD_RECONF_PIDFD", "%d", pidfd);
		fd_set_noclose_on_exec(pidfd);
	}
	slurm_mutex_lock(&listeners.mutex);
	/*
	 * Need space in array for:
	 *  - to_parent[1]
	 *  - pidfd
	 *  - auth fd
	 *  - terminator (-1)
	 *  - listeners.count number of listening sockets
	 */
	skip_close = xcalloc((listeners.count + 4), sizeof(*skip_close));
	if (listeners.count) {
		char *ports = NULL, *pos = NULL;

		setenvf(&child_env, "SLURMCTLD_RECONF_LISTEN_COUNT", "%d",
			listeners.count);
		for (int i = 0; i < listeners.count; i++) {
			xstrfmtcatat(ports, &pos, "%d,", listeners.fd[i]);
			if (listeners.fd[i] >= 0) {
				fd_set_noclose_on_exec(listeners.fd[i]);
				skip_close[skip_index++] = listeners.fd[i];
			}
		}
		setenvf(&child_env, "SLURMCTLD_RECONF_LISTEN_FDS", "%s", ports);
		xfree(ports);
	}
	slurm_mutex_unlock(&listeners.mutex);
	if ((auth_fd = auth_g_get_reconfig_fd(AUTH_PLUGIN_SLURM)) >= 0)
		skip_close[skip_index++] = auth_fd;
	for (int i = 0; i < 3; i++)
		fd_set_noclose_on_exec(i);
	if (!daemonize && !under_systemd) {
		/*
		 * If in attached mode, the slurmctld does not fork() so it
		 * does not change its PID. The slurmctld needs to call
		 * slurmscriptd_fini() to reap the slurmscriptd PID, otherwise
		 * the slurmscriptd PID would be a defunct entry in the process
		 * table.
		 * For detached mode, the parent slurmctld needs to keep the
		 * slurmscriptd running in order to recover if the child
		 * slurmctld fails to start. If the child slurmctld starts
		 * successfully, then when the parent slurmctld shuts down the
		 * corresponding slurmscriptd is reparented to init, shuts itself
		 * down, and init will reap the PID for us.
		 */
		slurmscriptd_fini();
		goto start_child;
	}

	if (pipe(to_parent))
		fatal("%s: pipe() failed: %m", __func__);

	setenvf(&child_env, "SLURMCTLD_RECONF_PARENT_FD", "%d", to_parent[1]);
	if ((pid = fork()) < 0) {
		fatal("%s: fork() failed: %m", __func__);
	} else if (pid > 0) {
		pid_t grandchild_pid;
		int rc;
		/*
		 * Close the input side of the pipe so the read() will return
		 * immediately if the child process fatal()s.
		 * Otherwise we'd be stuck here indefinitely assuming another
		 * internal thread might write something to the pipe.
		 */
		(void) close(to_parent[1]);
		safe_read(to_parent[0], &grandchild_pid, sizeof(pid_t));
		info("Relinquishing control to new slurmctld process");
		/*
		 * Ensure child has exited.
		 * Grandchild should be owned by init.
		 */
		if (under_systemd) {
			waitpid(pid, &rc, 0);
			xsystemd_change_mainpid(grandchild_pid);
		}
		xfree(skip_close);
		return SLURM_SUCCESS;

rwfail:
		close(to_parent[0]);
		env_array_free(child_env);
		waitpid(pid, &rc, 0);
		info("Resuming operation, reconfigure failed.");
		xfree(skip_close);
		return SLURM_ERROR;
	}

start_child:
	if (to_parent[1] >= 0)
		skip_close[skip_index++] = to_parent[1];
	if (pidfd >= 0)
		skip_close[skip_index++] = pidfd;
	skip_close[skip_index] = -1;
	closeall_except(3, skip_close);

	/*
	 * This second fork() ensures that the new grandchild's parent is init,
	 * which avoids a nuisance warning from systemd of:
	 * "Supervising process 123456 which is not our child. We'll most likely not notice when it exits"
	 */
	if (under_systemd) {
		if ((pid = fork()) < 0)
			fatal("fork() failed: %m");
		else if (pid)
			exit(0);
	}

	execve(binary, main_argv, child_env);
	fatal("execv() failed: %m");
}

extern void notify_parent_of_success(void)
{
	char *parent_fd_env = getenv("SLURMCTLD_RECONF_PARENT_FD");
	pid_t pid = getpid();
	int fd = -1;
	static bool notified = false;

	if (original || !parent_fd_env || notified)
		return;

	notified = true;

	fd = atoi(parent_fd_env);
	info("child started successfully");
	safe_write(fd, &pid, sizeof(pid_t));
	(void) close(fd);
	return;

rwfail:
	error("failed to notify parent, may have two processes running now");
	(void) close(fd);
}

extern void reconfigure_slurm(slurm_msg_t *msg)
{
	xassert(msg);

	list_append(reconfig_reqs, msg);

	pthread_kill(pthread_self(), SIGHUP);
}

static void _post_reconfig(void)
{
	if (running_configless) {
		configless_update();
		push_reconfig_to_slurmd();
		sackd_mgr_push_reconfig();
	} else {
		msg_to_slurmd(REQUEST_RECONFIGURE);
	}
}

/* Request that the job scheduler execute soon (typically within seconds) */
extern void queue_job_scheduler(void)
{
	slurm_mutex_lock(&sched_cnt_mutex);
	job_sched_cnt++;
	slurm_mutex_unlock(&sched_cnt_mutex);
}

static void *_on_listen_connect(conmgr_fd_t *con, void *arg)
{
	const int *i_ptr = arg;
	const int i = *i_ptr;
	int rc = EINVAL;

	debug3("%s: [%s] Successfully opened RPC listener",
	       __func__, conmgr_fd_get_name(con));

	slurm_mutex_lock(&listeners.mutex);

	xassert(!listeners.cons[i]);
	listeners.cons[i] = con;

	if (!listeners.quiesced &&
	    (rc = conmgr_unquiesce_fd(listeners.cons[i])))
		fatal_abort("%s: conmgr_unquiesce_fd(%s) failed: %s",
			    __func__, conmgr_fd_get_name(con),
			    slurm_strerror(rc));

	slurm_mutex_unlock(&listeners.mutex);

	return arg;
}

static void _on_listen_finish(conmgr_fd_t *con, void *arg)
{
	int *i_ptr = arg;
	const int i = *i_ptr;

	debug3("%s: [%s] Closed RPC listener",
	       __func__, conmgr_fd_get_name(con));

	slurm_mutex_lock(&listeners.mutex);
	xassert(listeners.cons[i] == con);
	listeners.cons[i] = NULL;
	slurm_mutex_unlock(&listeners.mutex);

	xfree(i_ptr);
}

static void *_on_primary_connection(conmgr_fd_t *con, void *arg)
{
	debug3("%s: [%s] PRIMARY: New RPC connection",
	       __func__, conmgr_fd_get_name(con));

	return con;
}

static void _on_primary_finish(conmgr_fd_t *con, void *arg)
{
	debug3("%s: [%s] PRIMARY: RPC connection closed",
	       __func__, conmgr_fd_get_name(con));
}

/*
 * Process incoming primary RPCs.
 *
 * WARNING: conmgr will read all available incoming data and could process
 * multiple RPCs on a single connection but the current RPC model of the
 * controller is to only have 1 incoming RPC and then to reply and close the
 * connection. This is not ideal if the RPC handler should try to read from the
 * extracted fd since conmgr may have already read some data it expects. This
 * currently does not appear to be an issue but may be one in the future until
 * all of the RPC handlers are converted to conmgr fully.
 */
static int _on_primary_msg(conmgr_fd_t *con, slurm_msg_t *msg, void *arg)
{
	int rc = SLURM_SUCCESS;

	if (!msg->auth_ids_set)
		fatal_abort("this should never happen");

	log_flag(AUDIT_RPCS, "[%s] msg_type=%s uid=%u client=[%pA] protocol=%u",
		 conmgr_fd_get_name(con), rpc_num2string(msg->msg_type),
		 msg->auth_uid, &msg->address, msg->protocol_version);

	/*
	 * Check msg against the rate limit. Tell client to retry in a second
	 * to minimize controller disruption.
	 */
	if (rate_limit_exceeded(msg)) {
		rc = slurm_send_rc_msg(msg, SLURMCTLD_COMMUNICATIONS_BACKOFF);
		slurm_free_msg(msg);
	} else if ((rc = conmgr_queue_extract_con_fd(
			    con, _service_connection,
			    XSTRINGIFY(_service_connection), msg))) {
		error("%s: [%s] Extracting FDs failed: %s",
		      __func__, conmgr_fd_get_name(con), slurm_strerror(rc));
	}

	return rc;
}

static void *_on_connection(conmgr_fd_t *con, void *arg)
{
	bool standby_mode;

	slurm_mutex_lock(&listeners.mutex);
	standby_mode = listeners.standby_mode;
	slurm_mutex_unlock(&listeners.mutex);

	if (!standby_mode)
		return _on_primary_connection(con, arg);
	else
		return on_backup_connection(con, arg);
}

static void _on_finish(conmgr_fd_t *con, void *arg)
{
	bool standby_mode;

	slurm_mutex_lock(&listeners.mutex);
	standby_mode = listeners.standby_mode;
	slurm_mutex_unlock(&listeners.mutex);

	if (!standby_mode)
		return _on_primary_finish(con, arg);
	else
		return on_backup_finish(con, arg);
}

static int _on_msg(conmgr_fd_t *con, slurm_msg_t *msg, int unpack_rc, void *arg)
{
	bool standby_mode;

	if ((unpack_rc == SLURM_PROTOCOL_AUTHENTICATION_ERROR) ||
	    !msg->auth_ids_set) {
		/*
		 * Avoid closing connection immediately on authentication
		 * failure to give the sender a hint to fix their authentication
		 * issue with authentication disabled.
		 */
		msg->flags |= SLURM_NO_AUTH_CRED;
		slurm_send_rc_msg(msg, SLURM_PROTOCOL_AUTHENTICATION_ERROR);
		slurm_free_msg(msg);
		return SLURM_SUCCESS;
	} else if (unpack_rc) {
		error("%s: [%s] rejecting malformed RPC and closing connection: %s",
		      __func__, conmgr_fd_get_name(con),
		      slurm_strerror(unpack_rc));
		slurm_free_msg(msg);
		return unpack_rc;
	}

	slurm_mutex_lock(&listeners.mutex);
	standby_mode = listeners.standby_mode;
	slurm_mutex_unlock(&listeners.mutex);

	if (!standby_mode)
		return _on_primary_msg(con, msg, arg);
	else
		return on_backup_msg(con, msg, arg);
}

extern void listeners_quiesce(void)
{
	slurm_mutex_lock(&listeners.mutex);

	if (listeners.quiesced) {
		slurm_mutex_unlock(&listeners.mutex);
		return;
	}

	for (int i = 0; i < listeners.count; i++) {
		int rc;

		if (!listeners.cons[i])
			continue;

		/* This should always work */
		if ((rc = conmgr_quiesce_fd(listeners.cons[i])))
			fatal_abort("%s: conmgr_quiesce_fd(%s) failed: %s",
				    __func__,
				    conmgr_fd_get_name(listeners.cons[i]),
				    slurm_strerror(rc));
	}

	listeners.quiesced = true;

	slurm_mutex_unlock(&listeners.mutex);
}

extern void listeners_unquiesce(void)
{
	slurm_mutex_lock(&listeners.mutex);

	if (!listeners.quiesced) {
		slurm_mutex_unlock(&listeners.mutex);
		return;
	}

	for (int i = 0; i < listeners.count; i++) {
		int rc;

		if (!listeners.cons[i])
			continue;

		/* This should always work */
		if ((rc = conmgr_unquiesce_fd(listeners.cons[i])))
			fatal_abort("%s: conmgr_unquiesce_fd(%s) failed: %s",
				    __func__,
				    conmgr_fd_get_name(listeners.cons[i]),
				    slurm_strerror(rc));
	}

	listeners.quiesced = false;

	slurm_mutex_unlock(&listeners.mutex);
}

/*
 * _open_ports - Open all ports for the slurmctld to listen on.
 */
static void _open_ports(void)
{
	static const conmgr_events_t events = {
		.on_listen_connect = _on_listen_connect,
		.on_listen_finish = _on_listen_finish,
		.on_connection = _on_connection,
		.on_msg = _on_msg,
		.on_finish = _on_finish,
	};

	slurm_mutex_lock(&listeners.mutex);

	/* initialize ports for RPCs */
	if (original) {
		if (!(listeners.count = slurm_conf.slurmctld_port_count))
			fatal("slurmctld port count is zero");
		listeners.fd = xcalloc(listeners.count, sizeof(*listeners.fd));
		listeners.cons = xcalloc(listeners.count,
					 sizeof(*listeners.cons));
		for (int i = 0; i < listeners.count; i++) {
			listeners.fd[i] = slurm_init_msg_engine_port(
				slurm_conf.slurmctld_port + i);
		}
	} else {
		char *pos = getenv("SLURMCTLD_RECONF_LISTEN_FDS");
		listeners.count = atoi(getenv("SLURMCTLD_RECONF_LISTEN_COUNT"));
		listeners.fd = xcalloc(listeners.count, sizeof(*listeners.fd));
		listeners.cons = xcalloc(listeners.count,
					 sizeof(*listeners.cons));
		for (int i = 0; i < listeners.count; i++) {
			listeners.fd[i] = strtol(pos, &pos, 10);
			pos++; /* skip comma */
		}
	}

	for (uint64_t i = 0; i < listeners.count; i++) {
		static const conmgr_con_flags_t flags =
			(CON_FLAG_RPC_KEEP_BUFFER | CON_FLAG_QUIESCE);
		int rc, *index_ptr;

		index_ptr = xmalloc(sizeof(*index_ptr));
		*index_ptr = i;

		if ((rc = conmgr_process_fd_listen(listeners.fd[i],
						   CON_TYPE_RPC, &events, flags,
						   index_ptr))) {
			if (rc == SLURM_COMMUNICATIONS_INVALID_FD)
				fatal("%s: Unable to listen to file descriptors. Existing slurmctld process likely already is listening on the ports.",
				      __func__);

			fatal("%s: unable to process fd:%d error:%s",
			      __func__, listeners.fd[i], slurm_strerror(rc));
		}
	}

	slurm_mutex_unlock(&listeners.mutex);
}

/*
 * _service_connection - service the RPC
 * IN/OUT arg - really just the connection's file descriptor, freed
 *	upon completion
 * RET - NULL
 */
static void _service_connection(conmgr_callback_args_t conmgr_args,
				int input_fd, int output_fd, void *arg)
{
	int rc;
	slurm_msg_t *msg = arg;
	slurmctld_rpc_t *this_rpc = NULL;

	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED) {
		debug3("%s: [fd:%d] connection work cancelled",
		       __func__, input_fd);

		if (input_fd != output_fd)
			fd_close(&output_fd);
		fd_close(&input_fd);
		slurm_free_msg(msg);
		return;
	}

	if ((input_fd < 0) || (output_fd < 0)) {
		error("%s: Rejecting partially open connection input_fd=%d output_fd=%d",
		      __func__, input_fd, output_fd);
		if (input_fd != output_fd)
			fd_close(&output_fd);
		fd_close(&input_fd);
		slurm_free_msg(msg);
		return;
	}

	/*
	 * The fd was extracted from conmgr, so the conmgr connection is
	 * invalid.
	 */
	msg->conmgr_fd = NULL;
	msg->conn_fd = input_fd;

	server_thread_incr();

	if (!(rc = rpc_enqueue(msg))) {
		server_thread_decr();
		return;
	}

	if (rc == SLURMCTLD_COMMUNICATIONS_BACKOFF) {
		slurm_send_rc_msg(msg, SLURMCTLD_COMMUNICATIONS_BACKOFF);
	} else if (rc == SLURMCTLD_COMMUNICATIONS_HARD_DROP) {
		slurm_send_rc_msg(msg, SLURMCTLD_COMMUNICATIONS_HARD_DROP);
	} else if ((this_rpc = find_rpc(msg->msg_type))) {
		/* directly process the request */
		slurmctld_req(msg, this_rpc);
	} else {
		error("invalid RPC msg_type=%s", rpc_num2string(msg->msg_type));
		slurm_send_rc_msg(msg, EINVAL);
	}

	if (!this_rpc || !this_rpc->keep_msg) {
		if ((msg->conn_fd >= 0) && (close(msg->conn_fd) < 0))
			error("close(%d): %m", msg->conn_fd);
		slurm_free_msg(msg);
	}

	server_thread_decr();
}

/* Decrement slurmctld thread count (as applies to thread limit) */
extern void server_thread_decr(void)
{
	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	if (slurmctld_config.server_thread_count > 0)
		slurmctld_config.server_thread_count--;
	else
		error("slurmctld_config.server_thread_count underflow");
	slurm_cond_broadcast(&slurmctld_config.thread_count_cond);
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
}

/* Increment slurmctld thread count (as applies to thread limit) */
extern void server_thread_incr(void)
{
	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	slurmctld_config.server_thread_count++;
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
}

static int _accounting_cluster_ready(void)
{
	return clusteracct_storage_g_cluster_tres(acct_db_conn,
						  NULL,
						  NULL,
						  0,
						  SLURM_PROTOCOL_VERSION);
}

static int _accounting_mark_all_nodes_down(char *reason)
{
	char *state_file;
	struct stat stat_buf;
	node_record_t *node_ptr;
	int i;
	time_t event_time;
	int rc = SLURM_ERROR;

	state_file = xstrdup_printf("%s/node_state",
	                            slurm_conf.state_save_location);
	if (stat(state_file, &stat_buf)) {
		debug("_accounting_mark_all_nodes_down: could not stat(%s) "
		      "to record node down time", state_file);
		event_time = time(NULL);
	} else {
		event_time = stat_buf.st_mtime;
	}
	xfree(state_file);

	if ((rc = acct_storage_g_flush_jobs_on_cluster(acct_db_conn,
						      event_time))
	   == SLURM_ERROR)
		return rc;

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (!node_ptr->name)
			continue;
		if ((rc = clusteracct_storage_g_node_down(
			acct_db_conn, node_ptr, event_time,
			reason, slurm_conf.slurm_user_id))
		   == SLURM_ERROR)
			break;
	}
	return rc;
}

static void _remove_assoc(slurmdb_assoc_rec_t *rec)
{
	int cnt = 0;

	bb_g_reconfig();

	cnt = job_hold_by_assoc_id(rec->id);

	if (cnt) {
		info("Removed association id:%u user:%s, held %u jobs",
		     rec->id, rec->user, cnt);
	} else
		debug("Removed association id:%u user:%s", rec->id, rec->user);
}

static void _remove_qos(slurmdb_qos_rec_t *rec)
{
	int cnt = 0;
	list_itr_t *itr;
	part_record_t *part_ptr;
	slurmctld_lock_t part_write_lock =
		{ NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };

	lock_slurmctld(part_write_lock);
	if (part_list) {
		itr = list_iterator_create(part_list);
		while ((part_ptr = list_next(itr))) {
			if (part_ptr->qos_ptr != rec)
				continue;
			info("Partition %s's QOS %s was just removed, "
			     "you probably didn't mean for this to happen "
			     "unless you are also removing the partition.",
			     part_ptr->name, rec->name);
			part_ptr->qos_ptr = NULL;
		}
		list_iterator_destroy(itr);
	}
	unlock_slurmctld(part_write_lock);

	bb_g_reconfig();

	cnt = job_hold_by_qos_id(rec->id);

	if (cnt) {
		info("Removed QOS:%s held %u jobs", rec->name, cnt);
	} else
		debug("Removed QOS:%s", rec->name);
}

static int _update_assoc_for_each(void *x, void *arg) {
	slurmdb_assoc_rec_t *rec = arg;
	job_record_t *job_ptr = x;

	if ((rec == job_ptr->assoc_ptr) && (IS_JOB_PENDING(job_ptr)))
		acct_policy_update_pending_job(job_ptr);

	return 0;
}

static void _update_assoc(slurmdb_assoc_rec_t *rec)
{
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list || !accounting_enforce
	    || !(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return;

	lock_slurmctld(job_write_lock);
	list_for_each(job_list, _update_assoc_for_each, rec);
	unlock_slurmctld(job_write_lock);
}

static void _resize_qos(void)
{
	list_itr_t *itr;
	part_record_t *part_ptr;
	slurmctld_lock_t part_write_lock =
		{ NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };

	lock_slurmctld(part_write_lock);
	if (part_list) {
		itr = list_iterator_create(part_list);
		while ((part_ptr = list_next(itr))) {
			if (part_ptr->allow_qos) {
				info("got count for %s of %"BITSTR_FMT, part_ptr->name,
				     bit_size(part_ptr->allow_qos_bitstr));
				qos_list_build(part_ptr->allow_qos,
					       &part_ptr->allow_qos_bitstr);
				info("now count for %s of %"BITSTR_FMT, part_ptr->name,
				     bit_size(part_ptr->allow_qos_bitstr));
			}
			if (part_ptr->deny_qos)
				qos_list_build(part_ptr->deny_qos,
					       &part_ptr->deny_qos_bitstr);
		}
		list_iterator_destroy(itr);
	}
	unlock_slurmctld(part_write_lock);
}

static int _update_qos_for_each(void *x, void *arg) {
	slurmdb_qos_rec_t *rec = arg;
	job_record_t *job_ptr = x;

	if ((rec == job_ptr->qos_ptr) && (IS_JOB_PENDING(job_ptr)))
		acct_policy_update_pending_job(job_ptr);

	return 0;
}

static void _update_qos(slurmdb_qos_rec_t *rec)
{
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list || !accounting_enforce
	    || !(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return;

	lock_slurmctld(job_write_lock);
	list_for_each(job_list, _update_qos_for_each, rec);
	unlock_slurmctld(job_write_lock);
}

static int _init_tres(void)
{
	char *temp_char;
	list_t *char_list = NULL;
	list_t *add_list = NULL;
	slurmdb_tres_rec_t *tres_rec;
	slurmdb_update_object_t update_object;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (!slurm_conf.accounting_storage_tres) {
		error("No tres defined, this should never happen");
		return SLURM_ERROR;
	}

	char_list = list_create(xfree_ptr);
	slurm_addto_char_list(char_list, slurm_conf.accounting_storage_tres);

	memset(&update_object, 0, sizeof(slurmdb_update_object_t));
	if (!slurm_with_slurmdbd()) {
		update_object.type = SLURMDB_ADD_TRES;
		update_object.objects = list_create(slurmdb_destroy_tres_rec);
	} else if (!g_tres_count)
		fatal("You are running with a database but for some reason "
		      "we have no TRES from it.  This should only happen if "
		      "the database is down and you don't have "
		      "any state files.");
	else if ((g_tres_count < TRES_ARRAY_TOTAL_CNT) ||
		 (xstrcmp(assoc_mgr_tres_array[TRES_ARRAY_BILLING]->type,
			  "billing")))
		fatal("You are running with a database but for some reason we have less TRES than should be here (%d < %d) and/or the \"billing\" TRES is missing. This should only happen if the database is down after an upgrade.",
		      g_tres_count, TRES_ARRAY_TOTAL_CNT);

	while ((temp_char = list_pop(char_list))) {
		tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));

		tres_rec->type = temp_char;

		if (!xstrcasecmp(temp_char, "cpu"))
			tres_rec->id = TRES_CPU;
		else if (!xstrcasecmp(temp_char, "mem"))
			tres_rec->id = TRES_MEM;
		else if (!xstrcasecmp(temp_char, "energy"))
			tres_rec->id = TRES_ENERGY;
		else if (!xstrcasecmp(temp_char, "node"))
			tres_rec->id = TRES_NODE;
		else if (!xstrcasecmp(temp_char, "billing"))
			tres_rec->id = TRES_BILLING;
		else if (!xstrcasecmp(temp_char, "vmem"))
			tres_rec->id = TRES_VMEM;
		else if (!xstrcasecmp(temp_char, "pages"))
			tres_rec->id = TRES_PAGES;
		else if (!xstrncasecmp(temp_char, "bb/", 3)) {
			tres_rec->type[2] = '\0';
			tres_rec->name = xstrdup(temp_char+3);
			if (!tres_rec->name)
				fatal("Burst Buffer type tres need to have a "
				      "name, (i.e. bb/datawarp).  You gave %s",
				      temp_char);
		} else if (!xstrncasecmp(temp_char, "gres/", 5)) {
			tres_rec->type[4] = '\0';
			tres_rec->name = xstrdup(temp_char+5);
			if (!tres_rec->name)
				fatal("Gres type tres need to have a name, "
				      "(i.e. Gres/GPU).  You gave %s",
				      temp_char);
		} else if (!xstrncasecmp(temp_char, "license/", 8)) {
			tres_rec->type[7] = '\0';
			tres_rec->name = xstrdup(temp_char+8);
			if (!tres_rec->name)
				fatal("License type tres need to "
				      "have a name, (i.e. License/Foo).  "
				      "You gave %s",
				      temp_char);
		} else if (!xstrncasecmp(temp_char, "fs/", 3)) {
			tres_rec->type[2] = '\0';
			tres_rec->name = xstrdup(temp_char+3);
			if (!tres_rec->name)
				fatal("Filesystem type tres need to have a name, (i.e. fs/disk).  You gave %s",
				      temp_char);
			if (!xstrncasecmp(tres_rec->name, "disk", 4))
				tres_rec->id = TRES_FS_DISK;
		} else if (!xstrncasecmp(temp_char, "ic/", 3)) {
			tres_rec->type[2] = '\0';
			tres_rec->name = xstrdup(temp_char+3);
			if (!tres_rec->name)
				fatal("Interconnect type tres need to have a name, (i.e. ic/ofed).  You gave %s",
				      temp_char);
		} else {
			fatal("%s: Unknown tres type '%s', acceptable types are Billing,CPU,Energy,FS/,Gres/,IC/,License/,Mem,Node,Pages,VMem",
			      __func__, temp_char);
			xfree(tres_rec->type);
			xfree(tres_rec);
		}

		if (!slurm_with_slurmdbd()) {
			if (!tres_rec->id)
				fatal("slurmdbd is required to run with TRES %s%s%s. Either setup slurmdbd or remove this TRES from your configuration.",
				      tres_rec->type, tres_rec->name ? "/" : "",
				      tres_rec->name ? tres_rec->name : "");
			list_append(update_object.objects, tres_rec);
		} else if (!tres_rec->id &&
			   assoc_mgr_fill_in_tres(
				   acct_db_conn, tres_rec,
				   ACCOUNTING_ENFORCE_TRES, NULL, 0)
			   != SLURM_SUCCESS) {
			if (!add_list)
				add_list = list_create(
					slurmdb_destroy_tres_rec);
			info("Couldn't find tres %s%s%s in the database, "
			     "creating.",
			     tres_rec->type, tres_rec->name ? "/" : "",
			     tres_rec->name ? tres_rec->name : "");
			list_append(add_list, tres_rec);
		} else
			slurmdb_destroy_tres_rec(tres_rec);
	}
	FREE_NULL_LIST(char_list);

	if (add_list) {
		if (acct_storage_g_add_tres(acct_db_conn,
		                            slurm_conf.slurm_user_id,
		                            add_list) != SLURM_SUCCESS)
			fatal("Problem adding tres to the database, "
			      "can't continue until database is able to "
			      "make new tres");
		/* refresh list here since the updates are not
		   sent dynamically */
		assoc_mgr_refresh_lists(acct_db_conn, ASSOC_MGR_CACHE_TRES);
		FREE_NULL_LIST(add_list);
	}

	if (!slurm_with_slurmdbd()) {
		assoc_mgr_update_tres(&update_object, false);
		FREE_NULL_LIST(update_object.objects);
	}

	/* Set up the slurmctld_tres_cnt here (Current code is set to
	 * not have this ever change).
	*/
	assoc_mgr_lock(&locks);
	slurmctld_tres_cnt = g_tres_count;
	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/*
 * NOTE: the job_write_lock as well as the assoc_mgr TRES Read lock should be
 * locked before coming in here.
 */
static int _update_job_tres(void *x, void *args)
{
	job_record_t *job_ptr = x;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	/* If this returns 1 it means the positions were
	   altered so just rebuild it.
	*/
	if (assoc_mgr_set_tres_cnt_array(&job_ptr->tres_req_cnt,
					 job_ptr->tres_req_str,
					 0, true, false, NULL))
		job_set_req_tres(job_ptr, true);
	if (assoc_mgr_set_tres_cnt_array(&job_ptr->tres_alloc_cnt,
					 job_ptr->tres_alloc_str,
					 0, true, false, NULL))
		job_set_alloc_tres(job_ptr, true);

	update_job_limit_set_tres(&job_ptr->limit_set.tres, slurmctld_tres_cnt);

	return 0;
}

/* any association manager locks should be unlocked before hand */
static void _update_cluster_tres(void)
{
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (!job_list)
		return;

	lock_slurmctld(job_write_lock);
	assoc_mgr_lock(&locks);
	list_for_each(job_list, _update_job_tres, NULL);
	assoc_mgr_unlock(&locks);
	unlock_slurmctld(job_write_lock);
}

static void _update_parts_and_resvs()
{
	update_assocs_in_resvs();
	part_list_update_assoc_lists();
}

static void _queue_reboot_msg(void)
{
	agent_arg_t *reboot_agent_args = NULL;
	node_record_t *node_ptr;
	char *host_str;
	time_t now = time(NULL);
	int i;
	bool want_reboot;

	want_nodes_reboot = false;
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		/* Allow nodes in maintenance reservations to reboot
		 * (they previously could not).
		 */
		if (!IS_NODE_REBOOT_REQUESTED(node_ptr))
			continue;	/* No reboot needed */
		else if (IS_NODE_REBOOT_ISSUED(node_ptr)) {
			debug2("%s: Still waiting for boot of node %s",
			       __func__, node_ptr->name);
			continue;
		}
		if (IS_NODE_COMPLETING(node_ptr)) {
			want_nodes_reboot = true;
			continue;
		}
                /* only active idle nodes, don't reboot
                 * nodes that are idle but have suspended
                 * jobs on them
                 */
		if (IS_NODE_IDLE(node_ptr)
                    && !IS_NODE_NO_RESPOND(node_ptr)
                    && !IS_NODE_POWERING_UP(node_ptr)
                    && node_ptr->sus_job_cnt == 0)
			want_reboot = true;
		else if (IS_NODE_FUTURE(node_ptr) &&
			 (node_ptr->last_response == (time_t) 0))
			want_reboot = true; /* system just restarted */
		else if (IS_NODE_DOWN(node_ptr))
			want_reboot = true;
		else
			want_reboot = false;
		if (!want_reboot) {
			want_nodes_reboot = true;	/* defer reboot */
			continue;
		}
		if (reboot_agent_args == NULL) {
			reboot_agent_args = xmalloc(sizeof(agent_arg_t));
			reboot_agent_args->msg_type = REQUEST_REBOOT_NODES;
			reboot_agent_args->retry = 0;
			reboot_agent_args->hostlist = hostlist_create(NULL);
			reboot_agent_args->protocol_version =
				SLURM_PROTOCOL_VERSION;
		}
		if (reboot_agent_args->protocol_version
		    > node_ptr->protocol_version)
			reboot_agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(reboot_agent_args->hostlist, node_ptr->name);
		reboot_agent_args->node_count++;
		/*
		 * node_ptr->node_state &= ~NODE_STATE_MAINT;
		 * The NODE_STATE_MAINT bit will just get set again as long
		 * as the node remains in the maintenance reservation, so
		 * don't clear it here because it won't do anything.
		 */
		node_ptr->node_state &=  NODE_STATE_FLAGS;
		node_ptr->node_state |=  NODE_STATE_DOWN;
		node_ptr->node_state &= ~NODE_STATE_REBOOT_REQUESTED;
		node_ptr->node_state |= NODE_STATE_REBOOT_ISSUED;

		bit_clear(avail_node_bitmap, node_ptr->index);
		bit_clear(idle_node_bitmap, node_ptr->index);

		/* Unset this as this node is not in reboot ASAP anymore. */
		bit_clear(asap_node_bitmap, node_ptr->index);

		node_ptr->boot_req_time = now;

		set_node_reason(node_ptr, "reboot issued", now);

		clusteracct_storage_g_node_down(acct_db_conn, node_ptr, now,
		                                NULL, slurm_conf.slurm_user_id);
	}
	if (reboot_agent_args != NULL) {
		hostlist_uniq(reboot_agent_args->hostlist);
		host_str = hostlist_ranged_string_xmalloc(
				reboot_agent_args->hostlist);
		debug("Issuing reboot request for nodes %s", host_str);
		xfree(host_str);
		set_agent_arg_r_uid(reboot_agent_args, SLURM_AUTH_UID_ANY);
		agent_queue_request(reboot_agent_args);
		last_node_update = now;
		schedule_node_save();
	}
}

static void _flush_rpcs(void)
{
	struct timespec ts = {0, 0};
	struct timeval now;
	int exp_thread_cnt = slurmctld_config.resume_backup ? 1 : 0;

	/* wait for RPCs to complete */
	gettimeofday(&now, NULL);
	ts.tv_sec = now.tv_sec + CONTROL_TIMEOUT;
	ts.tv_nsec = now.tv_usec * 1000;

	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	while (slurmctld_config.server_thread_count > exp_thread_cnt) {
		slurm_cond_timedwait(&slurmctld_config.thread_count_cond,
				     &slurmctld_config.thread_count_lock, &ts);
	}

	if (slurmctld_config.server_thread_count > exp_thread_cnt) {
		info("shutdown server_thread_count=%d",
		     slurmctld_config.server_thread_count);
	}

	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
}

/*
 * _slurmctld_background - process slurmctld background activities
 *	purge defunct job records, save state, schedule jobs, and
 *	ping other nodes
 */
static void *_slurmctld_background(void *no_data)
{
	static time_t last_sched_time;
	static time_t last_config_list_update_time;
	static time_t last_full_sched_time;
	static time_t last_checkpoint_time;
	static time_t last_group_time;
	static time_t last_health_check_time;
	static time_t last_acct_gather_node_time;
	static time_t last_no_resp_msg_time;
	static time_t last_ping_node_time = (time_t) 0;
	static time_t last_ping_srun_time;
	static time_t last_purge_job_time;
	static time_t last_resv_time;
	static time_t last_timelimit_time;
	static time_t last_assert_primary_time;
	static time_t last_trigger;
	static time_t last_node_acct;
	static time_t last_ctld_bu_ping;
	static time_t last_uid_update;
	time_t now;
	int no_resp_msg_interval, ping_interval, purge_job_interval;
	DEF_TIMERS;

	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Read config, read job */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock2 = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Read config, write job, write node
	 * (Might kill jobs on nodes set DOWN) */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Write node */
	slurmctld_lock_t node_write_lock2 = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Write partition */
	slurmctld_lock_t part_write_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };
	/* Locks: Read job and node */
	slurmctld_lock_t job_node_read_lock = {
		NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	/*
	 * purge_old_job modifes jobs and reads conf info. It can also
	 * call re_kill_job(), which can modify nodes and reads fed info.
	 */
	slurmctld_lock_t purge_job_locks = {
		.conf = READ_LOCK, .job = WRITE_LOCK,
		.node = WRITE_LOCK, .fed = READ_LOCK
	};

	/* Let the dust settle before doing work */
	now = time(NULL);
	last_sched_time = last_full_sched_time = now;
	last_checkpoint_time = last_group_time = now;
	last_purge_job_time = last_trigger = last_health_check_time = now;
	last_timelimit_time = last_assert_primary_time = now;
	last_no_resp_msg_time = last_resv_time = last_ctld_bu_ping = now;
	last_uid_update = now;
	last_acct_gather_node_time = now;
	last_config_list_update_time = now;


	last_ping_srun_time = now;
	last_node_acct = now;
	debug3("_slurmctld_background pid = %u", getpid());

	while (1) {
		bool call_schedule = false, full_queue = false;

		slurm_mutex_lock(&shutdown_mutex);
		if (!slurmctld_config.shutdown_time) {
			struct timespec ts = {0, 0};

			/* Listen to new incoming RPCs if not shutting down */
			listeners_unquiesce();

			ts.tv_sec = time(NULL) + 1;
			slurm_cond_timedwait(&shutdown_cond, &shutdown_mutex,
					     &ts);
		}
		slurm_mutex_unlock(&shutdown_mutex);

		now = time(NULL);
		START_TIMER;

		if (slurm_conf.slurmctld_debug <= 3)
			no_resp_msg_interval = 300;
		else if (slurm_conf.slurmctld_debug == 4)
			no_resp_msg_interval = 60;
		else
			no_resp_msg_interval = 1;

		if ((slurm_conf.min_job_age > 0) &&
		    (slurm_conf.min_job_age < PURGE_JOB_INTERVAL)) {
			/* Purge jobs more quickly, especially for high job flow */
			purge_job_interval = MAX(10, slurm_conf.min_job_age);
		} else
			purge_job_interval = PURGE_JOB_INTERVAL;

		if (slurm_conf.slurmd_timeout) {
			/* We ping nodes that haven't responded in SlurmdTimeout/3,
			 * but need to do the test at a higher frequency or we might
			 * DOWN nodes with times that fall in the gap. */
			ping_interval = slurm_conf.slurmd_timeout / 3;
		} else {
			/* This will just ping non-responding nodes
			 * and restore them to service */
			ping_interval = 100;	/* 100 seconds */
		}

		if (!last_ping_node_time) {
			last_ping_node_time = now + (time_t)MIN_CHECKIN_TIME -
					      ping_interval;
		}

		if (slurmctld_config.shutdown_time) {
			/* Always stop listening when shutdown requested */
			listeners_quiesce();

			_flush_rpcs();

			/*
			 * Wait for all already accepted connection work to
			 * finish before continuing on with control loop that
			 * will unload all the plugins which requires there be
			 * no active RPCs.
			 */
			conmgr_quiesce(__func__);

			if (!report_locks_set()) {
				info("Saving all slurm state");
				save_all_state();
			} else {
				error("Semaphores still set after %d seconds, "
				      "can not save state", CONTROL_TIMEOUT);
			}

			/*
			 * Allow other connections to start processing again as
			 * the listeners are already quiesced
			 */
			conmgr_unquiesce(__func__);

			break;
		}

		if (difftime(now, last_resv_time) >= 5) {
			lock_slurmctld(node_write_lock);
			now = time(NULL);
			last_resv_time = now;
			if (set_node_maint_mode(false) > 0)
				queue_job_scheduler();
			unlock_slurmctld(node_write_lock);
		}

		if (difftime(now, last_no_resp_msg_time) >=
		    no_resp_msg_interval) {
			lock_slurmctld(node_write_lock2);
			now = time(NULL);
			last_no_resp_msg_time = now;
			node_no_resp_msg();
			unlock_slurmctld(node_write_lock2);
		}

		validate_all_reservations(true);

		if (difftime(now, last_timelimit_time) >= PERIODIC_TIMEOUT) {
			lock_slurmctld(job_write_lock);
			now = time(NULL);
			last_timelimit_time = now;
			debug2("Testing job time limits and checkpoints");
			job_time_limit();
			job_resv_check();
			unlock_slurmctld(job_write_lock);

			lock_slurmctld(node_write_lock);
			check_node_timers();
			unlock_slurmctld(node_write_lock);
		}

		if (slurm_conf.health_check_interval &&
		    (difftime(now, last_health_check_time) >=
		     slurm_conf.health_check_interval) &&
		    is_ping_done()) {
			lock_slurmctld(node_write_lock);
			if (slurm_conf.health_check_node_state &
			     HEALTH_CHECK_CYCLE) {
				/* Call run_health_check() on each cycle */
			} else {
				now = time(NULL);
				last_health_check_time = now;
			}
			run_health_check();
			unlock_slurmctld(node_write_lock);
		}

		if (slurm_conf.acct_gather_node_freq &&
		    (difftime(now, last_acct_gather_node_time) >=
		     slurm_conf.acct_gather_node_freq) &&
		    is_ping_done()) {
			lock_slurmctld(node_write_lock);
			now = time(NULL);
			last_acct_gather_node_time = now;
			update_nodes_acct_gather_data();
			unlock_slurmctld(node_write_lock);
		}

		if (((difftime(now, last_ping_node_time) >= ping_interval) ||
		     ping_nodes_now) && is_ping_done()) {
			lock_slurmctld(node_write_lock);
			now = time(NULL);
			last_ping_node_time = now;
			ping_nodes_now = false;
			ping_nodes();
			unlock_slurmctld(node_write_lock);
		}

		if (slurm_conf.inactive_limit &&
		    ((now - last_ping_srun_time) >=
		     (slurm_conf.inactive_limit / 3))) {
			lock_slurmctld(job_read_lock);
			now = time(NULL);
			last_ping_srun_time = now;
			debug2("Performing srun ping");
			srun_ping();
			unlock_slurmctld(job_read_lock);
		}

		if (want_nodes_reboot) {
			lock_slurmctld(node_write_lock);
			_queue_reboot_msg();
			unlock_slurmctld(node_write_lock);
		}

		/* Process any pending agent work */
		agent_trigger(RPC_RETRY_INTERVAL, true, true);

		if (slurm_conf.group_time &&
		    (difftime(now, last_group_time)
		     >= slurm_conf.group_time)) {
			lock_slurmctld(part_write_lock);
			now = time(NULL);
			last_group_time = now;
			load_part_uid_allow_list(slurm_conf.group_force);
			reservation_update_groups(slurm_conf.group_force);
			unlock_slurmctld(part_write_lock);
			group_cache_cleanup();
		}

		if (difftime(now, last_purge_job_time) >= purge_job_interval) {
			/*
			 * If backfill is running, it will have a list of
			 * job_record pointers which could include this
			 * job. Skip over in that case to prevent
			 * _attempt_backfill() from potentially dereferencing an
			 * invalid pointer.
			 */
			slurm_mutex_lock(&check_bf_running_lock);
			if (!slurmctld_diag_stats.bf_active) {
				lock_slurmctld(purge_job_locks);
				now = time(NULL);
				last_purge_job_time = now;
				debug2("Performing purge of old job records");
				purge_old_job();
				unlock_slurmctld(purge_job_locks);
			}
			slurm_mutex_unlock(&check_bf_running_lock);
			free_old_jobs();
		}

		if (difftime(now, last_full_sched_time) >= sched_interval) {
			slurm_mutex_lock(&sched_cnt_mutex);
			call_schedule = true;
			full_queue = true;
			job_sched_cnt = 0;
			slurm_mutex_unlock(&sched_cnt_mutex);
			last_full_sched_time = now;
		} else {
			slurm_mutex_lock(&sched_cnt_mutex);
			if (job_sched_cnt &&
			    (difftime(now, last_sched_time) >=
			     batch_sched_delay)) {
				call_schedule = true;
				job_sched_cnt = 0;
			}
			slurm_mutex_unlock(&sched_cnt_mutex);
		}
		if (call_schedule) {
			lock_slurmctld(job_write_lock2);
			now = time(NULL);
			last_sched_time = now;
			bb_g_load_state(false);	/* May alter job nice/prio */
			unlock_slurmctld(job_write_lock2);
			schedule(full_queue);
			set_job_elig_time();
		}

		if (difftime(now, last_config_list_update_time) >=
		    UPDATE_CONFIG_LIST_TIMEOUT) {
			last_config_list_update_time = now;
			consolidate_config_list(false, false);
		}

		if (slurm_conf.slurmctld_timeout &&
		    (difftime(now, last_ctld_bu_ping) >
		     slurm_conf.slurmctld_timeout)) {
			ping_controllers(true);
			last_ctld_bu_ping = now;
		}

		if (difftime(now, last_trigger) > TRIGGER_INTERVAL) {
			lock_slurmctld(job_node_read_lock);
			now = time(NULL);
			last_trigger = now;
			trigger_process();
			unlock_slurmctld(job_node_read_lock);
		}

		if (difftime(now, last_checkpoint_time) >=
		    PERIODIC_CHECKPOINT) {
			now = time(NULL);
			last_checkpoint_time = now;
			debug2("Performing full system state save");
			save_all_state();
		}

		if (difftime(now, last_node_acct) >= PERIODIC_NODE_ACCT) {
			/* Report current node state to account for added
			 * or reconfigured nodes.  Locks are done
			 * inside _accounting_cluster_ready, don't
			 * lock here. */
			now = time(NULL);
			last_node_acct = now;
			_accounting_cluster_ready();
		}

		if (difftime(now, slurmctld_diag_stats.job_states_ts) >=
		    JOB_COUNT_INTERVAL) {
			lock_slurmctld(job_read_lock);
			_update_diag_job_state_counts();
			unlock_slurmctld(job_read_lock);
		}

		/* Stats will reset at midnight (approx) local time. */
		if (last_proc_req_start == 0) {
			last_proc_req_start = now;
			next_stats_reset = now - (now % 86400) + 86400;
		} else if (now >= next_stats_reset) {
			next_stats_reset = now - (now % 86400) + 86400;
			reset_stats(0);
		}

		/*
		 * Reassert this machine as the primary controller.
		 * A network or security problem could result in
		 * the backup controller assuming control even
		 * while the real primary controller is running.
		 */
		lock_slurmctld(config_read_lock);
		if (slurmctld_primary && slurm_conf.slurmctld_timeout &&
		    (difftime(now, last_assert_primary_time) >=
		     slurm_conf.slurmctld_timeout)) {
			now = time(NULL);
			last_assert_primary_time = now;
			(void) _shutdown_backup_controller();
		}
		unlock_slurmctld(config_read_lock);

		if (difftime(now, last_uid_update) >= 3600) {
			/*
			 * Make sure we update the uids in the
			 * assoc_mgr if there were any users
			 * with unknown uids at the time of startup.
			 */
			now = time(NULL);
			last_uid_update = now;
			assoc_mgr_set_missing_uids();
		}

		END_TIMER2(__func__);
	}

	debug3("_slurmctld_background shutting down");

	return NULL;
}

/* save_all_state - save entire slurmctld state for later recovery */
extern void save_all_state(void)
{
	/* Each of these functions lock their own databases */
	schedule_front_end_save();
	schedule_job_save();
	schedule_node_save();
	schedule_part_save();
	schedule_resv_save();
	schedule_trigger_save();

	select_g_state_save(slurm_conf.state_save_location);
	dump_assoc_mgr_state();
	fed_mgr_state_save();
}

/* make sure the assoc_mgr is up and running with the most current state */
extern void ctld_assoc_mgr_init(void)
{
	assoc_init_args_t assoc_init_arg;
	int num_jobs = 0;
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	memset(&assoc_init_arg, 0, sizeof(assoc_init_args_t));
	assoc_init_arg.enforce = accounting_enforce;
	assoc_init_arg.running_cache = &running_cache;
	assoc_init_arg.add_license_notify = license_add_remote;
	assoc_init_arg.resize_qos_notify = _resize_qos;
	assoc_init_arg.remove_assoc_notify = _remove_assoc;
	assoc_init_arg.remove_license_notify = license_remove_remote;
	assoc_init_arg.remove_qos_notify = _remove_qos;
	assoc_init_arg.sync_license_notify = license_sync_remote;
	assoc_init_arg.update_assoc_notify = _update_assoc;
	assoc_init_arg.update_license_notify = license_update_remote;
	assoc_init_arg.update_qos_notify = _update_qos;
	assoc_init_arg.update_cluster_tres = _update_cluster_tres;
	assoc_init_arg.update_resvs = _update_parts_and_resvs;
	assoc_init_arg.cache_level = ASSOC_MGR_CACHE_ASSOC |
				     ASSOC_MGR_CACHE_USER  |
				     ASSOC_MGR_CACHE_QOS   |
				     ASSOC_MGR_CACHE_RES   |
				     ASSOC_MGR_CACHE_TRES  |
				     ASSOC_MGR_CACHE_WCKEY;
	/* Don't save state but blow away old lists if they exist. */
	assoc_mgr_fini(0);

	_init_db_conn();

	if (assoc_mgr_init(acct_db_conn, &assoc_init_arg, errno)) {
		trigger_primary_dbd_fail();

		error("Association database appears down, reading from state files.");

		if (!slurm_conf.cluster_id ||
		    (load_assoc_mgr_last_tres() != SLURM_SUCCESS) ||
		    (load_assoc_mgr_state() != SLURM_SUCCESS)) {
			error("Unable to get any information from the state file");
			_retry_init_db_conn(&assoc_init_arg);
		}
	}

	if (!slurm_conf.cluster_id) {
		slurm_conf.cluster_id = generate_cluster_id();
		_create_clustername_file();
	}
	sluid_init(slurm_conf.cluster_id, 0);

	/* Now load the usage from a flat file since it isn't kept in
	   the database
	*/
	load_assoc_usage();
	load_qos_usage();

	lock_slurmctld(job_read_lock);
	if (job_list)
		num_jobs = list_count(job_list);
	unlock_slurmctld(job_read_lock);

	_init_tres();

	/* This thread is looking for when we get correct data from
	   the database so we can update the assoc_ptr's in the jobs
	*/
	if ((running_cache != RUNNING_CACHE_STATE_NOTRUNNING) || num_jobs) {
		slurm_thread_create(&assoc_cache_thread,
				    _assoc_cache_mgr, NULL);
	}

}

/* Make sure the assoc_mgr thread is terminated */
extern void ctld_assoc_mgr_fini(void)
{
	if (running_cache == RUNNING_CACHE_STATE_NOTRUNNING)
		return;

	/* break out and end the association cache
	 * thread since we are shutting down, no reason
	 * to wait for current info from the database */
	slurm_mutex_lock(&assoc_cache_mutex);
	running_cache = RUNNING_CACHE_STATE_EXITING;
	slurm_cond_signal(&assoc_cache_cond);
	slurm_mutex_unlock(&assoc_cache_mutex);
	slurm_thread_join(assoc_cache_thread);
}

static int _add_node_gres_tres(void *x, void *arg)
{
	uint64_t gres_cnt;
	int tres_pos;
	slurmdb_tres_rec_t *tres_rec_in = x;
	node_record_t *node_ptr = arg;

	xassert(tres_rec_in);

	if (xstrcmp(tres_rec_in->type, "gres"))
		return 0;

	gres_cnt = gres_node_config_cnt(node_ptr->gres_list, tres_rec_in->name);

	/*
	 * Set the count here for named GRES as we don't store the count the
	 * same way we do for unnamed GRES.
	 */
	if (strchr(tres_rec_in->name, ':'))
		tres_rec_in->count += gres_cnt;

	if ((tres_pos = assoc_mgr_find_tres_pos(tres_rec_in, true)) != -1)
		node_ptr->tres_cnt[tres_pos] = gres_cnt;

	return 0;
}

/*
 * Set the node's billing tres to the highest billing of all partitions that the
 * node is a part of.
 */
static void _set_node_billing_tres(node_record_t *node_ptr, uint64_t cpu_count,
				   bool assoc_mgr_locked)
{
	int i;
	part_record_t *part_ptr = NULL;
	double max_billing = 0;
	xassert(node_ptr);

	for (i = 0; i < node_ptr->part_cnt; i++) {
		double tmp_billing;
		part_ptr = node_ptr->part_pptr[i];
		if (!part_ptr->billing_weights)
			continue;

		tmp_billing = assoc_mgr_tres_weighted(
			node_ptr->tres_cnt, part_ptr->billing_weights,
			slurm_conf.priority_flags, assoc_mgr_locked);
		max_billing = MAX(max_billing, tmp_billing);
	}

	/* Set to the configured cpu_count if no partition has
	 * tresbillingweights set because the job will be allocated the job's
	 * cpu count if there are no tresbillingweights defined. */
	if (!max_billing)
		max_billing = cpu_count;
	node_ptr->tres_cnt[TRES_ARRAY_BILLING] = max_billing;
}

extern void set_cluster_tres(bool assoc_mgr_locked)
{
	node_record_t *node_ptr;
	slurmdb_tres_rec_t *tres_rec, *cpu_tres = NULL, *mem_tres = NULL;
	int i;
	uint64_t cluster_billing = 0;
	char *unique_tres = NULL;
	assoc_mgr_lock_t locks = {
		.qos = WRITE_LOCK,
		.tres = WRITE_LOCK };
	int active_node_count = 0;

	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(PART_LOCK, WRITE_LOCK));

	if (!assoc_mgr_locked)
		assoc_mgr_lock(&locks);

	xassert(assoc_mgr_tres_array);

	for (i = 0; i < g_tres_count; i++) {
		tres_rec = assoc_mgr_tres_array[i];

		if (!tres_rec->type) {
			error("TRES %d doesn't have a type given, this should never happen",
			      tres_rec->id);
			continue; /* this should never happen */
		}

		if (unique_tres)
			xstrfmtcat(unique_tres, ",%s",
				   assoc_mgr_tres_name_array[i]);
		else
			unique_tres = xstrdup(assoc_mgr_tres_name_array[i]);


		/* reset them now since we are about to add to them */
		tres_rec->count = 0;
		if (tres_rec->id == TRES_CPU) {
			cpu_tres = tres_rec;
			continue;
		} else if (tres_rec->id == TRES_MEM) {
			mem_tres = tres_rec;
			continue;
		} else if (!xstrcmp(tres_rec->type, "bb")) {
			tres_rec->count = bb_g_get_system_size(tres_rec->name);
			continue;
		} else if (!xstrcmp(tres_rec->type, "gres")) {
			/*
			 * Skip named GRES as we don't store
			 * the count the same way we do for unnamed GRES.
			 */
			if (strchr(tres_rec->name, ':'))
				continue;

			tres_rec->count = gres_get_system_cnt(tres_rec->name);
			if (tres_rec->count == NO_VAL64)
				tres_rec->count = 0;   /* GRES name not found */
			continue;
		} else if (!xstrcmp(tres_rec->type, "license")) {
			tres_rec->count = get_total_license_cnt(
				tres_rec->name);
			continue;
		}
		/* FIXME: set up the other tres here that aren't specific */
	}

	xfree(slurm_conf.accounting_storage_tres);
	slurm_conf.accounting_storage_tres = unique_tres;

	cluster_cpus = 0;

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		uint64_t cpu_count = 0, mem_count = 0;
		if (!node_ptr->name)
			continue;

		active_node_count++;
		cpu_count = node_ptr->cpus_efctv;
		mem_count = node_ptr->config_ptr->real_memory;

		cluster_cpus += cpu_count;
		if (mem_tres)
			mem_tres->count += mem_count;

		if (!node_ptr->tres_cnt)
			node_ptr->tres_cnt = xcalloc(slurmctld_tres_cnt,
						     sizeof(uint64_t));
		node_ptr->tres_cnt[TRES_ARRAY_CPU] = cpu_count;
		node_ptr->tres_cnt[TRES_ARRAY_MEM] = mem_count;

		list_for_each(assoc_mgr_tres_list,
			      _add_node_gres_tres, node_ptr);

		_set_node_billing_tres(node_ptr, cpu_count, true);
		cluster_billing += node_ptr->tres_cnt[TRES_ARRAY_BILLING];

		xfree(node_ptr->tres_str);
		node_ptr->tres_str =
			assoc_mgr_make_tres_str_from_array(node_ptr->tres_cnt,
							   TRES_STR_FLAG_SIMPLE,
							   true);
		xfree(node_ptr->tres_fmt_str);
		node_ptr->tres_fmt_str =
			assoc_mgr_make_tres_str_from_array(
				node_ptr->tres_cnt,
				TRES_STR_CONVERT_UNITS,
				true);
	}

	/* FIXME: cluster_cpus probably needs to be removed and handled
	 * differently in the spots this is used.
	 */
	if (cpu_tres)
		cpu_tres->count = cluster_cpus;

	assoc_mgr_tres_array[TRES_ARRAY_NODE]->count = active_node_count;
	assoc_mgr_tres_array[TRES_ARRAY_BILLING]->count = cluster_billing;

	set_partition_tres(true);

	if (!assoc_mgr_locked)
		assoc_mgr_unlock(&locks);
}

/*
 * slurmctld_shutdown - wake up _slurm_rpc_mgr thread via signal
 * RET 0 or error code
 */
int slurmctld_shutdown(void)
{
	sched_debug("slurmctld terminating");
	slurmctld_config.shutdown_time = time(NULL);
	slurm_cond_signal(&shutdown_cond);
	pthread_kill(pthread_self(), SIGUSR1);
	return SLURM_SUCCESS;
}

/*
 * _parse_commandline - parse and process any command line arguments
 * IN argc - number of command line arguments
 * IN argv - the command line arguments
 * IN/OUT conf_ptr - pointer to current configuration, update as needed
 */
static void _parse_commandline(int argc, char **argv)
{
	int c = 0;
	char *tmp_char;

	enum {
		LONG_OPT_ENUM_START = 0x100,
		LONG_OPT_SYSTEMD,
	};

	static struct option long_options[] = {
		{"systemd", no_argument, 0, LONG_OPT_SYSTEMD},
		{"version", no_argument, 0, 'V'},
		{NULL, 0, 0, 0}
	};

	if (run_command_is_launcher(argc, argv)) {
		char *ctx = getenv("SLURM_SCRIPT_CONTEXT");

		if (!xstrcmp(ctx, "burst_buffer.lua")) {
			unsetenv("SLURM_SCRIPT_CONTEXT");
			slurmscriptd_handle_bb_lua_mode(argc, argv);
			_exit(127);
		}

		run_command_launcher(argc, argv);
		_exit(127); /* Should not get here */
	}

	opterr = 0;
	while ((c = getopt_long(argc, argv, "cdDf:hiL:n:rRsvV",
	       long_options, NULL)) > 0) {
		switch (c) {
		case 'c':
			recover = 0;
			break;
		case 'D':
			daemonize = false;
			break;
		case 'f':
			xfree(slurm_conf_filename);
			slurm_conf_filename = xstrdup(optarg);
			break;
		case 'h':
			_usage();
			exit(0);
			break;
		case 'i':
			ignore_state_errors = true;
			break;
		case 'L':
			xfree(debug_logfile);
			debug_logfile = xstrdup(optarg);
			break;
		case 'n':
			new_nice = strtol(optarg, &tmp_char, 10);
			if (tmp_char[0] != '\0') {
				error("Invalid option for -n option (nice "
				      "value), ignored");
				new_nice = 0;
			}
			break;
		case 'r':
			recover = 1;
			break;
		case 'R':
			recover = 2;
			break;
		case 's':
			setwd = true;
			break;
		case 'v':
			debug_level++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		case LONG_OPT_SYSTEMD:
			under_systemd = true;
			break;
		default:
			_usage();
			exit(1);
		}
	}

	if (under_systemd && !daemonize)
		fatal("--systemd and -D options are mutually exclusive");

	/*
	 * Reconfiguration has historically been equivalent to recover = 1.
	 * Force defaults in case the original process used '-c', '-i' or '-R'.
	 */
	if (!original) {
		ignore_state_errors = false;
		recover = 1;
	}

	if (under_systemd) {
		if (!getenv("NOTIFY_SOCKET"))
			fatal("Missing NOTIFY_SOCKET.");
		daemonize = false;
		setwd = true;
	}

	/*
	 * Using setwd() later means a relative path to ourselves may shift.
	 * Capture /proc/self/exe now and save this for reconfig later.
	 * Cannot wait to capture it later as Linux will append " (deleted)"
	 * to the filename if it's been replaced, which would break reconfig
	 * after an upgrade.
	 */
	if (argv[0][0] != '/') {
		if (readlink("/proc/self/exe", binary, PATH_MAX) < 0)
			fatal("%s: readlink failed: %m", __func__);
	} else {
		strlcpy(binary, argv[0], PATH_MAX);
	}
}

static void _usage(void)
{
        char *txt;
        static_ref_to_cstring(txt, usage_txt);
        fprintf(stderr, "%s", txt);
        xfree(txt);
}

static void *_shutdown_bu_thread(void *arg)
{
	int bu_inx, rc = SLURM_SUCCESS, rc2 = SLURM_SUCCESS;
	slurm_msg_t req;
	bool do_shutdown = false;
	shutdown_arg_t *shutdown_arg;
	shutdown_msg_t shutdown_msg;

	shutdown_arg = arg;
	bu_inx = shutdown_arg->index;
	do_shutdown = shutdown_arg->shutdown;
	xfree(arg);

	slurm_msg_t_init(&req);
	slurm_msg_set_r_uid(&req, slurm_conf.slurm_user_id);
	slurm_set_addr(&req.address, slurm_conf.slurmctld_port,
	               slurm_conf.control_addr[bu_inx]);
	if (do_shutdown) {
		req.msg_type = REQUEST_SHUTDOWN;
		shutdown_msg.options = SLURMCTLD_SHUTDOWN_CTLD;
		req.data = &shutdown_msg;
	} else {
		req.msg_type = REQUEST_CONTROL;
	}
	debug("Requesting control from backup controller %s",
	      slurm_conf.control_machine[bu_inx]);
	if (slurm_send_recv_rc_msg_only_one(&req, &rc2,
				(CONTROL_TIMEOUT * 1000)) < 0) {
		error("%s:send/recv %s: %m",
		      __func__, slurm_conf.control_machine[bu_inx]);
		rc = SLURM_ERROR;
	} else if (rc2 == ESLURM_DISABLED) {
		debug("backup controller %s responding",
		      slurm_conf.control_machine[bu_inx]);
	} else if (rc2 == SLURM_SUCCESS) {
		debug("backup controller %s has relinquished control",
		      slurm_conf.control_machine[bu_inx]);
	} else {
		error("%s (%s): %s", __func__,
		      slurm_conf.control_machine[bu_inx],
		      slurm_strerror(rc2));
		rc = SLURM_ERROR;
	}

	slurm_mutex_lock(&bu_mutex);
	if (rc != SLURM_SUCCESS)
		bu_rc = rc;
	bu_thread_cnt--;
	slurm_cond_signal(&bu_cond);
	slurm_mutex_unlock(&bu_mutex);
	return NULL;
}

/*
 * Tell the backup_controllers to relinquish control, primary control_machine
 *	has resumed operation. Messages sent to all controllers in parallel.
 * RET 0 or an error code
 * NOTE: READ lock_slurmctld config before entry (or be single-threaded)
 */
static int _shutdown_backup_controller(void)
{
	int i;
	shutdown_arg_t *shutdown_arg;

	bu_rc = SLURM_SUCCESS;

	/* If we don't have any backups configured just return */
	if (slurm_conf.control_cnt == 1)
		return bu_rc;

	debug2("shutting down backup controllers (my index: %d)", backup_inx);
	for (i = 1; i < slurm_conf.control_cnt; i++) {
		if (i == backup_inx)
			continue;	/* No message to self */

		if ((slurm_conf.control_addr[i] == NULL) ||
		    (slurm_conf.control_addr[i][0] == '\0'))
			continue;

		shutdown_arg = xmalloc(sizeof(*shutdown_arg));
		shutdown_arg->index = i;
		/*
		 * need to send actual REQUEST_SHUTDOWN to non-primary ctlds
		 * in order to have them properly shutdown and not contend
		 * for primary position, otherwise "takeover" results in
		 * contention among backups for primary position.
		 */
		if (i < backup_inx)
			shutdown_arg->shutdown = true;
		slurm_thread_create_detached(_shutdown_bu_thread,
					     shutdown_arg);
		slurm_mutex_lock(&bu_mutex);
		bu_thread_cnt++;
		slurm_mutex_unlock(&bu_mutex);
	}

	slurm_mutex_lock(&bu_mutex);
	while (bu_thread_cnt != 0) {
		slurm_cond_wait(&bu_cond, &bu_mutex);
	}
	slurm_mutex_unlock(&bu_mutex);

	return bu_rc;
}

/*
 * Update log levels given requested levels
 * NOTE: Will not turn on originally configured off (quiet) channels
 */
void update_log_levels(int req_slurmctld_debug, int req_syslog_debug)
{
	static bool conf_init = false;
	static int conf_slurmctld_debug, conf_syslog_debug;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	int slurmctld_debug;
	int syslog_debug;

	/*
	 * Keep track of the original debug levels from slurm.conf so that
	 * `scontrol setdebug` does not turn on non-active logging channels.
	 * NOTE: It is known that `scontrol reconfigure` will cause an issue
	 *       when reconfigured with a slurm.conf that changes SlurmctldDebug
	 *       from level QUIET to a non-quiet value.
	 * NOTE: Planned changes to `reconfigure` behavior should make this a
	 *       non-issue in a future release.
	 */
	if (!conf_init) {
		conf_slurmctld_debug = slurm_conf.slurmctld_debug;
		conf_syslog_debug = slurm_conf.slurmctld_syslog_debug;
		conf_init = true;
	}

	/*
	 * NOTE: not offset by LOG_LEVEL_INFO, since it's inconvenient
	 * to provide negative values for scontrol
	 */
	slurmctld_debug = MIN(req_slurmctld_debug, (LOG_LEVEL_END - 1));
	slurmctld_debug = MAX(slurmctld_debug, LOG_LEVEL_QUIET);
	syslog_debug = MIN(req_syslog_debug, (LOG_LEVEL_END - 1));
	syslog_debug = MAX(syslog_debug, LOG_LEVEL_QUIET);

	if (daemonize)
		log_opts.stderr_level = LOG_LEVEL_QUIET;
	else
		log_opts.stderr_level = slurmctld_debug;

	if (slurm_conf.slurmctld_logfile &&
	    (conf_slurmctld_debug != LOG_LEVEL_QUIET))
		log_opts.logfile_level = slurmctld_debug;
	else
		log_opts.logfile_level = LOG_LEVEL_QUIET;

	if (conf_syslog_debug == LOG_LEVEL_QUIET)
		log_opts.syslog_level = LOG_LEVEL_QUIET;
	else if (slurm_conf.slurmctld_syslog_debug != LOG_LEVEL_END)
		log_opts.syslog_level = syslog_debug;
	else if (!daemonize)
		log_opts.syslog_level = LOG_LEVEL_QUIET;
	else if (!slurm_conf.slurmctld_logfile &&
		 (conf_slurmctld_debug > LOG_LEVEL_QUIET))
		log_opts.syslog_level = slurmctld_debug;
	else
		log_opts.syslog_level = LOG_LEVEL_FATAL;

	log_alter(log_opts, LOG_DAEMON, slurm_conf.slurmctld_logfile);

	debug("slurmctld log levels: stderr=%s logfile=%s syslog=%s",
	      log_num2string(log_opts.stderr_level),
	      log_num2string(log_opts.logfile_level),
	      log_num2string(log_opts.syslog_level));
}

/*
 * Reset slurmctld logging based upon configuration parameters uses common
 * slurm_conf data structure
 */
void update_logging(void)
{
	int rc;
	uid_t slurm_user_id  = slurm_conf.slurm_user_id;
	gid_t slurm_user_gid = gid_from_uid(slurm_user_id);

	xassert(verify_lock(CONF_LOCK, WRITE_LOCK));

	/* Preserve execute line arguments (if any) */
	if (debug_level) {
		slurm_conf.slurmctld_debug = MIN(
			(LOG_LEVEL_INFO + debug_level),
			(LOG_LEVEL_END - 1));
	}
	if (slurm_conf.slurmctld_debug != NO_VAL16) {
		log_opts.logfile_level = slurm_conf.slurmctld_debug;
	}
	if (debug_logfile) {
		xfree(slurm_conf.slurmctld_logfile);
		slurm_conf.slurmctld_logfile = xstrdup(debug_logfile);
	}

	log_set_timefmt(slurm_conf.log_fmt);

	update_log_levels(slurm_conf.slurmctld_debug,
			  slurm_conf.slurmctld_syslog_debug);

	debug("Log file re-opened");

	/*
	 * SchedLogLevel restore
	 */
	if (slurm_conf.sched_log_level != NO_VAL16)
		sched_log_opts.logfile_level = slurm_conf.sched_log_level;

	sched_log_alter(sched_log_opts, LOG_DAEMON, slurm_conf.sched_logfile);

	if (slurm_conf.slurmctld_logfile) {
		rc = chown(slurm_conf.slurmctld_logfile,
			   slurm_user_id, slurm_user_gid);
		if (rc && daemonize) {
			error("chown(%s, %u, %u): %m",
			      slurm_conf.slurmctld_logfile,
			      slurm_user_id, slurm_user_gid);
		}
	}
	if (slurm_conf.sched_logfile) {
		rc = chown(slurm_conf.sched_logfile,
			   slurm_user_id, slurm_user_gid);
		if (rc && daemonize) {
			error("chown(%s, %u, %u): %m",
			      slurm_conf.sched_logfile,
			      slurm_user_id, slurm_user_gid);
		}
	}
}

/* Reset slurmd nice value */
static void _update_nice(void)
{
	int cur_nice;
	id_t pid;

	if (new_nice == 0)	/* No change */
		return;

	pid = getpid();
	cur_nice = getpriority(PRIO_PROCESS, pid);
	if (cur_nice == new_nice)
		return;
	if (setpriority(PRIO_PROCESS, pid, new_nice))
		error("Unable to reset nice value to %d: %m", new_nice);
}

/*
 * Verify that ClusterName from slurm.conf matches the state directory.
 * If mismatched, exit immediately to protect state files from corruption.
 */
static void _verify_clustername(void)
{
	FILE *fp;
	char *filename = NULL;
	char name[512] = {0};

	xstrfmtcat(filename, "%s/clustername", slurm_conf.state_save_location);

	if ((fp = fopen(filename, "r"))) {
		char *pipe;

		/* read value and compare */
		if (!fgets(name, sizeof(name), fp)) {
			error("%s: reading cluster name from clustername file",
			      __func__);
		}
		fclose(fp);

		pipe = xstrchr(name, '|');
		if (pipe) {
			pipe[0] = '\0';
			slurm_conf.cluster_id = slurm_atoul(pipe+1);
		}

		if (xstrcmp(name, slurm_conf.cluster_name)) {
			fatal("CLUSTER NAME MISMATCH.\n"
			      "slurmctld has been started with \"ClusterName=%s\", but read \"%s\" from the state files in StateSaveLocation.\n"
			      "Running multiple clusters from a shared StateSaveLocation WILL CAUSE CORRUPTION.\n"
			      "Remove %s to override this safety check if this is intentional (e.g., the ClusterName has changed).",
			      slurm_conf.cluster_name, name, filename);
			exit(1);
		}
	}

	xfree(filename);
}

static void _create_clustername_file(void)
{
	FILE *fp;
	char *filename = NULL;
	char *tmp_str = xstrdup_printf("%s|%u",
				       slurm_conf.cluster_name,
				       slurm_conf.cluster_id);

	filename = xstrdup_printf("%s/clustername",
	                          slurm_conf.state_save_location);

	info("creating clustername file: ClusterName=%s ClusterID=%u",
	     slurm_conf.cluster_name, slurm_conf.cluster_id);

	if (!(fp = fopen(filename, "w"))) {
		fatal("%s: failed to create file %s", __func__, filename);
		exit(1);
	}

	if (fputs(tmp_str, fp) < 0) {
		fatal("%s: failed to write to file %s", __func__, filename);
		exit(1);
	}
	fclose(fp);

	xfree(tmp_str);
	xfree(filename);
}

/* Kill the currently running slurmctld
 * NOTE: No need to lock the config data since we are still single-threaded */
static void _kill_old_slurmctld(void)
{
	int fd;
	pid_t oldpid = read_pidfile(slurm_conf.slurmctld_pidfile, &fd);
	if (oldpid != (pid_t) 0) {
		if (!ignore_state_errors && xstrstr(slurm_conf.slurmctld_params, "no_quick_restart"))
			fatal("SlurmctldParameters=no_quick_restart set. Please shutdown your previous slurmctld (pid oldpid) before starting a new one. (-i to ignore this message)");

		info ("killing old slurmctld[%ld]", (long) oldpid);
		kill(oldpid, SIGTERM);

		/*
		 * Wait for previous daemon to terminate
		 */
		if (fd_get_readw_lock(fd) < 0)
			fatal ("unable to wait for readw lock: %m");
		(void) close(fd); /* Ignore errors */
	}
}

/* NOTE: No need to lock the config data since we are still single-threaded */
static void _init_pidfile(void)
{
	if (!xstrcmp(slurm_conf.slurmctld_pidfile, slurm_conf.slurmd_pidfile))
		error("SlurmctldPid == SlurmdPid, use different names");

	/* Don't close the fd returned here since we need to keep the
	 * fd open to maintain the write lock */
	pidfd = create_pidfile(slurm_conf.slurmctld_pidfile,
			       slurm_conf.slurm_user_id);
}

static void _update_pidfile(void)
{
	char *env = getenv("SLURMCTLD_RECONF_PIDFD");

	if (!env) {
		debug("%s: missing SLURMCTLD_RECONF_PIDFD envvar", __func__);
		return;
	}

	pidfd = atoi(env);
	update_pidfile(pidfd);
}

/*
 * set_slurmctld_state_loc - create state directory as needed and "cd" to it
 * NOTE: config read lock must be set on entry
 */
extern void set_slurmctld_state_loc(void)
{
	int rc;
	struct stat st;
	const char *path = slurm_conf.state_save_location;

	/*
	 * If state save location does not exist, try to create it.
	 *  Otherwise, ensure path is a directory as expected, and that
	 *  we have permission to write to it.
	 */
	if (((rc = stat(path, &st)) < 0) && (errno == ENOENT)) {
		if (mkdir(path, 0755) < 0)
			fatal("mkdir(%s): %m", path);
	}
	else if (rc < 0)
		fatal("Unable to stat state save loc: %s: %m", path);
	else if (!S_ISDIR(st.st_mode))
		fatal("State save loc: %s: Not a directory!", path);
	else if (access(path, R_OK|W_OK|X_OK) < 0)
		fatal("Incorrect permissions on state save loc: %s", path);
}

/* _assoc_cache_mgr - hold out until we have real data from the
 * database so we can reset the job ptr's assoc ptr's */
static void *_assoc_cache_mgr(void *no_data)
{
	list_itr_t *itr = NULL;
	job_record_t *job_ptr = NULL;
	part_record_t *part_ptr = NULL;
	slurmdb_assoc_rec_t assoc_rec;
	/* Write lock on jobs, nodes and partitions */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	assoc_mgr_lock_t locks =
		{ .assoc = READ_LOCK, .qos = WRITE_LOCK, .tres = WRITE_LOCK,
		  .user = READ_LOCK };

	if (running_cache != RUNNING_CACHE_STATE_RUNNING) {
		slurm_mutex_lock(&assoc_cache_mutex);
		lock_slurmctld(job_write_lock);
		/*
		 * It is ok to have the job_write_lock here as long as
		 * running_cache != RUNNING_CACHE_STATE_NOTRUNNING. This short
		 * circuits the association manager to not call callbacks. If
		 * we come out of cache we need the job_write_lock locked until
		 * the end to prevent a race condition on the job_list (some
		 * running without new info and some running with the cached
		 * info).
		 *
		 * Make sure not to have the assoc_mgr or the
		 * slurmdbd_lock locked when refresh_lists is called or you may
		 * get deadlock.
		 */
		assoc_mgr_refresh_lists(acct_db_conn, 0);
		if (g_tres_count != slurmctld_tres_cnt) {
			info("TRES in database does not match cache (%u != %u).  Updating...",
			     g_tres_count, slurmctld_tres_cnt);
			_init_tres();
		}
		slurm_mutex_unlock(&assoc_cache_mutex);
	}

	while (running_cache == RUNNING_CACHE_STATE_RUNNING) {
		slurm_mutex_lock(&assoc_cache_mutex);
		slurm_cond_wait(&assoc_cache_cond, &assoc_cache_mutex);
		/* This is here to see if we are exiting.  If so then
		   just return since we are closing down.
		*/
		if (running_cache == RUNNING_CACHE_STATE_EXITING) {
			slurm_mutex_unlock(&assoc_cache_mutex);
			return NULL;
		}

		lock_slurmctld(job_write_lock);
		/*
		 * It is ok to have the job_write_lock here as long as
		 * running_cache != RUNNING_CACHE_STATE_NOTRUNNING. This short
		 * circuits the association manager to not call callbacks. If
		 * we come out of cache we need the job_write_lock locked until
		 * the end to prevent a race condition on the job_list (some
		 * running without new info and some running with the cached
		 * info).
		 *
		 * Make sure not to have the assoc_mgr or the
		 * slurmdbd_lock locked when refresh_lists is called or you may
		 * get deadlock.
		 */
		assoc_mgr_refresh_lists(acct_db_conn, 0);
		if (g_tres_count != slurmctld_tres_cnt) {
			info("TRES in database does not match cache "
			     "(%u != %u).  Updating...",
			     g_tres_count, slurmctld_tres_cnt);
			_init_tres();
		}

		/*
		 * If running_cache == RUNNING_CACHE_STATE_LISTS_REFRESHED it
		 * means the assoc_mgr has deemed all is good but we can't
		 * actually enforce it until now since _init_tres() could call
		 * assoc_mgr_refresh_lists() again which makes it so you could
		 * get deadlock.
		 */
		if (running_cache == RUNNING_CACHE_STATE_LISTS_REFRESHED)
			running_cache = RUNNING_CACHE_STATE_NOTRUNNING;
		else if (running_cache == RUNNING_CACHE_STATE_RUNNING)
			unlock_slurmctld(job_write_lock);

		slurm_mutex_unlock(&assoc_cache_mutex);
	}

	if (!job_list) {
		/* This could happen in rare occations, it doesn't
		 * matter since when the job_list is populated things
		 * will be in sync.
		 */
		debug2("No job list yet");
		unlock_slurmctld(job_write_lock);
		goto handle_parts;
	}

	debug2("got real data from the database "
	       "refreshing the association ptr's for %d jobs",
	       list_count(job_list));
	assoc_mgr_lock(&locks);
	itr = list_iterator_create(job_list);
	while ((job_ptr = list_next(itr))) {
		(void) _update_job_tres(job_ptr, NULL);

		if (job_ptr->assoc_id) {
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_assoc_rec_t));
			assoc_rec.id = job_ptr->assoc_id;

			debug("assoc is %zx (%d) for %pJ",
			      (size_t)job_ptr->assoc_ptr, job_ptr->assoc_id,
			      job_ptr);

			if (assoc_mgr_fill_in_assoc(
				    acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    (slurmdb_assoc_rec_t **)
				    &job_ptr->assoc_ptr, true)) {
				verbose("Invalid association id %u for %pJ",
					job_ptr->assoc_id, job_ptr);
				/* not a fatal error, association could have
				 * been removed */
			}

			debug("now assoc is %zx (%d) for %pJ",
			      (size_t)job_ptr->assoc_ptr, job_ptr->assoc_id,
			      job_ptr);
		}
		if (job_ptr->qos_list) {
			list_flush(job_ptr->qos_list);
			char *token, *last = NULL;
			char *tmp_qos_req = xstrdup(job_ptr->details->qos_req);
			slurmdb_qos_rec_t *qos_ptr = NULL;

			token = strtok_r(tmp_qos_req, ",", &last);
			while (token) {
				slurmdb_qos_rec_t qos_rec = {
					.name = token,
				};
				if ((assoc_mgr_fill_in_qos(
					     acct_db_conn, &qos_rec,
					     accounting_enforce,
					     &qos_ptr,
					     true)) != SLURM_SUCCESS) {
					verbose("Invalid qos (%u) for %pJ",
						job_ptr->qos_id, job_ptr);
					/* not a fatal error, qos could have
					 * been removed */
				} else
					list_append(job_ptr->qos_list, qos_ptr);
				token = strtok_r(NULL, ",", &last);
			}
			xfree(tmp_qos_req);

			if (list_count(job_ptr->qos_list)) {
				list_sort(job_ptr->qos_list,
					  priority_sort_qos_desc);
				/* If we are pending we want the highest prio */
				if (IS_JOB_PENDING(job_ptr)) {
					job_ptr->qos_ptr =
						list_peek(job_ptr->qos_list);
					job_ptr->qos_id = job_ptr->qos_ptr->id;
				} else {
					job_ptr->qos_ptr = list_find_first(
						job_ptr->qos_list,
						slurmdb_find_qos_in_list,
						&job_ptr->qos_id);
					if (!job_ptr->qos_ptr) {
						verbose("Invalid qos (%u) for %pJ from qos_req '%s'",
							job_ptr->qos_id,
							job_ptr,
							job_ptr->details->
							qos_req);
						goto use_qos_id;
					}
				}
			} else
				FREE_NULL_LIST(job_ptr->qos_list);
		} else if (job_ptr->qos_id) {
use_qos_id: ; /* must be a blank ; for older compilers (el7) */
			slurmdb_qos_rec_t qos_rec = {
				.id = job_ptr->qos_id,
			};

			if ((assoc_mgr_fill_in_qos(
				    acct_db_conn, &qos_rec,
				    accounting_enforce,
				    (slurmdb_qos_rec_t **)&job_ptr->qos_ptr,
				    true))
			   != SLURM_SUCCESS) {
				verbose("Invalid qos (%u) for %pJ",
					job_ptr->qos_id, job_ptr);
				/* not a fatal error, qos could have
				 * been removed */
			}
		}
	}
	list_iterator_destroy(itr);

handle_parts:
	if (!part_list) {
		/* This could happen in rare occations, it doesn't
		 * matter since when the job_list is populated things
		 * will be in sync.
		 */
		debug2("No part list yet");
		goto end_it;
	}

	itr = list_iterator_create(part_list);
	while ((part_ptr = list_next(itr))) {
		if (part_ptr->allow_qos)
			qos_list_build(part_ptr->allow_qos,
				       &part_ptr->allow_qos_bitstr);

		if (part_ptr->deny_qos)
			qos_list_build(part_ptr->deny_qos,
				       &part_ptr->deny_qos_bitstr);

		if (part_ptr->qos_char) {
			slurmdb_qos_rec_t qos_rec;

			memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
			qos_rec.name = part_ptr->qos_char;
			part_ptr->qos_ptr = NULL;
			if (assoc_mgr_fill_in_qos(
				    acct_db_conn, &qos_rec, accounting_enforce,
				    (slurmdb_qos_rec_t **)&part_ptr->qos_ptr,
				    true)
			    != SLURM_SUCCESS) {
				fatal("Partition %s has an invalid qos (%s), "
				      "please check your configuration",
				      part_ptr->name, qos_rec.name);
			}
		}

		part_update_assoc_lists(part_ptr, NULL);
	}
	list_iterator_destroy(itr);

end_it:

	set_cluster_tres(true);

	assoc_mgr_unlock(&locks);
	/* issuing a reconfig will reset the pointers on the burst
	   buffers */
	bb_g_reconfig();

	unlock_slurmctld(job_write_lock);
	/* This needs to be after the lock and after we update the
	   jobs so if we need to send them we are set. */
	_accounting_cluster_ready();
	_get_fed_updates();

	return NULL;
}

/*
 * Find this host in the controller index, or return -1 on error.
 */
static int _controller_index(void)
{
	int i;

	/*
	 * Slurm internal HA mode (or no HA).
	 * Each controller is separately defined, and a single hostname is in
	 * each control_machine entry.
	 */
	for (i = 0; i < slurm_conf.control_cnt; i++) {
		if (slurm_conf.control_machine[i] &&
		    slurm_conf.control_addr[i]    &&
		    (!xstrcmp(slurmctld_config.node_name_short,
		              slurm_conf.control_machine[i])  ||
		     !xstrcmp(slurmctld_config.node_name_long,
		              slurm_conf.control_machine[i]))) {
			return i;
		}
	}

	/*
	 * External HA mode. Here a single control_addr has been defined,
	 * but multiple hostnames are in control_machine[0] with comma
	 * separation. If our hostname matches any of those, we are considered
	 * to be a valid controller, and which is active must be managed by
	 * an external HA solution.
	 */
	if (xstrchr(slurm_conf.control_machine[0], ',')) {
		char *token, *last = NULL;
		char *tmp_name = xstrdup(slurm_conf.control_machine[0]);

		token = strtok_r(tmp_name, ",", &last);
		while (token) {
			if (!xstrcmp(slurmctld_config.node_name_short, token) ||
			    !xstrcmp(slurmctld_config.node_name_long, token)) {
				xfree(tmp_name);
				return 0;
			}
			token = strtok_r(NULL, ",", &last);
		}
		xfree(tmp_name);
	}

	return -1;
}


static void _test_thread_limit(void)
{
#ifdef RLIMIT_NOFILE
	struct rlimit rlim[1];
	if (getrlimit(RLIMIT_NOFILE, rlim) < 0)
		error("Unable to get file count limit");
	else if ((rlim->rlim_cur != RLIM_INFINITY) &&
		 (max_server_threads > rlim->rlim_cur)) {
		max_server_threads = rlim->rlim_cur;
		info("Reducing max_server_thread to %u due to file count limit "
		     "of %u", max_server_threads, max_server_threads);
	}
#endif
}

static void  _set_work_dir(void)
{
	bool success = false;

	if (slurm_conf.slurmctld_logfile &&
	    (slurm_conf.slurmctld_logfile[0] == '/')) {
		char *slash_ptr, *work_dir;
		work_dir = xstrdup(slurm_conf.slurmctld_logfile);
		slash_ptr = strrchr(work_dir, '/');
		if (slash_ptr == work_dir)
			work_dir[1] = '\0';
		else
			slash_ptr[0] = '\0';
		if ((access(work_dir, W_OK) != 0) || (chdir(work_dir) < 0))
			error("chdir(%s): %m", work_dir);
		else
			success = true;
		xfree(work_dir);
	}

	if (!success) {
		if ((access(slurm_conf.state_save_location, W_OK) != 0) ||
		    (chdir(slurm_conf.state_save_location) < 0)) {
			error("chdir(%s): %m",
			      slurm_conf.state_save_location);
		} else
			success = true;
	}

	if (!success) {
		if ((access("/var/tmp", W_OK) != 0) ||
		    (chdir("/var/tmp") < 0)) {
			error("chdir(/var/tmp): %m");
		} else
			info("chdir to /var/tmp");
	}
}

/*
 * _purge_files_thread - separate thread to remove job batch/environ files
 * from the state directory. Runs async from purge_old_jobs to avoid
 * holding locks while the files are removed, which can cause performance
 * problems under high throughput conditions.
 *
 * Uses the purge_cond to wakeup on demand, then works through the global
 * purge_files_list of job_ids and removes their files.
 */
static void *_purge_files_thread(void *no_data)
{
	int *job_id;

	/*
	 * Use the purge_files_list as a queue. _delete_job_details()
	 * in job_mgr.c always enqueues (at the end), while
	 *_purge_files_thread consumes off the front.
	 *
	 * There is a potential race condition if the job numbers have
	 * wrapped between _purge_thread removing the state files and
	 * get_next_job_id trying to re-assign it. This is mitigated
	 * the call to _dup_job_file_test() in job_mgr.c ensuring
	 * there is no existing directory for an id before assigning it.
	 */

	/*
	 * pthread_cond_wait requires a lock to release and reclaim.
	 * the list structure is already handling locking for itself,
	 * so this lock isn't actually useful, and the thread calling
	 * pthread_cond_signal isn't required to have the lock. So
	 * lock it once and hold it until slurmctld shuts down.
	 */
	slurm_mutex_lock(&purge_thread_lock);
	while (!slurmctld_config.shutdown_time) {
		slurm_cond_wait(&purge_thread_cond, &purge_thread_lock);
		debug2("%s: starting, %d jobs to purge", __func__,
		       list_count(purge_files_list));

		/*
		 * Use list_dequeue here (instead of list_flush) as it will not
		 * hold up the list lock when we try to enqueue jobs that need
		 * to be freed.
		 */
		while ((job_id = list_dequeue(purge_files_list))) {
			debug2("%s: purging files from JobId=%u",
			       __func__, *job_id);
			delete_job_desc_files(*job_id);
			xfree(job_id);
		}
	}
	slurm_mutex_unlock(&purge_thread_lock);
	return NULL;
}

static int _acct_update_list_for_each(void *x, void *arg)
{
	slurmdb_update_object_t *object = x;
	bool locked = false;

	switch (object->type) {
	case SLURMDB_UPDATE_FEDS:
#if HAVE_SYS_PRCTL_H
		if (prctl(PR_SET_NAME, "fedmgr", NULL, NULL, NULL) < 0){
			error("%s: cannot set my name to %s %m",
			      __func__, "fedmgr");
		}
#endif
		fed_mgr_update_feds(object);
		break;
	default:
		(void) assoc_mgr_update_object(x, &locked);
	}

	/* Always delete it */
	return 1;
}

static void *_acct_update_thread(void *no_data)
{
	slurm_mutex_lock(&slurmctld_config.acct_update_lock);
	while (!slurmctld_config.shutdown_time) {
		slurm_cond_wait(&slurmctld_config.acct_update_cond,
				&slurmctld_config.acct_update_lock);

		(void) list_delete_all(slurmctld_config.acct_update_list,
				       _acct_update_list_for_each,
				       NULL);
	}
	slurm_mutex_unlock(&slurmctld_config.acct_update_lock);

	return NULL;
}

static void _get_fed_updates(void)
{
	list_t *fed_list = NULL;
	slurmdb_update_object_t update = {0};
	slurmdb_federation_cond_t fed_cond;

	slurmdb_init_federation_cond(&fed_cond, 0);
	fed_cond.cluster_list = list_create(NULL);
	list_append(fed_cond.cluster_list, slurm_conf.cluster_name);

	fed_list = acct_storage_g_get_federations(acct_db_conn,
	                                          slurm_conf.slurm_user_id,
	                                          &fed_cond);
	FREE_NULL_LIST(fed_cond.cluster_list);

	if (fed_list) {
		update.objects = fed_list;
		fed_mgr_update_feds(&update);
	}

	FREE_NULL_LIST(fed_list);
}

static int _foreach_job_running(void *object, void *arg)
{
	job_record_t *job_ptr = object;

	if (IS_JOB_PENDING(job_ptr)) {
		int job_cnt = (job_ptr->array_recs &&
			       job_ptr->array_recs->task_cnt) ?
			job_ptr->array_recs->task_cnt : 1;
		slurmctld_diag_stats.jobs_pending += job_cnt;
	}
	if (IS_JOB_RUNNING(job_ptr))
		slurmctld_diag_stats.jobs_running++;

	return SLURM_SUCCESS;
}

static void _update_diag_job_state_counts(void)
{
	slurmctld_diag_stats.jobs_running = 0;
	slurmctld_diag_stats.jobs_pending = 0;
	slurmctld_diag_stats.job_states_ts = time(NULL);
	list_for_each_ro(job_list, _foreach_job_running, NULL);
}

static void *_wait_primary_prog(void *arg)
{
	primary_thread_arg_t *wait_arg = arg;
	int status = 0;

	waitpid(wait_arg->cpid, &status, 0);
	if (status != 0) {
		error("%s: %s exit status %u:%u", __func__, wait_arg->prog_type,
		      WEXITSTATUS(status), WTERMSIG(status));
	} else {
		info("%s: %s completed successfully", __func__,
		     wait_arg->prog_type);
	}
	xfree(wait_arg->prog_type);
	xfree(wait_arg);
	return NULL;
}

static void _run_primary_prog(bool primary_on)
{
	primary_thread_arg_t *wait_arg;
	char *prog_name, *prog_type;
	char *argv[2], *sep;
	pid_t cpid;

	if (primary_on) {
		prog_name = slurm_conf.slurmctld_primary_on_prog;
		prog_type = "SlurmctldPrimaryOnProg";
	} else {
		prog_name = slurm_conf.slurmctld_primary_off_prog;
		prog_type = "SlurmctldPrimaryOffProg";
	}

	if ((prog_name == NULL) || (prog_name[0] == '\0'))
		return;

	if (access(prog_name, X_OK) < 0) {
		error("%s: Invalid %s: %m", __func__, prog_type);
		return;
	}

	sep = strrchr(prog_name, '/');
	if (sep)
		argv[0] = sep + 1;
	else
		argv[0] = prog_name;
	argv[1] = NULL;
	if ((cpid = fork()) < 0) {	/* Error */
		error("%s fork error: %m", __func__);
		return;
	}
	if (cpid == 0) {		/* Child */
		closeall(0);
		setpgid(0, 0);
		execv(prog_name, argv);
		_exit(127);
	}

	/* Create thread to wait for and log program completion */
	wait_arg = xmalloc(sizeof(primary_thread_arg_t));
	wait_arg->cpid = cpid;
	wait_arg->prog_type = xstrdup(prog_type);
	slurm_thread_create_detached(_wait_primary_prog, wait_arg);
}

static int _init_dep_job_ptr(void *object, void *arg)
{
	depend_spec_t *dep_ptr = object;
	dep_ptr->job_ptr = find_job_array_rec(dep_ptr->job_id,
					      dep_ptr->array_task_id);
	return SLURM_SUCCESS;
}

/*
 * Restore dependency job pointers.
 *
 * test_job_dependency() initializes dep_ptr->job_ptr but in
 * case a job's dependency is updated before test_job_dependency() is called,
 * dep_ptr->job_ptr needs to be initialized for all jobs so that we can test
 * for circular dependencies properly. Otherwise, if slurmctld is restarted,
 * then immediately a job dependency is updated before test_job_dependency()
 * is called, it is possible to create a circular dependency.
 */
static void _restore_job_dependencies(void)
{
	job_record_t *job_ptr;
	list_itr_t *job_iterator;
	slurmctld_lock_t job_fed_lock = {.job = WRITE_LOCK, .fed = READ_LOCK};

	lock_slurmctld(job_fed_lock);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (job_ptr->details && job_ptr->details->depend_list)
			list_for_each(job_ptr->details->depend_list,
				      _init_dep_job_ptr, NULL);
	}
	list_iterator_destroy(job_iterator);
	unlock_slurmctld(job_fed_lock);
}

/*
 * Respond to request for primary/backup slurmctld status
 */
extern void slurm_rpc_control_status(slurm_msg_t *msg)
{
	control_status_msg_t status = {
		.backup_inx = backup_inx,
		.control_time = control_time,
	};

	(void) send_msg_response(msg, RESPONSE_CONTROL_STATUS, &status);
}

extern int controller_init_scheduling(bool init_gang)
{
	int rc = sched_g_init();

	if (rc != SLURM_SUCCESS) {
		error("failed to initialize sched plugin");
		return rc;
	}

	main_sched_init();

	if (init_gang)
		gs_init();

	return rc;
}

extern void controller_fini_scheduling(void)
{
	(void) sched_g_fini();

	main_sched_fini();

	if (slurm_conf.preempt_mode & PREEMPT_MODE_GANG)
		gs_fini();
}

extern void controller_reconfig_scheduling(void)
{
	gs_reconfig();

	(void) sched_g_reconfig();
}
