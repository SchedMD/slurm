/*****************************************************************************
 *  alloc.c - Slurm scrun job alloc handlers
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "limits.h"

#include "slurm/slurm.h"

#include "src/common/conmgr.h"
#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/spank.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "scrun.h"

/* max number of seconds to delay while waiting for job */
#define MAX_DELAY 60

typedef struct {
	const char *var;
	int type;
} env_vars_t;

static const env_vars_t env_vars[] = {
	{ "SCRUN_ACCOUNT", 'A' },
	{ "SCRUN_ACCTG_FREQ", LONG_OPT_ACCTG_FREQ },
	{ "SCRUN_BURST_BUFFER", LONG_OPT_BURST_BUFFER_SPEC },
	{ "SCRUN_CLUSTER_CONSTRAINT", LONG_OPT_CLUSTER_CONSTRAINT },
	{ "SCRUN_CLUSTERS", 'M' },
	{ "SCRUN_CONSTRAINT", 'C' },
	{ "SCRUN_CORE_SPEC", 'S' },
	{ "SCRUN_CPU_BIND", LONG_OPT_CPU_BIND },
	{ "SCRUN_CPU_FREQ_REQ", LONG_OPT_CPU_FREQ },
	{ "SCRUN_CPUS_PER_GPU", LONG_OPT_CPUS_PER_GPU },
	{ "SCRUN_CPUS_PER_TASK", 'c' },
	{ "SCRUN_DELAY_BOOT", LONG_OPT_DELAY_BOOT },
	{ "SCRUN_DEPENDENCY", 'd' },
	{ "SCRUN_DISTRIBUTION", 'm' },
	{ "SCRUN_EPILOG", LONG_OPT_EPILOG },
	{ "SCRUN_EXACT", LONG_OPT_EXACT },
	{ "SCRUN_EXCLUSIVE", LONG_OPT_EXCLUSIVE },
	{ "SCRUN_GPU_BIND", LONG_OPT_GPU_BIND },
	{ "SCRUN_GPU_FREQ", LONG_OPT_GPU_FREQ },
	{ "SCRUN_GPUS", 'G' },
	{ "SCRUN_GPUS_PER_NODE", LONG_OPT_GPUS_PER_NODE },
	{ "SCRUN_GPUS_PER_SOCKET", LONG_OPT_GPUS_PER_SOCKET },
	{ "SCRUN_GPUS_PER_TASK", LONG_OPT_GPUS_PER_TASK },
	{ "SCRUN_GRES_FLAGS", LONG_OPT_GRES_FLAGS },
	{ "SCRUN_GRES", LONG_OPT_GRES },
	{ "SCRUN_HINT", LONG_OPT_HINT },
	{ "SCRUN_JOB_NAME", 'J' },
	{ "SCRUN_JOB_NODELIST", LONG_OPT_ALLOC_NODELIST },
	{ "SCRUN_JOB_NUM_NODES", 'N' },
	{ "SCRUN_LABELIO", 'l' },
	{ "SCRUN_MEM_BIND", LONG_OPT_MEM_BIND },
	{ "SCRUN_MEM_PER_CPU", LONG_OPT_MEM_PER_CPU },
	{ "SCRUN_MEM_PER_GPU", LONG_OPT_MEM_PER_GPU },
	{ "SCRUN_MEM_PER_NODE", LONG_OPT_MEM },
	{ "SCRUN_MPI_TYPE", LONG_OPT_MPI },
	{ "SCRUN_NCORES_PER_SOCKET", LONG_OPT_CORESPERSOCKET },
	{ "SCRUN_NETWORK", LONG_OPT_NETWORK },
	{ "SCRUN_NSOCKETS_PER_NODE", LONG_OPT_SOCKETSPERNODE },
	{ "SCRUN_NTASKS", 'n' },
	{ "SCRUN_NTASKS_PER_CORE", LONG_OPT_NTASKSPERCORE },
	{ "SCRUN_NTASKS_PER_GPU", LONG_OPT_NTASKSPERGPU },
	{ "SCRUN_NTASKS_PER_NODE", LONG_OPT_NTASKSPERNODE },
	{ "SCRUN_NTASKS_PER_TRES", LONG_OPT_NTASKSPERTRES },
	{ "SCRUN_OPEN_MODE", LONG_OPT_OPEN_MODE },
	{ "SCRUN_OVERCOMMIT", 'O' },
	{ "SCRUN_OVERLAP", LONG_OPT_OVERLAP },
	{ "SCRUN_PARTITION", 'p' },
	{ "SCRUN_POWER", LONG_OPT_POWER },
	{ "SCRUN_PROFILE", LONG_OPT_PROFILE },
	{ "SCRUN_PROLOG", LONG_OPT_PROLOG },
	{ "SCRUN_QOS", 'q' },
	{ "SCRUN_REMOTE_CWD", 'D' },
	{ "SCRUN_REQ_SWITCH", LONG_OPT_SWITCH_REQ },
	{ "SCRUN_RESERVATION", LONG_OPT_RESERVATION },
	{ "SCRUN_SIGNAL", LONG_OPT_SIGNAL },
	{ "SCRUN_SLURMD_DEBUG", LONG_OPT_SLURMD_DEBUG },
	{ "SCRUN_SPREAD_JOB", LONG_OPT_SPREAD_JOB },
	{ "SCRUN_TASK_EPILOG", LONG_OPT_TASK_EPILOG },
	{ "SCRUN_TASK_PROLOG", LONG_OPT_TASK_PROLOG },
	{ "SCRUN_THREAD_SPEC", LONG_OPT_THREAD_SPEC },
	{ "SCRUN_THREADS_PER_CORE", LONG_OPT_THREADSPERCORE },
	{ "SCRUN_THREADS", 'T' },
	{ "SCRUN_TIMELIMIT", 't' },
	{ "SCRUN_TRES_PER_TASK", LONG_OPT_TRES_PER_TASK },
	{ "SCRUN_UNBUFFEREDIO", 'u' },
	{ "SCRUN_USE_MIN_NODES", LONG_OPT_USE_MIN_NODES },
	{ "SCRUN_WAIT4SWITCH", LONG_OPT_SWITCH_WAIT },
	{ "SCRUN_WCKEY", LONG_OPT_WCKEY },
	{ "SCRUN_WORKING_DIR", 'D' },
};

#define _set_env_args(state, field, pattern, ...)                              \
	do {                                                                   \
		xassert(state.locked);                                         \
		(void) env_array_overwrite_fmt(&state.job_env, field, pattern, \
					       __VA_ARGS__);                   \
	} while (0)

#define _set_env(state, field, value)                                       \
	do {                                                                \
		const char *v = value; /* avoid field=(null) */             \
		xassert(state.locked);                                      \
		(void) env_array_overwrite_fmt(&state.job_env, field, "%s", \
					       ((v && v[0]) ? v : ""));     \
	} while (0)

static int _foreach_env_annotation(void *x, void *arg)
{
	config_key_pair_t *key_pair_ptr = x;
	char *key = xstrdup_printf("SCRUN_ANNOTATION_%s", key_pair_ptr->name);

	xassert(!arg);
	xassert(state.locked);

	_set_env(state, key, key_pair_ptr->value);

	xfree(key);
	return SLURM_SUCCESS;
}

static void _script_env(void)
{
	xassert(state.locked);

	/* variables required for OCI state */
	_set_env(state, "SCRUN_OCI_VERSION", state.oci_version);
	_set_env(state, "SCRUN_CONTAINER_ID", state.id);
	if (state.pid && (state.pid != INFINITE64))
		_set_env_args(state, "SCRUN_PID", "%"PRIu64,
			      (uint64_t) state.pid);
	_set_env(state, "SCRUN_BUNDLE", state.bundle);
	_set_env(state, "SCRUN_SUBMISSION_BUNDLE", state.orig_bundle);
	list_for_each_ro(state.annotations, _foreach_env_annotation, NULL);
	_set_env(state, "SCRUN_PID_FILE", state.pid_file);
	_set_env(state, "SCRUN_SOCKET", state.anchor_socket);
	_set_env(state, "SCRUN_SPOOL_DIR", state.spool_dir);
	_set_env(state, "SCRUN_SUBMISSION_CONFIG_FILE", state.config_file);
	if ((state.user_id != NO_VAL) && (state.user_id != SLURM_AUTH_NOBODY)) {
		/* set user if we know it but we may not in a user namespace */
		char *u = uid_to_string_or_null(state.user_id);
		_set_env(state, "SCRUN_USER", u);
		xfree(u);
		_set_env_args(state, "SCRUN_USER_ID", "%u", state.user_id);
	}
	if ((state.group_id != NO_VAL) &&
	    (state.group_id != SLURM_AUTH_NOBODY)) {
		/* set group if we know it but we may not in a user namespace */
		char *u = gid_to_string_or_null(state.group_id);
		_set_env(state, "SCRUN_GROUP", u);
		xfree(u);
		_set_env_args(state, "SCRUN_GROUP_ID", "%u", state.group_id);
	}
	_set_env(state, "SCRUN_ROOT", state.root_dir);
	_set_env(state, "SCRUN_ROOTFS_PATH", state.root_path);
	_set_env(state, "SCRUN_SUBMISSION_ROOTFS_PATH", state.root_path);

	if (log_file)
		_set_env(state, "SCRUN_LOG_FILE", log_file);
	if (log_format)
		_set_env(state, "SCRUN_LOG_FORMAT", log_format);

	if (state.tty_size.ws_col)
		_set_env_args(state, "SLURM_PTY_WIN_COL", "%hu",
			      state.tty_size.ws_col);
	if (state.tty_size.ws_row)
		_set_env_args(state, "SLURM_PTY_WIN_ROW", "%hu",
			      state.tty_size.ws_row);
}

#undef _set_env
#undef _set_env_args

static int _stage_in()
{
	int rc;

	if (get_log_level() >= LOG_LEVEL_DEBUG) {
		read_lock_state();
		debug("%s: BEGIN container %s staging in", __func__, state.id);
		unlock_state();
	}

	rc = stage_in();

	if (get_log_level() >= LOG_LEVEL_DEBUG) {
		read_lock_state();
		debug("%s: END container %s staging in: %s",
		      __func__, state.id, slurm_strerror(rc));
		unlock_state();
	}

	if (rc) {
		read_lock_state();
		error("%s: stage_in() for %s failed: %s",
		      __func__, state.id, slurm_strerror(rc));
		unlock_state();
	}

	return rc;
}

static void *_on_connection(con_mgr_fd_t *con, void *arg)
{
	debug("%s:[%s] new srun connection", __func__, con->name);

	/* must return !NULL or connection will be closed */
	return con;
}

static int _on_msg(con_mgr_fd_t *con, slurm_msg_t *msg, void *arg)
{
	int rc = SLURM_SUCCESS;
	xassert(arg == con);

	switch (msg->msg_type)
	{
	case SRUN_PING:
	{
		/* if conmgr is alive then always respond success */
		slurm_msg_t resp_msg;
		return_code_msg_t rc_msg = {
			.return_code = SLURM_SUCCESS,
		};

		response_init(&resp_msg, msg, RESPONSE_SLURM_RC, &rc_msg);
		resp_msg.data_size = sizeof(rc_msg);

		rc = con_mgr_queue_write_msg(con, &resp_msg);
		/* nothing to xfree() */

		debug("%s:[%s] srun RPC PING has been PONGED", __func__, con->name);
		break;
	}
	case SRUN_JOB_COMPLETE:
	{
		xassert(sizeof(srun_job_complete_msg_t) ==
			sizeof(slurm_step_id_t));
		slurm_step_id_t *step = msg->data;
		debug("%s:[%s] %pS complete srun RPC", __func__, con->name, step);
		stop_anchor(SLURM_SUCCESS);
		break;
	}
	case SRUN_TIMEOUT:
	{
		srun_timeout_msg_t *to = msg->data;
		debug("%s:[%s] srun RPC %pS timeout at %ld RPC",
		      __func__, con->name, &to->step_id, to->timeout);
		stop_anchor(ESLURM_JOB_TIMEOUT_KILLED);
		break;
	}
	case SRUN_USER_MSG:
	{
		srun_user_msg_t *um = msg->data;

		debug("%s:[%s] JobId=%u srun user message RPC",
		      __func__, con->name, um->job_id);

		print_multi_line_string(um->msg, -1, LOG_LEVEL_INFO);
		break;
	}
	case SRUN_NODE_FAIL:
	{
		srun_node_fail_msg_t *nf = msg->data;
		debug("%s:[%s] srun RPC %pS nodes failed: %s",
		      __func__, con->name, &nf->step_id, nf->nodelist);
		stop_anchor(ESLURM_JOB_NODE_FAIL_KILLED);
		break;
	}
	case SRUN_REQUEST_SUSPEND:
	{
		suspend_msg_t *sus_msg = msg->data;
		rc = SLURM_UNEXPECTED_MSG_ERROR;
		error("%s:[%s] rejecting srun suspend RPC for %s",
		      __func__, con->name, sus_msg->job_id_str);
		break;
	}
	case SRUN_NET_FORWARD:
	{
		net_forward_msg_t *net = msg->data;
		rc = SLURM_UNEXPECTED_MSG_ERROR;
		error("%s:[%s] rejecting srun net forward RPC for %s",
		      __func__, con->name, net->target);
		break;
	}
	default:
		rc = SLURM_UNEXPECTED_MSG_ERROR;
		error("%s:[%s] received spurious srun message type: %u",
		      __func__, con->name, msg->msg_type);
	}

	return rc;
}

static void _on_finish(void *arg)
{
	con_mgr_fd_t *con = arg;

	if (get_log_level() > LOG_LEVEL_DEBUG) {
		read_lock_state();
		debug("%s: [%s] closed srun connection state=%s",
		      __func__, con->name,
		      slurm_container_status_to_str(state.status));
		unlock_state();
	}
}

/*
 * Listen on srun port to make sure that slurmctld doesn't mark job as dead
 * RET port listening on
 */
static uint32_t _setup_listener(con_mgr_t *conmgr)
{
	static const con_mgr_events_t events = {
		.on_connection = _on_connection,
		.on_msg = _on_msg,
		.on_finish = _on_finish,
	};
	uint16_t *ports;
	uint16_t port = 0;
	int fd = -1;
	int rc;

	if ((ports = slurm_get_srun_port_range())) {
		if (net_stream_listen_ports(&fd, &port,
					    slurm_get_srun_port_range(),
					    false) < 0)
			fatal("%s: unable to open local listening port. Try increasing range of SrunPortRange in slurm.conf.",
			      __func__);
	} else {
		if (net_stream_listen(&fd, &port) < 0)
			fatal("%s: unable to open local listening port",
			      __func__);
	}

	xassert(port > 0);
	debug("%s: listening for srun RPCs on port=%hu", __func__, port);

	if ((rc = con_mgr_process_fd(conmgr, CON_TYPE_RPC, fd, fd, events, NULL,
				     0, NULL)))
		fatal("%s: conmgr refuesed fd=%d: %s",
		      __func__, fd, slurm_strerror(rc));

	return port;
}

static void _pending_callback(uint32_t job_id)
{
	info("waiting on pending job allocation %u", job_id);
}

/* check allocation has all nodes ready */
extern void check_allocation(con_mgr_t *conmgr, con_mgr_fd_t *con,
			     con_mgr_work_type_t type,
			     con_mgr_work_status_t status, const char *tag,
			     void *arg)
{
	/* there must be only 1 thread that will call this at any one time */
	static long delay = 1;
	bool bail = false;
	int rc, job_id;

	read_lock_state();
	bail = (state.status != CONTAINER_ST_CREATING);
	job_id = state.jobid;
	unlock_state();

	if (bail) {
		/*
		 * Only check allocations while creating. Something else must
		 * have broke before now so bail out.
		 */
		debug("%s: bailing due to status %s != %s",
		      __func__,
		      slurm_container_status_to_str(state.status),
		      slurm_container_status_to_str(CONTAINER_ST_CREATING));
		stop_anchor(ESLURM_ALREADY_DONE);
		return;
	}

	if (status != CONMGR_WORK_STATUS_RUN) {
		debug("%s: bailing due to callback status %s",
		      __func__, con_mgr_work_status_string(status));
		stop_anchor(ESLURM_ALREADY_DONE);
		return;
	}

	if (get_log_level() >= LOG_LEVEL_DEBUG) {
		read_lock_state();
		debug("%s: checking JobId=%d for nodes ready",
		      __func__, state.jobid);
		unlock_state();
	}
	rc = slurm_job_node_ready(job_id);
	if ((rc == READY_JOB_ERROR) || (rc == EAGAIN)) {
		delay *= 2;
		if ((delay < 0) || (delay > MAX_DELAY))
			delay = MAX_DELAY;

		if (get_log_level() >= LOG_LEVEL_DEBUG) {
			read_lock_state();
			debug("%s: rechecking JobId=%d for nodes ready in %ld ns",
			      __func__, state.jobid, delay);
			unlock_state();
		}
		con_mgr_add_delayed_work(conmgr, NULL, check_allocation, delay,
					 0, NULL, "check_allocation");
	} else if ((rc == READY_JOB_FATAL) || !(rc & READY_JOB_STATE)) {
		/* job failed! */
		if (get_log_level() >= LOG_LEVEL_DEBUG) {
			read_lock_state();
			debug("%s: JobId=%d failed. Bailing on checking for nodes: %s",
			      __func__, state.jobid, slurm_strerror(rc));
			unlock_state();
		}
		stop_anchor(ESLURM_ALREADY_DONE);
		return;
	} else {
		/* job is ready! */
		if (get_log_level() >= LOG_LEVEL_DEBUG) {
			read_lock_state();
			debug("%s: JobId=%d is ready", __func__, state.jobid);
			unlock_state();
		}

		if ((rc = _stage_in())) {
			stop_anchor(rc);
		} else {
			/* we have a job now. see if creating is done */
			con_mgr_add_work(conmgr, NULL, on_allocation,
					 CONMGR_WORK_TYPE_FIFO, NULL, __func__);
		}
	}
}

static void _alloc_job(con_mgr_t *conmgr)
{
	int rc;
	resource_allocation_response_msg_t *alloc = NULL;
	salloc_opt_t aopt = { 0 };
	slurm_opt_t opt = {
		.salloc_opt = &aopt,
	};
	char *opt_string = NULL;
	struct option *spanked = slurm_option_table_create(&opt, &opt_string);
	job_desc_msg_t *desc;

	slurm_reset_all_options(&opt, true);

	for (int i = 0; i < ARRAY_SIZE(env_vars); i++) {
		const char *val;
		const env_vars_t *e = &env_vars[i];

		if ((val = getenv(e->var)))
			slurm_process_option_or_exit(&opt, e->type, val, true,
						     false);
	}

	/* Process spank env options */
	if ((rc = spank_process_env_options()))
		fatal("%s: spank_process_env_options() failed: %s",
		      __func__, slurm_strerror(rc));

	slurm_option_table_destroy(spanked);
	spanked = NULL;
	xfree(opt_string);

	desc = slurm_opt_create_job_desc(&opt, true);
	xfree(desc->name);
	read_lock_state();
	desc->name = xstrdup(state.id);
	desc->container_id = xstrdup(state.id);
	unlock_state();
	if (!desc->min_nodes || (desc->min_nodes == NO_VAL))
		desc->min_nodes = 1;

	/*
	 * Avoid giving the user/group as this may be run
	 * in user namespace as uid 0.
	 */
	desc->user_id = SLURM_AUTH_NOBODY;
	desc->group_id = SLURM_AUTH_NOBODY;
	desc->name = xstrdup("scrun");
	desc->other_port = _setup_listener(conmgr);

	debug("%s: requesting allocation with %u tasks and %u hosts",
	      __func__, (desc->num_tasks == NO_VAL ? 1 : desc->num_tasks),
	      (desc->min_nodes == NO_VAL ? 1 : desc->min_nodes));
	alloc = slurm_allocate_resources_blocking(desc, false,
						  _pending_callback);
	if (!alloc)
		fatal("Unable to request job allocation: %m");
	if (alloc->error_code) {
		error("%s: unable to request job allocation: %s",
		      __func__, slurm_strerror(alloc->error_code));

		stop_anchor(alloc->error_code);
	}

	if (get_log_level() >= LOG_LEVEL_DEBUG) {
		char *user = uid_to_string_or_null(alloc->uid);
		char *group = gid_to_string_or_null(alloc->gid);

		debug("allocated jobId=%u user[%u]=%s group[%u]=%s",
		      alloc->job_id, alloc->uid, user, alloc->uid, group);

		xfree(user);
		xfree(group);
	}

	write_lock_state();
	state.jobid = alloc->job_id;

	/* take job env (if any) for srun calls later */
	SWAP(state.job_env, alloc->environment);

	/* apply SPANK env (if any) */
	for (int i = 0; i < opt.spank_job_env_size; i++) {
		char *value, *name = NULL;

		if ((value = strchr(opt.spank_job_env[i], '='))) {
			value[0] = '\0';
			value++;
		}

		xstrfmtcat(name, "SLURM_SPANK_%s", opt.spank_job_env[i]);
		env_array_overwrite(&state.job_env, name, value);
		xfree(name);
	}

	xassert(state.user_id == getuid());
	state.user_id = alloc->uid;
	xassert(state.user_id != SLURM_AUTH_NOBODY);

	xassert(state.group_id == getgid());
	state.group_id = alloc->gid;
	xassert(state.group_id != SLURM_AUTH_NOBODY);

	env_array_for_job(&state.job_env, alloc, desc, -1);
	unlock_state();

	slurm_free_job_desc_msg(desc);
	slurm_free_resource_allocation_response_msg(alloc);
}

extern void get_allocation(con_mgr_t *conmgr, con_mgr_fd_t *con,
			   con_mgr_work_type_t type,
			   con_mgr_work_status_t status, const char *tag,
			   void *arg)
{
	int rc;
	job_info_msg_t *jobs = NULL;
	int job_id;
	char *job_id_str = getenv("SLURM_JOB_ID");
	bool existing_allocation = false;

	if (job_id_str && job_id_str[0]) {
		extern char **environ;
		slurm_selected_step_t id = {0};

		if ((rc = unfmt_job_id_string(job_id_str, &id))) {
			fatal("%s: invalid SLURM_JOB_ID=%s: %s",
			      __func__, job_id_str, slurm_strerror(rc));
			return;
		}

		write_lock_state();
		state.jobid = job_id = id.step_id.job_id;
		state.existing_allocation = existing_allocation = true;

		/* scrape SLURM_* from calling env */
		state.job_env = env_array_create();
		env_array_merge_slurm(&state.job_env, (const char **) environ);
		unlock_state();

		debug("Running under existing JobId=%u", job_id);
	} else {
		_alloc_job(conmgr);

		read_lock_state();
		job_id = state.jobid;
		unlock_state();
	}

	/* alloc response is too sparse. get full job info */
	rc = slurm_load_job(&jobs, job_id, 0);
	if (rc || !jobs || (jobs->record_count <= 0)) {
		/* job not found or already died ? */
		if ((rc == SLURM_ERROR) && errno)
			rc = errno;

		error("%s: unable to find JobId=%u: %s",
		      __func__, job_id, slurm_strerror(rc));

		stop_anchor(rc);
		return;
	}

	/* grab the first job */
	xassert(jobs->job_array->job_id == job_id);

	write_lock_state();
	if (existing_allocation) {
		xassert(state.user_id == getuid());
		state.user_id = jobs->job_array->user_id;
		xassert(state.user_id != SLURM_AUTH_NOBODY);

		xassert(state.group_id == getgid());
		state.group_id = jobs->job_array->group_id;
		xassert(state.group_id != SLURM_AUTH_NOBODY);
	}
	_script_env();
	unlock_state();

	if (get_log_level() >= LOG_LEVEL_DEBUG) {
		read_lock_state();
		if (state.job_env)
			for (int i = 0; state.job_env[i]; i++)
				debug("Job env[%d]=%s", i, state.job_env[i]);
		else
			debug("JobId=%u did not provide an environment",
			      job_id);
		unlock_state();
	}

	slurm_free_job_info_msg(jobs);

	if (existing_allocation) {
		if ((rc = _stage_in()))
			stop_anchor(rc);
		else
			con_mgr_add_work(conmgr, NULL, on_allocation,
					 CONMGR_WORK_TYPE_FIFO, NULL, __func__);
	} else {
		con_mgr_add_delayed_work(conmgr, NULL, check_allocation, 0, 1,
					 NULL, "check_allocation");
	}
}
