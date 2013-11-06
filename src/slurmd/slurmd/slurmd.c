/*****************************************************************************\
 *  src/slurmd/slurmd/slurmd.c - main slurm node server daemon
 *  $Id$
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif
#if HAVE_HWLOC
#  include <hwloc.h>
#endif

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "src/common/bitstring.h"
#include "src/common/cpu_frequency.h"
#include "src/common/daemonize.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/parse_spec.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/slurmd/common/set_oomadj.h"
#include "src/slurmd/common/setproctitle.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_topology.h"
#include "src/common/stepd_api.h"
#include "src/common/switch.h"
#include "src/slurmd/common/task_plugin.h"
#include "src/common/xcpuinfo.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xsignal.h"
#include "src/common/plugstack.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmd/req.h"
#include "src/slurmd/slurmd/get_mach_stat.h"
#include "src/slurmd/slurmd/slurmd_plugstack.h"
#include "src/slurmd/common/job_container_plugin.h"
#include "src/slurmd/common/proctrack.h"

#define GETOPT_ARGS	"cCd:Df:hL:Mn:N:vV"

#ifndef MAXHOSTNAMELEN
#  define MAXHOSTNAMELEN	64
#endif

#define MAX_THREADS		130

/* global, copied to STDERR_FILENO in tasks before the exec */
int devnull = -1;
slurmd_conf_t * conf;

/*
 * count of active threads
 */
static int             active_threads = 0;
static pthread_mutex_t active_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond    = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t fork_mutex     = PTHREAD_MUTEX_INITIALIZER;

typedef struct connection {
	slurm_fd_t fd;
	slurm_addr_t *cli_addr;
} conn_t;



/*
 * static shutdown and reconfigure flags:
 */
static sig_atomic_t _shutdown = 0;
static sig_atomic_t _reconfig = 0;
static pthread_t msg_pthread = (pthread_t) 0;
static time_t sent_reg_time = (time_t) 0;

static void      _atfork_final(void);
static void      _atfork_prepare(void);
static void      _create_msg_socket(void);
static void      _decrement_thd_count(void);
static void      _destroy_conf(void);
static int       _drain_node(char *reason);
static void      _fill_registration_msg(slurm_node_registration_status_msg_t *);
static void      _handle_connection(slurm_fd_t fd, slurm_addr_t *client);
static void      _hup_handler(int);
static void      _increment_thd_count(void);
static void      _init_conf(void);
static void      _install_fork_handlers(void);
static void 	 _kill_old_slurmd(void);
static void      _msg_engine(void);
static void      _print_conf(void);
static void      _print_config(void);
static void      _process_cmdline(int ac, char **av);
static void      _read_config(void);
static void      _reconfigure(void);
static void     *_registration_engine(void *arg);
static int       _restore_cred_state(slurm_cred_ctx_t ctx);
static void     *_service_connection(void *);
static int       _set_slurmd_spooldir(void);
static int       _set_topo_info(void);
static int       _slurmd_init(void);
static int       _slurmd_fini(void);
static void      _spawn_registration_engine(void);
static void      _term_handler(int);
static void      _update_logging(void);
static void      _update_nice(void);
static void      _usage(void);
static void      _wait_for_all_threads(int secs);


int
main (int argc, char *argv[])
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
		if ((pw=getpwuid(slurmd_uid)))
			slurmd_user = xstrdup(pw->pw_name);
		if ((pw=getpwuid(curr_uid)))
			curr_user = pw->pw_name;

		fatal("You are running slurmd as something "
		      "other than user %s(%d).  If you want to "
		      "run as this user add SlurmdUser=%s "
		      "to the slurm.conf file.",
		      slurmd_user, slurmd_uid, curr_user);
	}
	init_setproctitle(argc, argv);

	xsignal(SIGTERM, &_term_handler);
	xsignal(SIGINT,  &_term_handler);
	xsignal(SIGHUP,  &_hup_handler );
	xsignal_block(blocked_signals);

	debug3("slurmd initialization successful");

	/*
	 * Become a daemon if desired.
	 * Do not chdir("/") or close all fd's
	 */
	if (conf->daemonize) {
		if (daemon(1,1) == -1)
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
	if (switch_g_node_init() < 0)
		fatal("Unable to initialize interconnect.");
	if (conf->cleanstart && switch_g_clear_node_state())
		fatal("Unable to clear interconnect state.");
	switch_g_slurmd_init();

	_create_msg_socket();

	conf->pid = getpid();
	/* This has to happen after daemon(), which closes all fd's,
	   so we keep the write lock of the pidfile.
	*/
	pidfd = create_pidfile(conf->pidfile, 0);

	rfc2822_timestamp(time_stamp, sizeof(time_stamp));
	info("%s started on %s", slurm_prog_name, time_stamp);

	_install_fork_handlers();
	list_install_fork_handlers();
	slurm_conf_install_fork_handlers();

	/*
	 * Initialize any plugins
	 */
	if (slurmd_plugstack_init())
		fatal("failed to initialize slurmd_plugstack");

	_spawn_registration_engine();
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

	info("Slurmd shutdown completing");
	log_fini();
       	return 0;
}

static void
_spawn_registration_engine(void)
{
	int            rc;
	pthread_attr_t attr;
	pthread_t      id;
	int            retries = 0;

	slurm_attr_init(&attr);
	rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		errno = rc;
		fatal("Unable to set detachstate on attr: %m");
		slurm_attr_destroy(&attr);
		return;
	}

	while (pthread_create(&id, &attr, &_registration_engine, NULL)) {
		error("msg_engine: pthread_create: %m");
		if (++retries > 3)
			fatal("msg_engine: pthread_create: %m");
		usleep(10);	/* sleep and again */
	}

	return;
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
	slurm_fd_t sock;

	msg_pthread = pthread_self();
	slurmd_req(NULL);	/* initialize timer */
	while (!_shutdown) {
		if (_reconfig) {
			verbose("got reconfigure request");
			_wait_for_all_threads(5); /* Wait for RPCs to finish */
			_reconfigure();
		}

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
	slurm_shutdown_msg_engine(conf->lfd);
	return;
}

static void
_decrement_thd_count(void)
{
	slurm_mutex_lock(&active_mutex);
	if (active_threads > 0)
		active_threads--;
	pthread_cond_signal(&active_cond);
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
		pthread_cond_wait(&active_cond, &active_mutex);
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
			pthread_cond_signal(&active_cond);
			slurm_mutex_unlock(&active_mutex);
			return;
		}
	}
	pthread_cond_signal(&active_cond);
	slurm_mutex_unlock(&active_mutex);
	verbose("all threads complete");
}

static void
_handle_connection(slurm_fd_t fd, slurm_addr_t *cli)
{
	int            rc;
	pthread_attr_t attr;
	pthread_t      id;
	conn_t         *arg = xmalloc(sizeof(conn_t));
	int            retries = 0;

	arg->fd       = fd;
	arg->cli_addr = cli;

	slurm_attr_init(&attr);
	rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		errno = rc;
		xfree(arg);
		error("Unable to set detachstate on attr: %m");
		slurm_attr_destroy(&attr);
		return;
	}

	fd_set_close_on_exec(fd);

	_increment_thd_count();
	while (pthread_create(&id, &attr, &_service_connection, (void *)arg)) {
		error("msg_engine: pthread_create: %m");
		if (++retries > 3) {
			error("running service_connection without starting "
			      "a new thread slurmd will be "
			      "unresponsive until done");

			_service_connection((void *) arg);
			info("slurmd should be responsive now");
			break;
		}
		usleep(10);	/* sleep and again */
	}

	return;
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
	slurmd_req(msg);

cleanup:
	if ((msg->conn_fd >= 0) && slurm_close_accepted_conn(msg->conn_fd) < 0)
		error ("close(%d): %m", con->fd);

	xfree(con->cli_addr);
	xfree(con);
	slurm_free_msg(msg);
	_decrement_thd_count();
	return NULL;
}

extern int
send_registration_msg(uint32_t status, bool startup)
{
	int rc, ret_val = SLURM_SUCCESS;
	slurm_msg_t req;
	slurm_node_registration_status_msg_t *msg =
		xmalloc (sizeof (slurm_node_registration_status_msg_t));

	slurm_msg_t_init(&req);

	msg->startup = (uint16_t) startup;
	_fill_registration_msg(msg);
	msg->status  = status;

	req.msg_type = MESSAGE_NODE_REGISTRATION_STATUS;
	req.data     = msg;

	if (slurm_send_recv_controller_rc_msg(&req, &rc) < 0) {
		error("Unable to register: %m");
		ret_val = SLURM_FAILURE;
	} else {
		sent_reg_time = time(NULL);
	}
	slurm_free_node_registration_status_msg (msg);

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
	msg->cpus	 = conf->cpus;
	msg->boards	 = conf->boards;
	msg->sockets	 = conf->sockets;
	msg->cores	 = conf->cores;
	msg->threads	 = conf->threads;
	msg->real_memory = conf->real_memory_size;
	msg->tmp_disk    = conf->tmp_disk_space;
	msg->hash_val    = slurm_get_hash_val();
	get_cpu_load(&msg->cpu_load);

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

	if (first_msg) {
		first_msg = false;
		info("CPUs=%u Boards=%u Sockets=%u Cores=%u Threads=%u "
		     "Memory=%u TmpDisk=%u Uptime=%u",
		     msg->cpus, msg->boards, msg->sockets, msg->cores,
		     msg->threads, msg->real_memory, msg->tmp_disk,
		     msg->up_time);
	} else {
		debug3("CPUs=%u Boards=%u Sockets=%u Cores=%u Threads=%u "
		       "Memory=%u TmpDisk=%u Uptime=%u",
		       msg->cpus, msg->boards, msg->sockets, msg->cores,
		       msg->threads, msg->real_memory, msg->tmp_disk,
		       msg->up_time);
	}
	uname(&buf);
	if ((arch = getenv("SLURM_ARCH")))
		msg->arch = xstrdup(arch);
	else
		msg->arch = xstrdup(buf.machine);
	if ((os = getenv("SLURM_OS")))
		msg->os   = xstrdup(os);
	else
		msg->os = xstrdup(buf.sysname);

	if (msg->startup) {
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
				   stepd->jobid, stepd->stepid);
		if (fd == -1) {
			--(msg->job_count);
			continue;
		}
		if (stepd_state(fd) == SLURMSTEPD_NOT_RUNNING) {
			debug("stale domain socket for stepd %u.%u ",
			      stepd->jobid, stepd->stepid);
			--(msg->job_count);
			close(fd);
			continue;
		}

		close(fd);
		if (stepd->stepid == NO_VAL)
			debug("found apparently running job %u", stepd->jobid);
		else
			debug("found apparently running step %u.%u",
			      stepd->jobid, stepd->stepid);
		msg->job_id[n]  = stepd->jobid;
		msg->step_id[n] = stepd->stepid;
		n++;
	}
	list_iterator_destroy(i);
	list_destroy(steps);

	if (!msg->energy)
		msg->energy = acct_gather_energy_alloc();
	acct_gather_energy_g_get_data(ENERGY_DATA_STRUCT, msg->energy);

	msg->timestamp = time(NULL);

	return;
}

static inline void
_free_and_set(char **confvar, char *newval)
{
	xfree(*confvar);
	*confvar = newval;
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
	uint16_t tmp16 = 0;

#ifndef HAVE_FRONT_END
	bool cr_flag = false, gang_flag = false;
#endif

	cf = slurm_conf_lock();

	slurm_mutex_lock(&conf->config_mutex);

	if (conf->conffile == NULL)
		conf->conffile = xstrdup(cf->slurm_conf);

	conf->slurm_user_id =  cf->slurm_user_id;

	conf->cr_type = cf->select_type_param;

	path_pubkey = xstrdup(cf->job_credential_public_certificate);

	if (!conf->logfile)
		conf->logfile = xstrdup(cf->slurmd_logfile);

#ifndef HAVE_FRONT_END
	if (!strcmp(cf->select_type, "select/cons_res"))
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
	    (strcmp(conf->node_addr, conf->hostname) == 0)) {
		xfree(conf->node_addr);	/* Sets to NULL */
	}

	conf->port = slurm_conf_get_port(conf->node_name);
	slurm_conf_get_cpus_bsct(conf->node_name,
				 &conf->conf_cpus, &conf->conf_boards,
				 &conf->conf_sockets, &conf->conf_cores,
				 &conf->conf_threads);

	/* store hardware properties in slurmd_config */
	xfree(conf->block_map);
	xfree(conf->block_map_inv);

	_update_logging();
	_update_nice();

	get_cpuinfo(&conf->actual_cpus,
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
			info("Node configuration differs from hardware: "
			     "CPUs=%u:%u(hw) Boards=%u:%u(hw) "
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
	_free_and_set(&conf->epilog,   xstrdup(cf->epilog));
	_free_and_set(&conf->prolog,   xstrdup(cf->prolog));
	_free_and_set(&conf->tmpfs,    xstrdup(cf->tmp_fs));
	_free_and_set(&conf->health_check_program,
		      xstrdup(cf->health_check_program));
	_free_and_set(&conf->spooldir, xstrdup(cf->slurmd_spooldir));
	_massage_pathname(&conf->spooldir);
	_free_and_set(&conf->pidfile,  xstrdup(cf->slurmd_pidfile));
	_massage_pathname(&conf->pidfile);
	_free_and_set(&conf->select_type, xstrdup(cf->select_type));
	_free_and_set(&conf->task_prolog, xstrdup(cf->task_prolog));
	_free_and_set(&conf->task_epilog, xstrdup(cf->task_epilog));
	_free_and_set(&conf->pubkey,   path_pubkey);

	conf->debug_flags = cf->debug_flags;
	conf->propagate_prio = cf->propagate_prio_process;

	_free_and_set(&conf->job_acct_gather_freq,
		      xstrdup(cf->job_acct_gather_freq));

	conf->acct_freq_task = (uint16_t)NO_VAL;
	tmp16 = acct_gather_parse_freq(PROFILE_TASK,
				       conf->job_acct_gather_freq);
	if (tmp16 != -1)
		conf->acct_freq_task = tmp16;

	_free_and_set(&conf->acct_gather_energy_type,
		      xstrdup(cf->acct_gather_energy_type));
	_free_and_set(&conf->acct_gather_filesystem_type,
		      xstrdup(cf->acct_gather_filesystem_type));
	_free_and_set(&conf->acct_gather_infiniband_type,
		      xstrdup(cf->acct_gather_infiniband_type));
	_free_and_set(&conf->acct_gather_profile_type,
		      xstrdup(cf->acct_gather_profile_type));
	_free_and_set(&conf->job_acct_gather_type,
		      xstrdup(cf->job_acct_gather_type));

	if ( (conf->node_name == NULL) ||
	     (conf->node_name[0] == '\0') )
		fatal("Node name lookup failure");

	if (cf->control_addr == NULL)
		fatal("Unable to establish controller machine");
	if (cf->slurmctld_port == 0)
		fatal("Unable to establish controller port");
	conf->slurmd_timeout = cf->slurmd_timeout;
	conf->use_pam = cf->use_pam;
	conf->task_plugin_param = cf->task_plugin_param;

	slurm_mutex_unlock(&conf->config_mutex);
	slurm_conf_unlock();
}

static void
_reconfigure(void)
{
	List steps;
	ListIterator i;
	slurm_ctl_conf_t *cf;
	step_loc_t *stepd;
	bool did_change;

	_reconfig = 0;
	slurm_conf_reinit(conf->conffile);
	_read_config();

	/*
	 * Rebuild topology information and refresh slurmd topo infos
	 */
	slurm_topo_build_config();
	_set_topo_info();

	/*
	 * In case the administrator changed the cpu frequency set capabilities
	 * on this node, rebuild the cpu frequency table information
	 */
	cpu_freq_init(conf);

	_print_conf();

	/*
	 * Make best effort at changing to new public key
	 */
	slurm_cred_ctx_key_update(conf->vctx, conf->pubkey);

	/*
	 * Reinitialize the groups cache
	 */
	cf = slurm_conf_lock();
	if (cf->group_info & GROUP_CACHE)
		init_gids_cache(1);
	else
		init_gids_cache(0);
	slurm_conf_unlock();

	/* send reconfig to each stepd so they can refresh their log
	 * file handle
	 */

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		int fd;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid);
		if (fd == -1)
			continue;
		if (stepd_reconfig(fd) != SLURM_SUCCESS)
			debug("Reconfig jobid=%u.%u failed: %m",
			      stepd->jobid, stepd->stepid);
		close(fd);
	}
	list_iterator_destroy(i);
	list_destroy(steps);

	gres_plugin_reconfig(&did_change);
	(void) switch_g_reconfig();
	container_g_reconfig();
	if (did_change) {
		uint32_t cpu_cnt = MAX(conf->conf_cpus, conf->block_map_size);
		(void) gres_plugin_node_config_load(cpu_cnt);
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
	char *str, time_str[32];
	int i;

	cf = slurm_conf_lock();
	debug3("NodeName    = %s",       conf->node_name);
	debug3("TopoAddr    = %s",       conf->node_topo_addr);
	debug3("TopoPattern = %s",       conf->node_topo_pattern);
	if (cf->group_info & GROUP_CACHE)
		i = 1;
	else
		i = 0;
	debug3("CacheGroups = %d",       i);
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

	str = xmalloc(conf->block_map_size*5);
	str[0] = '\0';
	for (i = 0; i < conf->block_map_size; i++) {
		char id[10];
		sprintf(id, "%u,", conf->block_map[i]);
		strcat(str, id);
	}
	str[strlen(str)-1] = '\0';		/* trim trailing "," */
	debug3("Block Map   = %s", str);
	str[0] = '\0';
	for (i = 0; i < conf->block_map_size; i++) {
		char id[10];
		sprintf(id, "%u,", conf->block_map_inv[i]);
		strcat(str, id);
	}
	str[strlen(str)-1] = '\0';		/* trim trailing "," */
	debug3("Inverse Map = %s", str);
	xfree(str);
	debug3("RealMemory  = %u",       conf->real_memory_size);
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
	conf->lfd         = -1;
	conf->log_opts    = lopts;
	conf->debug_level = LOG_LEVEL_INFO;
	conf->pidfile     = xstrdup(DEFAULT_SLURMD_PIDFILE);
	conf->spooldir	  = xstrdup(DEFAULT_SPOOLDIR);

	slurm_mutex_init(&conf->config_mutex);

	conf->starting_steps = list_create(destroy_starting_step);
	slurm_mutex_init(&conf->starting_steps_lock);
	pthread_cond_init(&conf->starting_steps_cond, NULL);
	conf->prolog_running_jobs = list_create(slurm_destroy_uint32_ptr);
	slurm_mutex_init(&conf->prolog_running_lock);
	pthread_cond_init(&conf->prolog_running_cond, NULL);
	return;
}

static void
_destroy_conf(void)
{
	if (conf) {
		xfree(conf->acct_gather_energy_type);
		xfree(conf->acct_gather_filesystem_type);
		xfree(conf->acct_gather_infiniband_type);
		xfree(conf->acct_gather_profile_type);
		xfree(conf->block_map);
		xfree(conf->block_map_inv);
		xfree(conf->conffile);
		xfree(conf->epilog);
		xfree(conf->health_check_program);
		xfree(conf->hostname);
		xfree(conf->job_acct_gather_freq);
		xfree(conf->job_acct_gather_type);
		xfree(conf->logfile);
		xfree(conf->node_name);
		xfree(conf->node_addr);
		xfree(conf->node_topo_addr);
		xfree(conf->node_topo_pattern);
		xfree(conf->pidfile);
		xfree(conf->prolog);
		xfree(conf->pubkey);
		xfree(conf->select_type);
		xfree(conf->spooldir);
		xfree(conf->stepd_loc);
		xfree(conf->task_prolog);
		xfree(conf->task_epilog);
		xfree(conf->tmpfs);
		slurm_mutex_destroy(&conf->config_mutex);
		list_destroy(conf->starting_steps);
		slurm_mutex_destroy(&conf->starting_steps_lock);
		pthread_cond_destroy(&conf->starting_steps_cond);
		list_destroy(conf->prolog_running_jobs);
		slurm_mutex_destroy(&conf->prolog_running_lock);
		pthread_cond_destroy(&conf->prolog_running_cond);
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

	gethostname_short(name, sizeof(name));
	printf("NodeName=%s ", name);

	get_cpuinfo(&conf->actual_cpus,
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
	get_tmp_disk(&conf->tmp_disk_space, "/tmp");
	printf("RealMemory=%u TmpDisk=%u\n",
	       conf->real_memory_size, conf->tmp_disk_space);

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
		case 'c':
			conf->cleanstart = 1;
			break;
		case 'C':
			_print_config();
			exit(0);
			break;
		case 'd':
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
	if (!conf->stepd_loc)
		conf->stepd_loc =
			xstrdup_printf("%s/sbin/slurmstepd", SLURM_PREFIX);
}


static void
_create_msg_socket(void)
{
	char* node_addr;

	slurm_fd_t ld = slurm_init_msg_engine_addrname_port(conf->node_addr,
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
	char dir_path[MAXPATHLEN], file_path[MAXPATHLEN];
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
		if (!strncmp(ent->d_name, "job", 3) &&
		    (ent->d_name[3] >= '0') && (ent->d_name[3] <= '9')) {
			snprintf(dir_path, sizeof(dir_path),
				 "%s/%s", directory, ent->d_name);
			snprintf(file_path, sizeof(file_path),
				 "%s/slurm_script", dir_path);
			info("Purging vestigial job script %s", file_path);
			(void) unlink(file_path);
			(void) rmdir(dir_path);
		}
	}
	closedir(dp);
}

static int
_slurmd_init(void)
{
	struct rlimit rlim;
	slurm_ctl_conf_t *cf;
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
	 * Node tables setup must preceed _read_config() so that the
	 * proper hostname is set.
	 */
	slurm_conf_init(conf->conffile);
	init_node_conf();
	/* slurm_select_init() must be called before
	 * build_all_nodeline_info() to be called with proper argument. */
	if (slurm_select_init(1) != SLURM_SUCCESS )
		return SLURM_FAILURE;
	build_all_nodeline_info(true);
	build_all_frontend_info(true);

	/*
	 * Read global slurm config file, override necessary values from
	 * defaults and command line.
	 */
	_read_config();

	cpu_cnt = MAX(conf->conf_cpus, conf->block_map_size);

	if ((gres_plugin_init() != SLURM_SUCCESS) ||
	    (gres_plugin_node_config_load(cpu_cnt) != SLURM_SUCCESS))
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

	/*
	 * Check for cpu frequency set capabilities on this node
	 */
	cpu_freq_init(conf);

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
#ifndef NDEBUG
	if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CORE, &rlim);
	}
#endif /* !NDEBUG */

	/*
	 * Create a context for verifying slurm job credentials
	 */
	if (!(conf->vctx = slurm_cred_verifier_ctx_create(conf->pubkey)))
		return SLURM_FAILURE;
	if (!strcmp(conf->select_type, "select/serial")) {
		/* Only cache credential for 5 seconds with select/serial
		 * for shorter cache searches and higher throughput */
		slurm_cred_ctx_set(conf->vctx, SLURM_CRED_OPT_EXPIRY_WINDOW, 5);
	}

	/*
	 * Create slurmd spool directory if necessary.
	 */
	if (_set_slurmd_spooldir() < 0) {
		error("Unable to initialize slurmd spooldir");
		return SLURM_FAILURE;
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

	/*
	 * Cache the group access list
	 */
	cf = slurm_conf_lock();
	if (cf->group_info & GROUP_CACHE)
		init_gids_cache(1);
	else
		init_gids_cache(0);
	slurm_conf_unlock();

	if ((devnull = open_cloexec("/dev/null", O_RDWR)) < 0) {
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
	uint32_t data_size = 0;
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
	data = xmalloc(sizeof(char)*data_allocated);
	while ((data_read = read(cred_fd, &data[data_size], 1024)) == 1024) {
		data_size += data_read;
		data_allocated += 1024;
		xrealloc(data, data_allocated);
	}
	data_size += data_read;
	close(cred_fd);
	buffer = create_buf(data, data_size);

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
	job_container_fini();
	acct_gather_conf_destroy();

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
	if (cred_fd > 0)
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

	if (slurm_send_only_controller_msg(&req_msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static void
_term_handler(int signum)
{
	if (signum == SIGTERM || signum == SIGINT) {
		_shutdown = 1;
		if (msg_pthread && (pthread_self() != msg_pthread))
			pthread_kill(msg_pthread, SIGTERM);
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
_usage(void)
{
	fprintf(stderr, "\
Usage: %s [OPTIONS]\n\
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
	log_options_t *o = &conf->log_opts;
	slurm_ctl_conf_t *cf;

	/* Preserve execute line verbose arguments (if any) */
	cf = slurm_conf_lock();
	if (!conf->debug_level_set && (cf->slurmd_debug != (uint16_t) NO_VAL))
		conf->debug_level = cf->slurmd_debug;
	conf->log_fmt = cf->log_fmt;
	slurm_conf_unlock();

	o->stderr_level  = conf->debug_level;
	o->logfile_level = conf->debug_level;
	o->syslog_level  = conf->debug_level;

	/*
	 * If daemonizing, turn off stderr logging -- also, if
	 * logging to a file, turn off syslog.
	 *
	 * Otherwise, if remaining in foreground, turn off logging
	 * to syslog (but keep logfile level)
	 */
	if (conf->daemonize) {
		o->stderr_level = LOG_LEVEL_QUIET;
		if (conf->logfile)
			o->syslog_level = LOG_LEVEL_FATAL;
	} else
		o->syslog_level  = LOG_LEVEL_QUIET;

	log_alter(conf->log_opts, SYSLOG_FACILITY_DAEMON, conf->logfile);
	log_set_timefmt(conf->log_fmt);
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
