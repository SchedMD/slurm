/*****************************************************************************\
 *  src/slurmd/slurmd/slurmd.c - main slurm node server daemon
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian.
 *  Copyright (C) SchedMD LLC.
 *  Copyright (C) 2013      Intel, Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

/* Needed for sched_setaffinity */
#define _GNU_SOURCE

#if HAVE_HWLOC
#  include <hwloc.h>
#endif

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/cpu_frequency.h"
#include "src/common/daemonize.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/forward.h"
#include "src/common/group_cache.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/run_command.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/slurm_time.h"
#include "src/common/spank.h"
#include "src/common/stepd_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xsystemd.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/acct_gather_energy.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/certmgr.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/gpu.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/hash.h"
#include "src/interfaces/job_container.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/mcs.h"
#include "src/interfaces/mpi.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/prep.h"
#include "src/interfaces/proctrack.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/task.h"
#include "src/interfaces/topology.h"

#include "src/slurmd/common/set_oomadj.h"
#include "src/slurmd/common/slurmd_cgroup.h"
#include "src/slurmd/common/slurmstepd_init.h"
#include "src/slurmd/common/xcpuinfo.h"

#include "src/slurmd/slurmd/cred_context.h"
#include "src/slurmd/slurmd/get_mach_stat.h"
#include "src/slurmd/slurmd/req.h"
#include "src/slurmd/slurmd/slurmd.h"

decl_static_data(usage_txt);



#define MAX_THREADS		256
#define TIMEOUT_SIGUSR2 5000000
#define TIMEOUT_RECONFIG 5000000
#define SLURMD_CONMGR_DEFAULT_THREADS 10
#define SLURMD_CONMGR_DEFAULT_MAX_CONNECTIONS 50
#define MAX_THREAD_DELAY_INC ((timespec_t) { .tv_nsec = 1500, })
#define MAX_THREAD_DELAY_MAX ((timespec_t) { .tv_sec = 1, })

#define _free_and_set(__dst, __src)		\
	do {					\
		xfree(__dst); __dst = __src;	\
	} while (0)

/* global, copied to STDERR_FILENO in tasks before the exec */
int devnull = -1;
bool get_reg_resp = 1;
bool sent_successful_registration = false;
slurmd_conf_t * conf = NULL;
int fini_job_cnt = 0;
uint32_t *fini_job_id = NULL;
pthread_mutex_t fini_job_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tres_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  tres_cond      = PTHREAD_COND_INITIALIZER;
bool tres_packed = false;

#define SERVICE_CONNECTION_ARGS_MAGIC 0x2aeaa8af
typedef struct {
	int magic; /* SERVICE_CONNECTION_ARGS_MAGIC */
	timespec_t delay;
	slurm_addr_t addr;
	int fd;
} service_connection_args_t;

/*
 * count of active threads
 */
static int             active_threads = 0;
static pthread_mutex_t active_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond    = PTHREAD_COND_INITIALIZER;

/*
 * Global data for resource specialization
 */
#define MAX_CPUSTR 256
static bitstr_t	*res_core_bitmap;	/* reserved abstract cores bitmap */
static bitstr_t	*res_cpu_bitmap;	/* reserved abstract CPUs bitmap */
static char	*res_abs_cores = NULL;	/* reserved abstract cores list */
static int32_t	res_abs_core_size = 0;	/* Length of res_abs_cores variable */
static char	res_abs_cpus[MAX_CPUSTR]; /* reserved abstract CPUs list */
static char	*res_mac_cpus = NULL;	/* reserved machine CPUs list */
static int	ncores;			/* number of cores on this node */
static int	ncpus;			/* number of CPUs on this node */

/*
 * static shutdown and reconfigure flags:
 */
static bool original = true;
static bool under_systemd = false;
static sig_atomic_t _shutdown = 0;
static time_t sent_reg_time = (time_t) 0;

/*
 * cached features
 */
static char *cached_features_avail = NULL;
static char *cached_features_active = NULL;
static bool plugins_registered = false;
bool refresh_cached_features = true;
pthread_mutex_t cached_features_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Reference to active listening socket */
pthread_mutex_t listen_mutex = PTHREAD_MUTEX_INITIALIZER;
conmgr_fd_ref_t *listener = NULL;
bool unquiesce_listener = false;

static int       _convert_spec_cores(void);
static int       _core_spec_init(void);
static void _create_msg_socket(void);
static void      _decrement_thd_count(void);
static void      _destroy_conf(void);
static void      _fill_registration_msg(slurm_node_registration_status_msg_t *);
static int _get_tls_certificate(void);
static int _increment_thd_count(bool block);
static void      _init_conf(void);
static int       _memory_spec_init(void);
static void _notify_parent_of_success(void);
static void      _print_conf(void);
static void      _print_config(void);
static void      _print_gres(void);
static void      _process_cmdline(int ac, char **av);
static void      _read_config(void);
static void     *_registration_engine(void *arg);
static void      _resource_spec_fini(void);
static int       _resource_spec_init(void);
static void      _select_spec_cores(void);
static int       _set_slurmd_spooldir(const char *dir);
static int       _set_topo_info(void);
static int       _set_work_dir(void);
static int       _slurmd_init(void);
static int       _slurmd_fini(void);
static void *_try_to_reconfig(void *ptr);
static void      _update_nice(void);
static void      _usage(void);
static int       _validate_and_convert_cpu_list(void);
static void      _wait_for_all_threads(int secs);
static void _wait_on_old_slurmd(bool kill_it);
static void *_service_connection(void *arg);

/**************************************************************************\
 * To test for memory leaks, set MEMORY_LEAK_DEBUG to 1 using
 * "configure --enable-memory-leak-debug" then execute
 *
 * $ valgrind --tool=memcheck --leak-check=yes --num-callers=40 \
 *   --leak-resolution=high --child-silent-after-fork=yes \
 *   --suppressions=<DIR>/hwloc/hwloc-valgrind.supp \
 *   ./slurmd -Dc >valg.slurmd.out 2>&1
 *
 * Then exercise the slurmctld functionality before executing
 * > scontrol shutdown
 *
 * Note that --enable-memory-leak-debug will cause the daemon to
 * unload the shared objects at exit thus preventing valgrind
 * to display the stack where the eventual leaks may be.
 * It is always best to test with and without --enable-memory-leak-debug.
 *
 * The HWLOC library generates quite a few memory leaks unless the following
 *    option is added to the valgrind execute line:
 *    --suppressions=<INSTALL_DIR>/share/hwloc/hwloc-valgrind.supp
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

static void _on_sigint(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGINT. Shutting down.");
	slurmd_shutdown();
}

static void _on_sigterm(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGTERM. Shutting down.");
	slurmd_shutdown();
}

static void _on_sigquit(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGQUIT. Shutting down.");
	slurmd_shutdown();
}

static void _on_sighup(conmgr_callback_args_t conmgr_args, void *arg)
{
	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED)
		return;

	info("Caught SIGHUP. Triggering reconfigure.");

	slurm_thread_create_detached(_try_to_reconfig, NULL);
}

static void _on_sigusr2(conmgr_callback_args_t conmgr_args, void *arg)
{
	DEF_TIMERS;

	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED)
		return;

	info("Caught SIGUSR2. Triggering logging update.");

	START_TIMER;

	update_slurmd_logging(LOG_LEVEL_END);
	update_stepd_logging(false);

	END_TIMER3(__func__, TIMEOUT_SIGUSR2);
}

static void _on_sigpipe(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGPIPE. Ignoring.");
}

static void _unquiesce_fd_listener(void)
{
	conmgr_fd_t *con = NULL;

	slurm_mutex_lock(&listen_mutex);
	if (listener) {
		con = conmgr_fd_get_ref(listener);
		conmgr_unquiesce_fd(con);
	} else {
		/* Need to unquiesce when on_connect() called */
		unquiesce_listener = true;
	}
	slurm_mutex_unlock(&listen_mutex);
}

int
main (int argc, char **argv)
{
	uint32_t curr_uid = 0;
	log_options_t lopts = LOG_OPTS_INITIALIZER;
	char *oom_value;
	char time_stamp[256];
	int pidfd;

	if (getenv("SLURMD_RECONF"))
		original = false;

	/* NOTE: logfile is NULL at this point */
	log_init(argv[0], lopts, LOG_DAEMON, NULL);

	if (original) {
		/*
		 * Make sure we have no extra open files which
		 * could propagate to spawned tasks.
		 */
		closeall(3);

		/* Drop supplementary groups. */
		if (geteuid())
			debug("Not running as root. Can't drop supplementary groups");
		else if (setgroups(0, NULL) < 0)
			fatal("Failed to drop supplementary groups, setgroups: %m");
	}

	/*
	 * Create and set default values for the slurmd global
	 * config variable "conf"
	 */
	conf = xmalloc(sizeof(slurmd_conf_t));
	_init_conf();
	conf->argv = argv;
	conf->argc = argc;

	/*
	 * Process commandline arguments first, since one option may be
	 * an alternate location for the slurm config file.
	 */
	_process_cmdline(conf->argc, conf->argv);

	/*
	 * Become a daemon if desired.
	 */
	if (original && conf->daemonize && xdaemon())
		fatal_abort("%s: xdaemon() failed: %m", __func__);

	if (_slurmd_init() < 0) {
		error( "slurmd initialization failed" );
		fflush( NULL );
		exit(1);
	}

	curr_uid = getuid();
	if (curr_uid != slurm_conf.slurmd_user_id) {
		char *slurmd_user =
			uid_to_string_or_null(slurm_conf.slurmd_user_id);
		char *curr_user = uid_to_string_or_null(curr_uid);

		fatal("You are running slurmd as something other than user %s(%u). "
		      "If you want to run as this user add SlurmdUser=%s to the slurm.conf file.",
		      slurmd_user, slurm_conf.slurm_user_id, curr_user);
	}

	debug3("slurmd initialization successful");

	test_core_limit();
	info("slurmd version %s started", SLURM_VERSION_STRING);
	debug3("finished daemonize");

	conmgr_init(SLURMD_CONMGR_DEFAULT_THREADS,
		    SLURMD_CONMGR_DEFAULT_MAX_CONNECTIONS,
		    (conmgr_callbacks_t) {0});

	conmgr_add_work_signal(SIGINT, _on_sigint, NULL);
	conmgr_add_work_signal(SIGTERM, _on_sigterm, NULL);
	conmgr_add_work_signal(SIGQUIT, _on_sigquit, NULL);
	conmgr_add_work_signal(SIGHUP, _on_sighup, NULL);
	conmgr_add_work_signal(SIGUSR2, _on_sigusr2, NULL);
	conmgr_add_work_signal(SIGPIPE, _on_sigpipe, NULL);

	if ((oom_value = getenv("SLURMD_OOM_ADJ"))) {
		int i = atoi(oom_value);
		debug("Setting slurmd oom_adj to %d", i);
		set_oom_adj(i);
	}

	if (original)
		_wait_on_old_slurmd(true);

	if (conf->mlock_pages) {
		/*
		 * Call mlockall() if available to ensure slurmd
		 *  doesn't get swapped out
		 */
		if (mlockall (MCL_FUTURE | MCL_CURRENT) < 0)
			error ("failed to mlock() slurmd pages: %m");
	}

	cred_state_init();

	if (acct_gather_conf_init() != SLURM_SUCCESS)
		fatal("Unable to initialize acct_gather_conf");
	if (jobacct_gather_init() != SLURM_SUCCESS)
		fatal("Unable to initialize jobacct_gather");
	if (job_container_init() < 0)
		fatal("Unable to initialize job_container plugin.");
	if (container_g_restore(conf->spooldir, !conf->cleanstart))
		error("Unable to restore job_container state.");
	if (prep_g_init(NULL) != SLURM_SUCCESS)
		fatal("failed to initialize prep plugin");
	if (switch_g_init(false) < 0)
		fatal("Unable to initialize switch plugin.");
	if (node_features_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize node_features plugin");
	if (mpi_g_daemon_init() != SLURM_SUCCESS)
		fatal("Failed to initialize MPI plugins.");
	if (select_g_init(1) != SLURM_SUCCESS)
		fatal("Failed to initialize select plugins.");
	file_bcast_init();
	if ((run_command_init(argc, argv, conf->binary) != SLURM_SUCCESS) &&
	    conf->binary[0])
		fatal("%s: Unable to reliably execute %s",
		      __func__, conf->binary);

	plugins_registered = true;

	_create_msg_socket();

	conf->pid = getpid();

	rfc2822_timestamp(time_stamp, sizeof(time_stamp));
	info("%s started on %s", slurm_prog_name, time_stamp);

	slurm_conf_install_fork_handlers();

	if (!original) {
		_notify_parent_of_success();
		if (conf->daemonize)
			_wait_on_old_slurmd(false);
	} else if (under_systemd)
		xsystemd_change_mainpid(getpid());

	if (!under_systemd)
		pidfd = create_pidfile(conf->pidfile, 0);

	conmgr_run(false);

	if (original)
		run_script_health_check();

	record_launched_jobs();
	slurm_thread_create_detached(_registration_engine, NULL);

	/* Allow listening socket to start accept()ing incoming */
	_unquiesce_fd_listener();

	conmgr_run(true);

	/*
	 * Unlink now while the slurm_conf.pidfile is still accessible,
	 * but do not close until later. Closing the file will release
	 * the flock, which will then let a new slurmd process start.
	 */
	if (!under_systemd && unlink(conf->pidfile) < 0)
		error("Unable to remove pidfile `%s': %m",
		      conf->pidfile);

	/* Wait for prolog/epilog scripts to finish or timeout */
	_wait_for_all_threads(slurm_conf.prolog_epilog_timeout);
	/*
	 * run_command_shutdown() will kill any scripts started with
	 * run_command() including the prolog and epilog.
	 * Call run_command_shutdown() *after* waiting for threads to complete
	 * to give prolog and epilog scripts a chance to finish,
	 * otherwise jobs will fail and the node will be drained due to prolog
	 * failure.
	 */
	run_command_shutdown();
	_slurmd_fini();
	_destroy_conf();
	cred_g_fini();	/* must be after _destroy_conf() */
	group_cache_purge();
	file_bcast_purge();

	/*
	 * Explicitly close the pidfile after all other shutdown has completed
	 * which will release the flock.
	 */
	fd_close(&pidfd);

	info("Slurmd shutdown completing");

	conmgr_fini();
	log_fini();

	return SLURM_SUCCESS;
}

static void _get_tls_cert_work(conmgr_callback_args_t conmgr_args, void *arg)
{
	if (_get_tls_certificate()) {
		error("%s: Unable to get TLS certificate", __func__);
	}
}

static int _get_tls_certificate(void)
{
	slurm_msg_t req, resp;
	tls_cert_request_msg_t cert_req = { 0 };
	tls_cert_response_msg_t *cert_resp;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	if (!certmgr_enabled()) {
		log_flag(TLS, "certmgr not enabled, skipping process to get signed TLS certificate from slurmctld (assume node already has signed TLS certificate)");
		return SLURM_SUCCESS;
	}

	/* Periodically renew TLS certificate indefinitely */
	conmgr_add_work_delayed_fifo(
		_get_tls_cert_work, NULL,
		certmgr_get_renewal_period_mins() * MINUTE_SECONDS, 0);

	if (!(cert_req.token = certmgr_g_get_node_token(conf->node_name))) {
		error("%s: Failed to get unique node token", __func__);
		return SLURM_ERROR;
	}

	if (!(cert_req.csr = certmgr_g_generate_csr(conf->node_name))) {
		error("%s: Failed to generate certificate signing request",
		      __func__);
		return SLURM_ERROR;
	}

	cert_req.node_name = xstrdup(conf->node_name);

	req.msg_type = REQUEST_TLS_CERT;
	req.data = &cert_req;

	if (slurm_send_recv_controller_msg(&req, &resp, working_cluster_rec)
	    < 0) {
		error("Unable to get TLS certificate from slurmctld: %m");
		return SLURM_ERROR;
	}

	switch (resp.msg_type) {
	case RESPONSE_TLS_CERT:
		break;
	case RESPONSE_SLURM_RC:
	{
		uint32_t resp_rc =
			((return_code_msg_t *) resp.data)->return_code;
		error("%s: slurmctld response to TLS certificate request: %s",
		      __func__, slurm_strerror(resp_rc));
		return SLURM_ERROR;
	}
	default:
		error("%s: slurmctld responded with unexpected msg type: %s",
		      __func__, rpc_num2string(resp.msg_type));
		return SLURM_ERROR;
	}

	cert_resp = resp.data;

	log_flag(TLS, "Successfully got signed certificate from slurmctld: \n%s",
		 cert_resp->signed_cert);

	return SLURM_SUCCESS;
}

/*
 * Spawn a thread to make sure we send at least one registration message to
 * slurmctld. If slurmctld restarts, it will request another registration
 * message.
 */
static void *
_registration_engine(void *arg)
{
	static const uint32_t MAX_DELAY = 128;
	uint32_t delay = 1;
	(void) _increment_thd_count(true);

	while (!_shutdown && !sent_reg_time) {
		int rc;

		if (_get_tls_certificate())
			error("Unable to get TLS certificate");

		if (!(rc = send_registration_msg(SLURM_SUCCESS)))
			break;

		debug("Unable to register with slurm controller (retry in %us): %s",
		      delay, slurm_strerror(rc));

		sleep(delay);

		/* increase delay until max on every failure */
		delay *= 2;
		if (delay > MAX_DELAY)
			delay = MAX_DELAY;
	}

	debug3("%s complete", __func__);

	_decrement_thd_count();
	return NULL;
}

static void _decrement_thd_count(void)
{
	slurm_mutex_lock(&active_mutex);
	if (active_threads > 0)
		active_threads--;
	slurm_cond_signal(&active_cond);
	slurm_mutex_unlock(&active_mutex);
}

/*
 * Increment thread count
 * IN block - true to block on too many active threads
 * RET SLURM_SUCCESS or EWOULDBLOCK
 */
static int _increment_thd_count(bool block)
{
	bool logged = false;

	slurm_mutex_lock(&active_mutex);
	while (active_threads >= MAX_THREADS) {
		if (!logged) {
			info("active_threads == MAX_THREADS(%d)",
			     MAX_THREADS);
			logged = true;
		}

		if (block)  {
			slurm_cond_wait(&active_cond, &active_mutex);
		} else {
			slurm_mutex_unlock(&active_mutex);
			return EWOULDBLOCK;
		}
	}
	active_threads++;
	slurm_mutex_unlock(&active_mutex);
	return SLURM_SUCCESS;
}

/* secs IN - wait up to this number of seconds for all threads to complete */
static void
_wait_for_all_threads(int secs)
{
	struct timespec ts;
	int rc;

	ts.tv_sec  = time(NULL);
	ts.tv_nsec = 0;
	ts.tv_sec += secs;

	slurm_mutex_lock(&active_mutex);
	while (active_threads > 0) {
		verbose("waiting on %d active threads", active_threads);
		if (secs == NO_VAL16) { /* Wait forever */
			slurm_cond_wait(&active_cond, &active_mutex);
		} else {
			rc = pthread_cond_timedwait(&active_cond,
						    &active_mutex, &ts);
			if (rc == ETIMEDOUT) {
				error("Timeout waiting for completion of %d threads",
				      active_threads);
				slurm_cond_signal(&active_cond);
				slurm_mutex_unlock(&active_mutex);
				return;
			}
		}
	}
	slurm_cond_signal(&active_cond);
	slurm_mutex_unlock(&active_mutex);
	verbose("all threads complete");
}

static void *_service_connection(void *arg)
{
	service_connection_args_t *args = arg;
	slurm_msg_t *msg = NULL;
	slurm_addr_t *addr = &args->addr;
	int fd = args->fd;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == SERVICE_CONNECTION_ARGS_MAGIC);

	debug3("%s: [%pA] processing new RPC connection", __func__, addr);

	msg = xmalloc_nz(sizeof(*msg));
	slurm_msg_t_init(msg);

	msg->flags |= SLURM_MSG_KEEP_BUFFER;

	if ((rc = slurm_receive_msg_and_forward(fd, addr, msg))) {
		error("service_connection: slurm_receive_msg: %m");
		/*
		 * if this fails we need to make sure the nodes we forward
		 * to are taken care of and sent back. This way the control
		 * also has a better idea what happened to us
		 */
		if (msg->auth_ids_set)
			slurm_send_rc_msg(msg, rc);
		else {
			debug("%s: incomplete message", __func__);
			forward_wait(msg);
		}
		goto cleanup;
	}
	debug2("Start processing RPC: %s", rpc_num2string(msg->msg_type));

	if (slurm_conf.debug_flags & DEBUG_FLAG_AUDIT_RPCS) {
		log_flag(AUDIT_RPCS, "msg_type=%s uid=%u client=[%pA] protocol=%u",
			 rpc_num2string(msg->msg_type), msg->auth_uid,
			 &addr, msg->protocol_version);
	}

	slurmd_req(msg);

cleanup:
	if ((msg->conn_fd >= 0) && close(msg->conn_fd) < 0)
		error ("close(%d): %m", fd);

	debug2("Finish processing RPC: %s", rpc_num2string(msg->msg_type));

	slurm_free_msg(msg);

	args->magic = ~SERVICE_CONNECTION_ARGS_MAGIC;
	xfree(args);

	_decrement_thd_count();
	return NULL;
}

static int _load_gres()
{
	int rc;
	uint32_t cpu_cnt;
	node_record_t *node_rec;
	list_t *gres_list = NULL;

	node_rec = find_node_record2(conf->node_name);
	if (node_rec && node_rec->config_ptr) {
		(void) gres_init_node_config(node_rec->config_ptr->gres,
					     &gres_list);
	}

	cpu_cnt = MAX(conf->conf_cpus, conf->block_map_size);
	rc = gres_g_node_config_load(cpu_cnt, conf->node_name, gres_list,
				     (void *)&xcpuinfo_abs_to_mac,
				     (void *)&xcpuinfo_mac_to_abs);
	FREE_NULL_LIST(gres_list);

	return rc;
}

static void _handle_node_reg_resp(slurm_msg_t *resp_msg)
{
	int rc;
	slurm_node_reg_resp_msg_t *resp = NULL;

	switch (resp_msg->msg_type) {
	case RESPONSE_NODE_REGISTRATION:
		resp = resp_msg->data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg->data)->return_code;
		if (rc)
			errno = rc;
		break;
	default:
		errno = SLURM_UNEXPECTED_MSG_ERROR;
		break;
	}


	if (resp) {
		/*
		 * We don't care about the assoc/qos locks
		 * assoc_mgr_post_tres_list is requesting as those lists
		 * don't exist here.
		 */
		assoc_mgr_lock_t locks = { .tres = WRITE_LOCK };
		uint32_t prev_tres_count;
		bool rebuild_conf_buf = false;

		/*
		 * We only needed the resp to get the tres the first time,
		 * Set it so we don't request it again.
		 */
		if (get_reg_resp)
			get_reg_resp = false;

		sent_successful_registration = true;

		assoc_mgr_lock(&locks);
		prev_tres_count = g_tres_count;
		assoc_mgr_post_tres_list(resp->tres_list);
		debug("%s: slurmctld sent back %u TRES.",
		       __func__, g_tres_count);

		/*
		 * If we change the TRES on the slurmctld we need to rebuild the
		 * config buf being sent to the stepds.
		 */
		if (prev_tres_count && (prev_tres_count != g_tres_count))
			rebuild_conf_buf = true;

		assoc_mgr_unlock(&locks);

		/*
		 * We have to call this outside of the assoc_mgr locks so we can
		 * keep the locking order correct as build_conf_buf() locks the
		 * assoc_mgr inside the conf->config_mutex.  If we called
		 * build_conf_buf() inside the locks above we would do it out of
		 * order.
		 */
		if (rebuild_conf_buf)
			build_conf_buf();

		/*
		 * Signal any threads potentially waiting to run.
		 */
		slurm_mutex_lock(&tres_mutex);
		slurm_cond_broadcast(&tres_cond);
		slurm_mutex_unlock(&tres_mutex);

		/* assoc_mgr_post_tres_list will destroy the list */
		resp->tres_list = NULL;

		/*
		 * Get the mapped node name for a dynamic future node so the
		 * slurmd can find slurm.conf config record.
		 */
		if ((conf->dynamic_type == DYN_NODE_FUTURE) &&
		    resp->node_name) {
			debug2("dynamic node response %s -> %s",
			       conf->node_name, resp->node_name);
			xfree(conf->node_name);
			conf->node_name = xstrdup(resp->node_name);
		}
	}
}

extern int send_registration_msg(uint32_t status)
{
	int ret_val = SLURM_SUCCESS;
	slurm_msg_t req, resp_msg;
	slurm_node_registration_status_msg_t *msg =
		xmalloc(sizeof(slurm_node_registration_status_msg_t));

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp_msg);

	if (get_reg_resp)
		msg->flags |= SLURMD_REG_FLAG_RESP;
	if (conf->conf_cache)
		msg->flags |= SLURMD_REG_FLAG_CONFIGLESS;

	_fill_registration_msg(msg);
	msg->status = status;

	req.msg_type = MESSAGE_NODE_REGISTRATION_STATUS;
	req.data = msg;

	ret_val = slurm_send_recv_controller_msg(&req, &resp_msg,
						 working_cluster_rec);
	slurm_free_node_registration_status_msg(msg);

	if (ret_val < 0) {
		error("Unable to register: %m");
		ret_val = SLURM_ERROR;
		goto fail;
	}

	_handle_node_reg_resp(&resp_msg);
	slurm_free_msg_data(resp_msg.msg_type, resp_msg.data);

	if (errno) {
		ret_val = errno;
		errno = 0;
	}

	if (ret_val == SLURM_SUCCESS)
		sent_reg_time = time(NULL);
fail:
	return ret_val;
}

static void
_fill_registration_msg(slurm_node_registration_status_msg_t *msg)
{
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd;
	int  n;
	char *arch, *os;
	struct utsname buf;
	static bool first_msg = true;
	buf_t *gres_info;

	if (!sent_successful_registration) {
		/*
		 * Only send this on the first registration. These are values
		 * that shouldn't be overwritten if modified by
		 * 'scontrol update' after the node's first registration.
		 */
		msg->extra = xstrdup(conf->extra);
		msg->instance_id = xstrdup(conf->instance_id);
		msg->instance_type = xstrdup(conf->instance_type);
	}

	msg->dynamic_type = conf->dynamic_type;
	msg->dynamic_conf = xstrdup(conf->dynamic_conf);
	msg->dynamic_feature = xstrdup(conf->dynamic_feature);

	msg->hostname = xstrdup(conf->hostname);
	msg->node_name   = xstrdup (conf->node_name);
	msg->version     = xstrdup(SLURM_VERSION_STRING);

	msg->cpus	 = conf->cpus;
	msg->boards	 = conf->boards;
	msg->sockets	 = conf->sockets;
	msg->cores	 = conf->cores;
	msg->threads	 = conf->threads;
	msg->cpu_spec_list = xstrdup(conf->cpu_spec_list);
	msg->real_memory = conf->physical_memory_size;
	msg->tmp_disk    = conf->tmp_disk_space;
	msg->hash_val = slurm_conf.hash_val;
	get_cpu_load(&msg->cpu_load);
	get_free_mem(&msg->free_mem);

	gres_info = init_buf(1024);
	if (gres_node_config_pack(gres_info) != SLURM_SUCCESS)
		error("error packing gres configuration");
	else
		msg->gres_info   = gres_info;

	get_up_time(&conf->up_time);
	msg->up_time     = conf->up_time;
	if (slurmd_start_time == 0)
		slurmd_start_time = time(NULL);
	msg->slurmd_start_time = slurmd_start_time;

	slurm_mutex_lock(&cached_features_mutex);
	if (refresh_cached_features && plugins_registered) {
		xfree(cached_features_avail);
		xfree(cached_features_active);
		node_features_g_node_state(&cached_features_avail,
					   &cached_features_active);
		refresh_cached_features = false;
	}
	msg->features_avail = xstrdup(cached_features_avail);
	msg->features_active = xstrdup(cached_features_active);
	slurm_mutex_unlock(&cached_features_mutex);

	if (first_msg) {
		first_msg = false;
		info("CPUs=%u Boards=%u Sockets=%u Cores=%u Threads=%u "
		     "Memory=%"PRIu64" TmpDisk=%u Uptime=%u CPUSpecList=%s "
		     "FeaturesAvail=%s FeaturesActive=%s",
		     msg->cpus, msg->boards, msg->sockets, msg->cores,
		     msg->threads, msg->real_memory, msg->tmp_disk,
		     msg->up_time, msg->cpu_spec_list, msg->features_avail,
		     msg->features_active);
	} else {
		debug3("CPUs=%u Boards=%u Sockets=%u Cores=%u Threads=%u "
		       "Memory=%"PRIu64" TmpDisk=%u Uptime=%u CPUSpecList=%s "
		       "FeaturesAvail=%s FeaturesActive=%s",
		       msg->cpus, msg->boards, msg->sockets, msg->cores,
		       msg->threads, msg->real_memory, msg->tmp_disk,
		       msg->up_time, msg->cpu_spec_list, msg->features_avail,
		       msg->features_active);
	}
	uname(&buf);
	if ((arch = getenv("SLURM_ARCH")))
		msg->arch = xstrdup(arch);
	else
		msg->arch = xstrdup(buf.machine);
	if ((os = getenv("SLURM_OS"))) {
		msg->os   = xstrdup(os);
	} else {
		xstrfmtcat(msg->os, "%s %s %s",
			   buf.sysname, buf.release, buf.version);
	}

	steps = stepd_available(conf->spooldir, conf->node_name);
	msg->job_count = list_count(steps);
	msg->step_id = xmalloc(msg->job_count * sizeof(*msg->step_id));

	i = list_iterator_create(steps);
	n = 0;
	while ((stepd = list_next(i))) {
		int fd;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   &stepd->step_id,
				   &stepd->protocol_version);
		if (fd == -1) {
			--(msg->job_count);
			continue;
		}

		if (stepd_state(fd, stepd->protocol_version)
		    == SLURMSTEPD_NOT_RUNNING) {
			debug("stale domain socket for %ps ", &stepd->step_id);
			--(msg->job_count);
			close(fd);
			continue;
		}

		close(fd);
		memcpy(&msg->step_id[n], &stepd->step_id,
		       sizeof(msg->step_id[n]));

		if (stepd->step_id.step_id == SLURM_BATCH_SCRIPT) {
			debug("%s: found apparently running job %u",
			      __func__, stepd->step_id.job_id);
		} else {
			debug("%s: found apparently running %ps",
			      __func__, &stepd->step_id);
		}
		n++;
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	if (plugins_registered) {
		if (!msg->energy)
			msg->energy = acct_gather_energy_alloc(1);
		acct_gather_energy_g_get_sum(ENERGY_DATA_NODE_ENERGY,
					     msg->energy);
	}

	msg->timestamp = time(NULL);

	return;
}

/*
 * Read the slurm configuration file (slurm.conf) and substitute some
 * values into the slurmd configuration in preference of the defaults.
 */
static void
_read_config(void)
{
	char *bcast_address;
	slurm_conf_t *cf = NULL;
	int cc;
	bool cgroup_mem_confinement = false;
#ifndef HAVE_FRONT_END
	node_record_t *node_ptr;
	bool cr_flag = false, gang_flag = false;
	bool config_overrides = false;
#endif

	slurm_mutex_lock(&conf->config_mutex);
	cf = slurm_conf_lock();

	if (conf->conffile == NULL)
		conf->conffile = xstrdup(cf->slurm_conf);

	/*
	 * Allow for Prolog and Epilog scripts to have non-absolute paths.
	 * This is needed for configless to work with Prolog and Epilog.
	 */
	for (int i = 0; i < cf->prolog_cnt; i++) {
		char *tmp_prolog = cf->prolog[i];
		cf->prolog[i] = get_extra_conf_path(tmp_prolog);
		xfree(tmp_prolog);
	}
	for (int i = 0; i < cf->epilog_cnt; i++) {
		char *tmp_epilog = cf->epilog[i];
		cf->epilog[i] = get_extra_conf_path(tmp_epilog);
		xfree(tmp_epilog);
	}


#ifndef HAVE_FRONT_END
	/*
	 * We can't call slurm_select_cr_type() because we don't load the select
	 * plugin here.
	 */
	if (!xstrcmp(cf->select_type, "select/cons_tres"))
		cr_flag = true;

	if (cf->preempt_mode & PREEMPT_MODE_GANG)
		gang_flag = true;
#endif

	slurm_conf_unlock();
	/* node_name may already be set from a command line parameter */
	if (conf->node_name == NULL)
		conf->node_name = slurm_conf_get_nodename(conf->hostname);

	/*
	 * If we didn't match the form of the hostname already stored in
	 * conf->hostname, check to see if we match any valid aliases
	 */
	if (conf->node_name == NULL)
		conf->node_name = slurm_conf_get_aliased_nodename();

	if (conf->node_name == NULL)
		conf->node_name = slurm_conf_get_nodename("localhost");

	if (!conf->node_name || conf->node_name[0] == '\0')
		fatal("Unable to determine this slurmd's NodeName");

	if ((bcast_address = slurm_conf_get_bcast_address(conf->node_name))) {
		if (xstrcasestr(slurm_conf.comm_params, "NoInAddrAny"))
			fatal("Cannot use BcastAddr option on this node with CommunicationParameters=NoInAddrAny");
		xfree(bcast_address);
	}

	if (!conf->logfile)
		conf->logfile = slurm_conf_expand_slurmd_path(
			cf->slurmd_logfile,
			conf->node_name,
			conf->hostname);

#ifndef HAVE_FRONT_END
	if (!(node_ptr = find_node_record(conf->node_name))) {
		error("Unable to find node record for %s",
		      conf->node_name);
		exit(1);
	}

	conf->port = node_ptr->port;
	slurm_conf.slurmd_port = conf->port;

	conf->conf_boards = node_ptr->boards;
	conf->conf_cores = node_ptr->cores;
	conf->conf_cpus = node_ptr->cpus;
	conf->conf_sockets = node_ptr->tot_sockets;
	conf->conf_threads = node_ptr->threads;
	conf->core_spec_cnt = node_ptr->core_spec_cnt;
	conf->cpu_spec_list = xstrdup(node_ptr->cpu_spec_list);
	conf->mem_spec_limit = node_ptr->mem_spec_limit;
#else
	conf->port = slurm_conf_get_frontend_port(conf->node_name);
#endif

	/* store hardware properties in slurmd_config */
	xfree(conf->block_map);
	xfree(conf->block_map_inv);

	/*
	 * This must be reset before update_slurmd_logging(), otherwise the
	 * slurmstepd processes will not get the reconfigure request, and logs
	 * may be lost if the path changed or the log was rotated.
	 */
	_free_and_set(conf->spooldir,
		      slurm_conf_expand_slurmd_path(
			      cf->slurmd_spooldir,
			      conf->node_name,
			      conf->hostname));
	/*
	 * Only rebuild this if running configless, which is indicated by
	 * the presence of a conf_cache value.
	 */
	if (conf->conf_cache)
		_free_and_set(conf->conf_cache,
			      xstrdup_printf("%s/conf-cache", conf->spooldir));

	update_slurmd_logging(LOG_LEVEL_END);
	update_stepd_logging(true);
	_update_nice();

	conf->actual_cpus = 0;

	if (!conf->conf_cache && xstrcasestr(cf->slurmctld_params,
					     "enable_configless"))
		warning("Running with local config file despite slurmctld having been setup for configless operation");

	/*
	 * xcpuinfo_hwloc_topo_get here needs spooldir to be set before
	 * it will work properly.  This is the earliest we can unset def_config.
	 */
	conf->def_config = false;
	xcpuinfo_hwloc_topo_get(&conf->actual_cpus,
				&conf->actual_boards,
				&conf->actual_sockets,
				&conf->actual_cores,
				&conf->actual_threads,
				&conf->block_map_size,
				&conf->block_map, &conf->block_map_inv);
#ifdef HAVE_FRONT_END
	/*
	 * When running with multiple frontends, the slurmd S:C:T values are not
	 * relevant, hence ignored by both _register_front_ends (sets all to 1)
	 * and validate_nodes_via_front_end (uses slurm.conf values).
	 * Report actual hardware configuration.
	 */
	conf->cpus    = conf->actual_cpus;
	conf->boards  = conf->actual_boards;
	conf->sockets = conf->actual_sockets;
	conf->cores   = conf->actual_cores;
	conf->threads = conf->actual_threads;
#else
	/* If the actual resources on a node differ than what is in
	 * the configuration file and we are using
	 * cons_res or gang scheduling we have to use what is in the
	 * configuration file because the slurmctld creates bitmaps
	 * for scheduling before these nodes check in.
	 */
	config_overrides = cf->conf_flags & CONF_FLAG_OR;
	if (conf->dynamic_type == DYN_NODE_FUTURE) {
		/* Already set to actual config earlier in _dynamic_init() */
	} else if ((conf->conf_sockets == conf->actual_cpus) &&
		   (conf->conf_cpus == conf->actual_cpus) &&
		   (conf->conf_cores == 1) &&
		   (conf->conf_threads == 1)) {
		/*
		 * Only "CPUs=" was configured in the node definition. Lie about
		 * the actual hardware so that more than one job can run on a
		 * single core. Keep the current configured values.
		 */
		conf->cpus = conf->conf_cpus;
		conf->boards = conf->conf_boards;
		conf->sockets = conf->actual_sockets = conf->actual_cpus;
		conf->cores = conf->actual_cores = 1;
		conf->threads = conf->actual_threads = 1;
	} else if (conf->dynamic_type == DYN_NODE_NORM) {
		conf->cpus = conf->conf_cpus;
		conf->boards = conf->conf_boards;
		conf->sockets = conf->conf_sockets;
		conf->cores = conf->conf_cores;
		conf->threads = conf->conf_threads;
	} else if (!config_overrides && (conf->actual_cpus < conf->conf_cpus)) {
		conf->cpus    = conf->actual_cpus;
		conf->boards  = conf->actual_boards;
		conf->sockets = conf->actual_sockets;
		conf->cores   = conf->actual_cores;
		conf->threads = conf->actual_threads;
	} else if (!config_overrides && (cr_flag || gang_flag) &&
		   (conf->actual_sockets != conf->conf_sockets) &&
		   (conf->actual_cores != conf->conf_cores) &&
		   ((conf->actual_sockets * conf->actual_cores) ==
		    (conf->conf_sockets * conf->conf_cores))) {
		/* Socket and core count can be changed when KNL node reboots
		 * in a different NUMA configuration */
		info("Node reconfigured socket/core boundaries "
		     "SocketsPerBoard=%u:%u(hw) CoresPerSocket=%u:%u(hw)",
		     (conf->conf_sockets / conf->conf_boards),
		     (conf->actual_sockets / conf->actual_boards),
		     conf->conf_cores, conf->actual_cores);
		conf->cpus    = conf->conf_cpus;
		conf->boards  = conf->conf_boards;
		conf->sockets = conf->actual_sockets;
		conf->cores   = conf->actual_cores;
		conf->threads = conf->conf_threads;
	} else {
		conf->cpus    = conf->conf_cpus;
		conf->boards  = conf->conf_boards;
		conf->sockets = conf->conf_sockets;
		conf->cores   = conf->conf_cores;
		conf->threads = conf->conf_threads;
	}

	if ((conf->cpus != conf->actual_cpus) &&
	    ((conf->cpus == conf->actual_cores) ||
	     (conf->cpus == conf->actual_sockets))) {
		log_var(config_overrides ? LOG_LEVEL_INFO : LOG_LEVEL_DEBUG,
			"CPUs has been set to match %s per node instead of threads CPUs=%u:%u(hw)",
			(conf->cpus == conf->actual_cores) ?
			"cores" : "sockets",
			conf->cpus, conf->actual_cpus);
	}

	if (((conf->cpus != conf->actual_cpus) &&
	     (conf->cpus != conf->actual_cores) &&
	     (conf->cpus != conf->actual_sockets)) ||
	    (conf->sockets != conf->actual_sockets) ||
	    (conf->cores   != conf->actual_cores)   ||
	    (conf->threads != conf->actual_threads)) {
		log_var(config_overrides ? LOG_LEVEL_INFO : LOG_LEVEL_ERROR,
			"Node configuration differs from hardware: CPUs=%u:%u(hw) Boards=%u:%u(hw) SocketsPerBoard=%u:%u(hw) CoresPerSocket=%u:%u(hw) ThreadsPerCore=%u:%u(hw)",
			conf->cpus,    conf->actual_cpus,
			conf->boards,  conf->actual_boards,
			(conf->sockets / conf->boards),
			(conf->actual_sockets / conf->actual_boards),
			conf->cores,   conf->actual_cores,
			conf->threads, conf->actual_threads);
	}
#endif

#ifdef HAVE_FRONT_END
	get_memory(&conf->conf_memory_size);
#else
	/*
	 * Set the node's configured 'RealMemory' as conf_memory_size as
	 * slurmd_conf_t->real_memory is set to the actual physical memory. We
	 * need to distinguish from configured memory and actual physical
	 * memory. Actual physical memory is reported to the controller to
	 * validate that the slurmd's memory isn't less than the configured
	 * memory and the configured memory is needed to setup the slurmd's
	 * memory cgroup.
	 */
	conf->conf_memory_size = node_ptr->real_memory;
#endif

	get_memory(&conf->physical_memory_size);
	get_up_time(&conf->up_time);

	cf = slurm_conf_lock();
	_free_and_set(conf->tmp_fs,
		      slurm_conf_expand_slurmd_path(
			      cf->tmp_fs,
			      conf->node_name,
			      conf->hostname));
	_free_and_set(conf->pidfile,
		      slurm_conf_expand_slurmd_path(
			      cf->slurmd_pidfile,
			      conf->node_name,
			      conf->hostname));

	get_tmp_disk(&conf->tmp_disk_space, conf->tmp_fs);

	conf->syslog_debug = cf->slurmd_syslog_debug;

	conf->acct_freq_task = NO_VAL16;
	cc = acct_gather_parse_freq(PROFILE_TASK, cf->job_acct_gather_freq);
	if (cc != -1)
		conf->acct_freq_task = cc;

	if (cf->control_addr == NULL)
		fatal("Unable to establish controller machine");
	if (cf->slurmctld_port == 0)
		fatal("Unable to establish controller port");

	slurm_mutex_unlock(&conf->config_mutex);

	slurm_conf_unlock();

	cgroup_mem_confinement = cgroup_memcg_job_confinement();

	if (slurm_conf.job_acct_oom_kill && cgroup_mem_confinement)
		fatal("Jobs memory is being constrained by both TaskPlugin cgroup and JobAcctGather plugin. This enables two incompatible memory enforcement mechanisms, one of them must be disabled.");
}

/*
 * Build a slurmd configuration buffer _once_ for sending to slurmstepd
 * This must happen after all configuration is available, including topology
 */
extern void build_conf_buf(void)
{
	slurm_mutex_lock(&conf->config_mutex);
	FREE_NULL_BUFFER(conf->buf);
	conf->buf = init_buf(0);
	pack_slurmd_conf_lite(conf, conf->buf);
	pack_slurm_conf_lite(conf->buf);
	if (assoc_mgr_tres_list) {
		assoc_mgr_lock_t locks = { .tres = READ_LOCK };
		assoc_mgr_lock(&locks);
		slurm_pack_list(assoc_mgr_tres_list,
				slurmdb_pack_tres_rec, conf->buf,
				SLURM_PROTOCOL_VERSION);
		assoc_mgr_unlock(&locks);
		tres_packed = true;
	} else
		tres_packed = false;

	slurm_mutex_unlock(&conf->config_mutex);
}

static int _reconfig_stepd(void *x, void *y)
{
	step_loc_t *stepd = x;
	bool reconfig = *(bool *) y;
	buf_t *reconf = NULL;
	int fd = stepd_connect(stepd->directory, stepd->nodename,
			       &stepd->step_id, &stepd->protocol_version);
	if (fd == -1)
		return 0;

	if (reconfig) {
		reconf = init_buf(1024);
		pack_stepd_reconf(reconf, stepd->protocol_version);
	}
	if (stepd_reconfig(fd, stepd->protocol_version, reconf) != SLURM_SUCCESS)
		debug("Reconfig %ps failed: %m", &stepd->step_id);
	close(fd);
	FREE_NULL_BUFFER(reconf);

	return 0;
}

extern void update_stepd_logging(bool reconfig)
{
	list_t *steps;

	steps = stepd_available(conf->spooldir, conf->node_name);
	list_for_each(steps, _reconfig_stepd, &reconfig);
	FREE_NULL_LIST(steps);
}

static void _notify_parent_of_success(void)
{
	char *parent_fd_env = getenv("SLURMD_RECONF_PARENT_FD");
	pid_t pid = getpid();
	int fd = -1;

	if (!parent_fd_env)
		return;

	fd = atoi(parent_fd_env);
	info("child started successfully");
	safe_write(fd, &pid, sizeof(pid_t));
	(void) close(fd);
	return;

rwfail:
	error("failed to notify parent, may have two processes running now");
	(void) close(fd);
	return;
}

static void *_try_to_reconfig(void *ptr)
{
	extern char **environ;
	struct rlimit rlim;
	char **child_env;
	pid_t pid;
	int to_parent[2] = {-1, -1};
	int rpc_wait = MAX(5, slurm_conf.msg_timeout);
	int close_skip[] = { -1, -1, -1, -1 }, skip_index = 0, auth_fd = -1;
	DEF_TIMERS;

	if ((auth_fd = auth_g_get_reconfig_fd(AUTH_PLUGIN_SLURM)) >= 0)
		close_skip[skip_index++] = auth_fd;

	conmgr_quiesce(__func__);

	START_TIMER;

	if (slurm_conf.prolog_epilog_timeout != NO_VAL16)
		rpc_wait = MAX(rpc_wait, slurm_conf.prolog_epilog_timeout);

	/* Wait for RPCs to finish */
	_wait_for_all_threads(rpc_wait);

	if (_shutdown) {
		conmgr_unquiesce(__func__);
		return NULL;
	}

	save_cred_state();

	if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) {
		error("getrlimit(RLIMIT_NOFILE): %m");
		rlim.rlim_cur = 4096;
	}

	child_env = env_array_copy((const char **) environ);
	setenvf(&child_env, "SLURMD_RECONF", "1");
	if (conf->boot_time)
		setenvf(&child_env, "SLURMD_BOOT_TIME", "%ld", conf->boot_time);
	if (conf->conf_cache)
		setenvf(&child_env, "SLURMD_RECONF_CONF_CACHE", "%s",
			conf->conf_cache);
	if (conf->lfd != -1) {
		setenvf(&child_env, "SLURMD_RECONF_LISTEN_FD", "%d", conf->lfd);
		fd_set_noclose_on_exec(conf->lfd);
		close_skip[skip_index++] = conf->lfd;
		debug3("%s: retaining listener socket fd:%d", __func__, conf->lfd);
	}

	if (!conf->daemonize && !under_systemd)
		goto start_child;

	if (pipe(to_parent))
		fatal("%s: pipe() failed: %m", __func__);

	setenvf(&child_env, "SLURMD_RECONF_PARENT_FD", "%d", to_parent[1]);
	close_skip[skip_index++] = to_parent[1];

	if ((pid = fork()) < 0) {
		fatal("%s: fork() failed, cannot reconfigure.", __func__);
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

		info("Relinquishing control to new slurmd process (%d)",
		     grandchild_pid);
		if (under_systemd) {
			/*
			 * Ensure child has exited.
			 * Grandchild should be owned by init then.
			 */
			waitpid(pid, &rc, 0);
			xsystemd_change_mainpid(grandchild_pid);
		}
		_exit(0);

rwfail:
		close(to_parent[0]);
		env_array_free(child_env);
		waitpid(pid, &rc, 0);
		info("Resuming operation, reconfigure failed.");

		END_TIMER3(__func__, TIMEOUT_RECONFIG);
		conmgr_unquiesce(__func__);
		return NULL;
	}

start_child:
	closeall_except(3, close_skip);

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

	execve(conf->binary, conf->argv, child_env);
	fatal("execv() failed: %m");
}

static void
_print_conf(void)
{
	slurm_conf_t *cf;
	char *str = NULL, time_str[32];
	int i;

	if (get_log_level() < LOG_LEVEL_DEBUG3)
		return;

	cf = slurm_conf_lock();
	debug3("NodeName    = %s",       conf->node_name);
	debug3("TopoAddr    = %s",       conf->node_topo_addr);
	debug3("TopoPattern = %s",       conf->node_topo_pattern);
	debug3("ClusterName = %s",       cf->cluster_name);
	debug3("Confile     = `%s'",     conf->conffile);
	debug3("Debug       = %d",       cf->slurmd_debug);
	debug3("CPUs        = %-2u (CF: %2u, HW: %2u)",
	       conf->cpus,
	       conf->conf_cpus,
	       conf->actual_cpus);
	debug3("Boards      = %-2u (CF: %2u, HW: %2u)",
	       conf->boards,
	       conf->conf_boards,
	       conf->actual_boards);
	debug3("Sockets     = %-2u (CF: %2u, HW: %2u)",
	       conf->sockets,
	       conf->conf_sockets,
	       conf->actual_sockets);
	debug3("Cores       = %-2u (CF: %2u, HW: %2u)",
	       conf->cores,
	       conf->conf_cores,
	       conf->actual_cores);
	debug3("Threads     = %-2u (CF: %2u, HW: %2u)",
	       conf->threads,
	       conf->conf_threads,
	       conf->actual_threads);

	secs2time_str((time_t)conf->up_time, time_str, sizeof(time_str));
	debug3("UpTime      = %u = %s", conf->up_time, time_str);

	for (i = 0; i < conf->block_map_size; i++)
		xstrfmtcat(str, "%s%u", (str ? "," : ""),
			   conf->block_map[i]);
	debug3("Block Map   = %s", str);
	xfree(str);
	for (i = 0; i < conf->block_map_size; i++)
		xstrfmtcat(str, "%s%u", (str ? "," : ""),
			   conf->block_map_inv[i]);
	debug3("Inverse Map = %s", str);
	xfree(str);

	debug3("ConfMemory  = %"PRIu64"", conf->conf_memory_size);
	debug3("PhysicalMem = %"PRIu64"", conf->physical_memory_size);
	debug3("TmpDisk     = %u",       conf->tmp_disk_space);

	for (int i = 0; i < cf->epilog_cnt; i++)
		debug3("Epilog[%d] = `%s'", i, cf->epilog[i]);

	debug3("Logfile     = `%s'",     conf->logfile);
	debug3("HealthCheck = `%s'",     cf->health_check_program);
	debug3("NodeName    = %s",       conf->node_name);
	debug3("Port        = %u",       conf->port);

	for (int i = 0; i < cf->prolog_cnt; i++)
		debug3("Prolog[%d] = `%s'", i, cf->prolog[i]);

	debug3("TmpFS       = `%s'",     conf->tmp_fs);
	debug3("Slurmstepd  = `%s'",     conf->stepd_loc);
	debug3("Spool Dir   = `%s'",     conf->spooldir);
	debug3("Syslog Debug  = %d",     cf->slurmd_syslog_debug);
	debug3("Pid File    = `%s'",     conf->pidfile);
	debug3("Slurm UID   = %u",       cf->slurm_user_id);
	debug3("TaskProlog  = `%s'",     cf->task_prolog);
	debug3("TaskEpilog  = `%s'",     cf->task_epilog);
	debug3("TaskPluginParam = %u",   cf->task_plugin_param);
	debug3("UsePAM      = %"PRIu64, (cf->conf_flags & CONF_FLAG_PAM));
	slurm_conf_unlock();
}

/* Initialize slurmd configuration table.
 * Everything is already NULL/zero filled when called */
static void _init_conf(void)
{
	char host[HOST_NAME_MAX];
	log_options_t lopts = LOG_OPTS_INITIALIZER;

	if (gethostname_short(host, HOST_NAME_MAX) < 0) {
		error("Unable to get my hostname: %m");
		exit(1);
	}
	conf->hostname = xstrdup(host);
	conf->daemonize = true;
	conf->def_config =  true;
	conf->lfd = -1;
	conf->log_opts = lopts;
	conf->debug_level = LOG_LEVEL_INFO;
	conf->spooldir = xstrdup(DEFAULT_SPOOLDIR);
	conf->setwd = false;
	conf->print_gres = false;

	slurm_mutex_init(&conf->config_mutex);

	conf->starting_steps = list_create(xfree_ptr);
	slurm_cond_init(&conf->starting_steps_cond, NULL);
	conf->prolog_running_jobs = list_create(xfree_ptr);
	slurm_cond_init(&conf->prolog_running_cond, NULL);
}

static void
_destroy_conf(void)
{
	if (conf) {
		xfree(conf->block_map);
		xfree(conf->block_map_inv);
		FREE_NULL_BUFFER(conf->buf);
		xfree(conf->conffile);
		xfree(conf->conf_server);
		xfree(conf->conf_cache);
		xfree(conf->cpu_spec_list);
		xfree(conf->dynamic_conf);
		xfree(conf->dynamic_feature);
		xfree(conf->extra);
		xfree(conf->hostname);
		if (conf->hwloc_xml) {
			/*
			 * When a slurmd is taking over the place of the next
			 * slurmd it will have already made this file.  So don't
			 * remove it or it will remove it for the new slurmd.
			 */
			/* (void)remove(conf->hwloc_xml); */
			xfree(conf->hwloc_xml);
		}
		xfree(conf->instance_id);
		xfree(conf->instance_type);
		xfree(conf->logfile);
		xfree(conf->node_name);
		xfree(conf->node_topo_addr);
		xfree(conf->node_topo_pattern);
		xfree(conf->pidfile);
		xfree(conf->spooldir);
		xfree(conf->stepd_loc);
		xfree(conf->tmp_fs);
		slurm_mutex_destroy(&conf->config_mutex);
		FREE_NULL_LIST(conf->starting_steps);
		slurm_cond_destroy(&conf->starting_steps_cond);
		FREE_NULL_LIST(conf->prolog_running_jobs);
		slurm_cond_destroy(&conf->prolog_running_cond);
		xfree(conf);
	}
	return;
}

static void
_print_config(void)
{
	int days, hours, mins, secs;
	char name[128], *gres_str = NULL, *autodetect_str = NULL;
	node_config_load_t node_conf = {
		/* Set cpu_cnt later */
		.in_slurmd = true,
		.gres_name = "gpu",
		.xcpuinfo_mac_to_abs = xcpuinfo_mac_to_abs
	};

	/* Since it is not running the daemon, silence the log output */
	(conf->log_opts).logfile_level = LOG_LEVEL_QUIET;
	(conf->log_opts).syslog_level = LOG_LEVEL_QUIET;

	/* Print to fatals to terminal by default (-v for more verbosity) */
	if (conf->debug_level_set)
		(conf->log_opts).stderr_level = conf->debug_level;
	else
		(conf->log_opts).stderr_level = LOG_LEVEL_FATAL;

	log_alter(conf->log_opts, SYSLOG_FACILITY_USER, NULL);

	gethostname_short(name, sizeof(name));
	xcpuinfo_hwloc_topo_get(&conf->actual_cpus,
				&conf->actual_boards,
				&conf->actual_sockets,
				&conf->actual_cores,
				&conf->actual_threads,
				&conf->block_map_size,
				&conf->block_map, &conf->block_map_inv);

	/* Set sockets and cores for xcpuinfo_mac_to_abs */
	conf->cpus = conf->actual_cpus;
	conf->boards = conf->actual_boards;
	conf->sockets = conf->actual_sockets;
	conf->cores = conf->actual_cores;
	conf->threads = conf->actual_threads;
	node_conf.cpu_cnt = MAX(conf->actual_cpus, conf->block_map_size);
	/* Use default_plugin_path here to avoid reading slurm.conf */
	slurm_conf.plugindir = xstrdup(default_plugin_path);
	gres_get_autodetected_gpus(node_conf, &gres_str, &autodetect_str);

	get_memory(&conf->physical_memory_size);

	printf("NodeName=%s CPUs=%u Boards=%u SocketsPerBoard=%u CoresPerSocket=%u ThreadsPerCore=%u RealMemory=%"PRIu64"%s%s\n",
	       name, conf->actual_cpus, conf->actual_boards,
	       (conf->actual_sockets / conf->actual_boards), conf->actual_cores,
	       conf->actual_threads, conf->physical_memory_size,
	       gres_str ? " Gres=" : "", gres_str ? gres_str : "");
	if (autodetect_str)
		printf("%s\n", autodetect_str);

	get_up_time(&conf->up_time);
	secs  =  conf->up_time % 60;
	mins  = (conf->up_time / 60) % 60;
	hours = (conf->up_time / 3600) % 24;
	days  = (conf->up_time / 86400);
	printf("UpTime=%u-%2.2u:%2.2u:%2.2u\n", days, hours, mins, secs);
}

static void _print_gres(void)
{
	log_options_t *o = &conf->log_opts;

	o->logfile_level = LOG_LEVEL_QUIET;
	o->stderr_level = conf->debug_level;
	o->syslog_level = LOG_LEVEL_INFO;

	log_alter(conf->log_opts, SYSLOG_FACILITY_USER, NULL);

	_load_gres();

	exit(0);
}

static void
_process_cmdline(int ac, char **av)
{
	static char *opt_string = "bcCd:Df:F::GhL:Mn:N:svVZ";
	int c;
	char *tmp_char;

	enum {
		LONG_OPT_ENUM_START = 0x100,
		LONG_OPT_AUTHINFO,
		LONG_OPT_CONF,
		LONG_OPT_CONF_SERVER,
		LONG_OPT_EXTRA,
		LONG_OPT_INSTANCE_ID,
		LONG_OPT_INSTANCE_TYPE,
		LONG_OPT_SYSTEMD,
	};

	static struct option long_options[] = {
		{"authinfo",		required_argument, 0, LONG_OPT_AUTHINFO},
		{"conf",		required_argument, 0, LONG_OPT_CONF},
		{"conf-server",		required_argument, 0, LONG_OPT_CONF_SERVER},
		{"extra",		required_argument, 0, LONG_OPT_EXTRA},
		{"instance-id",		required_argument, 0, LONG_OPT_INSTANCE_ID},
		{"instance-type",	required_argument, 0, LONG_OPT_INSTANCE_TYPE},
		{"systemd",		no_argument,       0, LONG_OPT_SYSTEMD},
		{"version",		no_argument,       0, 'V'},
		{NULL,			0,                 0, 0}
	};

	if (run_command_is_launcher(ac, av)) {
		run_command_launcher(ac, av);
		_exit(127); /* Should not get here */
	}
	conf->prog = xbasename(av[0]);

	while ((c = getopt_long(ac, av, opt_string, long_options, NULL)) > 0) {
		switch (c) {
		case 'b':
		{
			char *boot = getenv("SLURMD_BOOT_TIME");
			if (boot)
				conf->boot_time = strtol(boot, NULL, 10);
			else
				conf->boot_time = time(NULL);
			break;
		}
		case 'c':
			if (original)
				conf->cleanstart = 1;
			break;
		case 'C':
			_print_config();
			exit(0);
			break;
		case 'd':
			xfree(conf->stepd_loc);
			conf->stepd_loc = xstrdup(optarg);
			break;
		case 'D':
			conf->daemonize = 0;
			break;
		case 'f':
			xfree(conf->conffile);
			conf->conffile = xstrdup(optarg);
			break;
		case 'F':
			if (conf->dynamic_type == DYN_NODE_NORM) {
				error("-F and -Z options are mutually exclusive");
				exit(1);
			}
			conf->dynamic_type = DYN_NODE_FUTURE;
			conf->dynamic_feature = xstrdup(optarg);
			break;
		case 'G':
			conf->debug_level_set = 1;
			conf->daemonize = 0;
			conf->print_gres = true;
			break;
		case 'h':
			_usage();
			exit(0);
			break;
		case 'L':
			xfree(conf->logfile);
			conf->logfile = xstrdup(optarg);
			break;
		case 'M':
			conf->mlock_pages = 1;
			break;
		case 'n':
			conf->nice = strtol(optarg, &tmp_char, 10);
			if (tmp_char[0] != '\0') {
				error("Invalid option for -n option (nice value), ignored");
				conf->nice = 0;
			}
			break;
		case 'N':
			xfree(conf->node_name);
			conf->node_name = xstrdup(optarg);
			break;
		case 's':
			conf->setwd = true;
			break;
		case 'v':
			conf->debug_level++;
			conf->debug_level_set = 1;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		case 'Z':
			if (conf->dynamic_type == DYN_NODE_FUTURE) {
				error("-F and -Z options are mutually exclusive");
				exit(1);
			}
			conf->dynamic_type = DYN_NODE_NORM;
			break;
		case LONG_OPT_AUTHINFO:
			slurm_conf.authinfo = xstrdup(optarg);
			break;
		case LONG_OPT_CONF:
			conf->dynamic_conf = xstrdup(optarg);
			break;
		case LONG_OPT_CONF_SERVER:
			conf->conf_server = xstrdup(optarg);
			break;
		case LONG_OPT_EXTRA:
			conf->extra = xstrdup(optarg);
			break;
		case LONG_OPT_INSTANCE_ID:
			conf->instance_id = xstrdup(optarg);
			break;
		case LONG_OPT_INSTANCE_TYPE:
			conf->instance_type = xstrdup(optarg);
			break;
		case LONG_OPT_SYSTEMD:
			under_systemd = true;
			break;
		default:
			_usage();
			exit(1);
			break;
		}
	}

	if (under_systemd && !conf->daemonize)
		fatal("--systemd and -D options are mutually exclusive");

	/*
	 *  If slurmstepd path wasn't overridden by command line, set
	 *  it to the default here:
	 */
	if (!conf->stepd_loc)
		conf->stepd_loc = slurm_get_stepd_loc();

	/*
	 * Set instance_type and id to "" so that the controller will clear any
	 * previous instance_type or id.
	 *
	 * "extra" is left NULL if not explicitly set on the cmd line so that
	 * any existing "extra" information on the controller is left intact.
	 */
	if (!conf->instance_id)
		conf->instance_id = xstrdup("");
	if (!conf->instance_type)
		conf->instance_type = xstrdup("");

	if (under_systemd) {
		if (!getenv("NOTIFY_SOCKET"))
			fatal("Missing NOTIFY_SOCKET.");
		conf->daemonize = false;
		conf->setwd = true;
	}

	/*
	 * Using setwd() later means a relative path to ourselves may shift.
	 * Capture /proc/self/exe now and save this for reconfig later.
	 * Cannot wait to capture it later as Linux will append " (deleted)"
	 * to the filename if it's been replaced, which would break reconfig
	 * after an upgrade.
	 */
	if (conf->argv[0][0] != '/') {
		if (readlink("/proc/self/exe", conf->binary, PATH_MAX) < 0)
			fatal("%s: readlink failed: %m", __func__);
	} else {
		strlcpy(conf->binary, conf->argv[0], PATH_MAX);
	}
}

static void *_on_listen_connect(conmgr_fd_t *con, void *arg)
{
	debug3("%s: [%s] Successfully opened slurm listen port %u",
	       __func__, conmgr_fd_get_name(con), conf->port);

	slurm_mutex_lock(&listen_mutex);
	xassert(!listener);
	listener = conmgr_fd_new_ref(con);

	if (unquiesce_listener) {
		/* on_connect() happened after _unquiesce_fd_listener() */
		conmgr_unquiesce_fd(con);
	}
	slurm_mutex_unlock(&listen_mutex);

	slurmd_req(NULL);	/* initialize timer */

	return con;
}

static void _on_listen_finish(conmgr_fd_t *con, void *arg)
{
	xassert(con == arg);

#ifndef NDEBUG
	slurm_mutex_lock(&listen_mutex);
	xassert(!listener);
	slurm_mutex_unlock(&listen_mutex);
#endif

	debug3("%s: [%s] closed RPC listener. Queuing up cleanup.",
	       __func__, conmgr_fd_get_name(con));

	conf->lfd = -1;
}

/* Try to process connection if thread max has not been hit */
static void _try_service_connection(conmgr_callback_args_t conmgr_args,
				    void *arg)
{
	service_connection_args_t *args = arg;
	int rc = SLURM_ERROR;

	xassert(args->magic == SERVICE_CONNECTION_ARGS_MAGIC);

	if (!(rc = _increment_thd_count(false))) {
		debug3("%s: [%pA] detaching new thread for RPC connection",
		       __func__, &args->addr);

		slurm_thread_create_detached(_service_connection, args);
	} else {
		xassert(rc == EWOULDBLOCK);

		/*
		 * Servicing the connection will be deferred which means the
		 * thread count should no longer consider processing this
		 * connection until the delay is complete and this function is
		 * called again.
		 */
		_decrement_thd_count();

		debug3("%s: [%pA] deferring servicing connection",
		       __func__, &args->addr);

		/*
		 * Backoff attempts to avoid needless lock contention while
		 * avoiding having a new thread created
		 */
		args->delay = timespec_add(args->delay, MAX_THREAD_DELAY_INC);
		if (timespec_is_after(args->delay, MAX_THREAD_DELAY_MAX))
			args->delay = MAX_THREAD_DELAY_MAX;

		conmgr_add_work_delayed_fifo(_try_service_connection, args,
					     args->delay.tv_sec,
					     args->delay.tv_sec);
	}
}

static void _on_extract_fd(conmgr_callback_args_t conmgr_args,
			   int input_fd, int output_fd, void *arg)
{
	service_connection_args_t *args = NULL;
	int rc = SLURM_SUCCESS;

	xassert(!arg);

	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED) {
		debug3("%s: [fd:%d] connection work cancelled",
		       __func__, input_fd);

		if (input_fd != output_fd)
			fd_close(&output_fd);
		fd_close(&input_fd);
		return;
	}

	if ((input_fd < 0) || (output_fd < 0)) {
		error("%s: Rejecting partially open connection input_fd=%d output_fd=%d",
		      __func__, input_fd, output_fd);
		if (input_fd != output_fd)
			fd_close(&output_fd);
		fd_close(&input_fd);
		return;
	}

	args = xmalloc(sizeof(*args));
	args->magic = SERVICE_CONNECTION_ARGS_MAGIC;
	args->addr.ss_family = AF_UNSPEC;
	args->fd = input_fd;

	if ((rc = slurm_get_peer_addr(input_fd, &args->addr))) {
		error("%s: [fd:%d] getting socket peer failed: %s",
		      __func__, input_fd, slurm_strerror(rc));
		fd_close(&input_fd);
		args->magic = ~SERVICE_CONNECTION_ARGS_MAGIC;
		xfree(args);
		return;
	}

	/* force blocking mode for blocking handlers */
	fd_set_blocking(input_fd);

	_try_service_connection(conmgr_args, args);
}

static void *_on_connection(conmgr_fd_t *con, void *arg)
{
	int rc;

	debug3("%s: [%s] New RPC connection",
	       __func__, conmgr_fd_get_name(con));

	if ((rc = conmgr_queue_extract_con_fd(con, _on_extract_fd,
					      XSTRINGIFY(_on_extract_fd),
					      NULL))) {
		error("%s: [%s] Extracting FDs failed: %s",
		      __func__, conmgr_fd_get_name(con), slurm_strerror(rc));
		return NULL;
	}

	return con;
}

static void _on_finish(conmgr_fd_t *con, void *arg)
{
	xassert(arg == con);

	debug3("%s: [%s] RPC connection closed",
	       __func__, conmgr_fd_get_name(con));
}

static int _on_msg(conmgr_fd_t *con, slurm_msg_t *msg, int unpack_rc, void *arg)
{
	fatal_abort("should never happen");
}

static void _create_msg_socket(void)
{
	static const conmgr_events_t events = {
		.on_listen_connect = _on_listen_connect,
		.on_listen_finish = _on_listen_finish,
		.on_connection = _on_connection,
		.on_msg = _on_msg,
		.on_finish = _on_finish,
	};
	int rc;

	if (getenv("SLURMD_RECONF_LISTEN_FD")) {
		conf->lfd = atoi(getenv("SLURMD_RECONF_LISTEN_FD"));
		debug2("%s: inherited socket on fd:%d", __func__, conf->lfd);
	} else if ((conf->lfd = slurm_init_msg_engine_port(conf->port)) < 0) {
		fatal("Unable to bind listen port (%u): %m", conf->port);
	}

	if ((rc = conmgr_process_fd_listen(conf->lfd, CON_TYPE_RPC, &events,
					   CON_FLAG_QUIESCE, NULL)))
		fatal("%s: unable to process fd:%d error:%s",
		      __func__, conf->lfd, slurm_strerror(rc));
}

static void
_stepd_cleanup_batch_dirs(const char *directory, const char *nodename)
{
	DIR *dp;
	struct dirent *ent;
	struct stat stat_buf;

	/*
	 * Make sure that "directory" exists and is a directory.
	 */
	if (stat(directory, &stat_buf) < 0) {
		error("SlurmdSpoolDir stat error %s: %m", directory);
		return;
	} else if (!S_ISDIR(stat_buf.st_mode)) {
		error("SlurmdSpoolDir is not a directory %s", directory);
		return;
	}

	if ((dp = opendir(directory)) == NULL) {
		error("SlurmdSpoolDir open error %s: %m", directory);
		return;
	}

	while ((ent = readdir(dp)) != NULL) {
		if (!xstrncmp(ent->d_name, "job", 3) &&
		    (ent->d_name[3] >= '0') && (ent->d_name[3] <= '9')) {
			char *dir_path = NULL, *file_path = NULL;
			xstrfmtcat(dir_path, "%s/%s", directory, ent->d_name);
			xstrfmtcat(file_path, "%s/slurm_script", dir_path);
			info("%s: Purging vestigial job script %s",
			     __func__, file_path);
			(void) unlink(file_path);
			(void) rmdir(dir_path);
			xfree(dir_path);
			xfree(file_path);
		}
	}
	closedir(dp);
}

/*
 * See precedence rules before in comment for _establish_configuration().
 */
static bool _slurm_conf_file_exists(void)
{
	struct stat stat_buf;

	if (conf->conffile)
		return true;
	if ((conf->conffile = xstrdup(getenv("SLURM_CONF"))))
		return true;

	if (!stat(default_slurm_config_file, &stat_buf)) {
		conf->conffile = xstrdup(default_slurm_config_file);
		return true;
	}

	return false;
}

/*
 * Create /run/slurm/ if it does not exist, and add a symlink from
 * /run/slurm/conf to the conf-cache directory.
 *
 * User commands will test this if they've been unsuccessful locating
 * an alternate config.
 *
 * In the future we may disable this with a setting in SlurmdParameters,
 * but at the moment you would need to have enabled configless mode which
 * implies you are probably okay with this.
 *
 * It is not considered a critical error if this does not work on your system,
 * thus the minimized error handling.
 *
 * No attempt is made to deal with multiple-slurmd mode. Last slurmd started
 * will win.
 */
static void _handle_slash_run(void)
{
	if (_set_slurmd_spooldir("/run/slurm") < 0) {
		error("Unable to create /run/slurm dir");
		return;
	}

	(void) unlink("/run/slurm/conf");

	if (symlink(conf->conf_cache, "/run/slurm/conf"))
		error("Unable to create /run/slurm/conf symlink: %m");
}

/*
 * Configuration precedence rules for slurmd:
 * 1. conf_server if set
 * 2. SLURM_CONF_SERVER if set (not documented, meant for testing only)
 * 3. direct file
 *   a. conffile (-f option) if not NULL
 *   b. SLURM_CONF if not NULL
 *   c. default_slurm_config_file if it exists
 * 4. DNS SRV records if available
 */
static int _establish_configuration(void)
{
	config_response_msg_t *configs;

	if ((conf->conf_cache = xstrdup(getenv("SLURMD_RECONF_CONF_CACHE")))) {
		xstrfmtcat(conf->conffile, "%s/slurm.conf", conf->conf_cache);
		slurm_conf_init(conf->conffile);
		return SLURM_SUCCESS;
	}

	if (!conf->conf_server && _slurm_conf_file_exists()) {
		debug("%s: config will load from file", __func__);
		slurm_conf_init(conf->conffile);
		return SLURM_SUCCESS;
	}

	while (!(configs = fetch_config(conf->conf_server,
					CONFIG_REQUEST_SLURMD))) {
		error("%s: failed to load configs. Retrying in 10 seconds.",
		      __func__);
		sleep(10);
	}

	/*
	 * One limitation - if node_name was not set through -N
	 * the %n replacement here will not be possible since we can't
	 * load the node tables yet.
	 */
	_free_and_set(conf->spooldir,
		      slurm_conf_expand_slurmd_path(
			      configs->slurmd_spooldir,
			      conf->node_name,
			      conf->hostname));

	if (_set_slurmd_spooldir(conf->spooldir) < 0) {
		error("Unable to initialize slurmd spooldir");
		return SLURM_ERROR;
	}

	xfree(conf->conf_cache);
	xstrfmtcat(conf->conf_cache, "%s/conf-cache", conf->spooldir);
	if (_set_slurmd_spooldir(conf->conf_cache) < 0) {
		error("Unable to initialize slurmd conf-cache dir");
		return SLURM_ERROR;
	}

	if (write_configs_to_conf_cache(configs, conf->conf_cache))
		return SLURM_ERROR;

	slurm_free_config_response_msg(configs);
	xfree(conf->conffile);
	xstrfmtcat(conf->conffile, "%s/slurm.conf", conf->conf_cache);

	/*
	 * Be sure to force this in the environment as get_extra_conf_path()
	 * will pull from there. Not setting it means the plugins may fail
	 * to load their own configs... which may not cause problems for
	 * slurmd but will cause slurmstepd to fail later on.
	 */
	setenv("SLURM_CONF", conf->conffile, 1);

	_handle_slash_run();

	return SLURM_SUCCESS;
}

static int _build_node_callback(char *alias, char *hostname, char *address,
				char *bcast_address, uint16_t port,
				int state_val, slurm_conf_node_t *conf_node,
				config_record_t *config_ptr)
{
	int rc = SLURM_SUCCESS;
	node_record_t *node_ptr;

	if ((rc = create_node_record(config_ptr, alias, &node_ptr)))
		return rc;

	if ((state_val != NO_VAL) &&
	    (state_val != NODE_STATE_UNKNOWN))
		node_ptr->node_state = state_val;
	node_ptr->last_response = (time_t) 0;
	node_ptr->comm_name = xstrdup(address);
	node_ptr->cpu_bind  = conf_node->cpu_bind;
	node_ptr->node_hostname = xstrdup(hostname);
	node_ptr->bcast_address = xstrdup(bcast_address);
	node_ptr->port = port;
	node_ptr->reason = xstrdup(conf_node->reason);

	node_ptr->node_state |= NODE_STATE_DYNAMIC_NORM;

	slurm_conf_add_node(node_ptr);

	return rc;
}

static int _create_nodes(char *nodeline, char **err_msg)
{
	int rc = SLURM_SUCCESS;
	slurm_conf_node_t *conf_node;
	config_record_t *config_ptr;
	s_p_hashtbl_t *node_hashtbl = NULL;

	xassert(nodeline);
	xassert(err_msg);

	if (!xstrstr(slurm_conf.select_type, "cons_tres")) {
		*err_msg = xstrdup("Node creation only compatible with select/cons_tres");
		error("%s", *err_msg);
		return ESLURM_ACCESS_DENIED;
	}

	if (!(conf_node = slurm_conf_parse_nodeline(nodeline, &node_hashtbl))) {
		*err_msg = xstrdup_printf("Failed to parse nodeline '%s'",
					  nodeline);
		error("%s", *err_msg);
		rc = SLURM_ERROR;
		goto fini;
	}

	config_ptr = config_record_from_conf_node(conf_node, 0);
	if ((rc = expand_nodeline_info(conf_node, config_ptr, err_msg,
				       _build_node_callback)))
		error("%s", *err_msg);
	s_p_hashtbl_destroy(node_hashtbl);

fini:
	return rc;
}

static void _validate_dynamic_conf(void)
{
	char *invalid_opts[] = {
		"NodeName=",
		NULL
	};

	if (!conf->dynamic_conf)
		return;

	for (int i = 0; invalid_opts[i]; i++) {
		if (xstrcasestr(conf->dynamic_conf, invalid_opts[i]))
			fatal("option '%s' not allowed in --conf",
			      invalid_opts[i]);
	}
}

static void _dynamic_init(void)
{
	if (!conf->dynamic_type)
		return;

	slurm_mutex_lock(&conf->config_mutex);

	if ((conf->dynamic_type == DYN_NODE_FUTURE) && conf->node_name) {
		/*
		 * You can't specify a node name with dynamic future nodes,
		 * otherwise the slurmd will keep registering as a new dynamic
		 * future node because the node_name won't map to the hostname.
		 */
		fatal("Specifying a node name for dynamic future nodes is not supported.");
	}

	/* Use -N name if specified. */
	if (!conf->node_name) {
		char hostname[HOST_NAME_MAX];
		if (!gethostname(hostname, HOST_NAME_MAX))
			conf->node_name = xstrdup(hostname);
	}

	xcpuinfo_hwloc_topo_get(&conf->actual_cpus,
				&conf->actual_boards,
				&conf->actual_sockets,
				&conf->actual_cores,
				&conf->actual_threads,
				&conf->block_map_size,
				&conf->block_map, &conf->block_map_inv);

	conf->cpus    = conf->actual_cpus;
	conf->boards  = conf->actual_boards;
	conf->sockets = conf->actual_sockets;
	conf->cores   = conf->actual_cores;
	conf->threads = conf->actual_threads;
	get_memory(&conf->physical_memory_size);

	switch (conf->dynamic_type) {
	case DYN_NODE_FUTURE:
		/*
		 * dynamic future nodes need to be mapped to a slurm.conf node
		 * in order to load in correct configs (e.g. gres, etc.). First
		 * get the mapped node_name from the slurmctld.
		 */
		send_registration_msg(SLURM_SUCCESS);

		/* send registration again after loading everything in */
		sent_reg_time = 0;
		break;
	case DYN_NODE_NORM:
	{
		/*
		 * Build NodeName config line for slurmd and slurmctld to
		 * process and create instances from -- so things like Gres and
		 * CoreSpec work. A dynamic normal node doesn't need/can't to
		 * map to slurm.conf config record so no need to ask the
		 * slurmctld for a node name like dynamic future nodes have to
		 * do.
		 */
		char *err_msg = NULL;
		char *tmp;

		_validate_dynamic_conf();

		tmp = xstrdup_printf("NodeName=%s ", conf->node_name);
		if (xstrcasestr(conf->dynamic_conf, "CPUs=") ||
		    xstrcasestr(conf->dynamic_conf, "Boards=") ||
		    xstrcasestr(conf->dynamic_conf, "SocketsPerBoard=") ||
		    xstrcasestr(conf->dynamic_conf, "CoresPerSocket=") ||
		    xstrcasestr(conf->dynamic_conf, "ThreadsPerCore=")) {
			/* Using what the user gave */
		} else {
			xstrfmtcat(tmp, "CPUs=%u Boards=%u SocketsPerBoard=%u CoresPerSocket=%u ThreadsPerCore=%u ",
				conf->actual_cpus,
				conf->actual_boards,
				(conf->actual_sockets / conf->actual_boards),
				conf->actual_cores,
				conf->actual_threads);
		}

		if (!xstrcasestr(conf->dynamic_conf, "RealMemory="))
			xstrfmtcat(tmp, "RealMemory=%"PRIu64" ",
				   conf->physical_memory_size);

		if (conf->dynamic_conf)
			xstrcat(tmp, conf->dynamic_conf);

		xfree(conf->dynamic_conf);
		conf->dynamic_conf = tmp;

		if (_create_nodes(conf->dynamic_conf, &err_msg)) {
			fatal("failed to create dynamic node '%s'",
			      conf->dynamic_conf);
		}
		xfree(err_msg);
		break;
	}
	default:
		fatal("unknown dynamic registration type: %d",
		      conf->dynamic_type);
	}
	slurm_mutex_unlock(&conf->config_mutex);
}

static int
_slurmd_init(void)
{
	struct rlimit rlim;
	struct stat stat_buf;
	int rc = SLURM_SUCCESS;

	/*
	 * Work out how this node is going to be configured. If running in
	 * "configless" mode, also populate the conf-cache directory.
	 */
	if (_establish_configuration())
		return SLURM_ERROR;

	/*
	 * Build nodes table like in slurmctld
	 * This is required by the topology stack
	 * Node tables setup must precede _read_config() so that the
	 * proper hostname is set.
	 */
	slurm_conf_init(conf->conffile);
	init_node_conf();

	if (conf->print_gres)
		slurm_conf.debug_flags = DEBUG_FLAG_GRES;
	if (gres_init() != SLURM_SUCCESS)
		return SLURM_ERROR;
	if (build_all_nodeline_info(true, 0))
		return SLURM_ERROR;
	build_all_frontend_info(true);

	/*
	 * This needs to happen before _read_config where we will try to read
	 * cgroup.conf values
	 */
	if (cgroup_conf_init() != SLURM_SUCCESS)
		log_flag(CGROUP, "cgroup conf was already initialized.");

	/*
	 * If we are in the process of daemonizing ourselves, do not refresh
	 * the hwloc as the grandparent process have already done it. This
	 * is important as we're already constrained by cgroups in a specific
	 * cpuset, and hwloc does not return the correct e-cores vs p-cores
	 * kinds.
	 */
	xcpuinfo_refresh_hwloc(original);

	/*
	 * auth/slurm calls conmgr_init and we need to apply conmgr params
	 * before conmgr init.
	 */
	if (slurm_conf.slurmd_params)
		conmgr_set_params(slurm_conf.slurmd_params);

	/*
	 * auth and hash plugins must be initialized before the first dynamic
	 * future registration is send.
	 */
	if (auth_g_init() != SLURM_SUCCESS)
		return SLURM_ERROR;
	if (hash_g_init() != SLURM_SUCCESS)
		return SLURM_ERROR;
	if (certmgr_g_init() != SLURM_SUCCESS)
		return SLURM_ERROR;

	_dynamic_init();

	/*
	 * Read global slurm config file, override necessary values from
	 * defaults and command line.
	 */
	_read_config();
	/*
	 * This needs to happen before _resource_spec_init where we will try to
	 * attach the slurmd pid to system cgroup, and after _read_config to
	 * have proper logging.
	 */
	if (cgroup_g_init() != SLURM_SUCCESS) {
		error("Unable to initialize cgroup plugin");
		return SLURM_ERROR;
	}

#ifndef HAVE_FRONT_END
	if (!find_node_record(conf->node_name))
		return SLURM_ERROR;
#endif

	/*
	 * slurmd -G, calling it here rather than from _process_cmdline
	 * since it relies on gres_init and _read_config.
	 */
	if (conf->print_gres)
		_print_gres();

	/*
	 * Make sure all further plugin init() calls see this value to ensure
	 * they read from the correct directory, and that the slurmstepd
	 * picks up the correct configuration when fork()'d.
	 * Required for correct operation of the -f flag.
	 */
	setenv("SLURM_CONF", conf->conffile, 1);

	/*
	 * Create slurmd spool directory if necessary.
	 */
	if (_set_slurmd_spooldir(conf->spooldir) < 0) {
		error("Unable to initialize slurmd spooldir");
		return SLURM_ERROR;
	}

	/* Set up the hwloc whole system xml file */
	if (xcpuinfo_init() != SLURM_SUCCESS)
		return SLURM_ERROR;

	fini_job_cnt = MAX(conf->conf_cpus, conf->block_map_size);
	fini_job_id = xmalloc(sizeof(uint32_t) * fini_job_cnt);
	rc = _load_gres();
	if (rc != SLURM_SUCCESS)
		return SLURM_ERROR;
	if (topology_g_init() != SLURM_SUCCESS)
		return SLURM_ERROR;

	/*
	 * Get and set slurmd topology information
	 * Build node hash table first to speed up the topo build
	 */
	rehash_node();
	topology_g_build_config();
	_set_topo_info();
	build_conf_buf();

	/*
	 * Check for cpu frequency set capabilities on this node
	 */
	cpu_freq_init(conf);

	/*
	 * If configured, apply resource specialization
	 */
	_resource_spec_init();

	_print_conf();

	if (proctrack_g_init() != SLURM_SUCCESS)
		return SLURM_ERROR;
	if (task_g_init() != SLURM_SUCCESS)
		return SLURM_ERROR;
	if (spank_slurmd_init() < 0)
		return SLURM_ERROR;
	if (cred_g_init() != SLURM_SUCCESS)
		return SLURM_ERROR;

	if (getrlimit(RLIMIT_CPU, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CPU, &rlim);
		if (rlim.rlim_max != RLIM_INFINITY) {
			error("Slurmd process CPU time limit is %d seconds",
			      (int) rlim.rlim_max);
		}
	}

	if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CORE, &rlim);
	}

	rlimits_use_max_nofile();

	if (conf->cleanstart) {
		/*
		 * Need to kill any running slurmd's here
		 */
		_wait_on_old_slurmd(true);

		stepd_cleanup_sockets(conf->spooldir, conf->node_name);
		_stepd_cleanup_batch_dirs(conf->spooldir, conf->node_name);
	}

	if (conf->daemonize || conf->setwd) {
		if (_set_work_dir() != SLURM_SUCCESS)
			return SLURM_ERROR;
	}

	if ((devnull = open("/dev/null", O_RDWR | O_CLOEXEC)) < 0) {
		error("Unable to open /dev/null: %m");
		return SLURM_ERROR;
	}

	/* make sure we have slurmstepd installed */
	if (stat(conf->stepd_loc, &stat_buf))
		fatal("Unable to find slurmstepd file at %s", conf->stepd_loc);
	if (!S_ISREG(stat_buf.st_mode))
		fatal("slurmstepd not a file at %s", conf->stepd_loc);

	return SLURM_SUCCESS;
}

static int
_slurmd_fini(void)
{
	int rc;

	assoc_mgr_fini(false);
	mpi_fini();
	node_features_g_fini();
	jobacct_gather_fini();
	acct_gather_profile_fini();
	cred_state_fini();
	switch_g_fini();
	task_g_fini();
	slurm_conf_destroy();
	proctrack_g_fini();
	auth_g_fini();
	hash_g_fini();
	certmgr_g_fini();
	node_fini2();
	gres_fini();
	prep_g_fini();
	topology_g_fini();
	slurmd_req(NULL);	/* purge memory allocated by slurmd_req() */
	select_g_fini();
	if ((rc = spank_slurmd_exit())) {
		error("%s: SPANK slurmd exit failed: %s",
		      __func__, slurm_strerror(rc));
	}
	cpu_freq_fini();
	_resource_spec_fini();
	job_container_fini();
	acct_gather_conf_destroy();
	fini_system_cgroup();
	cgroup_g_fini();
	xcpuinfo_fini();
	slurm_mutex_lock(&cached_features_mutex);
	xfree(cached_features_avail);
	xfree(cached_features_active);
	refresh_cached_features = true;
	slurm_mutex_unlock(&cached_features_mutex);
	slurm_mutex_lock(&fini_job_mutex);
	xfree(fini_job_id);
	fini_job_cnt = 0;
	slurm_mutex_unlock(&fini_job_mutex);

	return SLURM_SUCCESS;
}

extern void slurmd_shutdown(void)
{
	_shutdown = 1;

	slurm_mutex_lock(&listen_mutex);
	conmgr_fd_free_ref(&listener);
	slurm_mutex_unlock(&listen_mutex);

	conmgr_request_shutdown();
}

static void _usage(void)
{
	char *txt;
	static_ref_to_cstring(txt, usage_txt);
	fprintf(stderr, "%s", txt);
	xfree(txt);
}

/*
 * create spool directory as needed
 */
static int _set_slurmd_spooldir(const char *dir)
{
	debug3("%s: initializing slurmd spool directory `%s`", __func__, dir);

	if (mkdir(dir, 0755) < 0) {
		if (errno != EEXIST) {
			fatal("mkdir(%s): %m", conf->spooldir);
			return SLURM_ERROR;
		}
	}

	/*
	 * Ensure spool directory permissions are correct.
	 */
	if (chmod(dir, 0755) < 0) {
		error("chmod(%s, 0755): %m", conf->spooldir);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void _wait_on_old_slurmd(bool kill_it)
{
	int fd;
	pid_t oldpid = read_pidfile(conf->pidfile, &fd);
	if (oldpid != (pid_t) 0) {
		if (kill_it) {
			info("killing old slurmd[%lu]", (unsigned long) oldpid);
			kill(oldpid, SIGTERM);
		}

		/*
		 * Wait for previous daemon to terminate
		 */
		if (fd_get_readw_lock(fd) < 0) {
			fatal("error getting readw lock on file %s: %m",
			      conf->pidfile);
		}
		(void) close(fd); /* Ignore errors */
	}
}

/* Reset slurmd logging based upon configuration parameters */
extern void update_slurmd_logging(log_level_t log_lvl)
{
	log_options_t *o = &conf->log_opts;
	slurm_conf_t *cf;

	/* Preserve execute line verbose arguments (if any) */
	cf = slurm_conf_lock();
	if (log_lvl != LOG_LEVEL_END) {
		conf->debug_level = log_lvl;
	} else if (!conf->debug_level_set && (cf->slurmd_debug != NO_VAL16))
		conf->debug_level = cf->slurmd_debug;
	conf->syslog_debug = cf->slurmd_syslog_debug;
	slurm_conf_unlock();

	o->logfile_level = conf->debug_level;

	if (conf->daemonize)
		o->stderr_level = LOG_LEVEL_QUIET;
	else
		o->stderr_level = conf->debug_level;

	if (conf->syslog_debug != LOG_LEVEL_END) {
		o->syslog_level = conf->syslog_debug;
	} else if (!conf->daemonize) {
		o->syslog_level = LOG_LEVEL_QUIET;
	} else if ((conf->debug_level > LOG_LEVEL_QUIET) && !conf->logfile) {
		o->syslog_level = conf->debug_level;
	} else
		o->syslog_level = LOG_LEVEL_FATAL;

	log_alter(conf->log_opts, SYSLOG_FACILITY_DAEMON, conf->logfile);
	log_set_timefmt(cf->log_fmt);

	/*
	 * If logging to syslog and running in
	 * MULTIPLE_SLURMD mode add my node_name
	 * in the name tag for syslog.
	 */

	debug("Log file re-opened");

#if defined(MULTIPLE_SLURMD)
	if (conf->logfile == NULL) {
		char buf[64];

		snprintf(buf, sizeof(buf), "slurmd-%s", conf->node_name);
		log_set_argv0(buf);
	}
#endif
}

/* Reset slurmd nice value */
static void _update_nice(void)
{
	int cur_nice;
	id_t pid;

	if (conf->nice == 0)	/* No change */
		return;

	pid = getpid();
	cur_nice = getpriority(PRIO_PROCESS, pid);
	if (cur_nice == conf->nice)
		return;
	if (setpriority(PRIO_PROCESS, pid, conf->nice))
		error("Unable to reset nice value to %d: %m", conf->nice);
}

/*
 * set topology address and address pattern of slurmd node
 */
static int _set_topo_info(void)
{
	int rc;
	char *addr = NULL, *pattern = NULL;

	slurm_mutex_lock(&conf->config_mutex);
	rc = topology_g_get_node_addr(conf->node_name, &addr, &pattern);
	if (rc == SLURM_SUCCESS) {
		xfree(conf->node_topo_addr);
		xfree(conf->node_topo_pattern);
		conf->node_topo_addr = addr;
		conf->node_topo_pattern = pattern;
	}
	slurm_mutex_unlock(&conf->config_mutex);

	return rc;
}

/*
 * Initialize resource specialization
 */
static int _resource_spec_init(void)
{
	fini_system_cgroup();	/* Prevent memory leak */
	if (_core_spec_init() != SLURM_SUCCESS)
		error("Resource spec: core specialization disabled");
	if (_memory_spec_init() != SLURM_SUCCESS)
		error("Resource spec: system cgroup memory limit disabled");
	return SLURM_SUCCESS;
}

/*
 * If configured, initialize core specialization
 */
static int _core_spec_init(void)
{
#if defined(__APPLE__)
	error("%s: not supported on macOS", __func__);
	return SLURM_SUCCESS;
#else
	int i, rval;
	pid_t pid;
	bool slurmd_off_spec;
	bitstr_t *res_mac_bitmap;
	cpu_set_t mask;

	if ((conf->core_spec_cnt == 0) && (conf->cpu_spec_list == NULL)) {
		debug("Resource spec: No specialized cores configured by "
		      "default on this node");
		return SLURM_SUCCESS;
	}

	ncores = conf->sockets * conf->cores;
	ncpus = ncores * conf->threads;
	res_abs_core_size = ncores * 4;
	res_abs_cores = xmalloc(res_abs_core_size);
	res_core_bitmap = bit_alloc(ncores);
	res_cpu_bitmap  = bit_alloc(ncpus);
	res_abs_cpus[0] = '\0';

	if (conf->cpu_spec_list != NULL) {
		/* CPUSpecList designated in slurm.conf */
		debug2("Resource spec: configured CPU specialization list: %s",
			conf->cpu_spec_list);
		if (_validate_and_convert_cpu_list() != SLURM_SUCCESS) {
			error("Resource spec: unable to process CPUSpecList");
			_resource_spec_fini();
			return SLURM_ERROR;
		}
	} else {
		/* CoreSpecCount designated in slurm.conf */
		debug2("Resource spec: configured core specialization "
		       "count: %u", conf->core_spec_cnt);
		if (conf->core_spec_cnt >= ncores) {
			error("Resource spec: CoreSpecCount too large");
			_resource_spec_fini();
			return SLURM_ERROR;
		}
		_select_spec_cores();
		if (_convert_spec_cores() != SLURM_SUCCESS) {
			error("Resource spec: unable to convert "
			      "selected cores to machine CPU IDs");
			_resource_spec_fini();
			return SLURM_ERROR;
		}
	}

	pid = getpid();
	slurmd_off_spec = (slurm_conf.task_plugin_param & SLURMD_OFF_SPEC);

	if (check_corespec_cgroup_job_confinement()) {
		if (init_system_cpuset_cgroup() != SLURM_SUCCESS) {
			error("Resource spec: unable to initialize system "
			      "cpuset cgroup");
			_resource_spec_fini();
			return SLURM_ERROR;
		}
		if (slurmd_off_spec) {
			char other_mac_cpus[1024];
			res_mac_bitmap = bit_alloc(ncpus);
			bit_unfmt(res_mac_bitmap, res_mac_cpus);
			bit_not(res_mac_bitmap);
			bit_fmt(other_mac_cpus, sizeof(other_mac_cpus),
				res_mac_bitmap);
			FREE_NULL_BITMAP(res_mac_bitmap);
			rval = set_system_cgroup_cpus(other_mac_cpus);
		} else {
			rval = set_system_cgroup_cpus(res_mac_cpus);
		}
		if (rval != SLURM_SUCCESS) {
			error("Resource spec: unable to set reserved CPU IDs in "
			      "system cpuset cgroup");
			_resource_spec_fini();
			return SLURM_ERROR;
		}
		if (attach_system_cpuset_pid(pid) != SLURM_SUCCESS) {
			error("Resource spec: unable to attach slurmd to "
			      "system cpuset cgroup");
			_resource_spec_fini();
			return SLURM_ERROR;
		}
	} else {
		res_mac_bitmap = bit_alloc(ncpus);
		bit_unfmt(res_mac_bitmap, res_mac_cpus);
		CPU_ZERO(&mask);
		for (i = 0; i < ncpus; i++) {
			bool cpu_in_spec = bit_test(res_mac_bitmap, i);
			if (slurmd_off_spec != cpu_in_spec) {
				CPU_SET(i, &mask);
			}
		}
		FREE_NULL_BITMAP(res_mac_bitmap);

#ifdef __FreeBSD__
		rval = cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID,
					  pid, sizeof(cpu_set_t), &mask);
#else
		rval = sched_setaffinity(pid, sizeof(cpu_set_t), &mask);
#endif

		if (rval != 0) {
			error("Resource spec: unable to establish slurmd CPU "
			      "affinity: %m");
			_resource_spec_fini();
			return SLURM_ERROR;
		}
	}

	info("Resource spec: Reserved abstract CPU IDs: %s", res_abs_cpus);
	info("Resource spec: Reserved machine CPU IDs: %s", res_mac_cpus);
	_resource_spec_fini();

	return SLURM_SUCCESS;
#endif
}

/*
 * If configured, initialize system memory limit
 */
static int _memory_spec_init(void)
{
	pid_t pid;

	if (conf->mem_spec_limit == 0) {
		debug("Resource spec: Reserved system memory limit not "
		      "configured for this node");
		return SLURM_SUCCESS;
	}
	if (!cgroup_memcg_job_confinement()) {
		if (slurm_conf.select_type_param & CR_MEMORY) {
			error("Resource spec: Limited MemSpecLimit support. "
			     "Slurmd daemon not memory constrained. "
			     "Reserved %"PRIu64" MB", conf->mem_spec_limit);
			return SLURM_SUCCESS;
		}
		error("Resource spec: cgroup job confinement not configured. "
		      "Full MemSpecLimit support requires task/cgroup and "
		      "ConstrainRAMSpace=yes in cgroup.conf");
		return SLURM_ERROR;
	}
	if (init_system_memory_cgroup() != SLURM_SUCCESS) {
		error("Resource spec: unable to initialize system "
		      "memory cgroup");
		return SLURM_ERROR;
	}
	if (set_system_cgroup_mem_limit(conf->mem_spec_limit)
			!= SLURM_SUCCESS) {
		error("Resource spec: unable to set memory limit in "
		      "system memory cgroup");
		return SLURM_ERROR;
	}
	pid = getpid();
	if (attach_system_memory_pid(pid) != SLURM_SUCCESS) {
		error("Resource spec: unable to attach slurmd to "
		      "system memory cgroup");
		return SLURM_ERROR;
	}
	info("Resource spec: system cgroup memory limit set to %"PRIu64" MB",
	     conf->mem_spec_limit);
	return SLURM_SUCCESS;
}

/*
 * Select cores and CPUs to be reserved for core specialization.
 * IN:
 *  	conf->sockets		= number of sockets on this node
 *   	conf->cores		= number of cores per socket on this node
 * 	conf->threads		= number of threads per core on this node
 * 	conf->core_spec_cnt 	= number of cores to be reserved
 * OUT:
 * 	res_core_bitmap		= bitmap of selected cores
 * 	res_cpu_bitmap		= bitmap of selected CPUs
 */
static void _select_spec_cores(void)
{
	int spec_cores, res_core, res_sock, res_off, core_off, thread_off;
	int from_core, to_core, incr_core, from_sock, to_sock, incr_sock;
	bool spec_cores_first;

	if (xstrcasestr(slurm_conf.sched_params, "spec_cores_first"))
		spec_cores_first = true;
	else
		spec_cores_first = false;

	if (spec_cores_first) {
		from_core = 0;
		to_core   = conf->cores;
		incr_core = 1;
		from_sock = 0;
		to_sock   = conf->sockets;
		incr_sock = 1;
	} else {
		from_core = conf->cores - 1;
		to_core   = -1;
		incr_core = -1;
		from_sock = conf->sockets - 1;
		to_sock   = -1;
		incr_sock = -1;
	}
	spec_cores = conf->core_spec_cnt;
	for (res_core = from_core;
	     (spec_cores && (res_core != to_core)); res_core += incr_core) {
		for (res_sock = from_sock;
		     (spec_cores && (res_sock != to_sock));
		      res_sock += incr_sock) {
			core_off = ((res_sock*conf->cores) + res_core) *
					conf->threads;
			for (thread_off = 0; thread_off < conf->threads;
			     thread_off++) {
				bit_set(res_cpu_bitmap, core_off + thread_off);
			}
			res_off = (res_sock * conf->cores) + res_core;
			bit_set(res_core_bitmap, res_off);
			spec_cores--;
		}
	}
	return;
}

/*
 * Convert Core/CPU bitmaps into lists
 * IN:
 * 	res_core_bitmap		= bitmap of selected cores
 * 	res_cpu_bitmap		= bitmap of selected CPUs
 * OUT:
 * 	res_abs_cores		= list of abstract core IDs
 * 	res_abs_cpus		= list of abstract CPU IDs
 * 	res_mac_cpus		= list of machine CPU IDs
 */
static int _convert_spec_cores(void)
{
	bit_fmt(res_abs_cores, res_abs_core_size, res_core_bitmap);
	bit_fmt(res_abs_cpus, sizeof(res_abs_cpus), res_cpu_bitmap);
	if (xcpuinfo_abs_to_mac(res_abs_cores, &res_mac_cpus) != SLURM_SUCCESS)
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

/*
 * Validate and convert CPU list
 * IN:
 *  	conf->sockets		= number of sockets on this node
 *   	conf->cores		= number of cores per socket on this node
 * 	conf->threads		= number of threads per core on this node
 * 	conf->cpu_spec_list 	= configured list of CPU IDs to be reserved
 * OUT:
 *	res_cpu_bitmap		= bitmap of input abstract CPUs
 *	res_core_bitmap		= bitmap of cores
 *	res_abs_cores		= list of abstract core IDs
 *	res_abs_cpus		= converted list of abstract CPU IDs
 *	res_mac_cpus		= converted list of machine CPU IDs
 */
static int _validate_and_convert_cpu_list(void)
{
	int core_off, thread_inx, thread_off;

	if (ncores >= conf->cpus){
		/*
		 * create core bitmap from input CPU list because "cpus" are
		 * representing cores rather than threads in this situation
		 */
		if (bit_unfmt(res_core_bitmap, conf->cpu_spec_list)) {
			return SLURM_ERROR;
		}
	} else {
		/* create CPU bitmap from input CPU list */
		if (bit_unfmt(res_cpu_bitmap, conf->cpu_spec_list) != 0) {
			return SLURM_ERROR;
		}
		/* create core bitmap and list from CPU bitmap */
		for (thread_off = 0; thread_off < ncpus; thread_off++) {
			if (bit_test(res_cpu_bitmap, thread_off) == 1)
				bit_set(res_core_bitmap,
					(thread_off / (conf->threads)));
		}
	}

	bit_fmt(res_abs_cores, res_abs_core_size, res_core_bitmap);
	/* create output abstract CPU list from core bitmap */
	for (core_off = 0; core_off < ncores; core_off++) {
		if (bit_test(res_core_bitmap, core_off) == 1) {
			for (thread_off = 0; thread_off < conf->threads;
			     thread_off++) {
				thread_inx = (core_off * (int) conf->threads) +
					     thread_off;
				bit_set(res_cpu_bitmap, thread_inx);
			}
		}
	}
	bit_fmt(res_abs_cpus, sizeof(res_abs_cpus), res_cpu_bitmap);
	/* create output machine CPU list from core list */
	if (xcpuinfo_abs_to_mac(res_abs_cores, &res_mac_cpus)
		   != SLURM_SUCCESS)
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

static void _resource_spec_fini(void)
{
	xfree(res_abs_cores);
	xfree(res_mac_cpus);
	FREE_NULL_BITMAP(res_core_bitmap);
	FREE_NULL_BITMAP(res_cpu_bitmap);
}

/*
 * Run the configured health check program
 *
 * Returns the run result. If the health check program
 * is not defined, returns success immediately.
 */
extern int run_script_health_check(void)
{
	int rc = SLURM_SUCCESS;

	if (slurm_conf.health_check_program &&
	    slurm_conf.health_check_interval) {
		char **env = env_array_create();
		char *cmd_argv[2];
		char *resp = NULL;
		/*
		 * We can point script_argv to cmd_argv now (before setting
		 * values inside cmd_argv) since cmd_argv is on the stack
		 * (not on the heap).
		 */
		run_command_args_t run_command_args = {
			.job_id = 0, /* implicit job_id = 0 */
			.max_wait = 60 * 1000,
			.script_argv = cmd_argv,
			.script_path = slurm_conf.health_check_program,
			.script_type = "health_check",
			.status = &rc,
		};

		cmd_argv[0] = slurm_conf.health_check_program;
		cmd_argv[1] = NULL;

		setenvf(&env, "SLURMD_NODENAME", "%s", conf->node_name);
		/*
		 * We need to set the pointer after we alter or we may be
		 * pointing to the wrong place otherwise.
		 */
		run_command_args.env = env;

		resp = run_command(&run_command_args);
		if (rc) {
			if (WIFEXITED(rc))
				error("health_check failed: rc:%u output:%s",
				      WEXITSTATUS(rc), resp);
			else if (WIFSIGNALED(rc))
				error("health_check killed by signal %u output:%s",
				      WTERMSIG(rc), resp);
			else
				error("health_check didn't run: status:%d reason:%s",
				      rc, resp);
			rc = SLURM_ERROR;
		} else
			debug2("health_check success rc:%d output:%s",
			       rc, resp);

		env_array_free(env);
		xfree(resp);
	}

	return rc;
}

static int _set_work_dir(void)
{
	bool success = false;

	if (conf->logfile && (conf->logfile[0] == '/')) {
		char *slash_ptr, *work_dir;
		work_dir = xstrdup(conf->logfile);
		slash_ptr = strrchr(work_dir, '/');
		if (slash_ptr == work_dir)
			work_dir[1] = '\0';
		else
			slash_ptr[0] = '\0';
		if ((access(work_dir, W_OK) != 0) ||
		    (chdir(work_dir) < 0)) {
			error("Unable to chdir to %s", work_dir);
		} else
			success = true;
		xfree(work_dir);
	}

	if (!success) {
		if ((access(conf->spooldir, W_OK) != 0) ||
		    (chdir(conf->spooldir) < 0)) {
			error("Unable to chdir to %s", conf->spooldir);
		} else
			success = true;
	}

	if (!success) {
		if ((access("/var/tmp", W_OK) != 0) ||
		    (chdir("/var/tmp") < 0)) {
			error("chdir(/var/tmp): %m");
			return SLURM_ERROR;
		} else
			info("chdir to /var/tmp");
	}

	return SLURM_SUCCESS;
}
