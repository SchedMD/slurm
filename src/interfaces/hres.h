/*****************************************************************************\
 *  hres.h - Hierarchical resource (HRES) plugin interface.
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

#ifndef _INTERFACES_HRES_H
#define _INTERFACES_HRES_H

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/slurmctld/licenses.h"

/*
 * One context per hierarchical resource that configures a plugin. Multiple
 * contexts may be backed by the same plugin type.
 */
typedef struct {
	list_t *config; /* list of hres_config_t, read-only for plugin */
	uint16_t hres_id; /* HRES this context serves */
	int idx; /* index into ops[]/g_context[] */
	time_t last_maintenance; /* time of last maintenance run, 0 if never */
	uint32_t maintenance_cycle; /* seconds between maintenance runs */
	uint32_t maintenance_timeout; /* max seconds for a maintenance run */
	char *name; /* hierarchical resource name */
	void *plugin_ctx; /* per-context state, alloc/free'd by the plugin */
} hres_ctx_t;

/* Job identification handed to the per job plugin callbacks. */
typedef struct {
	char *nodes; /* nodes allocated to the job */
	slurm_step_id_t step_id; /* job identification, carries the sluid */
	uint32_t user_id;
} hres_job_t;

/* One layer a job holds, as sent to its nodes. */
typedef struct {
	uint32_t count; /* amount charged against this layer */
	char *layer_name;
	char *nodes; /* job's nodes under this layer, frozen at prep build */
} hres_prep_layer_t;

/*
 * Everything a node needs to run a job's callbacks for one hierarchical
 * resource. resources.yaml is only read by the controller, so this travels in
 * the job credential.
 */
typedef struct {
	list_t *config; /* list of hres_config_t, the plugin configuration */
	uint16_t hres_id; /* DON'T PACK, only meaningful to the controller */
	list_t *layers; /* list of hres_prep_layer_t the job holds */
	char *name; /* hierarchical resource name */
	void *payload; /* opaque blob from the controller pre-run */
	uint32_t payload_size;
	uint32_t plugin_id; /* plugin serving the HRES, routes node callbacks */
} hres_prep_t;

/* Free a hres_prep_layer_t, suitable as a list destructor. */
extern void hres_prep_layer_free(void *x);

/* Free a hres_prep_t, suitable as a list destructor. */
extern void hres_prep_free(void *x);

/*
 * Deep copy a list of hres_prep_t.
 *
 * IN src - list to copy, may be NULL
 * RET list of hres_prep_t or NULL, caller must FREE_NULL_LIST()
 */
extern list_t *hres_prep_copy_list(list_t *src);

/* Pack a list of hres_prep_t. */
extern int hres_prep_pack_list(list_t *in, buf_t *buffer,
			       uint16_t protocol_version);

/*
 * Unpack a list of hres_prep_t packed by hres_prep_pack_list().
 *
 * OUT out - list of hres_prep_t, caller must FREE_NULL_LIST()
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
extern int hres_prep_unpack_list(list_t **out, buf_t *buffer,
				 uint16_t protocol_version);
/*
 * Initialize the HRES plugin interface and load the plugins. May be called
 * again to load plugin types referenced by a new configuration.
 *
 * IN hres_recs - cluster license list (list_t of licenses_t); only the plugin
 *		  types it references are loaded. NULL loads every installed
 *		  HRES plugin.
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_init(list_t *hres_recs);

/*
 * Create one context per hierarchical resource that defines a plugin and call
 * each plugin's create_context callback. A resource without a plugin is
 * ignored. The plugin allocates and owns the context's plugin_ctx.
 *
 * Any existing context is destroyed first, so this may be called again to pick
 * up a new configuration.
 *
 * IN hres_recs - cluster license list (list_t of licenses_t); records whose
 *		  hres_rec.plugin_type is set define an HRES plugin context
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_create_context(list_t *hres_recs);

/*
 * Destroy all contexts by calling each plugin's destroy_context callback. The
 * plugin frees the context's plugin_ctx.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_destroy_context(void);

/*
 * Terminate the HRES plugin interface and unload the plugins.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_fini(void);

/*
 * Start the maintenance thread that periodically invokes each context's
 * plugin maintenance callback. A no-op when no context is configured.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_maintenance_start(void);

/*
 * Stop and join the maintenance thread. Safe to call when not running.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_maintenance_stop(void);

/*
 * Stop the maintenance thread and wait until the background work of every
 * plugin has finished and been applied to slurmctld state. No controller
 * callback may start after this. Must be called without slurmctld locks, the
 * plugins may need them to finish.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_controller_stop(void);

/*
 * Describe the job's HRES on job_ptr->hres_prep and run the controller pre-run
 * callback of every plugin it draws HRES from, in the order the resources are
 * defined in resources.yaml. Each plugin may fill in the payload its own nodes
 * receive. Stops at the first failure. Runs after the job is scheduled and
 * before its nodes are allocated.
 *
 * IN job_ptr - job to run the callbacks for
 * OUT err_msg - plugin supplied failure reason, caller must xfree()
 * RET SLURM_SUCCESS, ESLURM_HRES_REMOTE_FAIL to leave the job pending and
 *	retry later, or any other error to hold the job
 */
extern int hres_g_controller_job_prerun(job_record_t *job_ptr, char **err_msg);

/*
 * Run the controller epilog callback of every plugin the job draws HRES from,
 * in the reverse order of the pre-runs, passing each the prep built by
 * hres_g_controller_job_prerun(). Every callback runs, even after one of them
 * fails. Runs once the job's node epilogs have all completed and before its
 * HRES are returned.
 *
 * IN job_ptr - job to run the callbacks for
 * RET SLURM_SUCCESS or SLURM_ERROR if any callback failed
 */
extern int hres_g_controller_job_epilog(job_record_t *job_ptr);

/*
 * Run the node prolog callback of every plugin the job draws HRES from, in the
 * order the resources are defined in resources.yaml. Every callback runs, even
 * after one of them fails, and the most significant failure is returned.
 *
 * IN prep_list - list of hres_prep_t from the job credential
 * IN job - job the prolog runs for
 * RET SLURM_SUCCESS, ESLURM_HRES_REMOTE_FAIL to requeue the job or any other
 *	error to requeue it and drain the node
 */
extern int hres_g_node_job_prolog(list_t *prep_list, hres_job_t *job);

/*
 * Run the node epilog callback of every plugin the job draws HRES from, in the
 * reverse order of the prologs. Every callback runs, even after one of them
 * fails, and even when the matching prolog failed. The most significant
 * failure is returned.
 *
 * IN prep_list - list of hres_prep_t from the job credential
 * IN job - job the epilog runs for
 * RET SLURM_SUCCESS, ESLURM_HRES_REMOTE_FAIL to leave the node up or any other
 *	error to drain it
 */
extern int hres_g_node_job_epilog(list_t *prep_list, hres_job_t *job);

/*
 * Run the task prolog callback of every plugin the job draws HRES from, in the
 * order the resources are defined in resources.yaml. Every callback runs, even
 * after one of them fails, and the most significant failure is returned.
 *
 * Runs in the task's forked child, still privileged, before it execs.
 *
 * IN prep_list - list of hres_prep_t from the job credential
 * IN job - job the task belongs to
 * IN node_tid - node local task id
 * IN global_tid - task id within the step
 * RET SLURM_SUCCESS or a failure returned by one of the callbacks, which fails
 *	the step
 */
extern int hres_g_node_task_prolog(list_t *prep_list, hres_job_t *job,
				   uint32_t node_tid, uint32_t global_tid);

/*
 * Run the task epilog callback of every plugin the job draws HRES from, in the
 * reverse order of the prologs. Every callback runs, even after one of them
 * fails, and even when the matching prolog failed. The most significant
 * failure is returned.
 *
 * Runs in slurmstepd once the task has exited.
 *
 * IN prep_list - list of hres_prep_t from the job credential
 * IN job - job the task belongs to
 * IN node_tid - node local task id
 * IN global_tid - task id within the step
 * RET SLURM_SUCCESS or a failure returned by one of the callbacks
 */
extern int hres_g_node_task_epilog(list_t *prep_list, hres_job_t *job,
				   uint32_t node_tid, uint32_t global_tid);

#endif /* !_INTERFACES_HRES_H */
