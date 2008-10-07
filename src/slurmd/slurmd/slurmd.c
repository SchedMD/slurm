/*****************************************************************************\
 *  src/slurmd/slurmd/slurmd.c - main slurm node server daemon
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <dlfcn.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xsignal.h"
#include "src/common/daemonize.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/parse_spec.h"
#include "src/common/hostlist.h"
#include "src/common/macros.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/bitstring.h"
#include "src/common/stepd_api.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmd/req.h"
#include "src/slurmd/slurmd/get_mach_stat.h"
#include "src/slurmd/common/setproctitle.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/common/task_plugin.h"

#define GETOPT_ARGS	"L:Dvhcf:MN:V"

#ifndef MAXHOSTNAMELEN
#  define MAXHOSTNAMELEN	64
#endif

#define MAX_THREADS		130

/* global, copied to STDERR_FILENO in tasks before the exec */
int devnull = -1;
slurmd_conf_t * conf;
extern char *slurm_stepd_path;

/*
 * count of active threads
 */
static int             active_threads = 0;
static pthread_mutex_t active_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond    = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t fork_mutex     = PTHREAD_MUTEX_INITIALIZER;

typedef struct connection {
	slurm_fd fd;
	slurm_addr *cli_addr;
} conn_t;



/*
 * static shutdown and reconfigure flags:
 */
static sig_atomic_t _shutdown = 0;
static sig_atomic_t _reconfig = 0;
static pthread_t msg_pthread = (pthread_t) 0;

static void      _term_handler(int);
static void      _hup_handler(int);
static void      _process_cmdline(int ac, char **av);
static void      _create_msg_socket();
static void      _msg_engine();
static int       _slurmd_init();
static int       _slurmd_fini();
static void      _init_conf();
static void      _destroy_conf();
static void      _print_conf();
static void      _read_config();
static void 	 _kill_old_slurmd();
static void      _reconfigure();
static int       _restore_cred_state(slurm_cred_ctx_t ctx);
static void      _increment_thd_count();
static void      _decrement_thd_count();
static void      _wait_for_all_threads();
static int       _set_slurmd_spooldir(void);
static void      _usage();
static void      _handle_connection(slurm_fd fd, slurm_addr *client);
static void     *_service_connection(void *);
static void      _fill_registration_msg(slurm_node_registration_status_msg_t *);
static void      _update_logging(void);
static void      _atfork_prepare(void);
static void      _atfork_final(void);
static void      _install_fork_handlers(void);

int 
main (int argc, char *argv[])
{
	int i, pidfd;
	int blocked_signals[] = {SIGPIPE, 0};

	/*
	 * Make sure we have no extra open files which 
	 * would be propagated to spawned tasks.
	 */
	for (i=3; i<256; i++)
		(void) close(i);

	/*
	 * Create and set default values for the slurmd global
	 * config variable "conf"
	 */
	conf = xmalloc(sizeof(slurmd_conf_t));
	_init_conf();
	conf->argv = &argv;
	conf->argc = &argc;

	init_setproctitle(argc, argv);

	/* NOTE: conf->logfile always NULL at this point */
	log_init(argv[0], conf->log_opts, LOG_DAEMON, conf->logfile);

	xsignal(SIGTERM, &_term_handler);
	xsignal(SIGINT,  &_term_handler);
	xsignal(SIGHUP,  &_hup_handler );
	xsignal_block(blocked_signals);

	/* 
	 * Run slurmd_init() here in order to report early errors
	 * (with public keyfile)
	 */
	if (_slurmd_init() < 0) {
		error( "slurmd initialization failed" );
		fflush( NULL );
		exit(1);
	}

	debug3("slurmd initialization successful");

	/* 
	 * Become a daemon if desired.
	 * Do not chdir("/") or close all fd's
	 */
	if (conf->daemonize) 
		daemon(1,1);
	info("slurmd version %s started", SLURM_VERSION);
	debug3("finished daemonize");

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
	
	if (interconnect_node_init() < 0)
		fatal("Unable to initialize interconnect.");
	if (conf->cleanstart && switch_g_clear_node_state())
		fatal("Unable to clear interconnect state.");
	switch_g_slurmd_init();

	_create_msg_socket();

	conf->pid = getpid();
	pidfd = create_pidfile(conf->pidfile);
	if (pidfd >= 0)
		fd_set_close_on_exec(pidfd);

	info("%s started on %T", xbasename(argv[0]));

        if (send_registration_msg(SLURM_SUCCESS, true) < 0) 
		error("Unable to register with slurm controller");

	_install_fork_handlers();
	list_install_fork_handlers();
	slurm_conf_install_fork_handlers();
	
	_msg_engine();

	/*
	 * Close fd here, otherwise we'll deadlock since create_pidfile()
	 * flocks the pidfile.
	 */
	if (pidfd >= 0)			/* valid pidfd, non-error */
		(void) close(pidfd);	/* Ignore errors */
	if (unlink(conf->pidfile) < 0)
		error("Unable to remove pidfile `%s': %m", conf->pidfile);

	_wait_for_all_threads();

	interconnect_node_fini();

	_slurmd_fini();
	_destroy_conf();
	slurm_crypto_fini();	/* must be after _destroy_conf() */

	info("Slurmd shutdown completing");
	log_fini();
       	return 0;
}


static void
_msg_engine()
{
	slurm_fd sock;

	msg_pthread = pthread_self();
	slurmd_req(NULL);	/* initialize timer */
	while (!_shutdown) {
		slurm_addr *cli = xmalloc (sizeof (slurm_addr));
		if ((sock = slurm_accept_msg_conn(conf->lfd, cli)) >= 0) {
			_handle_connection(sock, cli);
			continue;
		}
		/*
		 *  Otherwise, accept() failed.
		 */
		xfree (cli);
		if (errno == EINTR) {
			if (_reconfig) {
				verbose("got reconfigure request");
				_reconfigure();
			}
			continue;
		} 
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
	if(active_threads>0)
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

static void
_wait_for_all_threads()
{
	slurm_mutex_lock(&active_mutex);
	while (active_threads > 0) {
		verbose("waiting on %d active threads", active_threads);
		pthread_cond_wait(&active_cond, &active_mutex);
	}
	slurm_mutex_unlock(&active_mutex);
	verbose("all threads complete.");
}

static void
_handle_connection(slurm_fd fd, slurm_addr *cli)
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
	if((rc = slurm_receive_msg_and_forward(con->fd, con->cli_addr, msg, 0))
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

int
send_registration_msg(uint32_t status, bool startup)
{
	int retval = SLURM_SUCCESS;
	slurm_msg_t req;
	slurm_msg_t resp;
	slurm_node_registration_status_msg_t *msg = 
		xmalloc (sizeof (slurm_node_registration_status_msg_t));
	
	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);
	
	msg->startup = (uint16_t) startup;
	_fill_registration_msg(msg);
	msg->status  = status;

	req.msg_type = MESSAGE_NODE_REGISTRATION_STATUS;
	req.data     = msg;

	if (slurm_send_recv_controller_msg(&req, &resp) < 0) {
		error("Unable to register: %m");
		retval = SLURM_FAILURE;
	} else
		slurm_free_return_code_msg(resp.data);	
	slurm_free_node_registration_status_msg (msg);

	/* XXX look at response msg
	 */

	return SLURM_SUCCESS;
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

	msg->node_name  = xstrdup (conf->node_name);
	msg->cpus	 = conf->cpus;
	msg->sockets	 = conf->sockets;
	msg->cores	 = conf->cores;
	msg->threads	 = conf->threads;
	msg->real_memory = conf->real_memory_size;
	msg->tmp_disk    = conf->tmp_disk_space;

	debug3("Procs=%u Sockets=%u Cores=%u Threads=%u Memory=%u TmpDisk=%u",
	       msg->cpus, msg->sockets, msg->cores, msg->threads,
	       msg->real_memory, msg->tmp_disk);

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

	msg->timestamp = time(NULL);

	return;
}

static inline int
_free_and_set(char **confvar, char *newval)
{
	if (newval) {
		if (*confvar)
			xfree(*confvar);
		*confvar = newval;
		return 1;
	} else
		return 0;
}

/* Replace first "%h" in path string with actual hostname.
 * Replace first "%n" in path string with NodeName.
 *
 * Make sure to call _massage_pathname AFTER conf->node_name has been
 * fully initialized.
 */
static void
_massage_pathname(char **path)
{
	if (conf->logfile == NULL)
		return;

	xstrsubstitute(*path, "%h", conf->hostname);
	xstrsubstitute(*path, "%n", conf->node_name);
}

/*
 * Read the slurm configuration file (slurm.conf) and substitute some
 * values into the slurmd configuration in preference of the defaults.
 */
static void
_read_config()
{
        char *path_pubkey = NULL;
	slurm_ctl_conf_t *cf = NULL;
	slurm_conf_reinit(conf->conffile);
	cf = slurm_conf_lock();
	
	slurm_mutex_lock(&conf->config_mutex);

	if (conf->conffile == NULL)
		conf->conffile = xstrdup(cf->slurm_conf);

	conf->slurm_user_id =  cf->slurm_user_id;

	conf->cr_type = cf->select_type_param;

	path_pubkey = xstrdup(cf->job_credential_public_certificate);

	if (!conf->logfile)
		conf->logfile = xstrdup(cf->slurmd_logfile);

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

	conf->port = slurm_conf_get_port(conf->node_name);
	slurm_conf_get_cpus_sct(conf->node_name,
				&conf->conf_cpus,  &conf->conf_sockets,
				&conf->conf_cores, &conf->conf_threads);

	/* store hardware properties in slurmd_config */
	xfree(conf->block_map);
	xfree(conf->block_map_inv);
	
	conf->block_map_size = 0;
	
	_update_logging();
	get_procs(&conf->actual_cpus);
	get_cpuinfo(conf->actual_cpus,
		    &conf->actual_sockets,
		    &conf->actual_cores,
		    &conf->actual_threads,
		    &conf->block_map_size,
		    &conf->block_map, &conf->block_map_inv);

	conf->cpus    = conf->actual_cpus;
	conf->sockets = conf->actual_sockets;
	conf->cores   = conf->actual_cores;
	conf->threads = conf->actual_threads;

	get_memory(&conf->real_memory_size);

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
	_free_and_set(&conf->task_prolog, xstrdup(cf->task_prolog));
	_free_and_set(&conf->task_epilog, xstrdup(cf->task_epilog));
	_free_and_set(&conf->pubkey,   path_pubkey);
	
	conf->propagate_prio = cf->propagate_prio_process;
	conf->job_acct_gather_freq = cf->job_acct_gather_freq;

	if ( (conf->node_name == NULL) ||
	     (conf->node_name[0] == '\0') )
		fatal("Node name lookup failure");
 
	if (cf->control_addr == NULL)
		fatal("Unable to establish controller machine");
	if (cf->slurmctld_port == 0)
		fatal("Unable to establish controller port");
	conf->use_pam = cf->use_pam;
	conf->task_plugin_param = cf->task_plugin_param;

	slurm_mutex_unlock(&conf->config_mutex);
	slurm_conf_unlock();
}

static void
_reconfigure(void)
{
	slurm_ctl_conf_t *cf;
	
	_reconfig = 0;
	_read_config();
	
	/* _update_logging(); */
	_print_conf();
	
	/*
	 * Make best effort at changing to new public key
	 */
	slurm_cred_ctx_key_update(conf->vctx, conf->pubkey);

	/*
	 * Reinitialize the groups cache
	 */
	cf = slurm_conf_lock();
	init_gids_cache(cf->cache_groups);
	slurm_conf_unlock();

	/*
	 * XXX: reopen slurmd port?
	 */
}

static void
_print_conf()
{
	slurm_ctl_conf_t *cf;
	char *str;
	int i;

	cf = slurm_conf_lock();
	debug3("CacheGroups = %d",       cf->cache_groups);
	debug3("Confile     = `%s'",     conf->conffile);
	debug3("Debug       = %d",       cf->slurmd_debug);
	debug3("CPUs        = %-2u (CF: %2u, HW: %2u)",
	       conf->cpus,
	       conf->conf_cpus,
	       conf->actual_cpus);
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
	debug3("Port        = %u",       conf->port);
	debug3("Prolog      = `%s'",     conf->prolog);
	debug3("TmpFS       = `%s'",     conf->tmpfs);
	debug3("Public Cert = `%s'",     conf->pubkey);
	debug3("Spool Dir   = `%s'",     conf->spooldir);
	debug3("Pid File    = `%s'",     conf->pidfile);
	debug3("Slurm UID   = %u",       conf->slurm_user_id);
	debug3("TaskProlog  = `%s'",     conf->task_prolog);
	debug3("TaskEpilog  = `%s'",     conf->task_epilog);
	debug3("TaskPluginParam = %u",   conf->task_plugin_param);
	debug3("Use PAM     = %u",       conf->use_pam);
	slurm_conf_unlock();
}

static void
_init_conf()
{
	char  host[MAXHOSTNAMELEN];
	log_options_t lopts = LOG_OPTS_INITIALIZER;

	if (gethostname_short(host, MAXHOSTNAMELEN) < 0) {
		error("Unable to get my hostname: %m");
		exit(1);
	}
	conf->hostname    = xstrdup(host);
	conf->node_name   = NULL;
	conf->sockets     = 0;
	conf->cores       = 0;
	conf->threads     = 0;
	conf->block_map_size = 0;
	conf->block_map   = NULL;
	conf->block_map_inv = NULL;
	conf->conffile    = NULL;
	conf->epilog      = NULL;
	conf->health_check_program = NULL;
	conf->logfile     = NULL;
	conf->pubkey      = NULL;
	conf->prolog      = NULL;
	conf->task_prolog = NULL;
	conf->task_epilog = NULL;

	conf->port        =  0;
	conf->daemonize   =  1;
	conf->lfd         = -1;
	conf->cleanstart  =  0;
	conf->mlock_pages =  0;
	conf->log_opts    = lopts;
	conf->debug_level = LOG_LEVEL_INFO;
	conf->pidfile     = xstrdup(DEFAULT_SLURMD_PIDFILE);
	conf->spooldir	  = xstrdup(DEFAULT_SPOOLDIR);
	conf->use_pam	  =  0;
	conf->task_plugin_param = 0;

	slurm_mutex_init(&conf->config_mutex);
	return;
}

static void
_destroy_conf()
{
	if(conf) {
		xfree(conf->block_map);
		xfree(conf->block_map_inv);
		xfree(conf->health_check_program);
		xfree(conf->hostname);
		xfree(conf->node_name);
		xfree(conf->conffile);
		xfree(conf->prolog);
		xfree(conf->epilog);
		xfree(conf->logfile);
		xfree(conf->pubkey);
		xfree(conf->task_prolog);
		xfree(conf->task_epilog);
		xfree(conf->pidfile);
		xfree(conf->spooldir);
		xfree(conf->tmpfs);
		slurm_mutex_destroy(&conf->config_mutex);
		slurm_cred_ctx_destroy(conf->vctx);
		xfree(conf);
	}
	return;
}

static void
_process_cmdline(int ac, char **av)
{
	int c;

	conf->prog = xbasename(av[0]);

	while ((c = getopt(ac, av, GETOPT_ARGS)) > 0) {
		switch (c) {
		case 'D': 
			conf->daemonize = 0;
			break;
		case 'v':
			conf->debug_level++;
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
		case 'c':
			conf->cleanstart = 1;
			break;
		case 'M':
			conf->mlock_pages = 1;
			break;
		case 'N':
			conf->node_name = xstrdup(optarg);
			break;
		case 'V':
			printf("%s %s\n", PACKAGE, SLURM_VERSION);
			exit(0);
			break;
		default:
			_usage(c);
			exit(1);
			break;
		}
	}
}


static void
_create_msg_socket()
{
	slurm_fd ld = slurm_init_msg_engine_port(conf->port);

	if (ld < 0) {
		error("Unable to bind listen port (%d): %m", conf->port);
		exit(1);
	}

	fd_set_close_on_exec(ld);

	conf->lfd = ld;

	debug3("succesfully opened slurm listen port %d", conf->port);

	return;
}


static int
_slurmd_init()
{
	struct rlimit rlim;
	slurm_ctl_conf_t *cf;
	struct stat stat_buf;

	/*
	 * Process commandline arguments first, since one option may be
	 * an alternate location for the slurm config file.
	 */
	_process_cmdline(*conf->argc, *conf->argv);

	/*
	 * Read global slurm config file, ovverride necessary values from
	 * defaults and command line.
	 *
	 */
	_read_config();

	/* 
	 * Update location of log messages (syslog, stderr, logfile, etc.),
	 * print current configuration (if in debug mode), and 
	 * load appropriate plugin(s).
	 */
	/* _update_logging(); */
	_print_conf();
	if (slurm_proctrack_init() != SLURM_SUCCESS)
		return SLURM_FAILURE;
	if (slurmd_task_init() != SLURM_SUCCESS)
		return SLURM_FAILURE;
	if (slurm_auth_init(NULL) != SLURM_SUCCESS)
		return SLURM_FAILURE;

	if (getrlimit(RLIMIT_NOFILE,&rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_NOFILE,&rlim);
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
	}

	if (conf->daemonize && (chdir("/tmp") < 0)) {
		error("Unable to chdir to /tmp");
		return SLURM_FAILURE;
	}

	/*
	 * Cache the group access list
	 */
	cf = slurm_conf_lock();
	init_gids_cache(cf->cache_groups);
	slurm_conf_unlock();

	if ((devnull = open("/dev/null", O_RDWR)) < 0) {
		error("Unable to open /dev/null: %m");
		return SLURM_FAILURE;
	}
	fd_set_close_on_exec(devnull);

	/* make sure we have slurmstepd installed */
	if (stat(slurm_stepd_path, &stat_buf)) {
		fatal("Unable to find slurmstepd file at %s",
			slurm_stepd_path);
	}
	if (!S_ISREG(stat_buf.st_mode)) {
		fatal("slurmstepd not a file at %s", 
			slurm_stepd_path);
	}

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
 * > valgrind --tool=memcheck --leak-check=yes --num-callers=6
 *    --leak-resolution=med slurmd -D
 *
 * Then exercise the slurmd functionality before executing
 * > scontrol shutdown
 *
 * There should be some definitely lost records from 
 * init_setproctitle (setproctitle.c), but it should otherwise account 
 * for all memory.
\**************************************************************************/
static int
_slurmd_fini()
{
	save_cred_state(conf->vctx);
	int slurm_proctrack_init();
	switch_fini();
	slurmd_task_fini(); 
	slurm_conf_destroy();
	slurm_proctrack_fini();
	slurm_auth_fini();
	slurmd_req(NULL);	/* purge memory allocated by slurmd_req() */
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
	int cred_fd = -1, error_code = SLURM_SUCCESS;
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
		error_code = errno;
		goto cleanup;
	}
	buffer = init_buf(1024);
	slurm_cred_ctx_pack(ctx, buffer);
	if (write(cred_fd, get_buf_data(buffer), 
		  get_buf_offset(buffer)) != get_buf_offset(buffer)) {
		error("write %s error %m", new_file);
		(void) unlink(new_file);
		error_code = errno;
		goto cleanup;
	}
	(void) unlink(old_file);
	(void) link(reg_file, old_file);
	(void) unlink(reg_file);
	(void) link(new_file, reg_file);
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
_usage()
{
	fprintf(stderr, "\
Usage: %s [OPTIONS]\n\
   -c          Force cleanup of slurmd shared memory.\n\
   -D          Run daemon in foreground.\n\
   -M          Use mlock() to lock slurmd pages into memory.\n\
   -h          Print this help message.\n\
   -f config   Read configuration from the specified file.\n\
   -L logfile  Log messages to the file `logfile'.\n\
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
		if (fd_get_readw_lock(fd) < 0) 
			fatal ("unable to wait for readw lock: %m");
		(void) close(fd); /* Ignore errors */ 
	}
}

/* Reset slurmctld logging based upon configuration parameters */
static void _update_logging(void) 
{
	log_options_t *o = &conf->log_opts;
	slurm_ctl_conf_t *cf;

	/* 
	 * Initialize debug level if not already set
	 */
	cf = slurm_conf_lock();
	if ( (conf->debug_level == LOG_LEVEL_INFO)
	     && (cf->slurmd_debug != (uint16_t) NO_VAL) )
		conf->debug_level = cf->slurmd_debug; 
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
			o->syslog_level = LOG_LEVEL_QUIET;
	} else 
		o->syslog_level  = LOG_LEVEL_QUIET;

	log_alter(conf->log_opts, SYSLOG_FACILITY_DAEMON, conf->logfile);
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

