/*****************************************************************************\
 *  src/slurmd/slurmstepd/slurmstepd.c - Slurm job-step manager.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  and Christopher Morrone <morrone2@llnl.gov>.
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

#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>

#include "src/common/assoc_mgr.h"
#include "src/common/cpu_frequency.h"
#include "src/common/forward.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/port_mgr.h"
#include "src/common/run_command.h"
#include "src/common/setproctitle.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/spank.h"
#include "src/common/stepd_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/acct_gather_energy.h"
#include "src/interfaces/acct_gather_profile.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/gpu.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/hash.h"
#include "src/interfaces/job_container.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/mpi.h"
#include "src/interfaces/prep.h"
#include "src/interfaces/proctrack.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/task.h"
#include "src/interfaces/topology.h"

#include "src/slurmd/common/privileges.h"
#include "src/slurmd/common/set_oomadj.h"
#include "src/slurmd/common/slurmstepd_init.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/container.h"
#include "src/slurmd/slurmstepd/mgr.h"
#include "src/slurmd/slurmstepd/req.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "src/stepmgr/stepmgr.h"

static int _init_from_slurmd(int sock, char **argv, slurm_addr_t **_cli,
			    slurm_msg_t **_msg);

static void _send_ok_to_slurmd(int sock);
static void _send_fail_to_slurmd(int sock, int rc);
static void _got_ack_from_slurmd(int);
static stepd_step_rec_t *_step_setup(slurm_addr_t *cli, slurm_msg_t *msg);
#ifdef MEMORY_LEAK_DEBUG
static void _step_cleanup(stepd_step_rec_t *step, slurm_msg_t *msg, int rc);
#endif
static void _process_cmdline(int argc, char **argv);

static pthread_mutex_t cleanup_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool cleanup = false;

/* global variable */
slurmd_conf_t * conf;
extern char  ** environ;

list_t *job_list = NULL;
job_record_t *job_step_ptr = NULL;
list_t *job_node_array = NULL;
time_t last_job_update = 0;
bool time_limit_thread_shutdown = false;
pthread_t time_limit_thread_id = 0;

/* See _send_msg_maybe() in src/slurmctld/agent.c */
static void _send_msg_maybe(slurm_msg_t *req)
{
	int fd = -1;

	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		log_flag(NET, "%s: slurm_open_msg_conn(%pA): %m",
			 __func__, &req->address);
		return;
	}

	(void) slurm_send_node_msg(fd, req);

	(void) close(fd);
}

static int _foreach_ret_data_info(void *x, void *arg)
{
	int rc;
	ret_data_info_t *ret_data_info = x;

	if ((rc = slurm_get_return_code(ret_data_info->type,
					ret_data_info->data))) {
		error("stepmgr failed to send message %s: rc=%d(%s)",
		      rpc_num2string(ret_data_info->type), rc,
		      slurm_strerror(rc));
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void *_rpc_thread(void *data)
{
	bool srun_agent = false;
	agent_arg_t *agent_arg_ptr = data;
	slurm_msg_t msg;
	slurm_msg_t_init(&msg);

	msg.data = agent_arg_ptr->msg_args;
	msg.flags = agent_arg_ptr->msg_flags;
	msg.msg_type = agent_arg_ptr->msg_type;
	msg.protocol_version = agent_arg_ptr->protocol_version;

	slurm_msg_set_r_uid(&msg, agent_arg_ptr->r_uid);

	srun_agent = ((msg.msg_type == SRUN_PING) ||
		      (msg.msg_type == SRUN_JOB_COMPLETE) ||
		      (msg.msg_type == SRUN_STEP_MISSING) ||
		      (msg.msg_type == SRUN_STEP_SIGNAL) ||
		      (msg.msg_type == SRUN_TIMEOUT) ||
		      (msg.msg_type == SRUN_USER_MSG) ||
		      (msg.msg_type == RESPONSE_RESOURCE_ALLOCATION) ||
		      (msg.msg_type == SRUN_NODE_FAIL));

	if (agent_arg_ptr->addr) {
		msg.address = *agent_arg_ptr->addr;

		if (msg.msg_type == SRUN_JOB_COMPLETE) {
			_send_msg_maybe(&msg);
		} else if (slurm_send_only_node_msg(&msg) && !srun_agent) {
			error("failed to send message type %d/%s",
			      msg.msg_type, rpc_num2string(msg.msg_type));
		}
	} else {
		list_t *ret_list = NULL;
		if (!(ret_list = start_msg_tree(agent_arg_ptr->hostlist,
						&msg, 0))) {
			error("%s: no ret_list given", __func__);
		} else {
			list_for_each(ret_list, _foreach_ret_data_info, NULL);
			FREE_NULL_LIST(ret_list);
		}
	}

	purge_agent_args(agent_arg_ptr);

	return NULL;
}

static void _agent_queue_request(agent_arg_t *agent_arg_ptr)
{
	slurm_thread_create_detached(_rpc_thread, agent_arg_ptr);
}

extern job_record_t *find_job_record(uint32_t job_id)
{
	xassert(job_step_ptr);

	return job_step_ptr;
}

static void *_step_time_limit_thread(void *data)
{
	time_t now;

	xassert(job_step_ptr);

	while (!time_limit_thread_shutdown) {
		now = time(NULL);
		slurm_mutex_lock(&stepmgr_mutex);
		list_for_each(job_step_ptr->step_list,
			      check_job_step_time_limit, &now);
		slurm_mutex_unlock(&stepmgr_mutex);
		sleep(1);
	}

	return NULL;
}

stepmgr_ops_t stepd_stepmgr_ops = {
	.find_job_record = find_job_record,
	.last_job_update = &last_job_update,
	.agent_queue_request = _agent_queue_request
};

static int _foreach_job_node_array(void *x, void *arg)
{
	node_record_t *job_node_ptr = x;
	int *table_index = arg;

	config_record_t *config_ptr =
		config_record_from_node_record(job_node_ptr);

	*table_index = bit_ffs_from_bit(job_step_ptr->node_bitmap, *table_index);

	job_node_ptr->config_ptr = config_ptr;
	insert_node_record_at(job_node_ptr, *table_index);

	(*table_index)++;

	job_node_ptr->tot_cores =
		job_node_ptr->tot_sockets * job_node_ptr->cores;
	/*
	 * Sanity check to make sure we can take a version we
	 * actually understand.
	 */
	if (job_node_ptr->protocol_version < SLURM_MIN_PROTOCOL_VERSION)
		job_node_ptr->protocol_version = SLURM_MIN_PROTOCOL_VERSION;

	return SLURM_SUCCESS;
}

static void _setup_stepmgr_nodes(void)
{
	int table_index = 0;
	init_node_conf();

	xassert(job_node_array);
	/*
	 * next_node_bitmap() asserts
	 * bit_size(node_bitmap) == node_record_count
	 */
	node_record_count = bit_size(job_step_ptr->node_bitmap);
	grow_node_record_table_ptr();
	list_for_each(job_node_array, _foreach_job_node_array, &table_index);
}

static void _init_stepd_stepmgr(void)
{
	if (!job_step_ptr)
		return;

	stepd_stepmgr_ops.up_node_bitmap =
		bit_alloc(bit_size(job_step_ptr->node_bitmap));
	bit_set_all(stepd_stepmgr_ops.up_node_bitmap);
	stepmgr_init(&stepd_stepmgr_ops);
	reserve_port_stepmgr_init(job_step_ptr);

	_setup_stepmgr_nodes();
	node_features_build_active_list(job_step_ptr);

	if (!xstrcasecmp(slurm_conf.accounting_storage_type,
			 "accounting_storage/slurmdbd")) {
		xfree(slurm_conf.accounting_storage_type);
		slurm_conf.accounting_storage_type =
			xstrdup("accounting_storage/ctld_relay");
		acct_storage_g_init();
	} else {
			acct_storage_g_init();
	}

	slurm_thread_create(&time_limit_thread_id, _step_time_limit_thread,
			    NULL);
}

static void _on_sigalrm(conmgr_callback_args_t conmgr_args, void *arg)
{
	debug("Caught SIGALRM. Ignoring.");
}

static void _on_sigint(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGINT. Shutting down.");
	conmgr_request_shutdown();
}

static void _on_sigterm(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGTERM. Shutting down.");
	conmgr_request_shutdown();
}

static void _on_sigquit(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGQUIT. Shutting down.");
	conmgr_request_shutdown();
}

static void _on_sigtstp(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGTSTP. Ignoring");
}

static void _on_sighup(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGHUP. Ignoring");
}

static void _on_sigusr1(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGUSR1. Ignoring.");
}

static void _on_sigusr2(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGUSR2. Ignoring.");
}

static void _on_sigpipe(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGPIPE. Ignoring.");
}

static void _on_sigttin(conmgr_callback_args_t conmgr_args, void *arg)
{
	debug("Caught SIGTTIN. Ignoring.");
}

extern int main(int argc, char **argv)
{
	log_options_t lopts = LOG_OPTS_INITIALIZER;
	slurm_addr_t *cli;
	slurm_msg_t *msg;
	stepd_step_rec_t *step;
	int rc = SLURM_SUCCESS;
	bool only_mem = true;

	_process_cmdline(argc, argv);

	conf = xmalloc(sizeof(*conf));
	conf->argv = argv;
	conf->argc = argc;
	init_setproctitle(argc, argv);

	log_init(argv[0], lopts, LOG_DAEMON, NULL);

	/* Receive job parameters from the slurmd */
	_init_from_slurmd(STDIN_FILENO, argv, &cli, &msg);

	conmgr_init(0, 0, (conmgr_callbacks_t) {0});

	conmgr_add_work_signal(SIGALRM, _on_sigalrm, NULL);
	conmgr_add_work_signal(SIGINT, _on_sigint, NULL);
	conmgr_add_work_signal(SIGTERM, _on_sigterm, NULL);
	conmgr_add_work_signal(SIGQUIT, _on_sigquit, NULL);
	conmgr_add_work_signal(SIGTSTP, _on_sigtstp, NULL);
	conmgr_add_work_signal(SIGHUP, _on_sighup, NULL);
	conmgr_add_work_signal(SIGUSR1, _on_sigusr1, NULL);
	conmgr_add_work_signal(SIGUSR2, _on_sigusr2, NULL);
	conmgr_add_work_signal(SIGPIPE, _on_sigpipe, NULL);
	conmgr_add_work_signal(SIGTTIN, _on_sigttin, NULL);

	conmgr_run(false);

	if ((run_command_init(argc, argv, conf->stepd_loc) != SLURM_SUCCESS) &&
	    conf->stepd_loc && conf->stepd_loc[0])
		fatal("%s: Unable to reliably execute %s",
		      __func__, conf->stepd_loc);

	/* Create the stepd_step_rec_t, mostly from info in a
	 * launch_tasks_request_msg_t or a batch_job_launch_msg_t */
	if (!(step = _step_setup(cli, msg))) {
		rc = SLURM_ERROR;
		_send_fail_to_slurmd(STDOUT_FILENO, rc);
		goto ending;
	}

	_init_stepd_stepmgr();

	/* fork handlers cause mutexes on some global data structures
	 * to be re-initialized after the fork. */
	slurm_conf_install_fork_handlers();

	/* sets step->msg_handle and step->msgid */
	if (msg_thr_create(step) == SLURM_ERROR) {
		rc = SLURM_ERROR;
		_send_fail_to_slurmd(STDOUT_FILENO, rc);
		goto ending;
	}

	if (step->step_id.step_id != SLURM_EXTERN_CONT)
		close_slurmd_conn(rc);

	/* slurmstepd is the only daemon that should survive upgrade. If it
	 * had been swapped out before upgrade happened it could easily lead
	 * to SIGBUS at any time after upgrade. Avoid that by locking it
	 * in-memory. */
	if (xstrstr(slurm_conf.launch_params, "slurmstepd_memlock")) {
		int flags = MCL_CURRENT;
		if (xstrstr(slurm_conf.launch_params, "slurmstepd_memlock_all"))
			flags |= MCL_FUTURE;
		if (mlockall(flags) < 0)
			info("failed to mlock() slurmstepd pages: %m");
		else
			debug("slurmstepd locked in memory");
	}

	acct_gather_energy_g_set_data(ENERGY_DATA_STEP_PTR, step);

	/* This does most of the stdio setup, then launches all the tasks,
	 * and blocks until the step is complete */
	rc = job_manager(step);

	only_mem = false;
ending:
	rc = stepd_cleanup(msg, step, cli, rc, only_mem);

	conmgr_fini();
	return rc;
}

extern int stepd_cleanup(slurm_msg_t *msg, stepd_step_rec_t *step,
			 slurm_addr_t *cli, int rc, bool only_mem)
{
	time_limit_thread_shutdown = true;

	slurm_mutex_lock(&cleanup_mutex);

	if (cleanup)
		goto done;

	if (!step) {
		error("%s: step is NULL, skipping cleanup", __func__);
		goto done;
	}

	if (!only_mem) {
		if (step->batch)
			batch_finish(step, rc); /* sends batch complete message */

		/* signal the message thread to shutdown, and wait for it */
		if (step->msg_handle)
			eio_signal_shutdown(step->msg_handle);
		slurm_thread_join(step->msgid);
	}

	mpi_fini();

	/*
	 * This call is only done once per step since stepd_cleanup is protected
	 * agains multiple and concurrent calls.
	 */
	proctrack_g_destroy(step->cont_id);

	if (conf->hwloc_xml)
		(void)remove(conf->hwloc_xml);

	if (step->container)
		cleanup_container(step);

	if (step->step_id.step_id == SLURM_EXTERN_CONT) {
		if (container_g_stepd_delete(step->step_id.job_id))
			error("container_g_stepd_delete(%u): %m",
			      step->step_id.job_id);
	}

	run_command_shutdown();

	/*
	 * join() must be done before _step_cleanup() where job_step_ptr is
	 * freed.
	 */
	slurm_thread_join(time_limit_thread_id);

#ifdef MEMORY_LEAK_DEBUG
	acct_gather_conf_destroy();
	acct_storage_g_fini();

	if (job_step_ptr) {
		xfree(job_step_ptr->resv_ports);
		reserve_port_stepmgr_init(job_step_ptr);
		node_features_free_lists();
	}

	_step_cleanup(step, msg, rc);

	fini_setproctitle();

	cgroup_conf_destroy();

	xfree(cli);
	xfree(conf->block_map);
	xfree(conf->block_map_inv);
	xfree(conf->conffile);
	xfree(conf->hostname);
	xfree(conf->hwloc_xml);
	xfree(conf->logfile);
	xfree(conf->node_name);
	xfree(conf->node_topo_addr);
	xfree(conf->node_topo_pattern);
	xfree(conf->spooldir);
	xfree(conf->stepd_loc);
	xfree(conf->cpu_spec_list);
	xfree(conf);
#endif
	cleanup = true;
done:
	slurm_mutex_unlock(&cleanup_mutex);
	/* skipping lock of step_complete.lock */
	if (rc || step_complete.step_rc) {
		info("%s: done with step (rc[0x%x]:%s, cleanup_rc[0x%x]:%s)",
		     __func__, step_complete.step_rc,
		     slurm_strerror(step_complete.step_rc), rc,
		     slurm_strerror(rc));
	} else {
		info("done with step");
	}

	conmgr_request_shutdown();

	return rc;
}

extern void close_slurmd_conn(int rc)
{
	debug("%s: sending %d: %s", __func__, rc, slurm_strerror(rc));

	if (rc)
		_send_fail_to_slurmd(STDOUT_FILENO, rc);
	else
		_send_ok_to_slurmd(STDOUT_FILENO);

	_got_ack_from_slurmd(STDIN_FILENO);

	/* Fancy way of closing stdin that keeps STDIN_FILENO from being
	 * allocated to any random file.  The slurmd already opened /dev/null
	 * on STDERR_FILENO for us. */
	dup2(STDERR_FILENO, STDIN_FILENO);

	/* Fancy way of closing stdout that keeps STDOUT_FILENO from being
	 * allocated to any random file.  The slurmd already opened /dev/null
	 * on STDERR_FILENO for us. */
	dup2(STDERR_FILENO, STDOUT_FILENO);
}

static slurmd_conf_t *_read_slurmd_conf_lite(int fd)
{
	int rc;
	int len;
	buf_t *buffer = NULL;
	slurmd_conf_t *confl, *local_conf = NULL;
	int tmp_int = 0;
	list_t *tmp_list = NULL;
	assoc_mgr_lock_t locks = { .tres = WRITE_LOCK };

	/*  First check to see if we've already initialized the
	 *   global slurmd_conf_t in 'conf'. Allocate memory if not.
	 */
	if (conf) {
		confl = conf;
	} else {
		local_conf = xmalloc(sizeof(slurmd_conf_t));
		confl = local_conf;
	}

	safe_read(fd, &len, sizeof(int));

	buffer = init_buf(len);
	safe_read(fd, buffer->head, len);

	rc = unpack_slurmd_conf_lite_no_alloc(confl, buffer);
	if (rc == SLURM_ERROR)
		fatal("slurmstepd: problem with unpack of slurmd_conf");

	rc = unpack_slurm_conf_lite_no_alloc(buffer);
	if (rc == SLURM_ERROR)
		fatal("slurmstepd: problem with unpack of slurm_conf");
	slurm_conf_init_stepd();

	if (slurm_unpack_list(&tmp_list,
			      slurmdb_unpack_tres_rec,
			      slurmdb_destroy_tres_rec,
			      buffer, SLURM_PROTOCOL_VERSION)
	    != SLURM_SUCCESS)
		fatal("slurmstepd: problem with unpack of tres list");

	FREE_NULL_BUFFER(buffer);

	confl->log_opts.prefix_level = 1;
	confl->log_opts.logfile_level = confl->debug_level;

	if (confl->daemonize)
		confl->log_opts.stderr_level = LOG_LEVEL_QUIET;
	else
		confl->log_opts.stderr_level = confl->debug_level;

	if (confl->syslog_debug != LOG_LEVEL_END) {
		confl->log_opts.syslog_level = confl->syslog_debug;
	} else if (!confl->daemonize) {
		confl->log_opts.syslog_level = LOG_LEVEL_QUIET;
	} else if ((confl->debug_level > LOG_LEVEL_QUIET) && !confl->logfile) {
		confl->log_opts.syslog_level = confl->debug_level;
	} else
		confl->log_opts.syslog_level = LOG_LEVEL_FATAL;

	/*
	 * LOGGING BEFORE THIS WILL NOT WORK!  Only afterwards will it show
	 * up in the log.
	 */
	log_alter(confl->log_opts, SYSLOG_FACILITY_DAEMON, confl->logfile);
	log_set_timefmt(slurm_conf.log_fmt);
	debug2("debug level read from slurmd is '%s'.",
		log_num2string(confl->debug_level));

	confl->acct_freq_task = NO_VAL16;
	tmp_int = acct_gather_parse_freq(PROFILE_TASK,
					 slurm_conf.job_acct_gather_freq);
	if (tmp_int != -1)
		confl->acct_freq_task = tmp_int;

	xassert(tmp_list);

	assoc_mgr_lock(&locks);
	assoc_mgr_post_tres_list(tmp_list);
	debug2("%s: slurmd sent %u TRES.", __func__, g_tres_count);
	/* assoc_mgr_post_tres_list destroys tmp_list */
	tmp_list = NULL;
	assoc_mgr_unlock(&locks);

	return (confl);

rwfail:
	FREE_NULL_BUFFER(buffer);
	xfree(local_conf);
	return (NULL);
}

static int _get_jobid_uid_gid_from_env(uint32_t *jobid, uid_t *uid, gid_t *gid)
{
	const char *val;
	char *p;

	if (!(val = getenv("SLURM_JOBID")))
		return error("Unable to get SLURM_JOBID in env!");

	*jobid = (uint32_t) strtoul(val, &p, 10);
	if (*p != '\0')
		return error("Invalid SLURM_JOBID=%s", val);

	if (!(val = getenv("SLURM_UID")))
		return error("Unable to get SLURM_UID in env!");

	*uid = (uid_t) strtoul(val, &p, 10);
	if (*p != '\0')
		return error("Invalid SLURM_UID=%s", val);

	if (!(val = getenv("SLURM_JOB_GID")))
		return error("Unable to get SLURM_JOB_GID in env!");

	*gid = (gid_t) strtoul(val, &p, 10);
	if (*p != '\0')
		return error("Invalid SLURM_JOB_GID=%s", val);

	return SLURM_SUCCESS;
}

static int _handle_spank_mode(int argc, char **argv)
{
	char *prefix = NULL;
	const char *mode = argv[2];
	uid_t uid = (uid_t) -1;
	gid_t gid = (gid_t) -1;
	uint32_t jobid = (uint32_t) -1;
	log_options_t lopts = LOG_OPTS_INITIALIZER;

	/*
	 *  Not necessary to log to syslog
	 */
	lopts.syslog_level = LOG_LEVEL_QUIET;

	/*
	 *  Make our log prefix into spank-prolog: or spank-epilog:
	 */
	xstrfmtcat(prefix, "spank-%s", mode);
	log_init(prefix, lopts, LOG_DAEMON, NULL);
	xfree(prefix);

	/*
	 *  When we are started from slurmd, a lightweight config is
	 *   sent over the stdin fd. If we are able to read this conf
	 *   use it to reinitialize the log.
	 *  It is not a fatal error if we fail to read the conf file.
	 *   This could happen if slurmstepd is run standalone for
	 *   testing.
	 */
	conf = _read_slurmd_conf_lite(STDIN_FILENO);
	close(STDIN_FILENO);

	if (_get_jobid_uid_gid_from_env(&jobid, &uid, &gid))
		return error("spank environment invalid");

	debug("Running spank/%s for jobid [%u] uid [%u] gid [%u]",
	      mode, jobid, uid, gid);

	if (!xstrcmp(mode, "prolog")) {
		if (spank_job_prolog(jobid, uid, gid) < 0)
			return -1;
	} else if (!xstrcmp(mode, "epilog")) {
		if (spank_job_epilog(jobid, uid, gid) < 0)
			return -1;
	} else {
		error("Invalid mode %s specified!", mode);
		return -1;
	}

	return 0;
}

/*
 *  Process special "modes" of slurmstepd passed as cmdline arguments.
 */
static void _process_cmdline(int argc, char **argv)
{
	if ((argc == 2) && !xstrcmp(argv[1], "getenv")) {
		print_rlimits();
		for (int i = 0; environ[i]; i++)
			printf("%s\n", environ[i]);
		exit(0);
	}
	if ((argc == 2) && !xstrcmp(argv[1], "infinity")) {
		set_oom_adj(-1000);
		(void) poll(NULL, 0, -1);
		exit(0);
	}
	if ((argc == 3) && !xstrcmp(argv[1], "spank")) {
		if (_handle_spank_mode(argc, argv) < 0)
			exit(1);
		exit(0);
	}
	if (run_command_is_launcher(argc, argv)) {
		run_command_launcher(argc, argv);
		_exit(127); /* Should not get here */
	}
}

static void
_send_ok_to_slurmd(int sock)
{
	/*
	 * If running under memcheck, this pipe doesn't work correctly so just
	 * skip it.
	 */
#if (SLURMSTEPD_MEMCHECK != 1)
	int ok = SLURM_SUCCESS;
	safe_write(sock, &ok, sizeof(int));
	return;
rwfail:
	error("Unable to send \"ok\" to slurmd");
#endif
}

static void _send_fail_to_slurmd(int sock, int rc)
{
	/*
	 * If running under memcheck, this pipe doesn't work correctly so just
	 * skip it.
	 */
#if (SLURMSTEPD_MEMCHECK != 1)
	safe_write(sock, &rc, sizeof(int));
	return;
rwfail:
	error("Unable to send \"fail\" to slurmd");
#endif
}

static void
_got_ack_from_slurmd(int sock)
{
	/*
	 * If running under memcheck, this pipe doesn't work correctly so just
	 * skip it.
	 */
#if (SLURMSTEPD_MEMCHECK != 1)
	int ok;
	safe_read(sock, &ok, sizeof(int));
	return;
rwfail:
	error("Unable to receive \"ok ack\" to slurmd");
#endif
}

static void _set_job_log_prefix(slurm_step_id_t *step_id)
{
	char *buf;
	char tmp_char[64];

	log_build_step_id_str(step_id, tmp_char, sizeof(tmp_char),
			      STEP_ID_FLAG_NO_PREFIX);
	buf = xstrdup_printf("[%s%s]",
			     tmp_char,
			     job_step_ptr ? " stepmgr" : "");

	setproctitle("%s", buf);
	/* note: will claim ownership of buf, do not free */
	xstrcat(buf, " ");
	log_set_prefix(&buf);
}

/*
 *  This function handles the initialization information from slurmd
 *  sent by _send_slurmstepd_init() in src/slurmd/slurmd/req.c.
 */
static int
_init_from_slurmd(int sock, char **argv, slurm_addr_t **_cli,
		  slurm_msg_t **_msg)
{
	char *incoming_buffer = NULL;
	buf_t *buffer;
	int step_type;
	int len;
	uint16_t proto;
	slurm_addr_t *cli = NULL;
	slurm_msg_t *msg = NULL;
	slurm_step_id_t step_id = {
		.job_id = 0,
		.step_id = NO_VAL,
		.step_het_comp = NO_VAL,
	};

	/* receive conf from slurmd */
	if (!(conf = _read_slurmd_conf_lite(sock)))
		fatal("Failed to read conf from slurmd");

	/*
	 * Init select plugin after reading slurm.conf and before receiving step
	 */
	select_g_init(false);

	slurm_conf.slurmd_port = conf->port;
	slurm_conf.slurmd_syslog_debug = conf->syslog_debug;
	/*
	 * max_node_cnt is not sent over from slurmd and will be 0 unless we set
	 * it here to be consistent with the way it's used elsewhere.
	 */
	slurm_conf.max_node_cnt = NO_VAL;

	setenvf(NULL, "SLURMD_NODENAME", "%s", conf->node_name);

	/* receive conf_hashtbl from slurmd */
	read_conf_recv_stepd(sock);

	/* receive job type from slurmd */
	safe_read(sock, &step_type, sizeof(int));
	debug3("step_type = %d", step_type);

	/* receive reverse-tree info from slurmd */
	slurm_mutex_lock(&step_complete.lock);
	safe_read(sock, &step_complete.rank, sizeof(int));
	safe_read(sock, &step_complete.parent_rank, sizeof(int));
	safe_read(sock, &step_complete.children, sizeof(int));
	safe_read(sock, &step_complete.depth, sizeof(int));
	safe_read(sock, &step_complete.max_depth, sizeof(int));
	safe_read(sock, &len, sizeof(int));
	if (len) {
		step_complete.parent_name = xmalloc(len + 1);
		safe_read(sock, step_complete.parent_name, len);
	}

	if (step_complete.children)
		step_complete.bits = bit_alloc(step_complete.children);
	step_complete.jobacct = jobacctinfo_create(NULL);
	slurm_mutex_unlock(&step_complete.lock);

	debug3("slurmstepd rank %d, parent = %s",
	       step_complete.rank, step_complete.parent_name);

	/* receive cli from slurmd */
	safe_read(sock, &len, sizeof(int));
	incoming_buffer = xmalloc(len);
	safe_read(sock, incoming_buffer, len);
	buffer = create_buf(incoming_buffer,len);
	cli = xmalloc(sizeof(slurm_addr_t));
	if (slurm_unpack_addr_no_alloc(cli, buffer) == SLURM_ERROR)
		fatal("slurmstepd: problem with unpack of slurmd_conf");
	FREE_NULL_BUFFER(buffer);

	/* Grab the slurmd's spooldir. Has %n expanded. */
	cpu_freq_init(conf);

	/* Receive cpu_frequency info from slurmd */
	cpu_freq_recv_info(sock);

	/* get the protocol version of the srun */
	safe_read(sock, &proto, sizeof(uint16_t));

	/* receive req from slurmd */
	safe_read(sock, &len, sizeof(int));
	incoming_buffer = xmalloc(len);
	safe_read(sock, incoming_buffer, len);
	buffer = create_buf(incoming_buffer,len);

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);
	/* Always unpack as the current version. */
	msg->protocol_version = SLURM_PROTOCOL_VERSION;

	switch (step_type) {
	case LAUNCH_BATCH_JOB:
		msg->msg_type = REQUEST_BATCH_JOB_LAUNCH;
		break;
	case LAUNCH_TASKS:
		msg->msg_type = REQUEST_LAUNCH_TASKS;
		break;
	default:
		fatal("%s: Unrecognized launch RPC (%d)", __func__, step_type);
		break;
	}

	/* Init switch before unpack_msg to only init the default */
	if (switch_g_init(true) != SLURM_SUCCESS)
		fatal("failed to initialize switch plugin");

	if (cred_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize credential plugin");

	if (gres_init() != SLURM_SUCCESS)
		fatal("failed to initialize gres plugins");

	if (unpack_msg(msg, buffer) == SLURM_ERROR)
		fatal("slurmstepd: we didn't unpack the request correctly");
	FREE_NULL_BUFFER(buffer);

	switch (step_type) {
	case LAUNCH_BATCH_JOB:
		step_id.job_id = ((batch_job_launch_msg_t *)msg->data)->job_id;
		step_id.step_id = SLURM_BATCH_SCRIPT;
		step_id.step_het_comp = NO_VAL;
		break;
	case LAUNCH_TASKS:
	{
		launch_tasks_request_msg_t *task_msg;
		task_msg = (launch_tasks_request_msg_t *)msg->data;

		memcpy(&step_id, &task_msg->step_id, sizeof(step_id));

		if (task_msg->job_ptr &&
		    !xstrcmp(conf->node_name, task_msg->job_ptr->batch_host)) {
			slurm_addr_t *node_addrs;

			/* only allow one stepd to be stepmgr. */
			job_step_ptr = task_msg->job_ptr;
			job_step_ptr->part_ptr = task_msg->part_ptr;
			job_node_array = task_msg->job_node_array;

			/*
			 * job_record doesn't pack its node_addrs array, so get
			 * it from the cred.
			 */
			if (task_msg->cred &&
			    (node_addrs = slurm_cred_get(
				     task_msg->cred,
				     CRED_DATA_JOB_NODE_ADDRS))) {
				add_remote_nodes_to_conf_tbls(
					job_step_ptr->nodes, node_addrs);

				job_step_ptr->node_addrs =
					xcalloc(job_step_ptr->node_cnt,
						sizeof(slurm_addr_t));
				memcpy(job_step_ptr->node_addrs, node_addrs,
				       job_step_ptr->node_cnt *
				       sizeof(slurm_addr_t));
			}
		}

		break;
	}
	default:
		fatal("%s: Unrecognized launch RPC (%d)", __func__, step_type);
		break;
	}

	_set_job_log_prefix(&step_id);

	if (cgroup_read_state(sock) != SLURM_SUCCESS)
		fatal("Failed to read cgroup state from slurmd");

	/*
	 * Init all plugins after receiving the slurm.conf from the slurmd.
	 */
	if ((auth_g_init() != SLURM_SUCCESS) ||
	    (cgroup_g_init() != SLURM_SUCCESS) ||
	    (hash_g_init() != SLURM_SUCCESS) ||
	    (acct_gather_conf_init() != SLURM_SUCCESS) ||
	    (prep_g_init(NULL) != SLURM_SUCCESS) ||
	    (proctrack_g_init() != SLURM_SUCCESS) ||
	    (task_g_init() != SLURM_SUCCESS) ||
	    (jobacct_gather_init() != SLURM_SUCCESS) ||
	    (acct_gather_profile_init() != SLURM_SUCCESS) ||
	    (job_container_init() != SLURM_SUCCESS) ||
	    (topology_g_init() != SLURM_SUCCESS))
		fatal("Couldn't load all plugins");

	/*
	 * Receive all secondary conf files from the slurmd.
	 */

	/* receive cgroup conf from slurmd */
	if (cgroup_read_conf(sock) != SLURM_SUCCESS)
		fatal("Failed to read cgroup conf from slurmd");

	/* receive acct_gather conf from slurmd */
	if (acct_gather_read_conf(sock) != SLURM_SUCCESS)
		fatal("Failed to read acct_gather conf from slurmd");

	/* Receive job_container information from slurmd */
	if (container_g_recv_stepd(sock) != SLURM_SUCCESS)
		fatal("Failed to read job_container.conf from slurmd.");

	/* Receive GRES information from slurmd */
	if (gres_g_recv_stepd(sock, msg) != SLURM_SUCCESS)
		fatal("Failed to read gres.conf from slurmd.");

	/* Receive mpi.conf from slurmd */
	if ((step_type == LAUNCH_TASKS) &&
	    (step_id.step_id != SLURM_EXTERN_CONT) &&
	    (step_id.step_id != SLURM_INTERACTIVE_STEP) &&
	    (mpi_conf_recv_stepd(sock) != SLURM_SUCCESS))
		fatal("Failed to read MPI conf from slurmd");

	if (!conf->hwloc_xml) {
		conf->hwloc_xml = xstrdup_printf("%s/hwloc_topo_%u.%u",
						 conf->spooldir,
						 step_id.job_id,
						 step_id.step_id);
		if (step_id.step_het_comp != NO_VAL)
			xstrfmtcat(conf->hwloc_xml, ".%u",
				   step_id.step_het_comp);
		xstrcat(conf->hwloc_xml, ".xml");
	}
	/*
	 * Swap the field to the srun client version, which will eventually
	 * end up stored as protocol_version in srun_info_t. It's a hack to
	 * pass it in-band, while still using the correct version to unpack
	 * the launch request message above.
	 */
	msg->protocol_version = proto;

	*_cli = cli;
	*_msg = msg;

	return 1;

rwfail:
	fatal("Error reading initialization data from slurmd");
	exit(1);
}

static stepd_step_rec_t *_step_setup(slurm_addr_t *cli, slurm_msg_t *msg)
{
	stepd_step_rec_t *step = NULL;

	switch (msg->msg_type) {
	case REQUEST_BATCH_JOB_LAUNCH:
		debug2("setup for a batch_job");
		step = mgr_launch_batch_job_setup(msg->data, cli);
		break;
	case REQUEST_LAUNCH_TASKS:
		debug2("setup for a launch_task");
		step = mgr_launch_tasks_setup(msg->data, cli,
					      msg->protocol_version);
		break;
	default:
		fatal("handle_launch_message: Unrecognized launch RPC");
		break;
	}

	if (!step) {
		error("_step_setup: no job returned");
		return NULL;
	}

	if (step->container) {
	        struct priv_state sprivs;
		int rc;

		if (drop_privileges(step, false, &sprivs, true) < 0) {
			error("%s: drop_priviledges failed", __func__);
			return NULL;
		}
		rc = setup_container(step);
		if (reclaim_privileges(&sprivs) < 0) {
			error("%s: reclaim_priviledges failed", __func__);
			return NULL;
		}

		if (rc == ESLURM_CONTAINER_NOT_CONFIGURED) {
			debug2("%s: container %s requested but containers are not configured on this node",
			       __func__, step->container->bundle);
		} else if (rc) {
			error("%s: container setup failed: %s",
			      __func__, slurm_strerror(rc));
			stepd_step_rec_destroy(step);
			return NULL;
		} else {
			debug2("%s: container %s successfully setup",
			       __func__, step->container->bundle);
		}
	}

	step->jmgr_pid = getpid();
	step->jobacct = jobacctinfo_create(NULL);

	/* Establish GRES environment variables */
	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES) {
		gres_job_state_log(step->job_gres_list,
				   step->step_id.job_id);
		gres_step_state_log(step->step_gres_list,
				    step->step_id.job_id,
				    step->step_id.step_id);
	}
	if (step->batch ||
	    (step->step_id.step_id == SLURM_INTERACTIVE_STEP) ||
	    (step->flags & LAUNCH_EXT_LAUNCHER)) {
		gres_g_job_set_env(step, 0);
	} else if (msg->msg_type == REQUEST_LAUNCH_TASKS) {
		gres_g_step_set_env(step);
	}

	/*
	 * Add slurmd node topology informations to job env array
	 */
	env_array_overwrite(&step->env,"SLURM_TOPOLOGY_ADDR",
			    conf->node_topo_addr);
	env_array_overwrite(&step->env,"SLURM_TOPOLOGY_ADDR_PATTERN",
			    conf->node_topo_pattern);

	/* Reset addrs for dynamic/cloud nodes to hash tables */
	if (step->node_addrs &&
	    add_remote_nodes_to_conf_tbls(step->node_list, step->node_addrs)) {
		error("%s: failed to add node addrs: %s", __func__,
		      step->alias_list);
		stepd_step_rec_destroy(step);
		return NULL;
	}

	set_msg_node_id(step);

	return step;
}

#ifdef MEMORY_LEAK_DEBUG
static void
_step_cleanup(stepd_step_rec_t *step, slurm_msg_t *msg, int rc)
{
	if (step) {
		jobacctinfo_destroy(step->jobacct);
		if (!step->batch)
			stepd_step_rec_destroy(step);
	}

	/*
	 * The message cannot be freed until the jobstep is complete
	 * because the job struct has pointers into the msg, such
	 * as the switch jobinfo pointer.
	 */
	slurm_free_msg(msg);

	jobacctinfo_destroy(step_complete.jobacct);
}
#endif
