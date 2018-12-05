/*****************************************************************************\
 *  src/slurmd/slurmd/slurmd.c - main slurm node server daemon
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian.
 *  Portions Copyright (C) 2010-2013 SchedMD LLC.
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
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/cpu_frequency.h"
#include "src/common/daemonize.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/gres.h"
#include "src/common/group_cache.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/msg_aggr.h"
#include "src/common/node_conf.h"
#include "src/common/node_features.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/plugstack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_mcs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_route.h"
#include "src/common/slurm_topology.h"
#include "src/common/stepd_api.h"
#include "src/common/switch.h"
#include "src/slurmd/common/task_plugin.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xsignal.h"

#include "src/slurmd/common/core_spec_plugin.h"
#include "src/slurmd/slurmd/get_mach_stat.h"
#include "src/slurmd/common/job_container_plugin.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/slurmd/req.h"
#include "src/slurmd/common/run_script.h"
#include "src/slurmd/common/set_oomadj.h"
#include "src/slurmd/common/setproctitle.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/common/slurmd_cgroup.h"
#include "src/slurmd/common/xcpuinfo.h"

#define GETOPT_ARGS	"bcCd:Df:hL:Mn:N:vV"

#ifndef MAXHOSTNAMELEN
#  define MAXHOSTNAMELEN	64
#endif

#define MAX_THREADS		256

#define _free_and_set(__dst, __src) \
	xfree(__dst); __dst = __src

/* global, copied to STDERR_FILENO in tasks before the exec */
int devnull = -1;
bool get_reg_resp = 1;
slurmd_conf_t * conf = NULL;
int fini_job_cnt = 0;
uint32_t *fini_job_id = NULL;
pthread_mutex_t fini_job_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tres_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  tres_cond      = PTHREAD_COND_INITIALIZER;

/*
 * count of active threads
 */
static int             active_threads = 0;
static pthread_mutex_t active_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond    = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t fork_mutex     = PTHREAD_MUTEX_INITIALIZER;

typedef struct connection {
	int fd;
	slurm_addr_t *cli_addr;
} conn_t;

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
static sig_atomic_t _shutdown = 0;
static sig_atomic_t _reconfig = 0;
static sig_atomic_t _update_log = 0;
static pthread_t msg_pthread = (pthread_t) 0;
static time_t sent_reg_time = (time_t) 0;

static void      _atfork_final(void);
static void      _atfork_prepare(void);
static int       _convert_spec_cores(void);
static int       _core_spec_init(void);
static void      _create_msg_socket(void);
static void      _decrement_thd_count(void);
static void      _destroy_conf(void);
static int       _drain_node(char *reason);
static void      _fill_registration_msg(slurm_node_registration_status_msg_t *);
static uint64_t  _get_int(const char *my_str);
static void      _handle_connection(int fd, slurm_addr_t *client);
static void      _hup_handler(int);
static void      _increment_thd_count(void);
static void      _init_conf(void);
static void      _install_fork_handlers(void);
static bool      _is_core_spec_cray(void);
static void      _kill_old_slurmd(void);
static int       _memory_spec_init(void);
static void      _msg_engine(void);
static uint64_t  _parse_msg_aggr_params(int type, char *params);
static void      _print_conf(void);
static void      _print_config(void);
static void      _process_cmdline(int ac, char **av);
static void      _read_config(void);
static void      _reconfigure(void);
static void     *_registration_engine(void *arg);
static void      _resource_spec_fini(void);
static int       _resource_spec_init(void);
static int       _restore_cred_state(slurm_cred_ctx_t ctx);
static void      _select_spec_cores(void);
static void     *_service_connection(void *);
static void      _set_msg_aggr_params(void);
static int       _set_slurmd_spooldir(void);
static int       _set_topo_info(void);
static int       _slurmd_init(void);
static int       _slurmd_fini(void);
static void      _update_logging(void);
static void      _update_nice(void);
static void      _usage(void);
static void      _usr_handler(int);
static int       _validate_and_convert_cpu_list(void);
static void      _wait_for_all_threads(int secs);

int
main (int argc, char **argv)
{
	int i, pidfd;
	int blocked_signals[] = {SIGPIPE, 0};
	int cc;
	char *oom_value;
	uint32_t slurmd_uid = 0;
	uint32_t curr_uid = 0;
	char time_stamp[256];
	log_options_t lopts = LOG_OPTS_INITIALIZER;

	/* NOTE: logfile is NULL at this point */
	log_init(argv[0], lopts, LOG_DAEMON, NULL);

	/*
	 * Make sure we have no extra open files which
	 * would be propagated to spawned tasks.
	 */
	cc = sysconf(_SC_OPEN_MAX);
	for (i = 3; i < cc; i++)
		close(i);

	/*
	 * Drop supplementary groups.
	 */
	if (geteuid() == 0) {
		if (setgroups(0, NULL) != 0) {
			fatal("Failed to drop supplementary groups, "
			      "setgroups: %m");
		}
	} else {
		debug("Not running as root. Can't drop supplementary groups");
	}

	/*
	 * Create and set default values for the slurmd global
	 * config variable "conf"
	 */
	conf = xmalloc(sizeof(slurmd_conf_t));
	_init_conf();
	conf->argv = &argv;
	conf->argc = &argc;

	if (_slurmd_init() < 0) {
		error( "slurmd initialization failed" );
		fflush( NULL );
		exit(1);
	}

	slurmd_uid = slurm_get_slurmd_user_id();
	curr_uid = getuid();
	if (curr_uid != slurmd_uid) {
		struct passwd *pw = NULL;
		char *slurmd_user = NULL;
		char *curr_user = NULL;

		/* since when you do a getpwuid you get a pointer to a
		 * structure you have to do a xstrdup on the first
		 * call or your information will just get over
		 * written.  This is a memory leak, but a fatal is
		 * called right after so it isn't that big of a deal.
		 */
		if ((pw = getpwuid(slurmd_uid)))
			slurmd_user = xstrdup(pw->pw_name);
		if ((pw = getpwuid(curr_uid)))
			curr_user = pw->pw_name;

		fatal("You are running slurmd as something "
		      "other than user %s(%d).  If you want to "
		      "run as this user add SlurmdUser=%s "
		      "to the slurm.conf file.",
		      slurmd_user, slurmd_uid, curr_user);
	}
	init_setproctitle(argc, argv);

	xsignal(SIGTERM, slurmd_shutdown);
	xsignal(SIGINT,  slurmd_shutdown);
	xsignal(SIGHUP,  _hup_handler);
	xsignal(SIGUSR2, _usr_handler);
	xsignal_block(blocked_signals);

	debug3("slurmd initialization successful");

	/*
	 * Become a daemon if desired.
	 */
	if (conf->daemonize) {
		if (xdaemon())
			error("Couldn't daemonize slurmd: %m");
	}
	test_core_limit();
	info("slurmd version %s started", SLURM_VERSION_STRING);
	debug3("finished daemonize");

	if ((oom_value = getenv("SLURMD_OOM_ADJ"))) {
		i = atoi(oom_value);
		debug("Setting slurmd oom_adj to %d", i);
		set_oom_adj(i);
	}

	_kill_old_slurmd();

	if (conf->mlock_pages) {
		/*
		 * Call mlockall() if available to ensure slurmd
		 *  doesn't get swapped out
		 */
#ifdef _POSIX_MEMLOCK
		if (mlockall (MCL_FUTURE | MCL_CURRENT) < 0)
			error ("failed to mlock() slurmd pages: %m");
#else
		error ("mlockall() system call does not appear to be available");
#endif /* _POSIX_MEMLOCK */
	}


	/*
	 * Restore any saved revoked credential information
	 */
	if (!conf->cleanstart && (_restore_cred_state(conf->vctx) < 0))
		return SLURM_FAILURE;

	if (jobacct_gather_init() != SLURM_SUCCESS)
		fatal("Unable to initialize jobacct_gather");
	if (job_container_init() < 0)
		fatal("Unable to initialize job_container plugin.");
	if (container_g_restore(conf->spooldir, !conf->cleanstart))
		error("Unable to restore job_container state.");
	if (core_spec_g_init() < 0)
		fatal("Unable to initialize core specialization plugin.");
	if (switch_g_node_init() < 0)
		fatal("Unable to initialize interconnect.");
	if (node_features_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize node_features plugin");
	if (conf->cleanstart && switch_g_clear_node_state())
		fatal("Unable to clear interconnect state.");
	switch_g_slurmd_init();
	file_bcast_init();

	_create_msg_socket();

	conf->pid = getpid();
	pidfd = create_pidfile(conf->pidfile, 0);

	rfc2822_timestamp(time_stamp, sizeof(time_stamp));
	info("%s started on %s", slurm_prog_name, time_stamp);

	_install_fork_handlers();
	list_install_fork_handlers();
	slurm_conf_install_fork_handlers();
	record_launched_jobs();

	run_script_health_check();

	msg_aggr_sender_init(conf->hostname, conf->port,
			     conf->msg_aggr_window_time,
			     conf->msg_aggr_window_msgs);

	slurm_thread_create_detached(NULL, _registration_engine, NULL);

	_msg_engine();

	/*
	 * Close fd here, otherwise we'll deadlock since create_pidfile()
	 * flocks the pidfile.
	 */
	if (pidfd >= 0)			/* valid pidfd, non-error */
		(void) close(pidfd);	/* Ignore errors */
	if (unlink(conf->pidfile) < 0)
		error("Unable to remove pidfile `%s': %m", conf->pidfile);

	_wait_for_all_threads(120);
	_slurmd_fini();
	_destroy_conf();
	slurm_crypto_fini();	/* must be after _destroy_conf() */
	group_cache_purge();
	file_bcast_purge();

	info("Slurmd shutdown completing");
	log_fini();
       	return 0;
}

/* Spawn a thread to make sure we send at least one registration message to
 * slurmctld. If slurmctld restarts, it will request another registration
 * message. */
static void *
_registration_engine(void *arg)
{
	_increment_thd_count();

	while (!_shutdown) {
		if ((sent_reg_time == (time_t) 0) &&
		    (send_registration_msg(SLURM_SUCCESS, true) !=
		     SLURM_SUCCESS)) {
			debug("Unable to register with slurm controller, "
			      "retrying");
		} else if (_shutdown || sent_reg_time) {
			break;
		}
		sleep(1);
	}

	_decrement_thd_count();
	return NULL;
}

static void
_msg_engine(void)
{
	slurm_addr_t *cli;
	int sock;

	msg_pthread = pthread_self();
	slurmd_req(NULL);	/* initialize timer */
	while (!_shutdown) {
		if (_reconfig) {
			verbose("got reconfigure request");
			_wait_for_all_threads(5); /* Wait for RPCs to finish */
			_reconfigure();
		}
		if (_update_log)
			_update_logging();
		cli = xmalloc (sizeof (slurm_addr_t));
		if ((sock = slurm_accept_msg_conn(conf->lfd, cli)) >= 0) {
			_handle_connection(sock, cli);
			continue;
		}
		/*
		 *  Otherwise, accept() failed.
		 */
		xfree (cli);
		if (errno == EINTR)
			continue;
		error("accept: %m");
	}
	verbose("got shutdown request");
	close(conf->lfd);
	return;
}

static void
_decrement_thd_count(void)
{
	slurm_mutex_lock(&active_mutex);
	if (active_threads > 0)
		active_threads--;
	slurm_cond_signal(&active_cond);
	slurm_mutex_unlock(&active_mutex);
}

static void
_increment_thd_count(void)
{
	bool logged = false;

	slurm_mutex_lock(&active_mutex);
	while (active_threads >= MAX_THREADS) {
		if (!logged) {
			info("active_threads == MAX_THREADS(%d)",
			     MAX_THREADS);
			logged = true;
		}
		slurm_cond_wait(&active_cond, &active_mutex);
	}
	active_threads++;
	slurm_mutex_unlock(&active_mutex);
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
		rc = pthread_cond_timedwait(&active_cond, &active_mutex, &ts);
		if (rc == ETIMEDOUT) {
			error("Timeout waiting for completion of %d threads",
			      active_threads);
			slurm_cond_signal(&active_cond);
			slurm_mutex_unlock(&active_mutex);
			return;
		}
	}
	slurm_cond_signal(&active_cond);
	slurm_mutex_unlock(&active_mutex);
	verbose("all threads complete");
}

static void _handle_connection(int fd, slurm_addr_t *cli)
{
	conn_t *arg = xmalloc(sizeof(conn_t));

	arg->fd       = fd;
	arg->cli_addr = cli;

	fd_set_close_on_exec(fd);

	_increment_thd_count();
	slurm_thread_create_detached(NULL, _service_connection, arg);
}

static void *
_service_connection(void *arg)
{
	conn_t *con = (conn_t *) arg;
	slurm_msg_t *msg = xmalloc(sizeof(slurm_msg_t));
	int rc = SLURM_SUCCESS;

	debug3("in the service_connection");
	slurm_msg_t_init(msg);
	if ((rc = slurm_receive_msg_and_forward(con->fd, con->cli_addr, msg, 0))
	   != SLURM_SUCCESS) {
		error("service_connection: slurm_receive_msg: %m");
		/* if this fails we need to make sure the nodes we forward
		   to are taken care of and sent back. This way the control
		   also has a better idea what happened to us */
		slurm_send_rc_msg(msg, rc);
		goto cleanup;
	}
	debug2("got this type of message %d", msg->msg_type);

	if (msg->msg_type != MESSAGE_COMPOSITE)
		slurmd_req(msg);

cleanup:
	if ((msg->conn_fd >= 0) && close(msg->conn_fd) < 0)
		error ("close(%d): %m", con->fd);

	xfree(con->cli_addr);
	xfree(con);
	slurm_free_msg(msg);
	_decrement_thd_count();
	return NULL;
}

static void _handle_node_reg_resp(slurm_msg_t *resp_msg)
{
	int rc;
	slurm_node_reg_resp_msg_t *resp = NULL;

	switch (resp_msg->msg_type) {
	case RESPONSE_NODE_REGISTRATION:
		resp = (slurm_node_reg_resp_msg_t *) resp_msg->data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg->data)->return_code;
		slurm_free_return_code_msg(resp_msg->data);
		if (rc)
			slurm_seterrno(rc);
		resp = NULL;
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}


	if (resp) {
		/*
		 * We don't care about the assoc/qos locks
		 * assoc_mgr_post_tres_list is requesting as those lists
		 * don't exist here.
		 */
		assoc_mgr_lock_t locks = { .tres = WRITE_LOCK };

		/*
		 * We only needed the resp to get the tres the first time,
		 * Set it so we don't request it again.
		 */
		if (get_reg_resp)
			get_reg_resp = false;

		assoc_mgr_lock(&locks);
		assoc_mgr_post_tres_list(resp->tres_list);
		debug("%s: slurmctld sent back %u TRES.",
		       __func__, g_tres_count);
		assoc_mgr_unlock(&locks);

		/*
		 * Signal any threads potentially waiting to run.
		 */
		slurm_mutex_lock(&tres_mutex);
		slurm_cond_broadcast(&tres_cond);
		slurm_mutex_unlock(&tres_mutex);

		/* assoc_mgr_post_tres_list will destroy the list */
		resp->tres_list = NULL;
	}
}

extern int
send_registration_msg(uint32_t status, bool startup)
{
	int ret_val = SLURM_SUCCESS;
	slurm_node_registration_status_msg_t *msg =
		xmalloc (sizeof (slurm_node_registration_status_msg_t));

	if (startup)
		msg->flags |= SLURMD_REG_FLAG_STARTUP;
	if (get_reg_resp)
		msg->flags |= SLURMD_REG_FLAG_RESP;

	_fill_registration_msg(msg);
	msg->status  = status;

	if (conf->msg_aggr_window_msgs > 1) {
		slurm_msg_t *req = xmalloc_nz(sizeof(slurm_msg_t));

		slurm_msg_t_init(req);
		req->msg_type = MESSAGE_NODE_REGISTRATION_STATUS;
		req->data     = msg;

		msg_aggr_add_msg(req, 1, _handle_node_reg_resp);
	} else {
		slurm_msg_t req;
		slurm_msg_t resp_msg;

		slurm_msg_t_init(&req);
		slurm_msg_t_init(&resp_msg);

		req.msg_type = MESSAGE_NODE_REGISTRATION_STATUS;
		req.data     = msg;

		ret_val = slurm_send_recv_controller_msg(&req, &resp_msg,
							 working_cluster_rec);
		slurm_free_node_registration_status_msg(msg);

		if (ret_val < 0) {
			error("Unable to register: %m");
			ret_val = SLURM_FAILURE;
			goto fail;
		}

		_handle_node_reg_resp(&resp_msg);
		if (resp_msg.msg_type != RESPONSE_SLURM_RC) {
			/* RESPONSE_SLURM_RC freed by _handle_node_reg_resp() */
			slurm_free_msg_data(resp_msg.msg_type, resp_msg.data);
		}
		if (errno) {
			ret_val = errno;
			errno = 0;
		}

	}

	if (ret_val == SLURM_SUCCESS)
		sent_reg_time = time(NULL);
fail:
	return ret_val;
}

static void
_fill_registration_msg(slurm_node_registration_status_msg_t *msg)
{
	List steps;
	ListIterator i;
	step_loc_t *stepd;
	int  n;
	char *arch, *os;
	struct utsname buf;
	static bool first_msg = true;
	static time_t slurmd_start_time = 0;
	Buf gres_info;

	msg->node_name   = xstrdup (conf->node_name);
	msg->version     = xstrdup (PACKAGE_VERSION);

	msg->cpus	 = conf->cpus;
	msg->boards	 = conf->boards;
	msg->sockets	 = conf->sockets;
	msg->cores	 = conf->cores;
	msg->threads	 = conf->threads;
	if (res_abs_cpus[0] == '\0')
		msg->cpu_spec_list = NULL;
	else
		msg->cpu_spec_list = xstrdup (res_abs_cpus);
	msg->real_memory = conf->real_memory_size;
	msg->tmp_disk    = conf->tmp_disk_space;
	msg->hash_val    = slurm_get_hash_val();
	get_cpu_load(&msg->cpu_load);
	get_free_mem(&msg->free_mem);

	gres_info = init_buf(1024);
	if (gres_plugin_node_config_pack(gres_info) != SLURM_SUCCESS)
		error("error packing gres configuration");
	else
		msg->gres_info   = gres_info;

	get_up_time(&conf->up_time);
	msg->up_time     = conf->up_time;
	if (slurmd_start_time == 0)
		slurmd_start_time = time(NULL);
	msg->slurmd_start_time = slurmd_start_time;

	node_features_g_node_state(&msg->features_avail, &msg->features_active);

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

	if (msg->flags & SLURMD_REG_FLAG_STARTUP) {
		if (switch_g_alloc_node_info(&msg->switch_nodeinfo))
			error("switch_g_alloc_node_info: %m");
		if (switch_g_build_node_info(msg->switch_nodeinfo))
			error("switch_g_build_node_info: %m");
	}

	steps = stepd_available(conf->spooldir, conf->node_name);
	msg->job_count = list_count(steps);
	msg->job_id    = xmalloc(msg->job_count * sizeof(*msg->job_id));
	/* Note: Running batch jobs will have step_id == NO_VAL */
	msg->step_id   = xmalloc(msg->job_count * sizeof(*msg->step_id));

	i = list_iterator_create(steps);
	n = 0;
	while ((stepd = list_next(i))) {
		int fd;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1) {
			--(msg->job_count);
			continue;
		}

		if (stepd_state(fd, stepd->protocol_version)
		    == SLURMSTEPD_NOT_RUNNING) {
			debug("stale domain socket for stepd %u.%u ",
			      stepd->jobid, stepd->stepid);
			--(msg->job_count);
			close(fd);
			continue;
		}

		close(fd);
		if (stepd->stepid == NO_VAL) {
			debug("%s: found apparently running job %u",
			      __func__, stepd->jobid);
		} else {
			debug("%s: found apparently running step %u.%u",
			      __func__, stepd->jobid, stepd->stepid);
		}
		msg->job_id[n]  = stepd->jobid;
		msg->step_id[n] = stepd->stepid;
		n++;
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	if (!msg->energy)
		msg->energy = acct_gather_energy_alloc(1);
	acct_gather_energy_g_get_data(ENERGY_DATA_NODE_ENERGY, msg->energy);

	msg->timestamp = time(NULL);

	return;
}

/*
 * Replace first "%h" in path string with actual hostname.
 * Replace first "%n" in path string with NodeName.
 */
static void
_massage_pathname(char **path)
{
	if (path && *path) {
		if (conf->hostname)
			xstrsubstitute(*path, "%h", conf->hostname);
		if (conf->node_name)
			xstrsubstitute(*path, "%n", conf->node_name);
	}
}

/*
 * Read the slurm configuration file (slurm.conf) and substitute some
 * values into the slurmd configuration in preference of the defaults.
 */
static void
_read_config(void)
{
	char *path_pubkey = NULL;
	slurm_ctl_conf_t *cf = NULL;
	int cc;
#ifndef HAVE_FRONT_END
	bool cr_flag = false, gang_flag = false;
#endif
	char *tok, *save_ptr = NULL;
	bool over_memory_kill = false;

	slurm_mutex_lock(&conf->config_mutex);
	cf = slurm_conf_lock();

	xfree(conf->auth_info);
	conf->auth_info = xstrdup(cf->authinfo);

	conf->last_update = time(NULL);

	if (conf->conffile == NULL)
		conf->conffile = xstrdup(cf->slurm_conf);

	conf->slurm_user_id =  cf->slurm_user_id;

	conf->cr_type = cf->select_type_param;

	path_pubkey = xstrdup(cf->job_credential_public_certificate);

	if (!conf->logfile)
		conf->logfile = xstrdup(cf->slurmd_logfile);

#ifndef HAVE_FRONT_END
	if (!xstrcmp(cf->select_type, "select/cons_res"))
		cr_flag = true;
	if (!xstrcmp(cf->select_type, "select/cray") &&
	    (cf->select_type_param & CR_OTHER_CONS_RES))
		cr_flag = true;

	if (cf->preempt_mode & PREEMPT_MODE_GANG)
		gang_flag = true;
#endif

	slurm_conf_unlock();
	/* node_name may already be set from a command line parameter */
	if (conf->node_name == NULL)
		conf->node_name = slurm_conf_get_nodename(conf->hostname);
	/* if we didn't match the form of the hostname already
	 * stored in conf->hostname, check to see if we match any
	 * valid aliases */
	if (conf->node_name == NULL)
		conf->node_name = slurm_conf_get_aliased_nodename();

	if (conf->node_name == NULL)
		conf->node_name = slurm_conf_get_nodename("localhost");

	if (conf->node_name == NULL)
		fatal("Unable to determine this slurmd's NodeName");

	_massage_pathname(&conf->logfile);

	/* set node_addr if relevant */
	if ((conf->node_addr == NULL) &&
	    (conf->node_addr = slurm_conf_get_nodeaddr(conf->hostname)) &&
	    (xstrcmp(conf->node_addr, conf->hostname) == 0)) {
		xfree(conf->node_addr);	/* Sets to NULL */
	}

	conf->port = slurm_conf_get_port(conf->node_name);
	slurm_conf_get_cpus_bsct(conf->node_name,
				 &conf->conf_cpus, &conf->conf_boards,
				 &conf->conf_sockets, &conf->conf_cores,
				 &conf->conf_threads);

	slurm_conf_get_res_spec_info(conf->node_name,
				     &conf->cpu_spec_list,
				     &conf->core_spec_cnt,
				     &conf->mem_spec_limit);

	/* store hardware properties in slurmd_config */
	xfree(conf->block_map);
	xfree(conf->block_map_inv);

	/*
	 * This must be reset before _update_logging(), otherwise the
	 * slurmstepd processes will not get the reconfigure request,
	 * and logs may be lost if the path changed or the log was rotated.
	 */
	_free_and_set(conf->spooldir, xstrdup(cf->slurmd_spooldir));
	_massage_pathname(&conf->spooldir);

	_update_logging();
	_update_nice();

	conf->actual_cpus = 0;

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
	 * Report actual hardware configuration, irrespective of FastSchedule.
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
	if (((cf->fast_schedule == 0) && !cr_flag && !gang_flag) ||
	    ((cf->fast_schedule == 1) &&
	     (conf->actual_cpus < conf->conf_cpus))) {
		conf->cpus    = conf->actual_cpus;
		conf->boards  = conf->actual_boards;
		conf->sockets = conf->actual_sockets;
		conf->cores   = conf->actual_cores;
		conf->threads = conf->actual_threads;
	} else if ((cf->fast_schedule == 1) && (cr_flag || gang_flag) &&
		   (conf->actual_sockets != conf->conf_sockets) &&
		   (conf->actual_cores != conf->conf_cores) &&
		   ((conf->actual_sockets * conf->actual_cores) ==
		    (conf->conf_sockets * conf->conf_cores))) {
		/* Socket and core count can be changed when KNL node reboots
		 * in a different NUMA configuration */
		info("Node reconfigured socket/core boundaries "
		     "SocketsPerBoard=%u:%u(hw) CoresPerSocket=%u:%u(hw)",
		     conf->conf_sockets, conf->actual_sockets,
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

	if ((conf->cpus    != conf->actual_cpus)    ||
	    (conf->sockets != conf->actual_sockets) ||
	    (conf->cores   != conf->actual_cores)   ||
	    (conf->threads != conf->actual_threads)) {
		if (cf->fast_schedule) {
			error("Node configuration differs from hardware: "
			      "Procs=%u:%u(hw) Boards=%u:%u(hw) "
			      "SocketsPerBoard=%u:%u(hw) CoresPerSocket=%u:%u(hw) "
			      "ThreadsPerCore=%u:%u(hw)",
			      conf->cpus,    conf->actual_cpus,
			      conf->boards,  conf->actual_boards,
			      conf->sockets, conf->actual_sockets,
			      conf->cores,   conf->actual_cores,
			      conf->threads, conf->actual_threads);
		} else if ((cf->fast_schedule == 0) && (cr_flag || gang_flag)) {
			error("You are using cons_res or gang scheduling with "
			      "Fastschedule=0 and node configuration differs "
			      "from hardware.  The node configuration used "
			      "will be what is in the slurm.conf because of "
			      "the bitmaps the slurmctld must create before "
			      "the slurmd registers.\n"
			      "   CPUs=%u:%u(hw) Boards=%u:%u(hw) "
			      "SocketsPerBoard=%u:%u(hw) CoresPerSocket=%u:%u(hw) "
			      "ThreadsPerCore=%u:%u(hw)",
			      conf->cpus,    conf->actual_cpus,
			      conf->boards,  conf->actual_boards,
			      conf->sockets, conf->actual_sockets,
			      conf->cores,   conf->actual_cores,
			      conf->threads, conf->actual_threads);
		}
	}
#endif

	get_memory(&conf->real_memory_size);
	get_up_time(&conf->up_time);

	cf = slurm_conf_lock();
	get_tmp_disk(&conf->tmp_disk_space, cf->tmp_fs);
	_free_and_set(conf->cluster_name, xstrdup(cf->cluster_name));
	_free_and_set(conf->epilog,   xstrdup(cf->epilog));
	_free_and_set(conf->prolog,   xstrdup(cf->prolog));
	_free_and_set(conf->tmpfs,    xstrdup(cf->tmp_fs));
	_free_and_set(conf->health_check_program,
		      xstrdup(cf->health_check_program));
	_free_and_set(conf->pidfile,  xstrdup(cf->slurmd_pidfile));
	_massage_pathname(&conf->pidfile);
	_free_and_set(conf->plugstack,   xstrdup(cf->plugstack));
	_free_and_set(conf->select_type, xstrdup(cf->select_type));
	_free_and_set(conf->task_prolog, xstrdup(cf->task_prolog));
	_free_and_set(conf->task_epilog, xstrdup(cf->task_epilog));
	_free_and_set(conf->pubkey,   path_pubkey);
	_free_and_set(conf->x11_params, xstrdup(cf->x11_params));

	conf->debug_flags = cf->debug_flags;
	conf->syslog_debug = cf->slurmd_syslog_debug;
	conf->propagate_prio = cf->propagate_prio_process;

	_free_and_set(conf->job_acct_gather_freq,
		      xstrdup(cf->job_acct_gather_freq));

	conf->acct_freq_task = NO_VAL16;
	cc = acct_gather_parse_freq(PROFILE_TASK,
				    conf->job_acct_gather_freq);
	if (cc != -1)
		conf->acct_freq_task = cc;

	_free_and_set(conf->acct_gather_energy_type,
		      xstrdup(cf->acct_gather_energy_type));
	_free_and_set(conf->acct_gather_filesystem_type,
		      xstrdup(cf->acct_gather_filesystem_type));
	_free_and_set(conf->acct_gather_interconnect_type,
		      xstrdup(cf->acct_gather_interconnect_type));
	_free_and_set(conf->acct_gather_profile_type,
		      xstrdup(cf->acct_gather_profile_type));
	_free_and_set(conf->job_acct_gather_type,
		      xstrdup(cf->job_acct_gather_type));
	_free_and_set(conf->msg_aggr_params,
		      xstrdup(cf->msg_aggr_params));
	_set_msg_aggr_params();

	if ( (conf->node_name == NULL) ||
	     (conf->node_name[0] == '\0') )
		fatal("Node name lookup failure");

	if (cf->control_addr == NULL)
		fatal("Unable to establish controller machine");
	if (cf->slurmctld_port == 0)
		fatal("Unable to establish controller port");
	conf->slurmd_timeout = cf->slurmd_timeout;
	conf->kill_wait = cf->kill_wait;
	conf->use_pam = cf->use_pam;
	conf->task_plugin_param = cf->task_plugin_param;

	conf->mem_limit_enforce = cf->mem_limit_enforce;
	conf->health_check_interval = cf->health_check_interval;

	slurm_mutex_unlock(&conf->config_mutex);
	slurm_conf_unlock();

	if (check_memspec_cgroup_job_confinement()) {
		if (conf->mem_limit_enforce) {
			fatal("Job's memory is being constrained by TaskPlugin cgroup and at the same time MemoryLimitEnforce=yes is set in slurm.conf. This enables two incompatible memory enforcement mechanisms, one of them must be disabled.");
		}

		if (cf->job_acct_gather_params) {
			tok = strtok_r(cf->job_acct_gather_params, ",",
				       &save_ptr);
			while(tok) {
				if (xstrcasecmp(tok, "OverMemoryKill") == 0) {
					over_memory_kill = true;
					break;
				}
				tok = strtok_r(NULL, ",", &save_ptr);
			}
		}

		if (over_memory_kill) {
			fatal("Job's memory is being constrained by TaskPlugin cgroup and at the same time OverMemoryKill param is set in JobAcctGatherParams slurm.conf.  This enables two incompatible memory enforcement mechanisms, one of them must be disabled.");
		}
	}
}

static void
_reconfigure(void)
{
	bool did_change;

	_reconfig = 0;
	slurm_conf_reinit(conf->conffile);
	_read_config();

	/*
	 * Rebuild topology information and refresh slurmd topo infos
	 */
	slurm_topo_build_config();
	_set_topo_info();
	route_g_reconfigure();
	cpu_freq_reconfig();

	msg_aggr_sender_reconfig(conf->msg_aggr_window_time,
				 conf->msg_aggr_window_msgs);

	/*
	 * In case the administrator changed the cpu frequency set capabilities
	 * on this node, rebuild the cpu frequency table information
	 */
	cpu_freq_init(conf);

	/*
	 * If configured, apply resource specialization
	 */
	_resource_spec_init();

	_print_conf();

	/*
	 * Make best effort at changing to new public key
	 */
	slurm_cred_ctx_key_update(conf->vctx, conf->pubkey);

	/*
	 * Purge the username -> grouplist cache.
	 */
	group_cache_purge();

	gres_plugin_reconfig(&did_change);
	(void) switch_g_reconfig();
	container_g_reconfig();
	if (did_change) {
		uint32_t cpu_cnt = MAX(conf->conf_cpus, conf->block_map_size);
		(void) gres_plugin_node_config_load(cpu_cnt, conf->node_name,
						    NULL);
		send_registration_msg(SLURM_SUCCESS, false);
	}

	/* reconfigure energy */
	acct_gather_energy_g_set_data(ENERGY_DATA_RECONFIG, NULL);

	/*
	 * XXX: reopen slurmd port?
	 */
}

static void
_print_conf(void)
{
	slurm_ctl_conf_t *cf;
	char *str = NULL, time_str[32];
	int i;

	if (conf->log_opts.stderr_level < LOG_LEVEL_DEBUG3)
		return;

	cf = slurm_conf_lock();
	debug3("NodeName    = %s",       conf->node_name);
	debug3("TopoAddr    = %s",       conf->node_topo_addr);
	debug3("TopoPattern = %s",       conf->node_topo_pattern);
	debug3("ClusterName = %s",       conf->cluster_name);
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

	debug3("RealMemory  = %"PRIu64"",conf->real_memory_size);
	debug3("TmpDisk     = %u",       conf->tmp_disk_space);
	debug3("Epilog      = `%s'",     conf->epilog);
	debug3("Logfile     = `%s'",     cf->slurmd_logfile);
	debug3("HealthCheck = `%s'",     conf->health_check_program);
	debug3("NodeName    = %s",       conf->node_name);
	debug3("NodeAddr    = %s",       conf->node_addr);
	debug3("Port        = %u",       conf->port);
	debug3("Prolog      = `%s'",     conf->prolog);
	debug3("TmpFS       = `%s'",     conf->tmpfs);
	debug3("Public Cert = `%s'",     conf->pubkey);
	debug3("Slurmstepd  = `%s'",     conf->stepd_loc);
	debug3("Spool Dir   = `%s'",     conf->spooldir);
	debug3("Syslog Debug  = %d",     cf->slurmd_syslog_debug);
	debug3("Pid File    = `%s'",     conf->pidfile);
	debug3("Slurm UID   = %u",       conf->slurm_user_id);
	debug3("TaskProlog  = `%s'",     conf->task_prolog);
	debug3("TaskEpilog  = `%s'",     conf->task_epilog);
	debug3("TaskPluginParam = %u",   conf->task_plugin_param);
	debug3("Use PAM     = %u",       conf->use_pam);
	slurm_conf_unlock();
}

/* Initialize slurmd configuration table.
 * Everything is already NULL/zero filled when called */
static void
_init_conf(void)
{
	char  host[MAXHOSTNAMELEN];
	log_options_t lopts = LOG_OPTS_INITIALIZER;

	if (gethostname_short(host, MAXHOSTNAMELEN) < 0) {
		error("Unable to get my hostname: %m");
		exit(1);
	}
	conf->hostname    = xstrdup(host);
	conf->daemonize   =  1;
	conf->def_config  =  true;
	conf->lfd         = -1;
	conf->log_opts    = lopts;
	conf->debug_level = LOG_LEVEL_INFO;
	conf->pidfile     = xstrdup(DEFAULT_SLURMD_PIDFILE);
	conf->spooldir	  = xstrdup(DEFAULT_SPOOLDIR);

	slurm_mutex_init(&conf->config_mutex);

	conf->starting_steps = list_create(destroy_starting_step);
	slurm_cond_init(&conf->starting_steps_cond, NULL);
	conf->prolog_running_jobs = list_create(slurm_destroy_uint32_ptr);
	slurm_cond_init(&conf->prolog_running_cond, NULL);
	return;
}

static void
_destroy_conf(void)
{
	if (conf) {
		xfree(conf->acct_gather_energy_type);
		xfree(conf->acct_gather_filesystem_type);
		xfree(conf->acct_gather_interconnect_type);
		xfree(conf->acct_gather_profile_type);
		xfree(conf->auth_info);
		xfree(conf->block_map);
		xfree(conf->block_map_inv);
		xfree(conf->cluster_name);
		xfree(conf->conffile);
		xfree(conf->cpu_spec_list);
		xfree(conf->epilog);
		xfree(conf->health_check_program);
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
		xfree(conf->job_acct_gather_freq);
		xfree(conf->job_acct_gather_type);
		xfree(conf->logfile);
		xfree(conf->msg_aggr_params);
		xfree(conf->node_name);
		xfree(conf->node_addr);
		xfree(conf->node_topo_addr);
		xfree(conf->node_topo_pattern);
		xfree(conf->pidfile);
		xfree(conf->plugstack);
		xfree(conf->prolog);
		xfree(conf->pubkey);
		xfree(conf->select_type);
		xfree(conf->spooldir);
		xfree(conf->stepd_loc);
		xfree(conf->task_prolog);
		xfree(conf->task_epilog);
		xfree(conf->tmpfs);
		xfree(conf->x11_params);
		slurm_mutex_destroy(&conf->config_mutex);
		FREE_NULL_LIST(conf->starting_steps);
		slurm_cond_destroy(&conf->starting_steps_cond);
		FREE_NULL_LIST(conf->prolog_running_jobs);
		slurm_cond_destroy(&conf->prolog_running_cond);
		slurm_cred_ctx_destroy(conf->vctx);
		xfree(conf);
	}
	return;
}

static void
_print_config(void)
{
	int days, hours, mins, secs;
	char name[128];

	if (conf->cluster_name)
		printf("ClusterName=%s ", conf->cluster_name);

	gethostname_short(name, sizeof(name));
	printf("NodeName=%s ", name);

	xcpuinfo_hwloc_topo_get(&conf->actual_cpus,
				&conf->actual_boards,
				&conf->actual_sockets,
				&conf->actual_cores,
				&conf->actual_threads,
				&conf->block_map_size,
				&conf->block_map, &conf->block_map_inv);
	printf("CPUs=%u Boards=%u SocketsPerBoard=%u CoresPerSocket=%u "
	       "ThreadsPerCore=%u ",
	       conf->actual_cpus, conf->actual_boards, conf->actual_sockets,
	       conf->actual_cores, conf->actual_threads);

	get_memory(&conf->real_memory_size);
	printf("RealMemory=%"PRIu64"\n", conf->real_memory_size);

	get_up_time(&conf->up_time);
	secs  =  conf->up_time % 60;
	mins  = (conf->up_time / 60) % 60;
	hours = (conf->up_time / 3600) % 24;
	days  = (conf->up_time / 86400);
	printf("UpTime=%u-%2.2u:%2.2u:%2.2u\n", days, hours, mins, secs);
}

static void
_process_cmdline(int ac, char **av)
{
	int c;
	char *tmp_char;

	conf->prog = xbasename(av[0]);

	while ((c = getopt(ac, av, GETOPT_ARGS)) > 0) {
		switch (c) {
		case 'b':
			conf->boot_time = 1;
			break;
		case 'c':
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
			conf->conffile = xstrdup(optarg);
			break;
		case 'h':
			_usage();
			exit(0);
			break;
		case 'L':
			conf->logfile = xstrdup(optarg);
			break;
		case 'M':
			conf->mlock_pages = 1;
			break;
		case 'n':
			conf->nice = strtol(optarg, &tmp_char, 10);
			if (tmp_char[0] != '\0') {
				error("Invalid option for -n option (nice "
				      "value), ignored");
				conf->nice = 0;
			}
			break;
		case 'N':
			conf->node_name = xstrdup(optarg);
			break;
		case 'v':
			conf->debug_level++;
			conf->debug_level_set = 1;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		default:
			_usage();
			exit(1);
			break;
		}
	}

	/*
	 *  If slurmstepd path wasn't overridden by command line, set
	 *   it to the default here:
	 */
	if (!conf->stepd_loc) {
		conf->stepd_loc =
			xstrdup_printf("%s/sbin/slurmstepd", SLURM_PREFIX);
	}
}


static void
_create_msg_socket(void)
{
	char* node_addr;

	int ld = slurm_init_msg_engine_addrname_port(conf->node_addr,
						     conf->port);
	if (conf->node_addr == NULL)
		node_addr = "*";
	else
		node_addr = conf->node_addr;

	if (ld < 0) {
		error("Unable to bind listen port (%s:%d): %m",
		      node_addr, conf->port);
		exit(1);
	}

	fd_set_close_on_exec(ld);

	conf->lfd = ld;

	debug3("successfully opened slurm listen port %s:%d",
	       node_addr, conf->port);

	return;
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

static int
_slurmd_init(void)
{
	struct rlimit rlim;
	struct stat stat_buf;
	uint32_t cpu_cnt;

	/*
	 * Process commandline arguments first, since one option may be
	 * an alternate location for the slurm config file.
	 */
	_process_cmdline(*conf->argc, *conf->argv);

	/*
	 * Build nodes table like in slurmctld
	 * This is required by the topology stack
	 * Node tables setup must precede _read_config() so that the
	 * proper hostname is set.
	 */
	slurm_conf_init(conf->conffile);
	init_node_conf();
	/* slurm_select_init() must be called before
	 * build_all_nodeline_info() to be called with proper argument. */
	if (slurm_select_init(1) != SLURM_SUCCESS )
		return SLURM_FAILURE;
	build_all_nodeline_info(true, 0);
	build_all_frontend_info(true);

	/*
	 * Read global slurm config file, override necessary values from
	 * defaults and command line.
	 */
	_read_config();

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
	if (_set_slurmd_spooldir() < 0) {
		error("Unable to initialize slurmd spooldir");
		return SLURM_FAILURE;
	}

	/* Set up the hwloc whole system xml file */
	if (xcpuinfo_init() != XCPUINFO_SUCCESS)
		return SLURM_FAILURE;

	fini_job_cnt = cpu_cnt = MAX(conf->conf_cpus, conf->block_map_size);
	fini_job_id = xmalloc(sizeof(uint32_t) * fini_job_cnt);

	if ((gres_plugin_init() != SLURM_SUCCESS) ||
	    (gres_plugin_node_config_load(cpu_cnt, conf->node_name, NULL)
	     != SLURM_SUCCESS))
		return SLURM_FAILURE;
	if (slurm_topo_init() != SLURM_SUCCESS)
		return SLURM_FAILURE;

	/*
	 * Get and set slurmd topology information
	 * Build node hash table first to speed up the topo build
	 */
	rehash_node();
	slurm_topo_build_config();
	_set_topo_info();

	route_init(conf->node_name);

	/*
	 * Check for cpu frequency set capabilities on this node
	 */
	cpu_freq_init(conf);

	/*
	 * If configured, apply resource specialization
	 */
	_resource_spec_init();

	_print_conf();

	if (slurm_proctrack_init() != SLURM_SUCCESS)
		return SLURM_FAILURE;
	if (slurmd_task_init() != SLURM_SUCCESS)
		return SLURM_FAILURE;
	if (slurm_auth_init(NULL) != SLURM_SUCCESS)
		return SLURM_FAILURE;
	if (spank_slurmd_init() < 0)
		return SLURM_FAILURE;

	if (getrlimit(RLIMIT_CPU, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CPU, &rlim);
		if (rlim.rlim_max != RLIM_INFINITY) {
			error("Slurmd process CPU time limit is %d seconds",
			      (int) rlim.rlim_max);
		}
	}

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_NOFILE, &rlim);
	}
	if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CORE, &rlim);
	}

	/*
	 * Create a context for verifying slurm job credentials
	 */
	if (!(conf->vctx = slurm_cred_verifier_ctx_create(conf->pubkey)))
		return SLURM_FAILURE;
	if (!xstrcmp(conf->select_type, "select/serial")) {
		/* Only cache credential for 5 seconds with select/serial
		 * for shorter cache searches and higher throughput */
		slurm_cred_ctx_set(conf->vctx, SLURM_CRED_OPT_EXPIRY_WINDOW, 5);
	}

	if (conf->cleanstart) {
		/*
		 * Need to kill any running slurmd's here
		 */
		_kill_old_slurmd();

		stepd_cleanup_sockets(conf->spooldir, conf->node_name);
		_stepd_cleanup_batch_dirs(conf->spooldir, conf->node_name);
	}

	if (conf->daemonize) {
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
				return SLURM_FAILURE;
			} else
				info("chdir to /var/tmp");
		}
	}

	if ((devnull = open("/dev/null", O_RDWR | O_CLOEXEC)) < 0) {
		error("Unable to open /dev/null: %m");
		return SLURM_FAILURE;
	}

	/* make sure we have slurmstepd installed */
	if (stat(conf->stepd_loc, &stat_buf))
		fatal("Unable to find slurmstepd file at %s", conf->stepd_loc);
	if (!S_ISREG(stat_buf.st_mode))
		fatal("slurmstepd not a file at %s", conf->stepd_loc);

	return SLURM_SUCCESS;
}

static int
_restore_cred_state(slurm_cred_ctx_t ctx)
{
	char *file_name = NULL, *data = NULL;
	uint32_t data_offset = 0;
	int cred_fd, data_allocated, data_read = 0;
	Buf buffer = NULL;

	if ( (mkdir(conf->spooldir, 0755) < 0) && (errno != EEXIST) ) {
		fatal("mkdir(%s): %m", conf->spooldir);
		return SLURM_ERROR;
	}

	file_name = xstrdup(conf->spooldir);
	xstrcat(file_name, "/cred_state");
	cred_fd = open(file_name, O_RDONLY);
	if (cred_fd < 0)
		goto cleanup;

	data_allocated = 1024;
	data = xmalloc(data_allocated);
	while ((data_read = read(cred_fd, data + data_offset, 1024)) == 1024) {
		data_offset += data_read;
		data_allocated += 1024;
		xrealloc(data, data_allocated);
	}
	data_offset += data_read;
	close(cred_fd);
	buffer = create_buf(data, data_offset);

	slurm_cred_ctx_unpack(ctx, buffer);

cleanup:
	xfree(file_name);
	if (buffer)
		free_buf(buffer);
	return SLURM_SUCCESS;
}

/**************************************************************************\
 * To test for memory leaks, set MEMORY_LEAK_DEBUG to 1 using
 * "configure --enable-memory-leak-debug" then execute
 * $ valgrind --tool=memcheck --leak-check=yes --num-callers=8 \
 *   --leak-resolution=med ./slurmd -Dc >valg.slurmd.out 2>&1
 *
 * Then exercise the slurmd functionality before executing
 * > scontrol shutdown
 *
 * All allocated memory should be freed
\**************************************************************************/
static int
_slurmd_fini(void)
{
	assoc_mgr_fini(false);
	node_features_g_fini();
	core_spec_g_fini();
	switch_g_node_fini();
	jobacct_gather_fini();
	acct_gather_profile_fini();
	save_cred_state(conf->vctx);
	switch_fini();
	slurmd_task_fini();
	slurm_conf_destroy();
	slurm_proctrack_fini();
	slurm_auth_fini();
	node_fini2();
	gres_plugin_fini();
	slurm_topo_fini();
	slurmd_req(NULL);	/* purge memory allocated by slurmd_req() */
	fini_setproctitle();
	slurm_select_fini();
	spank_slurmd_exit();
	cpu_freq_fini();
	_resource_spec_fini();
	job_container_fini();
	acct_gather_conf_destroy();
	fini_system_cgroup();
	route_fini();
	xcpuinfo_fini();
	slurm_mutex_lock(&fini_job_mutex);
	xfree(fini_job_id);
	fini_job_cnt = 0;
	slurm_mutex_unlock(&fini_job_mutex);

	return SLURM_SUCCESS;
}

/*
 * save_cred_state - save the current credential list to a file
 * IN list - list of credentials
 * RET int - zero or error code
 */
int save_cred_state(slurm_cred_ctx_t ctx)
{
	char *old_file, *new_file, *reg_file;
	int cred_fd = -1, error_code = SLURM_SUCCESS, rc;
	Buf buffer = NULL;
	static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

	old_file = xstrdup(conf->spooldir);
	xstrcat(old_file, "/cred_state.old");
	reg_file = xstrdup(conf->spooldir);
	xstrcat(reg_file, "/cred_state");
	new_file = xstrdup(conf->spooldir);
	xstrcat(new_file, "/cred_state.new");

	slurm_mutex_lock(&state_mutex);
	if ((cred_fd = creat(new_file, 0600)) < 0) {
		error("creat(%s): %m", new_file);
		if (errno == ENOSPC)
			_drain_node("SlurmdSpoolDir is full");
		error_code = errno;
		goto cleanup;
	}
	buffer = init_buf(1024);
	slurm_cred_ctx_pack(ctx, buffer);
	rc = write(cred_fd, get_buf_data(buffer), get_buf_offset(buffer));
	if (rc != get_buf_offset(buffer)) {
		error("write %s error %m", new_file);
		(void) unlink(new_file);
		if ((rc < 0) && (errno == ENOSPC))
			_drain_node("SlurmdSpoolDir is full");
		error_code = errno;
		goto cleanup;
	}
	(void) unlink(old_file);
	if (link(reg_file, old_file))
		debug4("unable to create link for %s -> %s: %m",
		       reg_file, old_file);
	(void) unlink(reg_file);
	if (link(new_file, reg_file))
		debug4("unable to create link for %s -> %s: %m",
		       new_file, reg_file);
	(void) unlink(new_file);

cleanup:
	slurm_mutex_unlock(&state_mutex);
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	if (buffer)
		free_buf(buffer);
	if (cred_fd >= 0)
		close(cred_fd);
	return error_code;
}

static int _drain_node(char *reason)
{
	slurm_msg_t req_msg;
	update_node_msg_t update_node_msg;

	memset(&update_node_msg, 0, sizeof(update_node_msg_t));
	update_node_msg.node_names = conf->node_name;
	update_node_msg.node_state = NODE_STATE_DRAIN;
	update_node_msg.reason = reason;
	update_node_msg.reason_uid = getuid();
	update_node_msg.weight = NO_VAL;
	slurm_msg_t_init(&req_msg);
	req_msg.msg_type = REQUEST_UPDATE_NODE;
	req_msg.data = &update_node_msg;

	if (slurm_send_only_controller_msg(&req_msg, working_cluster_rec) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern void slurmd_shutdown(int signum)
{
	if (signum == SIGTERM || signum == SIGINT) {
		_shutdown = 1;
		if (msg_pthread && (pthread_self() != msg_pthread))
			pthread_kill(msg_pthread, SIGTERM);
		msg_aggr_sender_fini();
	}
}

static void
_hup_handler(int signum)
{
	if (signum == SIGHUP) {
		_reconfig = 1;
	}
}

static void
_usr_handler(int signum)
{
	if (signum == SIGUSR2) {
		_update_log = 1;
	}
}


static void
_usage(void)
{
	fprintf(stderr, "\
Usage: %s [OPTIONS]\n\
   -b          Report node reboot now.\n\
   -c          Force cleanup of slurmd shared memory.\n\
   -C          Print node configuration information and exit.\n\
   -d stepd    Pathname to the slurmstepd program.\n\
   -D          Run daemon in foreground.\n\
   -f config   Read configuration from the specified file.\n\
   -h          Print this help message.\n\
   -L logfile  Log messages to the file `logfile'.\n\
   -M          Use mlock() to lock slurmd pages into memory.\n\
   -n value    Run the daemon at the specified nice value.\n\
   -N host     Run the daemon for specified hostname.\n\
   -v          Verbose mode. Multiple -v's increase verbosity.\n\
   -V          Print version information and exit.\n", conf->prog);
	return;
}

/*
 * create spool directory as needed and "cd" to it
 */
static int
_set_slurmd_spooldir(void)
{
	debug3("initializing slurmd spool directory");

	if (mkdir(conf->spooldir, 0755) < 0) {
		if (errno != EEXIST) {
			fatal("mkdir(%s): %m", conf->spooldir);
			return SLURM_ERROR;
		}
	}

	/*
	 * Ensure spool directory permissions are correct.
	 */
	if (chmod(conf->spooldir, 0755) < 0) {
		error("chmod(%s, 0755): %m", conf->spooldir);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/* Kill the currently running slurmd
 *
 * Returns file descriptor for the existing pidfile so that the
 * current slurmd can wait on termination of the old.
 */
static void
_kill_old_slurmd(void)
{
	int fd;
	pid_t oldpid = read_pidfile(conf->pidfile, &fd);
	if (oldpid != (pid_t) 0) {
		info ("killing old slurmd[%lu]", (unsigned long) oldpid);
		kill(oldpid, SIGTERM);

		/*
		 * Wait for previous daemon to terminate
		 */
		if (fd_get_readw_lock(fd) < 0) {
			fatal ("error getting readw lock on file %s: %m",
			       conf->pidfile);
		}
		(void) close(fd); /* Ignore errors */
	}
}

/* Reset slurmd logging based upon configuration parameters */
static void _update_logging(void)
{
	List steps;
	ListIterator i;
	step_loc_t *stepd;
	log_options_t *o = &conf->log_opts;
	slurm_ctl_conf_t *cf;

	_update_log = 0;
	/* Preserve execute line verbose arguments (if any) */
	cf = slurm_conf_lock();
	if (!conf->debug_level_set && (cf->slurmd_debug != NO_VAL16))
		conf->debug_level = cf->slurmd_debug;
	conf->syslog_debug = cf->slurmd_syslog_debug;
	conf->log_fmt = cf->log_fmt;
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
	log_set_timefmt(conf->log_fmt);

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

	/*
	 * Send reconfig to each stepd so they will rotate as well.
	 */

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		int fd;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1)
			continue;

		if (stepd_reconfig(fd, stepd->protocol_version)
		    != SLURM_SUCCESS)
			debug("Reconfig jobid=%u.%u failed: %m",
			      stepd->jobid, stepd->stepid);
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);
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
 *  Lock the fork mutex to protect fork-critical regions
 */
static void _atfork_prepare(void)
{
	slurm_mutex_lock(&fork_mutex);
}

/*
 *  Unlock  fork mutex to allow fork-critical functions to continue
 */
static void _atfork_final(void)
{
	slurm_mutex_unlock(&fork_mutex);
}

static void _install_fork_handlers(void)
{
	int err;

	err = pthread_atfork(&_atfork_prepare, &_atfork_final, &_atfork_final);
	if (err) error ("pthread_atfork: %m");

	return;
}

/*
 * set topology address and address pattern of slurmd node
 */
static int _set_topo_info(void)
{
	int rc;
	char * addr, * pattern;

	rc = slurm_topo_get_node_addr(conf->node_name, &addr, &pattern);
	if ( rc == SLURM_SUCCESS ) {
		xfree(conf->node_topo_addr);
		xfree(conf->node_topo_pattern);
		conf->node_topo_addr = addr;
		conf->node_topo_pattern = pattern;
	}

	return rc;
}

static uint64_t _get_int(const char *my_str)
{
	char *end = NULL;
	uint64_t value;

	if (!my_str)
		return NO_VAL;
	value = strtol(my_str, &end, 10);
	if (my_str == end)
		return NO_VAL;
	return value;
}

static uint64_t _parse_msg_aggr_params(int type, char *params)
{
	uint64_t value = NO_VAL;
	char *sub_str = NULL;

	if (!params)
		return NO_VAL;

	switch (type) {
	case WINDOW_TIME:
		if ((sub_str = xstrcasestr(params, "WindowTime=")))
			value = _get_int(sub_str + 11);
		break;
	case WINDOW_MSGS:
		if ((sub_str = xstrcasestr(params, "WindowMsgs=")))
			value = _get_int(sub_str + 11);
		break;
	default:
		fatal("invalid message aggregation parameters: %s", params);
	}
	return value;
}

static void _set_msg_aggr_params(void)
{
	conf->msg_aggr_window_time = _parse_msg_aggr_params(WINDOW_TIME,
			       conf->msg_aggr_params);
	conf->msg_aggr_window_msgs = _parse_msg_aggr_params(WINDOW_MSGS,
			       conf->msg_aggr_params);

	if (conf->msg_aggr_window_time == NO_VAL)
		conf->msg_aggr_window_time = DEFAULT_MSG_AGGR_WINDOW_TIME;
	if (conf->msg_aggr_window_msgs == NO_VAL)
		conf->msg_aggr_window_msgs = DEFAULT_MSG_AGGR_WINDOW_MSGS;
	if (conf->msg_aggr_window_msgs > 1) {
		info("Message aggregation enabled: WindowMsgs=%"PRIu64", WindowTime=%"PRIu64,
		     conf->msg_aggr_window_msgs, conf->msg_aggr_window_time);
	} else
		info("Message aggregation disabled");
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

/* Return true if CoreSpecPlugin=core_spec/cray */
static bool _is_core_spec_cray(void)
{
	bool use_core_spec_cray = false;
	char *core_spec_plugin = slurm_get_core_spec_plugin();
	if (core_spec_plugin && strstr(core_spec_plugin, "cray"))
		use_core_spec_cray = true;
	xfree(core_spec_plugin);
	return use_core_spec_cray;
}

/*
 * If configured, initialize core specialization
 */
static int _core_spec_init(void)
{
	int i, rval;
	pid_t pid;
	uint32_t task_params;
	bool slurmd_off_spec;
	bitstr_t *res_mac_bitmap;
	cpu_set_t mask;

	if ((conf->core_spec_cnt == 0) && (conf->cpu_spec_list == NULL)) {
		debug("Resource spec: No specialized cores configured by "
		      "default on this node");
		return SLURM_SUCCESS;
	}
	if (_is_core_spec_cray()) {	/* No need to use cgroups */
		debug("Using core_spec/cray to manage specialized cores");
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
	task_params = slurm_get_task_plugin_param();
	slurmd_off_spec = (task_params & SLURMD_OFF_SPEC);

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
			bit_free(res_mac_bitmap);
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
		bit_free(res_mac_bitmap);

#ifdef __FreeBSD__
		rval = cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID,
					  pid, sizeof(cpu_set_t), &mask);
#elif defined(SCHED_GETAFFINITY_THREE_ARGS)
		rval = sched_setaffinity(pid, sizeof(cpu_set_t), &mask);
#else
		rval = sched_setaffinity(pid, &mask);
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
	if (!check_memspec_cgroup_job_confinement()) {
		if (slurm_get_select_type_param() & CR_MEMORY) {
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
	if (disable_system_cgroup_mem_oom()) {
		error("Resource spec: unable to disable OOM Killer in "
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
	char *sched_params;
	bool spec_cores_first;

	sched_params = slurm_get_sched_params();
	if (sched_params && strstr(sched_params, "spec_cores_first"))
		spec_cores_first = true;
	else
		spec_cores_first = false;
	xfree(sched_params);
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

	/* create CPU bitmap from input CPU list */
	if (bit_unfmt(res_cpu_bitmap, conf->cpu_spec_list) != 0) {
		return SLURM_ERROR;
	}
	/* create core bitmap and list from CPU bitmap */
	for (thread_off = 0; thread_off < ncpus; thread_off++) {
		if (bit_test(res_cpu_bitmap, thread_off) == 1)
			bit_set(res_core_bitmap, thread_off/(conf->threads));
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
		   != XCPUINFO_SUCCESS)
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

	if (conf->health_check_program && (conf->health_check_interval != 0)) {
		char *env[1] = { NULL };
		rc = run_script("health_check", conf->health_check_program,
				0, 60, env, 0);
	}

	return rc;
}
