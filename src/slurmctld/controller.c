/*****************************************************************************\
 *  controller.c - main control machine daemon for slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, Kevin Tew <tew1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <grp.h>
#include <errno.h>
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
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_topology.h"
#include "src/common/slurm_priority.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/switch.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/job_submit.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/port_mgr.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/srun_comm.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"


#define CRED_LIFE         60	/* Job credential lifetime in seconds */
#define DEFAULT_DAEMONIZE 1	/* Run as daemon by default if set */
#define DEFAULT_RECOVER   1	/* Default state recovery on restart
				 * 0 = use no saved state information
				 * 1 = recover saved job state,
				 *     node DOWN/DRAIN state and reason information
				 * 2 = recover all state saved from last shutdown */
#define MIN_CHECKIN_TIME  3	/* Nodes have this number of seconds to
				 * check-in before we ping them */
#define SHUTDOWN_WAIT     2	/* Time to wait for backup server shutdown */

#if (0)
/* If defined and FastSchedule=0 in slurm.conf, then report the CPU count that a
 * node registers with rather than the CPU count defined for the node in slurm.conf */
#define SLURM_NODE_ACCT_REGISTER 1
#endif

/**************************************************************************\
 * To test for memory leaks, set MEMORY_LEAK_DEBUG to 1 using
 * "configure --enable-memory-leak-debug" then execute
 * $ valgrind --tool=memcheck --leak-check=yes --num-callers=8 \
 *   --leak-resolution=med ./slurmctld -Dc >valg.ctld.out 2>&1
 *
 * Then exercise the slurmctld functionality before executing
 * > scontrol shutdown
 *
 * The OpenSSL code produces a bunch of errors related to use of
 *    non-initialized memory use.
 * The switch/elan functions will report one block "possibly lost"
 *    (640 bytes), it is really not lost.
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
slurmctld_config_t slurmctld_config;
int bg_recover = DEFAULT_RECOVER;
char *slurmctld_cluster_name = NULL; /* name of cluster */
void *acct_db_conn = NULL;
int accounting_enforce = 0;
int association_based_accounting = 0;
bool ping_nodes_now = false;
uint32_t      cluster_cpus = 0;
int   with_slurmdbd = 0;
bool want_nodes_reboot = true;

/* Next used for stats/diagnostics */
diag_stats_t slurmctld_diag_stats;

/* Local variables */
static int	daemonize = DEFAULT_DAEMONIZE;
static int	debug_level = 0;
static char	*debug_logfile = NULL;
static bool     dump_core = false;
static uint32_t max_server_threads = MAX_SERVER_THREADS;
static int	new_nice = 0;
static char	node_name[MAX_SLURM_NAME];
static int	recover   = DEFAULT_RECOVER;
static pthread_cond_t server_thread_cond = PTHREAD_COND_INITIALIZER;
static pid_t	slurmctld_pid;
static char    *slurm_conf_filename;
static int      primary = 1 ;
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
inline static void  _free_server_thread(void);
static void         _init_config(void);
static void         _init_pidfile(void);
static void         _kill_old_slurmctld(void);
static void         _parse_commandline(int argc, char *argv[]);
inline static int   _ping_backup_controller(void);
static void         _remove_assoc(slurmdb_association_rec_t *rec);
static void         _remove_qos(slurmdb_qos_rec_t *rec);
static void         _update_assoc(slurmdb_association_rec_t *rec);
static void         _update_qos(slurmdb_qos_rec_t *rec);
inline static int   _report_locks_set(void);
static void *       _service_connection(void *arg);
static void         _set_work_dir(void);
static int          _shutdown_backup_controller(int wait_time);
static void *       _slurmctld_background(void *no_data);
static void *       _slurmctld_rpc_mgr(void *no_data);
static void *       _slurmctld_signal_hand(void *no_data);
static void         _test_thread_limit(void);
inline static void  _update_cred_key(void);
static void         _update_nice(void);
inline static void  _usage(char *prog_name);
static bool         _valid_controller(void);
static bool         _wait_for_server_thread(void);

typedef struct connection_arg {
	int newsockfd;
} connection_arg_t;

time_t last_proc_req_start = 0;
time_t next_stats_reset = 0;

/* main - slurmctld main function, start various threads and process RPCs */
int main(int argc, char *argv[])
{
	int cnt, error_code, i;
	pthread_attr_t thread_attr;
	struct stat stat_buf;
	/* Locks: Write configuration, job, node, and partition */
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };
	assoc_init_args_t assoc_init_arg;
	pthread_t assoc_cache_thread;
	slurm_trigger_callbacks_t callbacks;
	char *dir_name;

	/*
	 * Establish initial configuration
	 */
	_init_config();
	log_init(argv[0], log_opts, LOG_DAEMON, NULL);
	sched_log_init(argv[0], sched_log_opts, LOG_DAEMON, NULL);
	slurmctld_pid = getpid();
	_parse_commandline(argc, argv);
	init_locks();
	slurm_conf_reinit(slurm_conf_filename);

	update_logging();
	_update_nice();
	_kill_old_slurmctld();

	if (daemonize) {
		slurmctld_config.daemonize = 1;
		if (daemon(1, 1))
			error("daemon(): %m");
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
	if (daemonize)
		_set_work_dir();

	if (stat(slurmctld_conf.mail_prog, &stat_buf) != 0)
		error("Configured MailProg is invalid");

	if (!strcmp(slurmctld_conf.accounting_storage_type,
		    "accounting_storage/none")) {
		if (strcmp(slurmctld_conf.job_acct_gather_type,
			   "jobacct_gather/none"))
			error("Job accounting information gathered, "
			      "but not stored");
	} else {
		if (!strcmp(slurmctld_conf.job_acct_gather_type,
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
	if(!strcasecmp(slurmctld_conf.accounting_storage_type,
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

	memset(&assoc_init_arg, 0, sizeof(assoc_init_args_t));
	assoc_init_arg.enforce = accounting_enforce;
	assoc_init_arg.update_resvs = update_assocs_in_resvs;
	assoc_init_arg.remove_assoc_notify = _remove_assoc;
	assoc_init_arg.remove_qos_notify = _remove_qos;
	assoc_init_arg.update_assoc_notify = _update_assoc;
	assoc_init_arg.update_qos_notify = _update_qos;
	assoc_init_arg.cache_level = ASSOC_MGR_CACHE_ASSOC |
				     ASSOC_MGR_CACHE_USER  |
				     ASSOC_MGR_CACHE_QOS;
	if (slurmctld_conf.track_wckey)
		assoc_init_arg.cache_level |= ASSOC_MGR_CACHE_WCKEY;

	callbacks.acct_full   = trigger_primary_ctld_acct_full;
	callbacks.dbd_fail    = trigger_primary_dbd_fail;
	callbacks.dbd_resumed = trigger_primary_dbd_res_op;
	callbacks.db_fail     = trigger_primary_db_fail;
	callbacks.db_resumed  = trigger_primary_db_res_op;
	acct_db_conn = acct_storage_g_get_connection(&callbacks, 0, false,
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

	/* This thread is looking for when we get correct data from
	   the database so we can update the assoc_ptr's in the jobs
	*/
	if (running_cache) {
		slurm_attr_init(&thread_attr);
		while (pthread_create(&assoc_cache_thread, &thread_attr,
				      _assoc_cache_mgr, NULL)) {
			error("pthread_create error %m");
			sleep(1);
		}
		slurm_attr_destroy(&thread_attr);
	}

	info("slurmctld version %s started on cluster %s",
	     SLURM_VERSION_STRING, slurmctld_cluster_name);

	if ((error_code = gethostname_short(node_name, MAX_SLURM_NAME)))
		fatal("getnodename error %s", slurm_strerror(error_code));

	/* init job credential stuff */
	slurmctld_config.cred_ctx = slurm_cred_creator_ctx_create(
			slurmctld_conf.job_credential_private_key);
	if (!slurmctld_config.cred_ctx) {
		fatal("slurm_cred_creator_ctx_create(%s): %m",
			slurmctld_conf.job_credential_private_key);
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
	if (slurm_acct_storage_init(NULL) != SLURM_SUCCESS )
		fatal( "failed to initialize accounting_storage plugin");
	if (jobacct_gather_init() != SLURM_SUCCESS )
		fatal( "failed to initialize jobacct_gather plugin");
	if (job_submit_plugin_init() != SLURM_SUCCESS )
		fatal( "failed to initialize job_submit plugin");

	while (1) {
		/* initialization for each primary<->backup switch */
		slurmctld_config.shutdown_time = (time_t) 0;
		slurmctld_config.resume_backup = false;

		/* start in primary or backup mode */
		if (slurmctld_conf.backup_controller &&
		    (strcmp(node_name,
			    slurmctld_conf.backup_controller) == 0)) {
			slurm_sched_fini();	/* make sure shutdown */
			primary = 0;
			run_backup();
		} else if (_valid_controller()) {
			(void) _shutdown_backup_controller(SHUTDOWN_WAIT);
			trigger_primary_ctld_res_ctrl();
			/* Now recover the remaining state information */
			lock_slurmctld(config_write_lock);
			if (switch_restore(slurmctld_conf.state_save_location,
					   recover ? true : false))
				fatal(" failed to initialize switch plugin" );
			if ((error_code = read_slurm_conf(recover, false))) {
				fatal("read_slurm_conf reading %s: %s",
					slurmctld_conf.slurm_conf,
					slurm_strerror(error_code));
			}
			unlock_slurmctld(config_write_lock);
			select_g_select_nodeinfo_set_all();

			if (recover == 0)
				_accounting_mark_all_nodes_down("cold-start");

			primary = 1;

		} else {
			error("this host (%s) not valid controller (%s or %s)",
				node_name, slurmctld_conf.control_machine,
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
		    (primary == 1)) {
			trigger_primary_ctld_res_op();
		}

		clusteracct_storage_g_register_ctld(
			acct_db_conn,
			slurmctld_conf.slurmctld_port);

		_accounting_cluster_ready();

		if (slurm_priority_init() != SLURM_SUCCESS)
			fatal("failed to initialize priority plugin");

		if (slurm_sched_init() != SLURM_SUCCESS)
			fatal("failed to initialize scheduling plugin");


		/*
		 * create attached thread to process RPCs
		 */
		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		slurmctld_config.server_thread_count++;
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
		slurm_attr_init(&thread_attr);
		while (pthread_create(&slurmctld_config.thread_id_rpc,
				      &thread_attr, _slurmctld_rpc_mgr,
				      NULL)) {
			error("pthread_create error %m");
			sleep(1);
		}
		slurm_attr_destroy(&thread_attr);

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
		switch_save(dir_name);
		xfree(dir_name);
		slurm_priority_fini();
		shutdown_state_save();
		pthread_join(slurmctld_config.thread_id_sig,  NULL);
		pthread_join(slurmctld_config.thread_id_rpc,  NULL);
		pthread_join(slurmctld_config.thread_id_save, NULL);

		if (running_cache) {
			/* break out and end the association cache
			 * thread since we are shutting down, no reason
			 * to wait for current info from the database */
			slurm_mutex_lock(&assoc_cache_mutex);
			running_cache = (uint16_t)NO_VAL;
			pthread_cond_signal(&assoc_cache_cond);
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
		    (primary == 1))
			break;

		recover = 2;
	}

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
	 * Wait up to 60 seconds (3 seconds * 20) */
	for (i=0; i<20; i++) {
		agent_purge();
		sleep(3);
		cnt = get_agent_count();
		if (cnt == 0)
			break;
	}
	if (i >= 10)
		error("Left %d agent threads active", cnt);

	slurm_sched_fini();	/* Stop all scheduling */

	/* Purge our local data structures */
	job_fini();
	part_fini();	/* part_fini() must preceed node_fini() */
	node_fini();
	purge_front_end_state();
	resv_fini();
	trigger_fini();
	dir_name = slurm_get_state_save_location();
	assoc_mgr_fini(dir_name);
	xfree(dir_name);
	reserve_port_config(NULL);

	/* Some plugins are needed to purge job/node data structures,
	 * unplug after other data structures are purged */
	gres_plugin_fini();
	job_submit_plugin_fini();
	slurm_preempt_fini();
	g_slurm_jobcomp_fini();
	jobacct_gather_fini();
	slurm_select_fini();
	slurm_topo_fini();
	checkpoint_fini();
	slurm_auth_fini();
	switch_fini();

	/* purge remaining data structures */
	license_free();
	slurm_cred_ctx_destroy(slurmctld_config.cred_ctx);
	slurm_crypto_fini();	/* must be after ctx_destroy */
	slurm_conf_destroy();
	slurm_api_clear_config();
	sleep(2);
}
#else
	/* Give REQUEST_SHUTDOWN a chance to get propagated,
	 * up to 3 seconds. */
	for (i=0; i<3; i++) {
		agent_purge();
		cnt = get_agent_count();
		if (cnt == 0)
			break;
		sleep(1);
	}
#ifdef HAVE_BG
	/* Always call slurm_select_fini() on some systems like
	   BlueGene we need to make sure other processes are ended
	   or we could get a random core from within it's
	   underlying infrastructure.
	*/
        slurm_select_fini();
#endif

#endif

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
#ifdef WITH_PTHREADS
	pthread_mutex_init(&slurmctld_config.thread_count_lock, NULL);
	slurmctld_config.thread_id_main    = (pthread_t) 0;
	slurmctld_config.thread_id_sig     = (pthread_t) 0;
	slurmctld_config.thread_id_rpc     = (pthread_t) 0;
#else
	slurmctld_config.thread_count_lock = 0;
	slurmctld_config.thread_id_main    = 0;
	slurmctld_config.thread_id_sig     = 0;
	slurmctld_config.thread_id_rpc     = 0;
#endif
}

/* Read configuration file.
 * Same name as API function for use in accounting_storage plugin.
 * Anything you add to this function must be added to the
 * _slurm_rpc_reconfigure_controller function inside proc_req.c try
 * to keep these in sync.
 */
static int _reconfigure_slurm(void)
{
	/* Locks: Write configuration, job, node, and partition */
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };
	int rc;

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
	slurm_sched_partition_change();	/* notify sched plugin */
	unlock_slurmctld(config_write_lock);
	assoc_mgr_set_missing_uids();
	start_power_mgr(&slurmctld_config.thread_id_power);
	trigger_reconfig();
	priority_g_reconfig();          /* notify priority plugin too */
	schedule(0);			/* has its own locks */
	save_all_state();

	return rc;
}

/* _slurmctld_signal_hand - Process daemon-wide signals */
static void *_slurmctld_signal_hand(void *no_data)
{
	int sig;
	int i, rc;
	int sig_array[] = {SIGINT, SIGTERM, SIGHUP, SIGABRT, 0};
	sigset_t set;

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
	slurm_fd_t newsockfd;
	slurm_fd_t *sockfd;	/* our set of socket file descriptors */
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
	    (strcmp(node_name, slurmctld_conf.backup_controller) == 0) &&
	    (strcmp(slurmctld_conf.backup_controller,
		    slurmctld_conf.backup_addr) != 0)) {
		node_addr = slurmctld_conf.backup_addr ;
	}
	else if (_valid_controller() &&
		 strcmp(slurmctld_conf.control_machine,
			 slurmctld_conf.control_addr)) {
		node_addr = slurmctld_conf.control_addr ;
	}

	/* initialize ports for RPCs */
	lock_slurmctld(config_read_lock);
	nports = slurmctld_conf.slurmctld_port_count;
	sockfd = xmalloc(sizeof(slurm_fd_t) * nports);
	for (i=0; i<nports; i++) {
		sockfd[i] = slurm_init_msg_engine_addrname_port(
					node_addr,
					slurmctld_conf.slurmctld_port+i);
		if (sockfd[i] == SLURM_SOCKET_ERROR)
			fatal("slurm_init_msg_engine_addrname_port error %m");
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
			_free_server_thread();
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
			_free_server_thread();
			continue;
		}
		conn_arg = xmalloc(sizeof(connection_arg_t));
		conn_arg->newsockfd = newsockfd;
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
	_free_server_thread();
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

	slurm_msg_t_init(msg);
	/*
	 * slurm_receive_msg sets msg connection fd to accepted fd. This allows
	 * possibility for slurmctld_req() to close accepted connection.
	 */
	if (slurm_receive_msg(conn->newsockfd, msg, 0) != 0) {
		error("slurm_receive_msg: %m");
		/* close the new socket */
		slurm_close_accepted_conn(conn->newsockfd);
		goto cleanup;
	}

	if (errno != SLURM_SUCCESS) {
		if (errno == SLURM_PROTOCOL_VERSION_ERROR) {
			slurm_send_rc_msg(msg, SLURM_PROTOCOL_VERSION_ERROR);
		} else
			info("_service_connection/slurm_receive_msg %m");
	} else {
		/* process the request */
		slurmctld_req(msg);
	}
	if ((conn->newsockfd >= 0)
	    && slurm_close_accepted_conn(conn->newsockfd) < 0)
		error ("close(%d): %m",  conn->newsockfd);

cleanup:
	slurm_free_msg(msg);
	xfree(arg);
	_free_server_thread();
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
			pthread_cond_wait(&server_thread_cond,
					  &slurmctld_config.thread_count_lock);
		}
	}
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
	return rc;
}

static void _free_server_thread(void)
{
	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	if (slurmctld_config.server_thread_count > 0)
		slurmctld_config.server_thread_count--;
	else
		error("slurmctld_config.server_thread_count underflow");
	pthread_cond_broadcast(&server_thread_cond);
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
}

static int _accounting_cluster_ready()
{
	struct node_record *node_ptr;
	int i;
	int rc = SLURM_ERROR;
	time_t event_time = time(NULL);
	uint32_t cpus = 0;
	bitstr_t *total_node_bitmap = NULL;
	char *cluster_nodes = NULL;
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	lock_slurmctld(node_read_lock);
	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (node_ptr->name == '\0')
			continue;
#ifdef SLURM_NODE_ACCT_REGISTER
		if (slurmctld_conf.fast_schedule)
			cpus += node_ptr->config_ptr->cpus;
		else
			cpus += node_ptr->cpus;
#else
		cpus += node_ptr->config_ptr->cpus;
#endif
	}

	/* Since cluster_cpus is used else where we need to keep a
	   local var here to avoid race conditions on cluster_cpus
	   not being correct.
	*/
	cluster_cpus = cpus;

	/* Now get the names of all the nodes on the cluster at this
	   time and send it also.
	*/
	total_node_bitmap = bit_alloc(node_record_count);
	bit_nset(total_node_bitmap, 0, node_record_count-1);
	cluster_nodes = bitmap2node_name_sortable(total_node_bitmap, 0);
	FREE_NULL_BITMAP(total_node_bitmap);
	unlock_slurmctld(node_read_lock);

	rc = clusteracct_storage_g_cluster_cpus(acct_db_conn,
						cluster_nodes,
						cluster_cpus, event_time);
	xfree(cluster_nodes);
	if(rc == ACCOUNTING_FIRST_REG) {
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

	if((rc = acct_storage_g_flush_jobs_on_cluster(acct_db_conn,
						      event_time))
	   == SLURM_ERROR)
		return rc;

	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (node_ptr->name == '\0')
			continue;
		if((rc = clusteracct_storage_g_node_down(
			    acct_db_conn,
			    node_ptr, event_time,
			    reason, slurm_get_slurm_user_id()))
		   == SLURM_ERROR)
			break;
	}
	return rc;
}

static void _remove_assoc(slurmdb_association_rec_t *rec)
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

	cnt = job_hold_by_qos_id(rec->id);

	if (cnt) {
		info("Removed QOS:%s held %u jobs", rec->name, cnt);
	} else
		debug("Removed QOS:%s", rec->name);
}

static void _update_assoc(slurmdb_association_rec_t *rec)
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
		if (!IS_NODE_MAINT(node_ptr) || /* do it only if node */
		    is_node_in_maint_reservation(i)) /*isn't in reservation */
			continue;
		want_nodes_reboot = true; /* mark it for the next cycle */
		if (IS_NODE_IDLE(node_ptr) && !IS_NODE_NO_RESPOND(node_ptr) &&
		    !IS_NODE_POWER_UP(node_ptr)) /* only active idle nodes */
			want_reboot = true;
		else if (IS_NODE_FUTURE(node_ptr) &&
			 (node_ptr->last_response == (time_t) 0))
			want_reboot = true; /* system just restarted */
		else
			want_reboot = false;
		if (!want_reboot)
			continue;
		if (reboot_agent_args == NULL) {
			reboot_agent_args = xmalloc(sizeof(agent_arg_t));
			reboot_agent_args->msg_type = REQUEST_REBOOT_NODES;
			reboot_agent_args->retry = 0;
			reboot_agent_args->hostlist = hostlist_create("");
		}
		hostlist_push(reboot_agent_args->hostlist, node_ptr->name);
		reboot_agent_args->node_count++;
		node_ptr->node_state = NODE_STATE_FUTURE |
				(node_ptr->node_state & NODE_STATE_FLAGS);
		bit_clear(avail_node_bitmap, i);
		bit_clear(idle_node_bitmap, i);
		node_ptr->last_response = now;
	}
	if (reboot_agent_args != NULL) {
		hostlist_uniq(reboot_agent_args->hostlist);
		host_str = hostlist_ranged_string_xmalloc(
				reboot_agent_args->hostlist);
		debug("Queuing reboot request for nodes %s", host_str);
		xfree(host_str);
		agent_queue_request(reboot_agent_args);
		last_node_update = now;
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
	static time_t last_checkpoint_time;
	static time_t last_group_time;
	static time_t last_health_check_time;
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
	static bool ping_msg_sent = false;
	time_t now;
	int no_resp_msg_interval, ping_interval, purge_job_interval;
	int group_time, group_force;
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
	last_sched_time = last_checkpoint_time = last_group_time = now;
	last_purge_job_time = last_trigger = last_health_check_time = now;
	last_timelimit_time = last_assert_primary_time = now;
	last_no_resp_msg_time = last_resv_time = last_ctld_bu_ping = now;
	last_uid_update = last_reboot_msg_time = now;

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
		if (slurmctld_config.shutdown_time == 0)
			sleep(1);

		now = time(NULL);
		START_TIMER;

		if (slurmctld_conf.slurmctld_debug <= 3)
			no_resp_msg_interval = 300;
		else if (slurmctld_conf.slurmctld_debug == 4)
			no_resp_msg_interval = 60;
		else
			no_resp_msg_interval = 1;

		if (slurmctld_config.shutdown_time) {
			int i;
			/* wait for RPC's to complete */
			for (i = 1; i < CONTROL_TIMEOUT; i++) {
				if (slurmctld_config.server_thread_count == 0)
					break;
				sleep(1);
			}
			if (slurmctld_config.server_thread_count)
				info("shutdown server_thread_count=%d",
					slurmctld_config.server_thread_count);
			if (_report_locks_set() == 0) {
				info("Saving all slurm state");
				save_all_state();
			} else
				error("can not save state, semaphores set");
			break;
		}

		if (difftime(now, last_resv_time) >= 2) {
			now = time(NULL);
			last_resv_time = now;
			lock_slurmctld(node_write_lock);
			set_node_maint_mode();
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
			now = time(NULL);
			last_health_check_time = now;
			lock_slurmctld(node_write_lock);
			run_health_check();
			unlock_slurmctld(node_write_lock);
		}
		if (((difftime(now, last_ping_node_time) >= ping_interval) ||
		     ping_nodes_now) && is_ping_done()) {
			now = time(NULL);
			last_ping_node_time = now;
			ping_msg_sent = false;
			ping_nodes_now = false;
			lock_slurmctld(node_write_lock);
			ping_nodes();
			unlock_slurmctld(node_write_lock);
		} else if ((difftime(now, last_ping_node_time) >=
			    ping_interval) && !is_ping_done() &&
			    !ping_msg_sent) {
			/* log failure once per ping_nodes() call,
			 * no error if node state update request
			 * processed while the ping is in progress */
			error("Node ping apparently hung, "
			      "many nodes may be DOWN or configured "
			      "SlurmdTimeout should be increased");
			ping_msg_sent = true;
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

		if (difftime(now, last_sched_time) >= PERIODIC_SCHEDULE) {
			now = time(NULL);
			last_sched_time = now;
			if (schedule(INFINITE))
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
			 * or reconfigured nodes */
			now = time(NULL);
			last_node_acct = now;
			_accounting_cluster_ready();
		}
 

		if (last_proc_req_start == 0) {
			/* Stats will reset at midnight (aprox).
			 * Uhmmm... UTC time?... It is  not so important.
			 * Just resetting during the night */
			last_proc_req_start = now;
			next_stats_reset = last_proc_req_start -
					   (last_proc_req_start % 86400) +
					   86400;
		}

		if ((next_stats_reset > 0) && (now > next_stats_reset)) {
			/* Resetting stats values */
			last_proc_req_start = now;
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
		    strcmp(node_name, slurmctld_conf.backup_controller)) {
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
			if(!bg_recover_override)
				bg_recover = 1;
			break;
		case 'R':
			recover = 2;
			if(!bg_recover_override)
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
		if (rc) {
			error("chown(%s, %d, %d): %m",
			      slurmctld_conf.slurmctld_logfile,
			      (int) slurm_user_id, (int) slurm_user_gid);
		}
	}
	if (slurmctld_conf.sched_logfile) {
		rc = chown(slurmctld_conf.sched_logfile,
			   slurm_user_id, slurm_user_gid);
		if (rc) {
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
	if (strcmp(slurmctld_conf.slurmctld_pidfile,
		   slurmctld_conf.slurmd_pidfile) == 0)
		error("SlurmctldPid == SlurmdPid, use different names");

	/* Don't close the fd returned here since we need to keep the
	   fd open to maintain the write lock.
	*/
	create_pidfile(slurmctld_conf.slurmctld_pidfile,
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
	slurmdb_qos_rec_t qos_rec;
	slurmdb_association_rec_t assoc_rec;
	/* Write lock on jobs, read lock on nodes and partitions */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };

	while(running_cache == 1) {
		slurm_mutex_lock(&assoc_cache_mutex);
		pthread_cond_wait(&assoc_cache_cond, &assoc_cache_mutex);
		/* This is here to see if we are exiting.  If we get
		   NO_VAL then just return since we are closing down.
		*/
		if(running_cache == (uint16_t)NO_VAL) {
			slurm_mutex_unlock(&assoc_cache_mutex);
			return NULL;
		}
		lock_slurmctld(job_write_lock);
		assoc_mgr_refresh_lists(acct_db_conn, NULL);
		if(running_cache)
			unlock_slurmctld(job_write_lock);
		slurm_mutex_unlock(&assoc_cache_mutex);
	}

	debug2("got real data from the database "
	       "refreshing the association ptr's for %d jobs",
	       list_count(job_list));
	itr = list_iterator_create(job_list);
	while ((job_ptr = list_next(itr))) {
		if (job_ptr->assoc_id) {
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_association_rec_t));
			assoc_rec.id = job_ptr->assoc_id;

			debug("assoc is %zx (%d) for job %u",
			      (size_t)job_ptr->assoc_ptr, job_ptr->assoc_id,
			      job_ptr->job_id);

			if (assoc_mgr_fill_in_assoc(
				    acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    (slurmdb_association_rec_t **)
				    &job_ptr->assoc_ptr)) {
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
		if(job_ptr->qos_id) {
			memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
			qos_rec.id = job_ptr->qos_id;
			if((assoc_mgr_fill_in_qos(
				    acct_db_conn, &qos_rec,
				    accounting_enforce,
				    (slurmdb_qos_rec_t **)&job_ptr->qos_ptr))
			   != SLURM_SUCCESS) {
				verbose("Invalid qos (%u) for job_id %u",
					job_ptr->qos_id, job_ptr->job_id);
				/* not a fatal error, qos could have
				 * been removed */
			}
		}
	}
	list_iterator_destroy(itr);
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

	if (strcmp(node_name, slurmctld_conf.control_machine) == 0)
		match = true;
	else if (strchr(slurmctld_conf.control_machine, ',')) {
		char *token, *last = NULL;
		char *tmp_name = xstrdup(slurmctld_conf.control_machine);

		token = strtok_r(tmp_name, ",", &last);
		while (token) {
			if (strcmp(node_name, token) == 0) {
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
