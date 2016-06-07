/*****************************************************************************\
 *  controller.c - main control machine daemon for slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2016 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, Kevin Tew <tew1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <errno.h>
#include <grp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/checkpoint.h"
#include "src/common/daemonize.h"
#include "src/common/fd.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/layouts_mgr.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/power.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_mcs.h"
#include "src/common/slurm_priority.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_route.h"
#include "src/common/slurm_topology.h"
#include "src/common/switch.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/job_submit.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/port_mgr.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmctld_plugstack.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/srun_comm.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"


#define CRED_LIFE         60	/* Job credential lifetime in seconds */
#define DEFAULT_DAEMONIZE 1	/* Run as daemon by default if set */
#define DEFAULT_RECOVER   1	/* Default state recovery on restart
				 * 0 = use no saved state information
				 * 1 = recover saved job state,
				 *     node DOWN/DRAIN state & reason information
				 * 2 = recover state saved from last shutdown */
#define MIN_CHECKIN_TIME  3	/* Nodes have this number of seconds to
				 * check-in before we ping them */
#define SHUTDOWN_WAIT     2	/* Time to wait for backup server shutdown */

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
 * The OpenSSL code produces a bunch of errors related to use of
 *    non-initialized memory use.
 * The _keyvalue_regex_init() function will generate two blocks "definitely
 *    lost", both of size zero. We haven't bothered to address this.
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
int	accounting_enforce = 0;
int	association_based_accounting = 0;
void *	acct_db_conn = NULL;
int	batch_sched_delay = 3;
int	bg_recover = DEFAULT_RECOVER;
uint32_t cluster_cpus = 0;
time_t	last_proc_req_start = 0;
bool	ping_nodes_now = false;
int	sched_interval = 60;
char *	slurmctld_cluster_name = NULL; /* name of cluster */
slurmctld_config_t slurmctld_config;
diag_stats_t slurmctld_diag_stats;
int	slurmctld_primary = 1;
bool	want_nodes_reboot = true;
int   slurmctld_tres_cnt = 0;

/* Local variables */
static pthread_t assoc_cache_thread = (pthread_t) 0;
static int	daemonize = DEFAULT_DAEMONIZE;
static int	debug_level = 0;
static char *	debug_logfile = NULL;
static bool	dump_core = false;
static int      job_sched_cnt = 0;
static uint32_t max_server_threads = MAX_SERVER_THREADS;
static time_t	next_stats_reset = 0;
static int	new_nice = 0;
static char	node_name_short[MAX_SLURM_NAME];
static char	node_name_long[MAX_SLURM_NAME];
static int	recover   = DEFAULT_RECOVER;
static pthread_mutex_t sched_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t server_thread_cond = PTHREAD_COND_INITIALIZER;
static pid_t	slurmctld_pid;
static char *	slurm_conf_filename;

/*
 * Static list of signals to block in this process
 * *Must be zero-terminated*
 */
static int controller_sigarray[] = {
	SIGINT,  SIGTERM, SIGCHLD, SIGUSR1,
	SIGUSR2, SIGTSTP, SIGXCPU, SIGQUIT,
	SIGPIPE, SIGALRM, SIGABRT, SIGHUP, 0
};

static int          _accounting_cluster_ready();
static int          _accounting_mark_all_nodes_down(char *reason);
static void *       _assoc_cache_mgr(void *no_data);
static void         _become_slurm_user(void);
static void         _default_sigaction(int sig);
static void         _init_config(void);
static void         _init_pidfile(void);
static void         _kill_old_slurmctld(void);
static void         _parse_commandline(int argc, char *argv[]);
inline static int   _ping_backup_controller(void);
static void         _remove_assoc(slurmdb_assoc_rec_t *rec);
static void         _remove_qos(slurmdb_qos_rec_t *rec);
static void         _update_assoc(slurmdb_assoc_rec_t *rec);
static void         _update_qos(slurmdb_qos_rec_t *rec);
static int          _init_tres(void);
static void         _update_cluster_tres(void);

inline static int   _report_locks_set(void);
static void *       _service_connection(void *arg);
static void         _set_work_dir(void);
static int          _shutdown_backup_controller(int wait_time);
static void *       _slurmctld_background(void *no_data);
static void *       _slurmctld_rpc_mgr(void *no_data);
static void *       _slurmctld_signal_hand(void *no_data);
static void         _test_thread_limit(void);
inline static void  _update_cred_key(void);
static bool	    _verify_clustername(void);
static void	    _create_clustername_file(void);
static void         _update_nice(void);
inline static void  _usage(char *prog_name);
static bool         _valid_controller(void);
static bool         _wait_for_server_thread(void);

/* main - slurmctld main function, start various threads and process RPCs */
int main(int argc, char *argv[])
{
	int cnt, error_code, i;
	pthread_attr_t thread_attr;
	struct stat stat_buf;
	/* Locks: Write configuration, job, node, and partition */
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };
	slurm_trigger_callbacks_t callbacks;
	char *dir_name;
	bool create_clustername_file;
	/*
	 * Make sure we have no extra open files which
	 * would be propagated to spawned tasks.
	 */
	cnt = sysconf(_SC_OPEN_MAX);
	for (i = 3; i < cnt; i++)
		close(i);

	/*
	 * Establish initial configuration
	 */
	_init_config();
	slurm_conf_init(NULL);
	log_init(argv[0], log_opts, LOG_DAEMON, NULL);
	sched_log_init(argv[0], sched_log_opts, LOG_DAEMON, NULL);
	slurmctld_pid = getpid();
	_parse_commandline(argc, argv);
	init_locks();
	slurm_conf_reinit(slurm_conf_filename);

	update_logging();

	/* Verify clustername from conf matches value in spool dir
	 * exit if inconsistent to protect state files from corruption.
	 * This needs to be done before we kill the old one just incase we
	 * fail. */
	create_clustername_file = _verify_clustername();

	_update_nice();
	_kill_old_slurmctld();

	for (i = 0; i < 3; i++)
		fd_set_close_on_exec(i);

	if (daemonize) {
		slurmctld_config.daemonize = 1;
		if (daemon(1, 1))
			error("daemon(): %m");
		log_set_timefmt(slurmctld_conf.log_fmt);
		log_alter(log_opts, LOG_DAEMON,
			  slurmctld_conf.slurmctld_logfile);
		sched_log_alter(sched_log_opts, LOG_DAEMON,
				slurmctld_conf.sched_logfile);
		debug("sched: slurmctld starting");
	} else {
		slurmctld_config.daemonize = 0;
	}

	/*
	 * Need to create pidfile here in case we setuid() below
	 * (init_pidfile() exits if it can't initialize pid file).
	 * On Linux we also need to make this setuid job explicitly
	 * able to write a core dump.
	 * This also has to happen after daemon(), which closes all fd's,
	 * so we keep the write lock of the pidfile.
	 */
	_init_pidfile();
	_become_slurm_user();

	if (create_clustername_file)
		_create_clustername_file();

	if (daemonize)
		_set_work_dir();

	if (stat(slurmctld_conf.mail_prog, &stat_buf) != 0)
		error("Configured MailProg is invalid");

	if (!xstrcmp(slurmctld_conf.accounting_storage_type,
		     "accounting_storage/none")) {
		if (xstrcmp(slurmctld_conf.job_acct_gather_type,
			    "jobacct_gather/none"))
			error("Job accounting information gathered, "
			      "but not stored");
	} else {
		if (!xstrcmp(slurmctld_conf.job_acct_gather_type,
			     "jobacct_gather/none"))
			info("Job accounting information stored, "
			     "but details not gathered");
	}

	if (license_init(slurmctld_conf.licenses) != SLURM_SUCCESS)
		fatal("Invalid Licenses value: %s", slurmctld_conf.licenses);

#ifdef PR_SET_DUMPABLE
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#endif /* PR_SET_DUMPABLE */

	/*
	 * Create StateSaveLocation directory if necessary.
	 */
	set_slurmctld_state_loc();

	test_core_limit();
	_test_thread_limit();

	/* This must happen before we spawn any threads
	 * which are not designed to handle them */
	if (xsignal_block(controller_sigarray) < 0)
		error("Unable to block signals");

	/* This needs to be copied for other modules to access the
	 * memory, it will report 'HashBase' if it is not duped
	 */
	slurmctld_cluster_name = xstrdup(slurmctld_conf.cluster_name);
	association_based_accounting =
		slurm_get_is_association_based_accounting();
	accounting_enforce = slurmctld_conf.accounting_storage_enforce;
	if (!xstrcasecmp(slurmctld_conf.accounting_storage_type,
			 "accounting_storage/slurmdbd")) {
		with_slurmdbd = 1;
		/* we need job_list not to be NULL */
		init_job_conf();
	}

	if (accounting_enforce && !association_based_accounting) {
		slurm_ctl_conf_t *conf = slurm_conf_lock();
		conf->track_wckey = false;
		conf->accounting_storage_enforce = 0;
		accounting_enforce = 0;
		slurmctld_conf.track_wckey = false;
		slurmctld_conf.accounting_storage_enforce = 0;
		slurm_conf_unlock();

		error("You can not have AccountingStorageEnforce "
		      "set for AccountingStorageType='%s'",
		      slurmctld_conf.accounting_storage_type);
	}

	memset(&callbacks, 0, sizeof(slurm_trigger_callbacks_t));
	callbacks.acct_full   = trigger_primary_ctld_acct_full;
	callbacks.dbd_fail    = trigger_primary_dbd_fail;
	callbacks.dbd_resumed = trigger_primary_dbd_res_op;
	callbacks.db_fail     = trigger_primary_db_fail;
	callbacks.db_resumed  = trigger_primary_db_res_op;

	info("%s version %s started on cluster %s",
	     slurm_prog_name, SLURM_VERSION_STRING, slurmctld_cluster_name);

	if ((error_code = gethostname_short(node_name_short, MAX_SLURM_NAME)))
		fatal("getnodename_short error %s", slurm_strerror(error_code));
	if ((error_code = gethostname(node_name_long, MAX_SLURM_NAME)))
		fatal("getnodename error %s", slurm_strerror(error_code));

	/* init job credential stuff */
	slurmctld_config.cred_ctx = slurm_cred_creator_ctx_create(
			slurmctld_conf.job_credential_private_key);
	if (!slurmctld_config.cred_ctx) {
		fatal("slurm_cred_creator_ctx_create(%s): %m",
			slurmctld_conf.job_credential_private_key);
	}

	/* Must set before plugins are loaded. */
	if (slurmctld_conf.backup_controller &&
	    ((xstrcmp(node_name_short,slurmctld_conf.backup_controller) == 0) ||
	     (xstrcmp(node_name_long, slurmctld_conf.backup_controller) == 0))) {
#ifndef HAVE_ALPS_CRAY
		char *sched_params = NULL;
#endif
		slurmctld_primary = 0;

#ifdef HAVE_ALPS_CRAY
		slurmctld_config.scheduling_disabled = true;
#else
		sched_params = slurm_get_sched_params();
		if (sched_params &&
		    strstr(sched_params, "no_backup_scheduling"))
			slurmctld_config.scheduling_disabled = true;
		xfree(sched_params);
#endif
	}


	/* Not used in creator
	 *
	 * slurm_cred_ctx_set(slurmctld_config.cred_ctx,
	 *                    SLURM_CRED_OPT_EXPIRY_WINDOW, CRED_LIFE);
	 */

	/*
	 * Initialize plugins.
	 */
	if (gres_plugin_init() != SLURM_SUCCESS )
		fatal( "failed to initialize gres plugin" );
	if (slurm_select_init(1) != SLURM_SUCCESS )
		fatal( "failed to initialize node selection plugin" );
	if (slurm_preempt_init() != SLURM_SUCCESS )
		fatal( "failed to initialize preempt plugin" );
	if (checkpoint_init(slurmctld_conf.checkpoint_type) != SLURM_SUCCESS )
		fatal( "failed to initialize checkpoint plugin" );
	if (acct_gather_conf_init() != SLURM_SUCCESS )
		fatal( "failed to initialize acct_gather plugins" );
	if (jobacct_gather_init() != SLURM_SUCCESS )
		fatal( "failed to initialize jobacct_gather plugin");
	if (job_submit_plugin_init() != SLURM_SUCCESS )
		fatal( "failed to initialize job_submit plugin");
	if (ext_sensors_init() != SLURM_SUCCESS )
		fatal( "failed to initialize ext_sensors plugin");
	if (node_features_g_init() != SLURM_SUCCESS )
		fatal( "failed to initialize node_features plugin");	
	if (switch_g_slurmctld_init() != SLURM_SUCCESS )
		fatal( "failed to initialize switch plugin");
	config_power_mgr();
	if (node_features_g_node_power() && !power_save_test()) {
		fatal("PowerSave required with NodeFeatures plugin, "
		      "but not fully configured (SuspendProgram, "
		      "ResumeProgram and SuspendTime all required)");
	}

	while (1) {
		/* initialization for each primary<->backup switch */
		xfree(slurmctld_config.auth_info);
		slurmctld_config.auth_info = slurm_get_auth_info();
		slurmctld_config.shutdown_time = (time_t) 0;
		slurmctld_config.resume_backup = false;

		/* start in primary or backup mode */
		if (!slurmctld_primary) {
			slurm_sched_fini();	/* make sure shutdown */
			run_backup(&callbacks);
			if (slurm_acct_storage_init(NULL) != SLURM_SUCCESS )
				fatal("failed to initialize "
				      "accounting_storage plugin");
		} else if (_valid_controller()) {
			(void) _shutdown_backup_controller(SHUTDOWN_WAIT);
			trigger_primary_ctld_res_ctrl();
			ctld_assoc_mgr_init(&callbacks);
			if (slurm_acct_storage_init(NULL) != SLURM_SUCCESS )
				fatal("failed to initialize "
				      "accounting_storage plugin");
			/* Now recover the remaining state information */
			lock_slurmctld(config_write_lock);
			if (switch_g_restore(slurmctld_conf.state_save_location,
					   recover ? true : false))
				fatal(" failed to initialize switch plugin" );
			if ((error_code = read_slurm_conf(recover, false))) {
				fatal("read_slurm_conf reading %s: %s",
					slurmctld_conf.slurm_conf,
					slurm_strerror(error_code));
			}
			unlock_slurmctld(config_write_lock);
			select_g_select_nodeinfo_set_all();

			if (recover == 0) {
				slurmctld_init_db = 1;
				/* This needs to be set up the nodes
				   going down and this happens before it is
				   normally set up so do it now.
				*/
				set_cluster_tres(false);
				_accounting_mark_all_nodes_down("cold-start");
			}
		} else {
			error("this host (%s/%s) not a valid controller "
			      "(%s or %s)", node_name_short, node_name_long,
			      slurmctld_conf.control_machine,
			      slurmctld_conf.backup_controller);
			exit(0);
		}

		if (!acct_db_conn) {
			acct_db_conn = acct_storage_g_get_connection(
						&callbacks, 0, false,
						slurmctld_cluster_name);
			/* We only send in a variable the first time
			 * we call this since we are setting up static
			 * variables inside the function sending a
			 * NULL will just use those set before. */
			if (assoc_mgr_init(acct_db_conn, NULL, errno) &&
			    (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS) &&
			    !running_cache) {
				trigger_primary_dbd_fail();
				error("assoc_mgr_init failure");
				fatal("slurmdbd and/or database must be up at "
				      "slurmctld start time");
			}
		}

		info("Running as primary controller");
		if ((slurmctld_config.resume_backup == false) &&
		    (slurmctld_primary == 1)) {
			trigger_primary_ctld_res_op();
		}

		/*
		 * create attached thread to process RPCs
		 * Create before registering so that the controller can listen
		 * to any updates from the dbd at startup.
		 */
		server_thread_incr();
		slurm_attr_init(&thread_attr);
		while (pthread_create(&slurmctld_config.thread_id_rpc,
				      &thread_attr, _slurmctld_rpc_mgr,
				      NULL)) {
			error("pthread_create error %m");
			sleep(1);
		}
		slurm_attr_destroy(&thread_attr);

		clusteracct_storage_g_register_ctld(
			acct_db_conn,
			slurmctld_conf.slurmctld_port);

		_accounting_cluster_ready();

		if (slurm_priority_init() != SLURM_SUCCESS)
			fatal("failed to initialize priority plugin");
		if (slurm_sched_init() != SLURM_SUCCESS)
			fatal("failed to initialize scheduling plugin");
		if (slurmctld_plugstack_init())
			fatal("failed to initialize slurmctld_plugstack");
		if (bb_g_init() != SLURM_SUCCESS )
			fatal( "failed to initialize burst buffer plugin");
		if (power_g_init() != SLURM_SUCCESS )
			fatal( "failed to initialize power management plugin");
		if (slurm_mcs_init() != SLURM_SUCCESS)
			fatal("failed to initialize mcs plugin");

		/*
		 * create attached thread for signal handling
		 */
		slurm_attr_init(&thread_attr);
		while (pthread_create(&slurmctld_config.thread_id_sig,
				      &thread_attr, _slurmctld_signal_hand,
				      NULL)) {
			error("pthread_create %m");
			sleep(1);
		}
		slurm_attr_destroy(&thread_attr);

		/*
		 * create attached thread for state save
		 */
		slurm_attr_init(&thread_attr);
		while (pthread_create(&slurmctld_config.thread_id_save,
				      &thread_attr, slurmctld_state_save,
				      NULL)) {
			error("pthread_create %m");
			sleep(1);
		}
		slurm_attr_destroy(&thread_attr);

		/*
		 * create attached thread for node power management
  		 */
		start_power_mgr(&slurmctld_config.thread_id_power);

		/*
		 * process slurm background activities, could run as pthread
		 */
		_slurmctld_background(NULL);

		/* termination of controller */
		dir_name = slurm_get_state_save_location();
		switch_g_save(dir_name);
		xfree(dir_name);
		slurm_priority_fini();
		slurmctld_plugstack_fini();
		shutdown_state_save();
		pthread_join(slurmctld_config.thread_id_sig,  NULL);
		pthread_join(slurmctld_config.thread_id_rpc,  NULL);
		pthread_join(slurmctld_config.thread_id_save, NULL);
		slurmctld_config.thread_id_sig  = (pthread_t) 0;
		slurmctld_config.thread_id_rpc  = (pthread_t) 0;
		slurmctld_config.thread_id_save = (pthread_t) 0;
		bb_g_fini();
		power_g_fini();
		slurm_mcs_fini();

		if (running_cache) {
			/* break out and end the association cache
			 * thread since we are shutting down, no reason
			 * to wait for current info from the database */
			slurm_mutex_lock(&assoc_cache_mutex);
			running_cache = (uint16_t)NO_VAL;
			slurm_cond_signal(&assoc_cache_cond);
			slurm_mutex_unlock(&assoc_cache_mutex);
			pthread_join(assoc_cache_thread, NULL);
		}

		/* Save any pending state save RPCs */
		acct_storage_g_close_connection(&acct_db_conn);
		slurm_acct_storage_fini();

		/* join the power save thread after saving all state
		 * since it could wait a while waiting for spawned
		 * processes to exit */
		pthread_join(slurmctld_config.thread_id_power, NULL);

		if (slurmctld_config.resume_backup == false)
			break;

		/* primary controller doesn't resume backup mode */
		if ((slurmctld_config.resume_backup == true) &&
		    (slurmctld_primary == 1))
			break;

		recover = 2;
	}

	layouts_fini();
	g_slurm_jobcomp_fini();

	/* Since pidfile is created as user root (its owner is
	 *   changed to SlurmUser) SlurmUser may not be able to
	 *   remove it, so this is not necessarily an error. */
	if (unlink(slurmctld_conf.slurmctld_pidfile) < 0) {
		verbose("Unable to remove pidfile '%s': %m",
			slurmctld_conf.slurmctld_pidfile);
	}


#ifdef MEMORY_LEAK_DEBUG
{
	/* This should purge all allocated memory,   *\
	\*   Anything left over represents a leak.   */


	/* Give running agents a chance to complete and free memory.
	 * Wait up to 60 seconds. */
	for (i=0; i<60; i++) {
		agent_purge();
		usleep(100000);
		cnt = get_agent_count();
		if (cnt == 0)
			break;
	}
	if (cnt)
		error("Left %d agent threads active", cnt);

	slurm_sched_fini();	/* Stop all scheduling */

	/* Purge our local data structures */
	job_fini();
	part_fini();	/* part_fini() must precede node_fini() */
	node_fini();
	node_features_g_fini();
	purge_front_end_state();
	resv_fini();
	trigger_fini();
	dir_name = slurm_get_state_save_location();
	assoc_mgr_fini(dir_name);
	xfree(dir_name);
	reserve_port_config(NULL);
	free_rpc_stats();

	/* Some plugins are needed to purge job/node data structures,
	 * unplug after other data structures are purged */
	ext_sensors_fini();
	gres_plugin_fini();
	job_submit_plugin_fini();
	slurm_preempt_fini();
	jobacct_gather_fini();
	acct_gather_conf_destroy();
	slurm_select_fini();
	slurm_topo_fini();
	checkpoint_fini();
	slurm_auth_fini();
	switch_fini();
	route_fini();

	/* purge remaining data structures */
	license_free();
	slurm_cred_ctx_destroy(slurmctld_config.cred_ctx);
	slurm_crypto_fini();	/* must be after ctx_destroy */
	slurm_conf_destroy();
	slurm_api_clear_config();
	usleep(500000);
}
#else
	/* Give REQUEST_SHUTDOWN a chance to get propagated,
	 * up to 3 seconds. */
	for (i=0; i<30; i++) {
		agent_purge();
		cnt = get_agent_count();
		if (cnt == 0)
			break;
		usleep(100000);
	}

	/* do this outside of MEMORY_LEAD_DEBUG so that remote connections get
	 * closed. */
	fed_mgr_fini();

#ifdef HAVE_BG
	/* Always call slurm_select_fini() on some systems like
	   BlueGene we need to make sure other processes are ended
	   or we could get a random core from within it's
	   underlying infrastructure.
	*/
        slurm_select_fini();
#endif

#endif

	xfree(slurmctld_config.auth_info);
	xfree(slurmctld_cluster_name);
	if (cnt) {
		info("Slurmctld shutdown completing with %d active agent "
		     "thread", cnt);
	}
	log_fini();
	sched_log_fini();

	if (dump_core)
		abort();
	else
		exit(0);
}

/* initialization of common slurmctld configuration */
static void  _init_config(void)
{
	struct rlimit rlim;

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		(void) setrlimit(RLIMIT_NOFILE, &rlim);
	}
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
	slurmctld_config.boot_time      = time(NULL);
	slurmctld_config.daemonize      = DEFAULT_DAEMONIZE;
	slurmctld_config.resume_backup  = false;
	slurmctld_config.server_thread_count = 0;
	slurmctld_config.shutdown_time  = (time_t) 0;
	slurmctld_config.thread_id_main = pthread_self();
	slurmctld_config.scheduling_disabled = false;
	slurm_mutex_init(&slurmctld_config.thread_count_lock);
	slurmctld_config.thread_id_main    = (pthread_t) 0;
	slurmctld_config.thread_id_sig     = (pthread_t) 0;
	slurmctld_config.thread_id_rpc     = (pthread_t) 0;
}

/* Read configuration file.
 * Same name as API function for use in accounting_storage plugin.
 * Anything you add to this function must be added to the
 * _slurm_rpc_reconfigure_controller function inside proc_req.c try
 * to keep these in sync.
 */
static void _reconfigure_slurm(void)
{
	/* Locks: Write configuration, job, node, and partition */
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };
	int rc;

	if (slurmctld_config.shutdown_time)
		return;

	/*
	 * XXX - need to shut down the scheduler
	 * plugin, re-read the configuration, and then
	 * restart the (possibly new) plugin.
	 */
	lock_slurmctld(config_write_lock);
	rc = read_slurm_conf(2, true);
	if (rc)
		error("read_slurm_conf: %s", slurm_strerror(rc));
	else {
		_update_cred_key();
		set_slurmctld_state_loc();
	}
	slurm_sched_g_partition_change();	/* notify sched plugin */
	unlock_slurmctld(config_write_lock);
	assoc_mgr_set_missing_uids();
	acct_storage_g_reconfig(acct_db_conn, 0);
	start_power_mgr(&slurmctld_config.thread_id_power);
	trigger_reconfig();
	priority_g_reconfig(true);	/* notify priority plugin too */
	save_all_state();		/* Has own locking */
	queue_job_scheduler();
}

/* Request that the job scheduler execute soon (typically within seconds) */
extern void queue_job_scheduler(void)
{
	slurm_mutex_lock(&sched_cnt_mutex);
	job_sched_cnt++;
	slurm_mutex_unlock(&sched_cnt_mutex);
}

/* _slurmctld_signal_hand - Process daemon-wide signals */
static void *_slurmctld_signal_hand(void *no_data)
{
	int sig;
	int i, rc;
	int sig_array[] = {SIGINT, SIGTERM, SIGHUP, SIGABRT, 0};
	sigset_t set;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "sigmgr", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "sigmgr");
	}
#endif
	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	/* Make sure no required signals are ignored (possibly inherited) */
	for (i = 0; sig_array[i]; i++)
		_default_sigaction(sig_array[i]);
	while (1) {
		xsignal_sigset_create(sig_array, &set);
		rc = sigwait(&set, &sig);
		if (rc == EINTR)
			continue;
		switch (sig) {
		case SIGINT:	/* kill -2  or <CTRL-C> */
		case SIGTERM:	/* kill -15 */
			info("Terminate signal (SIGINT or SIGTERM) received");
			slurmctld_config.shutdown_time = time(NULL);
			slurmctld_shutdown();
			return NULL;	/* Normal termination */
			break;
		case SIGHUP:	/* kill -1 */
			info("Reconfigure signal (SIGHUP) received");
			_reconfigure_slurm();
			break;
		case SIGABRT:	/* abort */
			info("SIGABRT received");
			slurmctld_config.shutdown_time = time(NULL);
			slurmctld_shutdown();
			dump_core = true;
			return NULL;
		default:
			error("Invalid signal (%d) received", sig);
		}
	}

}

static void _default_sigaction(int sig)
{
	struct sigaction act;
	if (sigaction(sig, NULL, &act)) {
		error("sigaction(%d): %m", sig);
		return;
	}
	if (act.sa_handler != SIG_IGN)
		return;

	act.sa_handler = SIG_DFL;
	if (sigaction(sig, &act, NULL))
		error("sigaction(%d): %m", sig);
}

static void _sig_handler(int signal)
{
}

/* _slurmctld_rpc_mgr - Read incoming RPCs and create pthread for each */
static void *_slurmctld_rpc_mgr(void *no_data)
{
	int newsockfd;
	int *sockfd;	/* our set of socket file descriptors */
	slurm_addr_t cli_addr, srv_addr;
	uint16_t port;
	char ip[32];
	pthread_t thread_id_rpc_req;
	pthread_attr_t thread_attr_rpc_req;
	int no_thread;
	int fd_next = 0, i, nports;
	fd_set rfds;
	connection_arg_t *conn_arg = NULL;
	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	int sigarray[] = {SIGUSR1, 0};
	char* node_addr = NULL;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "rpcmgr", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "rpcmgr");
	}
#endif

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3("_slurmctld_rpc_mgr pid = %u", getpid());

	/* threads to process individual RPC's are detached */
	slurm_attr_init(&thread_attr_rpc_req);
	if (pthread_attr_setdetachstate
	    (&thread_attr_rpc_req, PTHREAD_CREATE_DETACHED))
		fatal("pthread_attr_setdetachstate %m");

	/* set node_addr to bind to (NULL means any) */
	if (slurmctld_conf.backup_controller && slurmctld_conf.backup_addr &&
	    ((xstrcmp(node_name_short,slurmctld_conf.backup_controller) == 0) ||
	     (xstrcmp(node_name_long, slurmctld_conf.backup_controller) == 0))&&
	    (xstrcmp(slurmctld_conf.backup_controller,
		     slurmctld_conf.backup_addr) != 0)) {
		node_addr = slurmctld_conf.backup_addr ;
	}
	else if (_valid_controller() &&
		 xstrcmp(slurmctld_conf.control_machine,
			 slurmctld_conf.control_addr)) {
		node_addr = slurmctld_conf.control_addr ;
	}

	/* initialize ports for RPCs */
	lock_slurmctld(config_read_lock);
	nports = slurmctld_conf.slurmctld_port_count;
	if (nports == 0) {
		fatal("slurmctld port count is zero");
		return NULL;	/* Fix CLANG false positive */
	}
	sockfd = xmalloc(sizeof(int) * nports);
	for (i=0; i<nports; i++) {
		sockfd[i] = slurm_init_msg_engine_addrname_port(
					node_addr,
					slurmctld_conf.slurmctld_port+i);
		if (sockfd[i] == SLURM_SOCKET_ERROR) {
			fatal("slurm_init_msg_engine_addrname_port error %m");
			return NULL;	/* Fix CLANG false positive */
		}
		fd_set_close_on_exec(sockfd[i]);
		slurm_get_stream_addr(sockfd[i], &srv_addr);
		slurm_get_ip_str(&srv_addr, &port, ip, sizeof(ip));
		debug2("slurmctld listening on %s:%d", ip, ntohs(port));
	}
	unlock_slurmctld(config_read_lock);

	/* Prepare to catch SIGUSR1 to interrupt accept().
	 * This signal is generated by the slurmctld signal
	 * handler thread upon receipt of SIGABRT, SIGINT,
	 * or SIGTERM. That thread does all processing of
	 * all signals. */
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sigarray);

	/*
	 * Process incoming RPCs until told to shutdown
	 */
	while (_wait_for_server_thread()) {
		int max_fd = -1;
		FD_ZERO(&rfds);
		for (i=0; i<nports; i++) {
			FD_SET(sockfd[i], &rfds);
			max_fd = MAX(sockfd[i], max_fd);
		}
		if (select(max_fd+1, &rfds, NULL, NULL, NULL) == -1) {
			if (errno != EINTR)
				error("slurm_accept_msg_conn select: %m");
			server_thread_decr();
			continue;
		}
		/* find one to process */
		for (i=0; i<nports; i++) {
			if (FD_ISSET(sockfd[(fd_next+i) % nports], &rfds)) {
				i = (fd_next + i) % nports;
				break;
			}
		}
		fd_next = (i + 1) % nports;

		/*
		 * accept needed for stream implementation is a no-op in
		 * message implementation that just passes sockfd to newsockfd
		 */
		if ((newsockfd = slurm_accept_msg_conn(sockfd[i],
						       &cli_addr)) ==
		    SLURM_SOCKET_ERROR) {
			if (errno != EINTR)
				error("slurm_accept_msg_conn: %m");
			server_thread_decr();
			continue;
		}
		fd_set_close_on_exec(newsockfd);
		conn_arg = xmalloc(sizeof(connection_arg_t));
		conn_arg->newsockfd = newsockfd;
		memcpy(&conn_arg->cli_addr, &cli_addr, sizeof(slurm_addr_t));

		if (slurmctld_conf.debug_flags & DEBUG_FLAG_PROTOCOL) {
			char inetbuf[64];

			slurm_print_slurm_addr(&cli_addr,
						inetbuf,
						sizeof(inetbuf));
			info("%s: accept() connection from %s", __func__, inetbuf);
		}

		if (slurmctld_config.shutdown_time)
			no_thread = 1;
		else if (pthread_create(&thread_id_rpc_req,
					&thread_attr_rpc_req,
					_service_connection,
					(void *) conn_arg)) {
			error("pthread_create: %m");
			no_thread = 1;
		} else
			no_thread = 0;

		if (no_thread) {
			slurmctld_diag_stats.proc_req_raw++;
		       	_service_connection((void *) conn_arg);
	       	}
	}

	debug3("_slurmctld_rpc_mgr shutting down");
	slurm_attr_destroy(&thread_attr_rpc_req);
	for (i=0; i<nports; i++)
		(void) slurm_shutdown_msg_engine(sockfd[i]);
	xfree(sockfd);
	server_thread_decr();
	pthread_exit((void *) 0);
	return NULL;
}

/*
 * _service_connection - service the RPC
 * IN/OUT arg - really just the connection's file descriptor, freed
 *	upon completion
 * RET - NULL
 */
static void *_service_connection(void *arg)
{
	connection_arg_t *conn = (connection_arg_t *) arg;
	void *return_code = NULL;
	slurm_msg_t *msg = xmalloc(sizeof(slurm_msg_t));

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "srvcn", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "srvcn");
	}
#endif
	while (1) {
		if (slurmctld_config.shutdown_time) {
			break;
		}
		slurm_msg_t_init(msg);
		/*
		 * slurm_receive_msg sets msg connection fd to accepted fd. This allows
		 * possibility for slurmctld_req() to close accepted connection.
		 */
		if (slurm_receive_msg(conn->newsockfd, msg, 0) != 0) {
			char addr_buf[32];
			slurm_print_slurm_addr(&conn->cli_addr, addr_buf,
					       sizeof(addr_buf));
			error("slurm_receive_msg [%s]: %m", addr_buf);
			/* close the new socket */
			slurm_close(conn->newsockfd);
			goto cleanup;
		}

		if (errno != SLURM_SUCCESS) {
			if (errno == SLURM_PROTOCOL_VERSION_ERROR) {
				slurm_send_rc_msg(msg, SLURM_PROTOCOL_VERSION_ERROR);
			} else
				info("_service_connection/slurm_receive_msg %m");
		} else {
			/* process the request */
			slurmctld_req(msg, conn);
		}

		if (!conn->persist)
			break;
	}

	if ((conn->newsockfd >= 0)
	    && slurm_close(conn->newsockfd) < 0)
		error ("close(%d): %m",  conn->newsockfd);

cleanup:
	debug2("exiting service connection thread");
	slurm_free_msg(msg);
	xfree(arg);
	server_thread_decr();
	return return_code;
}

/* Increment slurmctld_config.server_thread_count and don't return
 * until its value is no larger than MAX_SERVER_THREADS,
 * RET true unless shutdown in progress */
static bool _wait_for_server_thread(void)
{
	bool print_it = true;
	bool rc = true;

	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	while (1) {
		if (slurmctld_config.shutdown_time) {
			rc = false;
			break;
		}
		if (slurmctld_config.server_thread_count < max_server_threads) {
			slurmctld_config.server_thread_count++;
			break;
		} else {
			/* wait for state change and retry,
			 * just a delay and not an error.
			 * This can happen when the epilog completes
			 * on a bunch of nodes at the same time, which
			 * can easily happen for highly parallel jobs. */
			if (print_it) {
				static time_t last_print_time = 0;
				time_t now = time(NULL);
				if (difftime(now, last_print_time) > 2) {
					verbose("server_thread_count over "
						"limit (%d), waiting",
						slurmctld_config.
						server_thread_count);
					last_print_time = now;
				}
				print_it = false;
			}
			slurm_cond_wait(&server_thread_cond,
					&slurmctld_config.thread_count_lock);
		}
	}
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
	return rc;
}

/* Decrement slurmctld thread count (as applies to thread limit) */
extern void server_thread_decr(void)
{
	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	if (slurmctld_config.server_thread_count > 0)
		slurmctld_config.server_thread_count--;
	else
		error("slurmctld_config.server_thread_count underflow");
	slurm_cond_broadcast(&server_thread_cond);
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
	int rc = SLURM_ERROR;
	time_t event_time = time(NULL);
	bitstr_t *total_node_bitmap = NULL;
	char *cluster_nodes = NULL, *cluster_tres_str;
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, WRITE_LOCK };
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld(node_write_lock);
	/* Now get the names of all the nodes on the cluster at this
	   time and send it also.
	*/
	total_node_bitmap = bit_alloc(node_record_count);
	bit_nset(total_node_bitmap, 0, node_record_count-1);
	cluster_nodes = bitmap2node_name_sortable(total_node_bitmap, 0);
	FREE_NULL_BITMAP(total_node_bitmap);

	assoc_mgr_lock(&locks);

	set_cluster_tres(true);

	cluster_tres_str = slurmdb_make_tres_string(
		assoc_mgr_tres_list, TRES_STR_FLAG_SIMPLE);
	assoc_mgr_unlock(&locks);

	unlock_slurmctld(node_write_lock);

	rc = clusteracct_storage_g_cluster_tres(acct_db_conn,
						cluster_nodes,
						cluster_tres_str, event_time);

	xfree(cluster_nodes);
	xfree(cluster_tres_str);

	if (rc == ACCOUNTING_FIRST_REG) {
		/* see if we are running directly to a database
		 * instead of a slurmdbd.
		 */
		send_all_to_accounting(event_time);
		rc = SLURM_SUCCESS;
	}

	return rc;
}

static int _accounting_mark_all_nodes_down(char *reason)
{
	char *state_file;
	struct stat stat_buf;
	struct node_record *node_ptr;
	int i;
	time_t event_time;
	int rc = SLURM_ERROR;

	state_file = slurm_get_state_save_location();
	xstrcat (state_file, "/node_state");
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

	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (node_ptr->name == '\0')
			continue;
		if ((rc = clusteracct_storage_g_node_down(
			    acct_db_conn,
			    node_ptr, event_time,
			    reason, slurm_get_slurm_user_id()))
		   == SLURM_ERROR)
			break;
	}
	return rc;
}

static void _remove_assoc(slurmdb_assoc_rec_t *rec)
{
	int cnt = 0;

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
	ListIterator itr;
	struct part_record *part_ptr;
	slurmctld_lock_t part_write_lock =
		{ NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK };

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
	}
	unlock_slurmctld(part_write_lock);

	cnt = job_hold_by_qos_id(rec->id);

	if (cnt) {
		info("Removed QOS:%s held %u jobs", rec->name, cnt);
	} else
		debug("Removed QOS:%s", rec->name);
}

static void _update_assoc(slurmdb_assoc_rec_t *rec)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list || !accounting_enforce
	    || !(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return;

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if ((rec != job_ptr->assoc_ptr) || (!IS_JOB_PENDING(job_ptr)))
			continue;

		acct_policy_update_pending_job(job_ptr);
	}
	list_iterator_destroy(job_iterator);
	unlock_slurmctld(job_write_lock);
}

static void _update_qos(slurmdb_qos_rec_t *rec)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list || !accounting_enforce
	    || !(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return;

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if ((rec != job_ptr->qos_ptr) || (!IS_JOB_PENDING(job_ptr)))
			continue;

		acct_policy_update_pending_job(job_ptr);
	}
	list_iterator_destroy(job_iterator);
	unlock_slurmctld(job_write_lock);
}

static int _init_tres(void)
{
	char *temp_char = slurm_get_accounting_storage_tres();
	List char_list;
	List add_list = NULL;
	slurmdb_tres_rec_t *tres_rec;
	slurmdb_update_object_t update_object;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	if (!temp_char) {
		error("No tres defined, this should never happen");
		return SLURM_ERROR;
	}

	char_list = list_create(slurm_destroy_char);
	slurm_addto_char_list(char_list, temp_char);
	xfree(temp_char);

	memset(&update_object, 0, sizeof(slurmdb_update_object_t));
	if (!association_based_accounting) {
		update_object.type = SLURMDB_ADD_TRES;
		update_object.objects = list_create(slurmdb_destroy_tres_rec);
	} else if (!g_tres_count)
		fatal("You are running with a database but for some reason "
		      "we have no TRES from it.  This should only happen if "
		      "the database is down and you don't have "
		      "any state files.");

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
		else if (!strncasecmp(temp_char, "bb/", 3)) {
			tres_rec->type[2] = '\0';
			tres_rec->name = xstrdup(temp_char+3);
			if (!tres_rec->name)
				fatal("Burst Buffer type tres need to have a "
				      "name, (i.e. bb/cray).  You gave %s",
				      temp_char);
		} else if (!strncasecmp(temp_char, "gres/", 5)) {
			tres_rec->type[4] = '\0';
			tres_rec->name = xstrdup(temp_char+5);
			if (!tres_rec->name)
				fatal("Gres type tres need to have a name, "
				      "(i.e. Gres/GPU).  You gave %s",
				      temp_char);
		} else if (!strncasecmp(temp_char, "license/", 8)) {
			tres_rec->type[7] = '\0';
			tres_rec->name = xstrdup(temp_char+8);
			if (!tres_rec->name)
				fatal("License type tres need to "
				      "have a name, (i.e. License/Foo).  "
				      "You gave %s",
				      temp_char);
		} else {
			fatal("%s: Unknown tres type '%s', acceptable "
			      "types are CPU,Gres/,License/,Mem",
			      __func__, temp_char);
			xfree(tres_rec->type);
			xfree(tres_rec);
		}

		if (!association_based_accounting) {
			if (!tres_rec->id)
				fatal("Unless running with a database you "
				      "can only run with certain TRES, "
				      "%s%s%s is not one of them.  "
				      "Either set up "
				      "a database preferably with a slurmdbd "
				      "or remove this TRES from your "
				      "configuration.",
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
		if (acct_storage_g_add_tres(acct_db_conn, getuid(), add_list)
		    != SLURM_SUCCESS)
			fatal("Problem adding tres to the database, "
			      "can't continue until database is able to "
			      "make new tres");
		/* refresh list here since the updates are not
		   sent dynamically */
		assoc_mgr_refresh_lists(acct_db_conn, ASSOC_MGR_CACHE_TRES);
		FREE_NULL_LIST(add_list);
	}

	if (!association_based_accounting) {
		assoc_mgr_update_tres(&update_object, false);
		list_destroy(update_object.objects);
	}

	/* Set up the slurmctld_tres_cnt here (Current code is set to
	 * not have this ever change).
	*/
	assoc_mgr_lock(&locks);
	slurmctld_tres_cnt = g_tres_count;
	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/* any association manager locks should be unlocked before hand */
static void _update_cluster_tres(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list)
		return;

	lock_slurmctld(job_write_lock);
	assoc_mgr_lock(&locks);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		/* If this returns 1 it means the positions were
		   altered so just rebuild it.
		*/
		if (assoc_mgr_set_tres_cnt_array(&job_ptr->tres_req_cnt,
						 job_ptr->tres_req_str,
						 0, true))
			job_set_req_tres(job_ptr, true);
		if (assoc_mgr_set_tres_cnt_array(&job_ptr->tres_alloc_cnt,
						 job_ptr->tres_alloc_str,
						 0, true))
			job_set_alloc_tres(job_ptr, true);
	}
	list_iterator_destroy(job_iterator);

	assoc_mgr_unlock(&locks);
	unlock_slurmctld(job_write_lock);
}


static void _queue_reboot_msg(void)
{
	agent_arg_t *reboot_agent_args = NULL;
	struct node_record *node_ptr;
	char *host_str;
	time_t now = time(NULL);
	int i;
	bool want_reboot;

	want_nodes_reboot = false;
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		if (!IS_NODE_MAINT(node_ptr))
			continue;
		if (is_node_in_maint_reservation(i)) {
			/* defer if node isn't in reservation */
			want_nodes_reboot = true;
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
                    && !IS_NODE_POWER_UP(node_ptr)
                    && node_ptr->sus_job_cnt == 0)
			want_reboot = true;
		else if (IS_NODE_FUTURE(node_ptr) &&
			 (node_ptr->last_response == (time_t) 0))
			want_reboot = true; /* system just restarted */
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
		node_ptr->node_state &= ~NODE_STATE_MAINT;
		node_ptr->node_state &=  NODE_STATE_FLAGS;
		node_ptr->node_state |=  NODE_STATE_DOWN;
		node_ptr->reason = xstrdup("Scheduled reboot");
		bit_clear(avail_node_bitmap, i);
		bit_clear(idle_node_bitmap, i);
		node_ptr->last_response = now + slurm_get_resume_timeout();
	}
	if (reboot_agent_args != NULL) {
		hostlist_uniq(reboot_agent_args->hostlist);
		host_str = hostlist_ranged_string_xmalloc(
				reboot_agent_args->hostlist);
		debug("Queuing reboot request for nodes %s", host_str);
		xfree(host_str);
		agent_queue_request(reboot_agent_args);
		last_node_update = now;
		schedule_node_save();
	}
}

/*
 * _slurmctld_background - process slurmctld background activities
 *	purge defunct job records, save state, schedule jobs, and
 *	ping other nodes
 */
static void *_slurmctld_background(void *no_data)
{
	static time_t last_sched_time;
	static time_t last_full_sched_time;
	static time_t last_checkpoint_time;
	static time_t last_group_time;
	static time_t last_health_check_time;
	static time_t last_acct_gather_node_time;
	static time_t last_ext_sensors_time;
	static time_t last_no_resp_msg_time;
	static time_t last_ping_node_time;
	static time_t last_ping_srun_time;
	static time_t last_purge_job_time;
	static time_t last_resv_time;
	static time_t last_timelimit_time;
	static time_t last_assert_primary_time;
	static time_t last_trigger;
	static time_t last_node_acct;
	static time_t last_ctld_bu_ping;
	static time_t last_uid_update;
	static time_t last_reboot_msg_time;
	time_t now;
	int no_resp_msg_interval, ping_interval, purge_job_interval;
	int group_time, group_force;
	int i;
	uint32_t job_limit;
	DEF_TIMERS;

	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Read config, read job */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock2 = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Read config, write job, write node
	 * (Might kill jobs on nodes set DOWN) */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	/* Locks: Write node */
	slurmctld_lock_t node_write_lock2 = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };
	/* Locks: Write partition */
	slurmctld_lock_t part_write_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK };
	/* Locks: Read job and node */
	slurmctld_lock_t job_node_read_lock = {
		NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };

	/* Let the dust settle before doing work */
	now = time(NULL);
	last_sched_time = last_full_sched_time = now;
	last_checkpoint_time = last_group_time = now;
	last_purge_job_time = last_trigger = last_health_check_time = now;
	last_timelimit_time = last_assert_primary_time = now;
	last_no_resp_msg_time = last_resv_time = last_ctld_bu_ping = now;
	last_uid_update = last_reboot_msg_time = now;
	last_acct_gather_node_time = last_ext_sensors_time = now;

	if ((slurmctld_conf.min_job_age > 0) &&
	    (slurmctld_conf.min_job_age < PURGE_JOB_INTERVAL)) {
		/* Purge jobs more quickly, especially for high job flow */
		purge_job_interval = MAX(10, slurmctld_conf.min_job_age);
	} else
		purge_job_interval = PURGE_JOB_INTERVAL;

	if (slurmctld_conf.slurmd_timeout) {
		/* We ping nodes that haven't responded in SlurmdTimeout/3,
		 * but need to do the test at a higher frequency or we might
		 * DOWN nodes with times that fall in the gap. */
		ping_interval = slurmctld_conf.slurmd_timeout / 3;
	} else {
		/* This will just ping non-responding nodes
		 * and restore them to service */
		ping_interval = 100;	/* 100 seconds */
	}
	last_ping_node_time = now + (time_t)MIN_CHECKIN_TIME - ping_interval;
	last_ping_srun_time = now;
	last_node_acct = now;
	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3("_slurmctld_background pid = %u", getpid());

	while (1) {
		for (i = 0; ((i < 10) && (slurmctld_config.shutdown_time == 0));
		     i++) {
			usleep(100000);
		}

		now = time(NULL);
		START_TIMER;

		if (slurmctld_conf.slurmctld_debug <= 3)
			no_resp_msg_interval = 300;
		else if (slurmctld_conf.slurmctld_debug == 4)
			no_resp_msg_interval = 60;
		else
			no_resp_msg_interval = 1;

		if (slurmctld_config.shutdown_time) {
			struct timespec ts = {0, 0};
			struct timeval now;
			/* wait for RPC's to complete */
			gettimeofday(&now, NULL);
			ts.tv_sec = now.tv_sec + CONTROL_TIMEOUT;
			ts.tv_nsec = now.tv_usec * 1000;
			slurm_mutex_lock(&slurmctld_config.thread_count_lock);
			while (slurmctld_config.server_thread_count > 0) {
				slurm_cond_timedwait(&server_thread_cond,
					&slurmctld_config.thread_count_lock,
					&ts);
			}
			if (slurmctld_config.server_thread_count) {
				info("shutdown server_thread_count=%d",
					slurmctld_config.server_thread_count);
			}
			slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

			if (_report_locks_set() == 0) {
				info("Saving all slurm state");
				save_all_state();
			} else {
				error("Semaphores still set after %d seconds, "
				      "can not save state", CONTROL_TIMEOUT);
			}
			break;
		}

		if (difftime(now, last_resv_time) >= 2) {
			now = time(NULL);
			last_resv_time = now;
			lock_slurmctld(node_write_lock);
			if (set_node_maint_mode(false) > 0)
				queue_job_scheduler();
			unlock_slurmctld(node_write_lock);
		}

		if (difftime(now, last_no_resp_msg_time) >=
		    no_resp_msg_interval) {
			now = time(NULL);
			last_no_resp_msg_time = now;
			lock_slurmctld(node_write_lock2);
			node_no_resp_msg();
			unlock_slurmctld(node_write_lock2);
		}

		if (difftime(now, last_timelimit_time) >= PERIODIC_TIMEOUT) {
			now = time(NULL);
			last_timelimit_time = now;
			debug2("Testing job time limits and checkpoints");
			lock_slurmctld(job_write_lock);
			job_time_limit();
			step_checkpoint();
			unlock_slurmctld(job_write_lock);
		}

		if (slurmctld_conf.health_check_interval &&
		    (difftime(now, last_health_check_time) >=
		     slurmctld_conf.health_check_interval) &&
		    is_ping_done()) {
			if (slurmctld_conf.health_check_node_state &
			     HEALTH_CHECK_CYCLE) {
				/* Call run_health_check() on each cycle */
			} else {
				now = time(NULL);
				last_health_check_time = now;
			}
			lock_slurmctld(node_write_lock);
			run_health_check();
			unlock_slurmctld(node_write_lock);
		}

		if (slurmctld_conf.acct_gather_node_freq &&
		    (difftime(now, last_acct_gather_node_time) >=
		     slurmctld_conf.acct_gather_node_freq) &&
		    is_ping_done()) {
			now = time(NULL);
			last_acct_gather_node_time = now;
			lock_slurmctld(node_write_lock);
			update_nodes_acct_gather_data();
			unlock_slurmctld(node_write_lock);
		}

		if (slurmctld_conf.ext_sensors_freq &&
		    (difftime(now, last_ext_sensors_time) >=
		     slurmctld_conf.ext_sensors_freq) &&
		    is_ping_done()) {
			now = time(NULL);
			last_ext_sensors_time = now;
			lock_slurmctld(node_write_lock);
			ext_sensors_g_update_component_data();
			unlock_slurmctld(node_write_lock);
		}

		if (((difftime(now, last_ping_node_time) >= ping_interval) ||
		     ping_nodes_now) && is_ping_done()) {
			now = time(NULL);
			last_ping_node_time = now;
			ping_nodes_now = false;
			lock_slurmctld(node_write_lock);
			ping_nodes();
			unlock_slurmctld(node_write_lock);
		}

		if (slurmctld_conf.inactive_limit &&
		    (difftime(now, last_ping_srun_time) >=
		     (slurmctld_conf.inactive_limit / 3))) {
			now = time(NULL);
			last_ping_srun_time = now;
			debug2("Performing srun ping");
			lock_slurmctld(job_read_lock);
			srun_ping();
			unlock_slurmctld(job_read_lock);
		}

		if (want_nodes_reboot && (now > last_reboot_msg_time)) {
			now = time(NULL);
			last_reboot_msg_time = now;
			lock_slurmctld(node_write_lock);
			_queue_reboot_msg();
			unlock_slurmctld(node_write_lock);
		}

		/* Process any pending agent work */
		agent_retry(RPC_RETRY_INTERVAL, true);

		group_time  = slurmctld_conf.group_info & GROUP_TIME_MASK;
		if (group_time &&
		    (difftime(now, last_group_time) >= group_time)) {
			if (slurmctld_conf.group_info & GROUP_FORCE)
				group_force = 1;
			else
				group_force = 0;
			now = time(NULL);
			last_group_time = now;
			lock_slurmctld(part_write_lock);
			load_part_uid_allow_list(group_force);
			unlock_slurmctld(part_write_lock);
		}

		if (difftime(now, last_purge_job_time) >= purge_job_interval) {
			now = time(NULL);
			last_purge_job_time = now;
			debug2("Performing purge of old job records");
			lock_slurmctld(job_write_lock);
			purge_old_job();
			unlock_slurmctld(job_write_lock);
		}

		job_limit = NO_VAL;
		if (difftime(now, last_full_sched_time) >= sched_interval) {
			slurm_mutex_lock(&sched_cnt_mutex);
			/* job_limit = job_sched_cnt;	Ignored */
			job_limit = INFINITE;
			job_sched_cnt = 0;
			slurm_mutex_unlock(&sched_cnt_mutex);
			last_full_sched_time = now;
		} else {
			slurm_mutex_lock(&sched_cnt_mutex);
			if (job_sched_cnt &&
			    (difftime(now, last_sched_time) >=
			     batch_sched_delay)) {
				job_limit = 0;	/* Default depth */
				job_sched_cnt = 0;
			}
			slurm_mutex_unlock(&sched_cnt_mutex);
		}
		if (job_limit != NO_VAL) {
			now = time(NULL);
			last_sched_time = now;
			lock_slurmctld(job_write_lock2);
			bb_g_load_state(false);	/* May alter job nice/prio */
			unlock_slurmctld(job_write_lock2);
			if (schedule(job_limit))
				last_checkpoint_time = 0; /* force state save */
			set_job_elig_time();
		}

		if (slurmctld_conf.slurmctld_timeout &&
		    (difftime(now, last_ctld_bu_ping) >
		     slurmctld_conf.slurmctld_timeout)) {
			if (_ping_backup_controller() != SLURM_SUCCESS)
				trigger_backup_ctld_fail();
			last_ctld_bu_ping = now;
		}

		if (difftime(now, last_trigger) > TRIGGER_INTERVAL) {
			now = time(NULL);
			last_trigger = now;
			lock_slurmctld(job_node_read_lock);
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

		/* Stats will reset at midnight (approx) local time. */
		if (last_proc_req_start == 0) {
			last_proc_req_start = now;
			next_stats_reset = now - (now % 86400) + 86400;
		} else if (now >= next_stats_reset) {
			next_stats_reset = now - (now % 86400) + 86400;
			reset_stats(0);
		}

		/* Reassert this machine as the primary controller.
		 * A network or security problem could result in
		 * the backup controller assuming control even
		 * while the real primary controller is running */
		lock_slurmctld(config_read_lock);
		if (slurmctld_conf.slurmctld_timeout   &&
		    slurmctld_conf.backup_addr         &&
		    slurmctld_conf.backup_addr[0]      &&
		    (difftime(now, last_assert_primary_time) >=
		     slurmctld_conf.slurmctld_timeout) &&
		    slurmctld_conf.backup_controller &&
		    xstrcmp(node_name_short,slurmctld_conf.backup_controller) &&
		    xstrcmp(node_name_long, slurmctld_conf.backup_controller)) {
			now = time(NULL);
			last_assert_primary_time = now;
			(void) _shutdown_backup_controller(0);
		}
		unlock_slurmctld(config_read_lock);

		if (difftime(now, last_uid_update) >= 3600) {
			/* Make sure we update the uids in the
			 * assoc_mgr if there were any users
			 * with unknown uids at the time of startup.
			 */
			now = time(NULL);
			last_uid_update = now;
			assoc_mgr_set_missing_uids();
		}

		END_TIMER2("_slurmctld_background");
	}

	debug3("_slurmctld_background shutting down");

	return NULL;
}

/* save_all_state - save entire slurmctld state for later recovery */
extern void save_all_state(void)
{
	char *save_loc;

	/* Each of these functions lock their own databases */
	schedule_front_end_save();
	schedule_job_save();
	schedule_node_save();
	schedule_part_save();
	schedule_resv_save();
	schedule_trigger_save();

	if ((save_loc = slurm_get_state_save_location())) {
		select_g_state_save(save_loc);
		dump_assoc_mgr_state(save_loc);
		xfree(save_loc);
	}
}

/* make sure the assoc_mgr is up and running with the most current state */
extern void ctld_assoc_mgr_init(slurm_trigger_callbacks_t *callbacks)
{
	assoc_init_args_t assoc_init_arg;
	int num_jobs = 0;
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	memset(&assoc_init_arg, 0, sizeof(assoc_init_args_t));
	assoc_init_arg.enforce = accounting_enforce;
	assoc_init_arg.add_license_notify = license_add_remote;
	assoc_init_arg.remove_assoc_notify = _remove_assoc;
	assoc_init_arg.remove_license_notify = license_remove_remote;
	assoc_init_arg.remove_qos_notify = _remove_qos;
	assoc_init_arg.sync_license_notify = license_sync_remote;
	assoc_init_arg.update_assoc_notify = _update_assoc;
	assoc_init_arg.update_license_notify = license_update_remote;
	assoc_init_arg.update_qos_notify = _update_qos;
	assoc_init_arg.update_cluster_tres = _update_cluster_tres;
	assoc_init_arg.update_resvs = update_assocs_in_resvs;
	assoc_init_arg.cache_level = ASSOC_MGR_CACHE_ASSOC |
				     ASSOC_MGR_CACHE_USER  |
				     ASSOC_MGR_CACHE_QOS   |
				     ASSOC_MGR_CACHE_RES   |
                         	     ASSOC_MGR_CACHE_TRES;
	if (slurmctld_conf.track_wckey)
		assoc_init_arg.cache_level |= ASSOC_MGR_CACHE_WCKEY;

	/* Don't save state but blow away old lists if they exist. */
	assoc_mgr_fini(NULL);

	if (acct_db_conn)
		acct_storage_g_close_connection(&acct_db_conn);

	acct_db_conn = acct_storage_g_get_connection(callbacks, 0, false,
						     slurmctld_cluster_name);

	if (assoc_mgr_init(acct_db_conn, &assoc_init_arg, errno)) {
		if (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)
			error("Association database appears down, "
			      "reading from state file.");
		else
			debug("Association database appears down, "
			      "reading from state file.");

		if ((load_assoc_mgr_state(slurmctld_conf.state_save_location)
		     != SLURM_SUCCESS)
		    && (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
			error("Unable to get any information from "
			      "the state file");
			fatal("slurmdbd and/or database must be up at "
			      "slurmctld start time");
		}
	}

	/* Now load the usage from a flat file since it isn't kept in
	   the database No need to check for an error since if this
	   fails we will get an error message and we will go on our
	   way.  If we get an error we can't do anything about it.
	*/
	load_assoc_usage(slurmctld_conf.state_save_location);
	load_qos_usage(slurmctld_conf.state_save_location);

	lock_slurmctld(job_read_lock);
	if (job_list)
		num_jobs = list_count(job_list);
	unlock_slurmctld(job_read_lock);

	_init_tres();

	/* This thread is looking for when we get correct data from
	   the database so we can update the assoc_ptr's in the jobs
	*/
	if (running_cache || num_jobs) {
		pthread_attr_t thread_attr;

		slurm_attr_init(&thread_attr);
		while (pthread_create(&assoc_cache_thread, &thread_attr,
				      _assoc_cache_mgr, NULL)) {
			error("pthread_create error %m");
			sleep(1);
		}
		slurm_attr_destroy(&thread_attr);
	}

}

/* send all info for the controller to accounting */
extern void send_all_to_accounting(time_t event_time)
{
	/* ignore the rcs here because if there was an error we will
	   push the requests on the queue and process them when the
	   database server comes back up.
	*/
	debug2("send_all_to_accounting: called");
	send_jobs_to_accounting();
	send_nodes_to_accounting(event_time);
	send_resvs_to_accounting();
}

static int _add_node_gres_tres(void *x, void *arg)
{
	uint64_t gres_cnt;
	int tres_pos;
	slurmdb_tres_rec_t *tres_rec_in = (slurmdb_tres_rec_t *)x;
	struct node_record *node_ptr = (struct node_record *)arg;

	xassert(tres_rec_in);

	if (xstrcmp(tres_rec_in->type, "gres"))
		return 0;

	gres_cnt = gres_plugin_node_config_cnt(node_ptr->gres_list,
					       tres_rec_in->name);
	if ((tres_pos = assoc_mgr_find_tres_pos(tres_rec_in, true)) != -1)
		node_ptr->tres_cnt[tres_pos] = gres_cnt;

	return 0;
}

/* A slurmctld lock needs to at least have a node read lock set before
 * this is called */
extern void set_cluster_tres(bool assoc_mgr_locked)
{
	struct node_record *node_ptr;
	slurmdb_tres_rec_t *tres_rec, *cpu_tres = NULL, *mem_tres = NULL;
	int i;
	char *unique_tres = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK };

	if (!assoc_mgr_locked)
		assoc_mgr_lock(&locks);

	xassert(assoc_mgr_tres_array);

	for (i=0; i < g_tres_count; i++) {
		tres_rec = assoc_mgr_tres_array[i];

		if (!tres_rec->type) {
			error("TRES %d doesn't have a type given, "
			      "this should never happen",
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
			tres_rec->count = gres_get_system_cnt(tres_rec->name);
			continue;
		} else if (!xstrcmp(tres_rec->type, "license")) {
			tres_rec->count = get_total_license_cnt(
				tres_rec->name);
			continue;
		}
		/* FIXME: set up the other tres here that aren't specific */
	}

	slurm_set_accounting_storage_tres(unique_tres);
	xfree(unique_tres);

	cluster_cpus = 0;

	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		uint64_t cpu_count = 0, mem_count = 0;
		if (node_ptr->name == '\0')
			continue;

		if (slurmctld_conf.fast_schedule) {
			cpu_count += node_ptr->config_ptr->cpus;
			mem_count += node_ptr->config_ptr->real_memory;
		} else {
			cpu_count += node_ptr->cpus;
			mem_count += node_ptr->real_memory;
		}

		cluster_cpus += cpu_count;
		if (mem_tres)
			mem_tres->count += mem_count;

		if (!node_ptr->tres_cnt)
			node_ptr->tres_cnt = xmalloc(sizeof(uint64_t) *
						     slurmctld_tres_cnt);
		node_ptr->tres_cnt[TRES_ARRAY_CPU] = cpu_count;
		node_ptr->tres_cnt[TRES_ARRAY_MEM] = mem_count;

		list_for_each(assoc_mgr_tres_list,
			      _add_node_gres_tres, node_ptr);

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

	assoc_mgr_tres_array[TRES_ARRAY_NODE]->count = node_record_count;

	set_partition_tres();

	if (!assoc_mgr_locked)
		assoc_mgr_unlock(&locks);
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
	if (lock_count > 0) {
		error("Locks left set "
		      "config:%s, job:%s, node:%s, partition:%s",
		      config, job, node, partition);
	}
	return lock_count;
}

/*
 * slurmctld_shutdown - wake up slurm_rpc_mgr thread via signal
 * RET 0 or error code
 */
int slurmctld_shutdown(void)
{
	debug("sched: slurmctld terminating");
	if (slurmctld_config.thread_id_rpc) {
		pthread_kill(slurmctld_config.thread_id_rpc, SIGUSR1);
		return SLURM_SUCCESS;
	} else {
		error("thread_id_rpc not set");
		return SLURM_ERROR;
	}
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
static void _parse_commandline(int argc, char *argv[])
{
	int c = 0;
	char *tmp_char;
	bool bg_recover_override = 0;

	opterr = 0;
	while ((c = getopt(argc, argv, "BcdDf:hL:n:rRvV")) != -1)
		switch (c) {
		case 'B':
			bg_recover = 0;
			bg_recover_override = 1;
			break;
		case 'c':
			recover = 0;
			bg_recover = 0;
			break;
		case 'd':
			daemonize = 1;
			break;
		case 'D':
			daemonize = 0;
			break;
		case 'f':
			slurm_conf_filename = xstrdup(optarg);
			break;
		case 'h':
			_usage(argv[0]);
			exit(0);
			break;
		case 'L':
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
			if (!bg_recover_override)
				bg_recover = 1;
			break;
		case 'R':
			recover = 2;
			if (!bg_recover_override)
				bg_recover = 1;
			break;
		case 'v':
			debug_level++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
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
#ifdef HAVE_BG
	fprintf(stderr, "  -B      "
			"\tDo not recover state of bluegene blocks.\n");
#endif
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
			"\tUse specified file for slurmctld configuration.\n");
	fprintf(stderr, "  -h      "
			"\tPrint this help message.\n");
	fprintf(stderr, "  -L logfile "
			"\tLog messages to the specified file.\n");
	fprintf(stderr, "  -n value "
			"\tRun the daemon at the specified nice value.\n");
#if (DEFAULT_RECOVER == 0)
	fprintf(stderr, "  -r      "
			"\tRecover state from last checkpoint.\n");
#else
	fprintf(stderr, "  -R      "
			"\tRecover full state from last checkpoint.\n");
#endif
	fprintf(stderr, "  -v      "
			"\tVerbose mode. Multiple -v's increase verbosity.\n");
	fprintf(stderr, "  -V      "
			"\tPrint version information and exit.\n");
}

/*
 * Tell the backup_controller to relinquish control, primary control_machine
 *	has resumed operation
 * wait_time - How long to wait for backup controller to write state, seconds.
 *             Must be zero when called from _slurmctld_background() loop.
 * RET 0 or an error code
 * NOTE: READ lock_slurmctld config before entry (or be single-threaded)
 */
static int _shutdown_backup_controller(int wait_time)
{
	int rc;
	slurm_msg_t req;

	slurm_msg_t_init(&req);
	if ((slurmctld_conf.backup_addr == NULL) ||
	    (slurmctld_conf.backup_addr[0] == '\0')) {
		debug("No backup controller to shutdown");
		return SLURM_SUCCESS;
	}

	slurm_set_addr(&req.address, slurmctld_conf.slurmctld_port,
		       slurmctld_conf.backup_addr);

	/* send request message */
	req.msg_type = REQUEST_CONTROL;

	if (slurm_send_recv_rc_msg_only_one(&req, &rc,
				(CONTROL_TIMEOUT * 1000)) < 0) {
		error("_shutdown_backup_controller:send/recv: %m");
		return SLURM_ERROR;
	}
	if (rc == ESLURM_DISABLED)
		debug("backup controller responding");
	else if (rc == 0) {
		debug("backup controller has relinquished control");
		if (wait_time == 0) {
			/* In case primary controller really did not terminate,
			 * but just temporarily became non-responsive */
			clusteracct_storage_g_register_ctld(
				acct_db_conn,
				slurmctld_conf.slurmctld_port);
		}
	} else {
		error("_shutdown_backup_controller: %s", slurm_strerror(rc));
		return SLURM_ERROR;
	}

	/* FIXME: Ideally the REQUEST_CONTROL RPC does not return until all
	 * other activity has ceased and the state has been saved. That is
	 * not presently the case (it returns when no other work is pending,
	 * so the state save should occur right away). We sleep for a while
	 * here and give the backup controller time to shutdown */
	if (wait_time)
		sleep(wait_time);

	return SLURM_SUCCESS;
}

/* Reset the job credential key based upon configuration parameters
 * NOTE: READ lock_slurmctld config before entry */
static void _update_cred_key(void)
{
	slurm_cred_ctx_key_update(slurmctld_config.cred_ctx,
				  slurmctld_conf.job_credential_private_key);
}

/* Reset slurmctld logging based upon configuration parameters
 *   uses common slurmctld_conf data structure
 * NOTE: READ lock_slurmctld config before entry */
void update_logging(void)
{
	int rc;
	uid_t slurm_user_id  = slurmctld_conf.slurm_user_id;
	gid_t slurm_user_gid = gid_from_uid(slurm_user_id);

	/* Preserve execute line arguments (if any) */
	if (debug_level) {
		slurmctld_conf.slurmctld_debug = MIN(
			(LOG_LEVEL_INFO + debug_level),
			(LOG_LEVEL_END - 1));
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
			log_opts.syslog_level = LOG_LEVEL_FATAL;
	} else
		log_opts.syslog_level = LOG_LEVEL_QUIET;

	log_alter(log_opts, SYSLOG_FACILITY_DAEMON,
		  slurmctld_conf.slurmctld_logfile);

	log_set_timefmt(slurmctld_conf.log_fmt);

	/*
	 * SchedLogLevel restore
	 */
	if (slurmctld_conf.sched_log_level != (uint16_t) NO_VAL)
		sched_log_opts.logfile_level = slurmctld_conf.sched_log_level;

	sched_log_alter(sched_log_opts, LOG_DAEMON,
			slurmctld_conf.sched_logfile);

	if (slurmctld_conf.slurmctld_logfile) {
		rc = chown(slurmctld_conf.slurmctld_logfile,
			   slurm_user_id, slurm_user_gid);
		if (rc && daemonize) {
			error("chown(%s, %d, %d): %m",
			      slurmctld_conf.slurmctld_logfile,
			      (int) slurm_user_id, (int) slurm_user_gid);
		}
	}
	if (slurmctld_conf.sched_logfile) {
		rc = chown(slurmctld_conf.sched_logfile,
			   slurm_user_id, slurm_user_gid);
		if (rc && daemonize) {
			error("chown(%s, %d, %d): %m",
			      slurmctld_conf.sched_logfile,
			      (int) slurm_user_id, (int) slurm_user_gid);
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

/* Verify that ClusterName from slurm.conf matches the state directory.
 * If mismatched exit to protect state files from corruption.
 * If the clustername file does not exist, return true so we can create it later
 * after dropping privileges. */
static bool _verify_clustername(void)
{
	FILE *fp;
	char *filename = NULL;
	char name[512];
	bool create_file = false;

	xstrfmtcat(filename, "%s/clustername",
		   slurmctld_conf.state_save_location);

	if ((fp = fopen(filename, "r"))) {
		/* read value and compare */
		fgets(name, sizeof(name), fp);
		fclose(fp);
		if (xstrcmp(name, slurmctld_conf.cluster_name)) {
			fatal("CLUSTER NAME MISMATCH.\n"
			      "slurmctld has been started with \""
			      "ClusterName=%s\", but read \"%s\" from "
			      "the state files in StateSaveLocation.\n"
			      "Running multiple clusters from a shared "
			      "StateSaveLocation WILL CAUSE CORRUPTION.\n"
			      "Remove %s to override this safety check if "
			      "this is intentional (e.g., the ClusterName "
			      "has changed).", name,
			      slurmctld_conf.cluster_name, filename);
			exit(1);
		}
	} else if (slurmctld_conf.cluster_name)
		create_file = true;

	xfree(filename);

	return create_file;
}

static void _create_clustername_file(void)
{
	FILE *fp;
	char *filename = NULL;

	if (!slurmctld_conf.cluster_name)
		return;

	filename = xstrdup_printf("%s/clustername",
				  slurmctld_conf.state_save_location);

	debug("creating clustername file: %s", filename);
	if (!(fp = fopen(filename, "w"))) {
		fatal("%s: failed to create file %s", __func__, filename);
		exit(1);
	}

	if (fputs(slurmctld_conf.cluster_name, fp) < 0) {
		fatal("%s: failed to write to file %s", __func__, filename);
		exit(1);
	}
	fclose(fp);

	xfree(filename);
}

/* Kill the currently running slurmctld
 * NOTE: No need to lock the config data since we are still single-threaded */
static void _kill_old_slurmctld(void)
{
	int fd;
	pid_t oldpid = read_pidfile(slurmctld_conf.slurmctld_pidfile, &fd);
	if (oldpid != (pid_t) 0) {
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
	if (xstrcmp(slurmctld_conf.slurmctld_pidfile,
		   slurmctld_conf.slurmd_pidfile) == 0)
		error("SlurmctldPid == SlurmdPid, use different names");

	/* Don't close the fd returned here since we need to keep the
	 * fd open to maintain the write lock */
	(void) create_pidfile(slurmctld_conf.slurmctld_pidfile,
			      slurmctld_conf.slurm_user_id);
}

/*
 * set_slurmctld_state_loc - create state directory as needed and "cd" to it
 * NOTE: config read lock must be set on entry
 */
extern void set_slurmctld_state_loc(void)
{
	int rc;
	struct stat st;
	const char *path = slurmctld_conf.state_save_location;

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
	ListIterator itr = NULL;
	struct job_record *job_ptr = NULL;
	struct part_record *part_ptr = NULL;
	slurmdb_qos_rec_t qos_rec;
	slurmdb_assoc_rec_t assoc_rec;
	/* Write lock on jobs, read lock on nodes and partitions */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, READ_LOCK, WRITE_LOCK };

	if (!running_cache)
		lock_slurmctld(job_write_lock);

	while (running_cache == 1) {
		slurm_mutex_lock(&assoc_cache_mutex);
		slurm_cond_wait(&assoc_cache_cond, &assoc_cache_mutex);
		/* This is here to see if we are exiting.  If we get
		   NO_VAL then just return since we are closing down.
		*/
		if (running_cache == (uint16_t)NO_VAL) {
			slurm_mutex_unlock(&assoc_cache_mutex);
			return NULL;
		}
		lock_slurmctld(job_write_lock);
		assoc_mgr_refresh_lists(acct_db_conn, 0);
		if (running_cache)
			unlock_slurmctld(job_write_lock);
		else if (g_tres_count != slurmctld_tres_cnt) {
			/* This has to be done outside of the job write lock.
			 * This should only happen in very rare situations
			 * where we have state, but the database some how has
			 * changed out from under us. */
			unlock_slurmctld(job_write_lock);
			info("TRES in database does not match cache "
			     "(%u != %u).  Updating...",
			     g_tres_count, slurmctld_tres_cnt);
			_init_tres();
			lock_slurmctld(job_write_lock);
		}
		slurm_mutex_unlock(&assoc_cache_mutex);
	}

	if (!job_list) {
		/* This could happen in rare occations, it doesn't
		 * matter since when the job_list is populated things
		 * will be in sync.
		 */
		debug2("No job list yet");
		goto handle_parts;
	}

	debug2("got real data from the database "
	       "refreshing the association ptr's for %d jobs",
	       list_count(job_list));
	itr = list_iterator_create(job_list);
	while ((job_ptr = list_next(itr))) {
		if (job_ptr->assoc_id) {
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_assoc_rec_t));
			assoc_rec.id = job_ptr->assoc_id;

			debug("assoc is %zx (%d) for job %u",
			      (size_t)job_ptr->assoc_ptr, job_ptr->assoc_id,
			      job_ptr->job_id);

			if (assoc_mgr_fill_in_assoc(
				    acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    (slurmdb_assoc_rec_t **)
				    &job_ptr->assoc_ptr, false)) {
				verbose("Invalid association id %u "
					"for job id %u",
					job_ptr->assoc_id, job_ptr->job_id);
				/* not a fatal error, association could have
				 * been removed */
			}

			debug("now assoc is %zx (%d) for job %u",
			      (size_t)job_ptr->assoc_ptr, job_ptr->assoc_id,
			      job_ptr->job_id);
		}
		if (job_ptr->qos_id) {
			memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
			qos_rec.id = job_ptr->qos_id;
			if ((assoc_mgr_fill_in_qos(
				    acct_db_conn, &qos_rec,
				    accounting_enforce,
				    (slurmdb_qos_rec_t **)&job_ptr->qos_ptr, 0))
			   != SLURM_SUCCESS) {
				verbose("Invalid qos (%u) for job_id %u",
					job_ptr->qos_id, job_ptr->job_id);
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
				    (slurmdb_qos_rec_t **)&part_ptr->qos_ptr, 0)
			    != SLURM_SUCCESS) {
				fatal("Partition %s has an invalid qos (%s), "
				      "please check your configuration",
				      part_ptr->name, qos_rec.name);
			}
		}
	}
	list_iterator_destroy(itr);

end_it:
	/* issuing a reconfig will reset the pointers on the burst
	   buffers */
	bb_g_reconfig();

	unlock_slurmctld(job_write_lock);
	/* This needs to be after the lock and after we update the
	   jobs so if we need to send them we are set. */
	_accounting_cluster_ready();
	return NULL;
}

static void _become_slurm_user(void)
{
	gid_t slurm_user_gid;

	/* Determine SlurmUser gid */
	slurm_user_gid = gid_from_uid(slurmctld_conf.slurm_user_id);
	if (slurm_user_gid == (gid_t) -1) {
		fatal("Failed to determine gid of SlurmUser(%u)",
		      slurmctld_conf.slurm_user_id);
	}

	/* Initialize supplementary groups ID list for SlurmUser */
	if (getuid() == 0) {
		/* root does not need supplementary groups */
		if ((slurmctld_conf.slurm_user_id == 0) &&
		    (setgroups(0, NULL) != 0)) {
			fatal("Failed to drop supplementary groups, "
			      "setgroups: %m");
		} else if ((slurmctld_conf.slurm_user_id != getuid()) &&
			   initgroups(slurmctld_conf.slurm_user_name,
				      slurm_user_gid)) {
			fatal("Failed to set supplementary groups, "
			      "initgroups: %m");
		}
	} else {
		info("Not running as root. Can't drop supplementary groups");
	}

	/* Set GID to GID of SlurmUser */
	if ((slurm_user_gid != getegid()) &&
	    (setgid(slurm_user_gid))) {
		fatal("Failed to set GID to %d", slurm_user_gid);
	}

	/* Set UID to UID of SlurmUser */
	if ((slurmctld_conf.slurm_user_id != getuid()) &&
	    (setuid(slurmctld_conf.slurm_user_id))) {
		fatal("Can not set uid to SlurmUser(%u): %m",
		      slurmctld_conf.slurm_user_id);
	}
}

/* Return true if node_name (a global) is a valid controller host name */
static bool  _valid_controller(void)
{
	bool match = false;

	if (slurmctld_conf.control_machine == NULL)
		return match;

	if ((xstrcmp(node_name_short,slurmctld_conf.control_machine) == 0) ||
	    (xstrcmp(node_name_long, slurmctld_conf.control_machine) == 0))
		match = true;
	else if (strchr(slurmctld_conf.control_machine, ',')) {
		char *token, *last = NULL;
		char *tmp_name = xstrdup(slurmctld_conf.control_machine);

		token = strtok_r(tmp_name, ",", &last);
		while (token) {
			if ((xstrcmp(node_name_short, token) == 0) ||
			    (xstrcmp(node_name_long, token) == 0)) {
				match = true;
				break;
			}
			token = strtok_r(NULL, ",", &last);
		}
		xfree(tmp_name);
	}

	return match;
}

static void _test_thread_limit(void)
{
#ifdef RLIMIT_NOFILE
{
	struct rlimit rlim[1];
	if (getrlimit(RLIMIT_NOFILE, rlim) < 0)
		error("Unable to get file count limit");
	else if ((rlim->rlim_cur != RLIM_INFINITY) &&
		 (max_server_threads > rlim->rlim_cur)) {
		max_server_threads = rlim->rlim_cur;
		info("Reducing max_server_thread to %u due to file count limit "
		     "of %u", max_server_threads, max_server_threads);
	}
}
#endif
	return;
}

/* Ping BackupController
 * RET SLURM_SUCCESS or error code */
static int _ping_backup_controller(void)
{
	int rc = SLURM_SUCCESS;
	slurm_msg_t req;
	/* Locks: Read configuration */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld(config_read_lock);
	if (!slurmctld_conf.backup_addr) {
		debug4("No backup slurmctld to ping");
		unlock_slurmctld(config_read_lock);
		return SLURM_SUCCESS;
	}

	/*
	 *  Set address of controller to ping
	 */
	debug3("pinging backup slurmctld at %s", slurmctld_conf.backup_addr);
	slurm_msg_t_init(&req);
	slurm_set_addr(&req.address, slurmctld_conf.slurmctld_port,
		       slurmctld_conf.backup_addr);
	unlock_slurmctld(config_read_lock);

	req.msg_type = REQUEST_PING;

	if (slurm_send_recv_rc_msg_only_one(&req, &rc, 0) < 0) {
		debug2("_ping_backup_controller/slurm_send_node_msg error: %m");
		return SLURM_ERROR;
	}

	if (rc)
		debug2("_ping_backup_controller/response error %d", rc);

	return rc;
}

static void  _set_work_dir(void)
{
	bool success = false;

	if (slurmctld_conf.slurmctld_logfile &&
	    (slurmctld_conf.slurmctld_logfile[0] == '/')) {
		char *slash_ptr, *work_dir;
		work_dir = xstrdup(slurmctld_conf.slurmctld_logfile);
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
		if ((access(slurmctld_conf.state_save_location, W_OK) != 0) ||
		    (chdir(slurmctld_conf.state_save_location) < 0)) {
			error("chdir(%s): %m",
			      slurmctld_conf.state_save_location);
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
