/*****************************************************************************\
 *  task_resctrl.c - task plugin confining steps to Linux resctrl groups
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <sys/stat.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/resctrl_conf.h"
#include "src/common/run_in_daemon.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/gres.h"
#include "src/interfaces/task.h"

#include "src/slurmd/slurmd/slurmd.h"

#include "src/plugins/task/resctrl/resctrl_fs.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "task resctrl plugin";
const char plugin_type[] = "task/resctrl";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

typedef struct {
	uint64_t count_only; /* GPUs allocated without device files */
	bitstr_t *gpus; /* GPU indexes allocated to the job on this node */
} job_gpus_t;

static resctrl_conf_t *resctrl_conf = NULL; /* slurmd: resctrl.yaml */
static const char *step_group = NULL; /* slurmstepd: group of this step */
static bool stepd_enabled = false; /* slurmstepd: groups exist on this node */

/*
 * list_for_each() callback: count the existing groups resctrl.yaml does not
 * define.
 * IN x - name of an existing group
 * IN/OUT arg - uint32_t counter
 * RET 0
 */
static int _count_foreign_groups(void *x, void *arg)
{
	char *name = x;
	uint32_t *count = arg;

	if (!resctrl_conf_find_group(resctrl_conf, name))
		(*count)++;

	return 0;
}

/*
 * Check how many CLOSIDs this hardware supports, how many groups are already
 * created, and fail if we have no room for our configured Slurm groups.
 */
static void _check_max_groups(void)
{
	uint32_t max_groups = resctrl_fs_max_groups();
	uint32_t configured = list_count(resctrl_conf->groups);
	uint32_t foreign = 0;
	list_t *existing = NULL;

	if (!max_groups) {
		debug("cannot determine how many resctrl groups the hardware supports");
		return;
	}

	existing = resctrl_fs_list_groups();
	list_for_each(existing, _count_foreign_groups, &foreign);
	FREE_NULL_LIST(existing);

	if ((configured + foreign + 1) > max_groups)
		fatal("%s configures %u resctrl groups, which with the root group and %u groups not managed by Slurm exceeds the %u groups the hardware supports",
		      RESCTRL_CONF_FILE, configured, foreign, max_groups);
}

/*
 * list_for_each() callback: make one configured group exist with its
 * schemata. Existing directories are reused, whoever created them.
 * IN x - resctrl_conf_group_t to apply
 * IN arg - unused
 * RET 0
 */
static int _apply_group(void *x, void *arg)
{
	resctrl_conf_group_t *group = x;
	char *status = NULL;
	int rc = EINVAL;

	rc = resctrl_fs_group_create(group->name);
	if (!rc) {
		info("created resctrl group %s", group->name);
	} else if (rc == EEXIST) {
		info("using existing resctrl group %s", group->name);
	} else {
		status = resctrl_fs_last_cmd_status();
		fatal("cannot create resctrl group %s: %s (%s)", group->name,
		      slurm_strerror(rc), (status ? status : "no status"));
	}

	if ((rc = resctrl_fs_write_schemata(group->name, group->schemata))) {
		status = resctrl_fs_last_cmd_status();
		fatal("cannot apply the schemata of resctrl group %s: %s (%s)",
		      group->name, slurm_strerror(rc),
		      (status ? status : "no status"));
	}

	return 0;
}

/*
 * Bring the resctrl filesystem in line with resctrl.yaml. Runs on every
 * slurmd start, which includes every reconfigure as slurmd re-execs. On a
 * node without kernel support for resctrl, without resctrl.yaml, or where
 * the filesystem cannot be mounted the plugin does nothing.
 * RET SLURM_SUCCESS
 */
static int _slurmd_init(void)
{
	int rc = EINVAL;

	if (!resctrl_fs_supported()) {
		info("the kernel does not support resctrl, resctrl groups are not managed on this node");
		return SLURM_SUCCESS;
	}

	if ((rc = resctrl_conf_load(&resctrl_conf))) {
		if (rc != ENOENT)
			fatal("cannot load %s", RESCTRL_CONF_FILE);
		info("%s not found, resctrl groups are not managed on this node",
		     RESCTRL_CONF_FILE);
		return SLURM_SUCCESS;
	}

	if (resctrl_fs_mount(resctrl_conf->mount_options)) {
		error("resctrl groups are not managed on this node");
		return SLURM_SUCCESS;
	}

	_check_max_groups();
	list_for_each(resctrl_conf->groups, _apply_group, NULL);

	return SLURM_SUCCESS;
}

/*
 * The stepd only needs to know whether slurmd manages the groups on this
 * node, which it does when resctrl.yaml is present and the groups exist;
 * the group names are fixed.
 * RET SLURM_SUCCESS
 */
static int _stepd_init(void)
{
	char *path = get_extra_conf_path(RESCTRL_CONF_FILE);
	struct stat st = { 0 };

	stepd_enabled = (!stat(path, &st) && resctrl_fs_is_mounted() &&
			 resctrl_fs_group_exists(RESCTRL_GROUP_CPU_ONLY));
	xfree(path);
	if (!stepd_enabled)
		debug("resctrl groups are not managed on this node, steps are not confined");

	return SLURM_SUCCESS;
}

/*
 * RET true for the job gres that stand for GPUs: the sharing gres itself
 * and the shared gres (mps, shard) carved out of it, whose bitmaps index the
 * same devices.
 * IN gres_state - job gres entry
 */
static bool _is_gpu_gres(gres_state_t *gres_state)
{
	return (gres_id_sharing(gres_state->plugin_id) ||
		gres_id_shared(gres_state->config_flags));
}

/*
 * list_for_each() callback: keep the GPU gres of the job.
 * IN x - gres_state_t of the job
 * IN/OUT arg - list the GPU gres are appended to
 * RET 0
 */
static int _add_gpu_gres(void *x, void *arg)
{
	gres_state_t *gres_state = x;
	list_t *gpu_gres = arg;

	if (_is_gpu_gres(gres_state))
		list_append(gpu_gres, gres_state);

	return 0;
}

/*
 * list_for_each() callback: merge the GPU indexes the job was allocated on
 * this node. GPUs configured without device files have no index and are
 * only counted.
 * IN x - gres_state_t of the job
 * IN/OUT arg - job_gpus_t accumulator
 * RET 0
 */
static int _collect_gpus(void *x, void *arg)
{
	gres_state_t *gres_state = x;
	gres_job_state_t *gres_js = gres_state->gres_data;
	job_gpus_t *job_gpus = arg;
	bitstr_t *alloc = NULL;

	if (!_is_gpu_gres(gres_state) || !gres_js->node_cnt)
		return 0;

	if (gres_js->gres_bit_alloc)
		alloc = gres_js->gres_bit_alloc[0];

	if (!alloc) {
		/* The job may have none of this gres on this node. */
		if (!gres_js->gres_cnt_node_alloc ||
		    !gres_js->gres_cnt_node_alloc[0])
			return 0;
		if (gres_id_shared(gres_state->config_flags))
			job_gpus->count_only++;
		else
			job_gpus->count_only += gres_js->gres_cnt_node_alloc[0];
		return 0;
	}

	if (!job_gpus->gpus) {
		job_gpus->gpus = bit_copy(alloc);
	} else {
		if (bit_size(job_gpus->gpus) < bit_size(alloc))
			bit_realloc(job_gpus->gpus, bit_size(alloc));
		bit_or(job_gpus->gpus, alloc);
	}

	return 0;
}

/*
 * list_find_first() callback: an allocated device record of a GPU index.
 * IN x - gres_device_t
 * IN key - int GPU index
 * RET 1 when the device is allocated and has that index, else 0
 */
static int _find_alloc_device(void *x, void *key)
{
	gres_device_t *device = x;
	int *index = key;

	return (device->alloc && (device->index == *index));
}

/*
 * Mark the sockets a device is attached to, from the CPUs gres.conf ties it
 * to. Machine CPU ids are mapped to the abstract numbering, which is socket
 * major over the detected hardware, so the detected layout is used rather
 * than the one in slurm.conf, which may differ. A device without CPUs, or
 * whose CPUs cannot be placed, may be fed from any socket and is marked on
 * all of them.
 * IN device - device record, may be NULL
 * OUT sockets - bitmap of the sockets, one bit per socket of the node
 */
static void _device_sockets(gres_device_t *device, bitstr_t *sockets)
{
	uint32_t cpus_per_socket = (conf->actual_cores * conf->actual_threads);

	bit_clear_all(sockets);

	if (!device || !device->cpus_bitmap || !cpus_per_socket) {
		bit_set_all(sockets);
		return;
	}

	for (int cpu = 0; cpu < bit_size(device->cpus_bitmap); cpu++) {
		uint32_t abstract = cpu, socket = 0;

		if (!bit_test(device->cpus_bitmap, cpu))
			continue;
		if (conf->block_map_inv && (cpu < conf->block_map_size))
			abstract = conf->block_map_inv[cpu];
		socket = (abstract / cpus_per_socket);
		if (socket < bit_size(sockets))
			bit_set(sockets, socket);
	}

	if (!bit_set_count(sockets))
		bit_set_all(sockets);
}

/*
 * Count one GPU on every socket it is attached to.
 * IN device - device record, may be NULL
 * IN sockets - scratch bitmap sized to the sockets of the node
 * IN/OUT per_socket - GPUs counted on each socket
 */
static void _count_gpu(gres_device_t *device, bitstr_t *sockets,
		       uint32_t *per_socket)
{
	_device_sockets(device, sockets);
	for (int s = 0; s < bit_size(sockets); s++) {
		if (bit_test(sockets, s))
			per_socket[s]++;
	}
}

/*
 * Count the GPUs of the job on this node and the most of them attached to
 * any one socket. Each GPU index counts once, whatever the number of device
 * files or shared gres behind it.
 * IN step - step record
 * OUT gpus - GPUs of the job on this node
 * OUT max_per_socket - most GPUs attached to one socket
 */
static void _count_gpus(stepd_step_rec_t *step, uint32_t *gpus,
			uint32_t *max_per_socket)
{
	job_gpus_t job_gpus = { 0 };
	list_t *gpu_gres = list_create(NULL);
	list_t *devices = NULL;
	uint32_t nsockets = MAX(conf->actual_sockets, 1);
	uint32_t *per_socket = xcalloc(nsockets, sizeof(*per_socket));
	bitstr_t *sockets = bit_alloc(nsockets);

	*gpus = 0;
	*max_per_socket = 0;

	if (step->job_gres_list) {
		list_for_each(step->job_gres_list, _collect_gpus, &job_gpus);
		list_for_each(step->job_gres_list, _add_gpu_gres, gpu_gres);
	}
	if (job_gpus.gpus)
		devices = gres_g_get_devices(gpu_gres, true, 0, NULL, 0, step);

	for (int i = 0; job_gpus.gpus && (i < bit_size(job_gpus.gpus)); i++) {
		gres_device_t *device = NULL;

		if (!bit_test(job_gpus.gpus, i))
			continue;
		if (devices)
			device = list_find_first(devices, _find_alloc_device,
						 &i);
		_count_gpu(device, sockets, per_socket);
		(*gpus)++;
	}
	if (job_gpus.count_only) {
		uint32_t extra = MIN(job_gpus.count_only, UINT32_MAX);

		/* GPUs without device files may be attached to any socket. */
		for (uint32_t s = 0; s < nsockets; s++)
			per_socket[s] = MIN(((uint64_t) per_socket[s] + extra),
					    UINT32_MAX);
		*gpus = MIN(((uint64_t) *gpus + extra), UINT32_MAX);
	}

	for (uint32_t s = 0; s < nsockets; s++)
		*max_per_socket = MAX(*max_per_socket, per_socket[s]);

	FREE_NULL_LIST(devices);
	FREE_NULL_LIST(gpu_gres);
	FREE_NULL_BITMAP(job_gpus.gpus);
	FREE_NULL_BITMAP(sockets);
	xfree(per_socket);
}

/*
 * Pick the group of a step from the shape of its job on this node.
 * IN step - step record
 * OUT gpus - GPUs of the job on this node
 * OUT max_per_socket - most GPUs attached to one socket
 * RET the group name, or NULL when the step is left where it is
 */
static const char *_select_group(stepd_step_rec_t *step, uint32_t *gpus,
				 uint32_t *max_per_socket)
{
	*gpus = 0;
	*max_per_socket = 0;

	if ((step->job_exclusive == JOB_EXCLUSIVE_NODE) ||
	    (step->job_exclusive == JOB_EXCLUSIVE_TOPO))
		return NULL;

	_count_gpus(step, gpus, max_per_socket);

	if (!*gpus)
		return RESCTRL_GROUP_CPU_ONLY;
	if (*max_per_socket == 1)
		return RESCTRL_GROUP_GPU_ONE_PER_SOCKET;

	return RESCTRL_GROUP_GPU_TWO_PER_SOCKET;
}

extern int init(void)
{
	debug("%s loaded", plugin_name);

	if (running_in_slurmd())
		return _slurmd_init();
	if (running_in_slurmstepd())
		return _stepd_init();

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	resctrl_conf_free(resctrl_conf);
	resctrl_conf = NULL;

	debug("%s unloaded", plugin_name);
}

extern int task_p_slurmd_batch_request(batch_job_launch_msg_t *req)
{
	return SLURM_SUCCESS;
}

extern int task_p_slurmd_launch_request(launch_tasks_request_msg_t *req,
					uint32_t node_id, char **err_msg)
{
	return SLURM_SUCCESS;
}

/*
 * The thread that forks the tasks joins the group before doing so, so that
 * every task inherits the membership.
 */
extern int task_p_pre_setuid(stepd_step_rec_t *step)
{
	uint32_t gpus = 0, max_per_socket = 0;
	char *status = NULL;
	int rc = EINVAL;

	if (!stepd_enabled)
		return SLURM_SUCCESS;

	if (!(step_group = _select_group(step, &gpus, &max_per_socket))) {
		info("%ps not confined, the job owns the whole node",
		     &step->step_id);
		return SLURM_SUCCESS;
	}

	if (!resctrl_fs_group_exists(step_group)) {
		error("resctrl group %s is not present on this node",
		      step_group);
		return SLURM_ERROR;
	}

	/* Pid 0 is the calling thread (ourselves, slurmstepd). */
	if ((rc = resctrl_fs_move_pid(step_group, 0))) {
		status = resctrl_fs_last_cmd_status();
		error("cannot move %ps into resctrl group %s: %s (%s)",
		      &step->step_id, step_group, slurm_strerror(rc),
		      (status ? status : "no status"));
		xfree(status);
		return SLURM_ERROR;
	}

	info("%ps confined to resctrl group %s, the job has %u GPU(s) on this node and at most %u on one socket",
	     &step->step_id, step_group, gpus, max_per_socket);

	return SLURM_SUCCESS;
}

extern int task_p_pre_launch_priv(stepd_step_rec_t *step, uint32_t node_tid,
				  uint32_t global_tid)
{
	return SLURM_SUCCESS;
}

extern int task_p_pre_launch(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

extern int task_p_post_term(stepd_step_rec_t *step,
			    stepd_step_task_info_t *task)
{
	return SLURM_SUCCESS;
}

extern int task_p_post_step(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

extern int task_p_add_pid(pid_t pid)
{
	char *status = NULL;
	int rc = EINVAL;

	if (!stepd_enabled || !step_group)
		return SLURM_SUCCESS;

	if (!resctrl_fs_group_exists(step_group)) {
		error("resctrl group %s is not present on this node",
		      step_group);
		return SLURM_ERROR;
	}

	rc = resctrl_fs_move_pid(step_group, pid);

	if ((rc == ESRCH) || (rc == ENOENT))
		return SLURM_SUCCESS;
	if (rc) {
		status = resctrl_fs_last_cmd_status();
		error("cannot move pid %d into resctrl group %s: %s (%s)",
		      (int) pid, step_group, slurm_strerror(rc),
		      (status ? status : "no status"));
		xfree(status);
		return SLURM_ERROR;
	}

	debug("moved pid %d into resctrl group %s", (int) pid, step_group);

	return SLURM_SUCCESS;
}

extern int task_p_update_mem_limit(stepd_step_rec_t *step, uint64_t new_job_mem,
				   uint64_t new_step_mem)
{
	return SLURM_SUCCESS;
}
