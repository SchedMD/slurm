/*****************************************************************************\
 *  job_record.h - JOB parameters and data structures
 *****************************************************************************
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

#include "job_record.h"

#include "src/common/port_mgr.h"
#include "src/common/assoc_mgr.h"
#include "src/common/sluid.h"
#include "src/common/slurm_protocol_pack.h"

#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"

#include "src/stepmgr/stepmgr.h"

#define DETAILS_FLAG 0xdddd

list_t *purge_files_list = NULL; /* list of job ids to purge files of */

typedef struct {
	int node_index;
	int node_count;
} node_inx_cnt_t;

extern job_record_t *job_record_create(void)
{
	job_record_t *job_ptr = xmalloc(sizeof(*job_ptr));
	job_details_t *detail_ptr = xmalloc(sizeof(*detail_ptr));

	job_ptr->magic = JOB_MAGIC;
	job_ptr->array_task_id = NO_VAL;
	job_ptr->details = detail_ptr;
	job_ptr->prio_factors = xmalloc(sizeof(priority_factors_t));
	job_ptr->site_factor = NICE_OFFSET;
	job_ptr->step_list = list_create(free_step_record);

	detail_ptr->magic = DETAILS_MAGIC;
	detail_ptr->submit_time = time(NULL);
	job_ptr->requid = -1; /* force to -1 for sacct to know this
			       * hasn't been set yet  */
	job_ptr->billable_tres = (double)NO_VAL;

	return job_ptr;
}

/* free_step_record - delete a step record's data structures */
extern void free_step_record(void *x)
{
	step_record_t *step_ptr = (step_record_t *) x;
	xassert(step_ptr);
	xassert(step_ptr->magic == STEP_MAGIC);
/*
 * FIXME: If job step record is preserved after completion,
 * the switch_g_job_step_complete() must be called upon completion
 * and not upon record purging. Presently both events occur simultaneously.
 */
	if (step_ptr->switch_step) {
		if (step_ptr->step_layout)
			switch_g_job_step_complete(
				step_ptr->switch_step,
				step_ptr->step_layout->node_list);
		switch_g_free_stepinfo(step_ptr->switch_step);
	}
	resv_port_step_free(step_ptr);

	xfree(step_ptr->container);
	xfree(step_ptr->container_id);
	xfree(step_ptr->host);
	xfree(step_ptr->name);
	slurm_step_layout_destroy(step_ptr->step_layout);
	jobacctinfo_destroy(step_ptr->jobacct);
	FREE_NULL_BITMAP(step_ptr->core_bitmap_job);
	xfree(step_ptr->cpu_alloc_reps);
	xfree(step_ptr->cpu_alloc_values);
	FREE_NULL_BITMAP(step_ptr->exit_node_bitmap);
	FREE_NULL_BITMAP(step_ptr->step_node_bitmap);
	xfree(step_ptr->resv_port_array);
	xfree(step_ptr->resv_ports);
	xfree(step_ptr->network);
	FREE_NULL_LIST(step_ptr->gres_list_alloc);
	FREE_NULL_LIST(step_ptr->gres_list_req);
	select_g_select_jobinfo_free(step_ptr->select_jobinfo);
	xfree(step_ptr->tres_alloc_str);
	xfree(step_ptr->tres_fmt_alloc_str);
	xfree(step_ptr->cpus_per_tres);
	xfree(step_ptr->mem_per_tres);
	xfree(step_ptr->submit_line);
	xfree(step_ptr->tres_bind);
	xfree(step_ptr->tres_freq);
	xfree(step_ptr->tres_per_step);
	xfree(step_ptr->tres_per_node);
	xfree(step_ptr->tres_per_socket);
	xfree(step_ptr->tres_per_task);
	xfree(step_ptr->memory_allocated);
	step_ptr->magic = ~STEP_MAGIC;
	xfree(step_ptr);
}

/* Clear a job's GRES details per node strings, rebuilt later on demand */
static void _clear_job_gres_details(job_record_t *job_ptr)
{
	int i;

	xfree(job_ptr->gres_used);
	for (i = 0; i < job_ptr->gres_detail_cnt; i++)
		xfree(job_ptr->gres_detail_str[i]);
	xfree(job_ptr->gres_detail_str);
	job_ptr->gres_detail_cnt = 0;
}

static void _delete_job_details_members(job_details_t *detail_ptr)
{
	int i;

	if (!detail_ptr)
		return;

	xassert(detail_ptr->magic == DETAILS_MAGIC);

	xfree(detail_ptr->acctg_freq);
	for (i=0; i<detail_ptr->argc; i++)
		xfree(detail_ptr->argv[i]);
	xfree(detail_ptr->argv);
	xfree(detail_ptr->cpu_bind);
	free_cron_entry(detail_ptr->crontab_entry);
	FREE_NULL_LIST(detail_ptr->depend_list);
	xfree(detail_ptr->dependency);
	xfree(detail_ptr->orig_dependency);
	xfree(detail_ptr->env_hash);
	for (i=0; i<detail_ptr->env_cnt; i++)
		xfree(detail_ptr->env_sup[i]);
	xfree(detail_ptr->env_sup);
	xfree(detail_ptr->std_err);
	FREE_NULL_BITMAP(detail_ptr->exc_node_bitmap);
	xfree(detail_ptr->exc_nodes);
	FREE_NULL_LIST(detail_ptr->feature_list);
	xfree(detail_ptr->features);
	xfree(detail_ptr->cluster_features);
	FREE_NULL_BITMAP(detail_ptr->job_size_bitmap);
	xfree(detail_ptr->std_in);
	xfree(detail_ptr->mc_ptr);
	xfree(detail_ptr->mem_bind);
	FREE_NULL_LIST(detail_ptr->prefer_list);
	xfree(detail_ptr->prefer);
	xfree(detail_ptr->qos_req);
	xfree(detail_ptr->req_context);
	xfree(detail_ptr->std_out);
	xfree(detail_ptr->submit_line);
	FREE_NULL_BITMAP(detail_ptr->req_node_bitmap);
	xfree(detail_ptr->req_nodes);
	xfree(detail_ptr->script);
	xfree(detail_ptr->script_hash);
	xfree(detail_ptr->arbitrary_tpn);
	xfree(detail_ptr->work_dir);
	xfree(detail_ptr->x11_magic_cookie);
	xfree(detail_ptr->x11_target);
}

/*
 * _delete_job_details - delete a job's detail record and clear it's pointer
 * IN job_entry - pointer to job_record to clear the record of
 */
static void _delete_job_details(job_record_t *job_entry)
{
	/*
	 * Queue up job to have the batch script and environment deleted.
	 * This is handled by a separate thread to limit the amount of
	 * time purge_old_job needs to spend holding locks.
	 */
	if (IS_JOB_FINISHED(job_entry) && purge_files_list) {
		uint32_t *job_id = xmalloc(sizeof(uint32_t));
		*job_id = job_entry->job_id;
		list_enqueue(purge_files_list, job_id);
	}

	_delete_job_details_members(job_entry->details);
	xfree(job_entry->details);	/* Must be last */
}

extern void job_record_delete(void *job_entry)
{
	job_record_t *job_ptr = job_entry;

	if (!job_entry)
		return;

	xassert(job_ptr->magic == JOB_MAGIC);

	_delete_job_details(job_ptr);
	xfree(job_ptr->account);
	xfree(job_ptr->admin_comment);
	xfree(job_ptr->alias_list);
	xfree(job_ptr->alloc_node);
	job_record_free_null_array_recs(job_ptr);
	if (job_ptr->array_recs) {
		FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);
		xfree(job_ptr->array_recs->task_id_str);
		xfree(job_ptr->array_recs);
	}
	xfree(job_ptr->batch_features);
	xfree(job_ptr->batch_host);
	xfree(job_ptr->burst_buffer);
	xfree(job_ptr->burst_buffer_state);
	xfree(job_ptr->comment);
	xfree(job_ptr->container);
	xfree(job_ptr->clusters);
	xfree(job_ptr->cpus_per_tres);
	xfree(job_ptr->extra);
	FREE_NULL_EXTRA_CONSTRAINTS(job_ptr->extra_constraints);
	xfree(job_ptr->failed_node);
	job_record_free_fed_details(&job_ptr->fed_details);
	free_job_resources(&job_ptr->job_resrcs);
	_clear_job_gres_details(job_ptr);
	xfree(job_ptr->gres_used);
	FREE_NULL_LIST(job_ptr->gres_list_req);
	FREE_NULL_LIST(job_ptr->gres_list_req_accum);
	FREE_NULL_LIST(job_ptr->gres_list_alloc);
	FREE_NULL_IDENTITY(job_ptr->id);
	xfree(job_ptr->licenses);
	xfree(job_ptr->lic_req);
	FREE_NULL_LIST(job_ptr->license_list);
	xfree(job_ptr->limit_set.tres);
	xfree(job_ptr->mail_user);
	xfree(job_ptr->mcs_label);
	xfree(job_ptr->mem_per_tres);
	xfree(job_ptr->name);
	xfree(job_ptr->network);
	xfree(job_ptr->node_addrs);
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	FREE_NULL_BITMAP(job_ptr->node_bitmap_cg);
	FREE_NULL_BITMAP(job_ptr->node_bitmap_pr);
	FREE_NULL_BITMAP(job_ptr->node_bitmap_preempt);
	xfree(job_ptr->nodes);
	xfree(job_ptr->nodes_completing);
	xfree(job_ptr->nodes_pr);
	xfree(job_ptr->origin_cluster);
	if (job_ptr->het_details && job_ptr->het_job_id) {
		/* xfree struct if hetjob leader and NULL ptr otherwise. */
		if (job_ptr->het_job_offset == 0)
			xfree(job_ptr->het_details);
		else
			job_ptr->het_details = NULL;
	}
	xfree(job_ptr->het_job_id_set);
	FREE_NULL_LIST(job_ptr->het_job_list);
	xfree(job_ptr->partition);
	FREE_NULL_LIST(job_ptr->part_ptr_list);
	if (job_ptr->part_prio) {
		xfree(job_ptr->part_prio->priority_array);
		xfree(job_ptr->part_prio->priority_array_names);
		xfree(job_ptr->part_prio);
	}
	slurm_destroy_priority_factors(job_ptr->prio_factors);
	FREE_NULL_LIST(job_ptr->qos_list);
	xfree(job_ptr->resp_host);
	FREE_NULL_LIST(job_ptr->resv_list);
	xfree(job_ptr->resv_name);
	xfree(job_ptr->resv_ports);
	xfree(job_ptr->resv_port_array);
	xfree(job_ptr->sched_nodes);
	for (int i = 0; i < job_ptr->spank_job_env_size; i++)
		xfree(job_ptr->spank_job_env[i]);
	xfree(job_ptr->spank_job_env);
	xfree(job_ptr->state_desc);
	FREE_NULL_LIST(job_ptr->step_list);
	xfree(job_ptr->system_comment);
	xfree(job_ptr->tres_alloc_cnt);
	xfree(job_ptr->tres_alloc_str);
	xfree(job_ptr->tres_bind);
	xfree(job_ptr->tres_freq);
	xfree(job_ptr->tres_fmt_alloc_str);
	xfree(job_ptr->tres_per_job);
	xfree(job_ptr->tres_per_node);
	xfree(job_ptr->tres_per_socket);
	xfree(job_ptr->tres_per_task);
	xfree(job_ptr->tres_req_cnt);
	xfree(job_ptr->tres_req_str);
	xfree(job_ptr->tres_fmt_req_str);
	select_g_select_jobinfo_free(job_ptr->select_jobinfo);
	xfree(job_ptr->user_name);
	xfree(job_ptr->wckey);

	job_ptr->job_id = 0;
	/* make sure we don't delete record twice */
	job_ptr->magic = ~JOB_MAGIC;
	xfree(job_ptr);
}

extern void job_record_free_null_array_recs(job_record_t *job_ptr)
{
	if (!job_ptr || !job_ptr->array_recs)
		return;

	FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);
	xfree(job_ptr->array_recs->task_id_str);
	xfree(job_ptr->array_recs);
}

extern void job_record_free_fed_details(job_fed_details_t **fed_details_pptr)
{
	job_fed_details_t *fed_details_ptr = *fed_details_pptr;

	if (fed_details_ptr) {
		xfree(fed_details_ptr->origin_str);
		xfree(fed_details_ptr->siblings_active_str);
		xfree(fed_details_ptr->siblings_viable_str);
		xfree(fed_details_ptr);
		*fed_details_pptr = NULL;
	}
}

/* Pack the data for a specific job step record */
extern int pack_ctld_job_step_info(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	pack_step_args_t *args = (pack_step_args_t *) arg;
	buf_t *buffer = args->buffer;
	uint32_t task_cnt, cpu_cnt;
	char *node_list = NULL;
	time_t begin_time, run_time;
	bitstr_t *pack_bitstr;

#if defined HAVE_FRONT_END
	/* On front-end systems, the steps only execute on one node.
	 * We need to make them appear like they are running on the job's
	 * entire allocation (which they really are). */
	task_cnt = step_ptr->job_ptr->cpu_cnt;
	node_list = step_ptr->job_ptr->nodes;
	pack_bitstr = step_ptr->job_ptr->node_bitmap;

	if (step_ptr->job_ptr->total_cpus)
		cpu_cnt = step_ptr->job_ptr->total_cpus;
	else if (step_ptr->job_ptr->details)
		cpu_cnt = step_ptr->job_ptr->details->min_cpus;
	else
		cpu_cnt = step_ptr->job_ptr->cpu_cnt;
#else
	pack_bitstr = step_ptr->step_node_bitmap;
	if (step_ptr->step_layout) {
		task_cnt = step_ptr->step_layout->task_cnt;
		node_list = step_ptr->step_layout->node_list;
	} else {
		task_cnt = step_ptr->cpu_count;
		node_list = step_ptr->job_ptr->nodes;
	}
	cpu_cnt = step_ptr->cpu_count;
#endif

	if (args->proto_version >= SLURM_23_11_PROTOCOL_VERSION) {
		pack32(step_ptr->job_ptr->array_job_id, buffer);
		pack32(step_ptr->job_ptr->array_task_id, buffer);

		pack_step_id(&step_ptr->step_id, buffer, args->proto_version);

		pack32(step_ptr->job_ptr->user_id, buffer);
		pack32(cpu_cnt, buffer);
		pack32(step_ptr->cpu_freq_min, buffer);
		pack32(step_ptr->cpu_freq_max, buffer);
		pack32(step_ptr->cpu_freq_gov, buffer);
		pack32(task_cnt, buffer);
		if (step_ptr->step_layout)
			pack32(step_ptr->step_layout->task_dist, buffer);
		else
			pack32((uint32_t) SLURM_DIST_UNKNOWN, buffer);
		pack32(step_ptr->time_limit, buffer);
		pack32(step_ptr->state, buffer);
		pack32(step_ptr->srun_pid, buffer);

		pack_time(step_ptr->start_time, buffer);
		if (IS_JOB_SUSPENDED(step_ptr->job_ptr)) {
			run_time = step_ptr->pre_sus_time;
		} else {
			begin_time = MAX(step_ptr->start_time,
					 step_ptr->job_ptr->suspend_time);
			run_time = step_ptr->pre_sus_time +
				difftime(time(NULL), begin_time);
		}
		pack_time(run_time, buffer);

		packstr(slurm_conf.cluster_name, buffer);
		packstr(step_ptr->container, buffer);
		packstr(step_ptr->container_id, buffer);
		if (step_ptr->job_ptr->part_ptr)
			packstr(step_ptr->job_ptr->part_ptr->name, buffer);
		else
			packstr(step_ptr->job_ptr->partition, buffer);
		packstr(step_ptr->host, buffer);
		packstr(step_ptr->resv_ports, buffer);
		packstr(node_list, buffer);
		packstr(step_ptr->name, buffer);
		packstr(step_ptr->network, buffer);
		pack_bit_str_hex(pack_bitstr, buffer);
		packstr(step_ptr->tres_fmt_alloc_str, buffer);
		pack16(step_ptr->start_protocol_ver, buffer);

		packstr(step_ptr->cpus_per_tres, buffer);
		packstr(step_ptr->mem_per_tres, buffer);
		packstr(step_ptr->submit_line, buffer);
		packstr(step_ptr->tres_bind, buffer);
		packstr(step_ptr->tres_freq, buffer);
		packstr(step_ptr->tres_per_step, buffer);
		packstr(step_ptr->tres_per_node, buffer);
		packstr(step_ptr->tres_per_socket, buffer);
		packstr(step_ptr->tres_per_task, buffer);
	} else if (args->proto_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(step_ptr->job_ptr->array_job_id, buffer);
		pack32(step_ptr->job_ptr->array_task_id, buffer);

		pack_step_id(&step_ptr->step_id, buffer, args->proto_version);

		pack32(step_ptr->job_ptr->user_id, buffer);
		pack32(cpu_cnt, buffer);
		pack32(step_ptr->cpu_freq_min, buffer);
		pack32(step_ptr->cpu_freq_max, buffer);
		pack32(step_ptr->cpu_freq_gov, buffer);
		pack32(task_cnt, buffer);
		if (step_ptr->step_layout)
			pack32(step_ptr->step_layout->task_dist, buffer);
		else
			pack32((uint32_t) SLURM_DIST_UNKNOWN, buffer);
		pack32(step_ptr->time_limit, buffer);
		pack32(step_ptr->state, buffer);
		pack32(step_ptr->srun_pid, buffer);

		pack_time(step_ptr->start_time, buffer);
		if (IS_JOB_SUSPENDED(step_ptr->job_ptr)) {
			run_time = step_ptr->pre_sus_time;
		} else {
			begin_time = MAX(step_ptr->start_time,
					 step_ptr->job_ptr->suspend_time);
			run_time = step_ptr->pre_sus_time +
				difftime(time(NULL), begin_time);
		}
		pack_time(run_time, buffer);

		packstr(slurm_conf.cluster_name, buffer);
		packstr(step_ptr->container, buffer);
		packstr(step_ptr->container_id, buffer);
		if (step_ptr->job_ptr->part_ptr)
			packstr(step_ptr->job_ptr->part_ptr->name, buffer);
		else
			packstr(step_ptr->job_ptr->partition, buffer);
		packstr(step_ptr->host, buffer);
		packstr(step_ptr->resv_ports, buffer);
		packstr(node_list, buffer);
		packstr(step_ptr->name, buffer);
		packstr(step_ptr->network, buffer);
		pack_bit_str_hex(pack_bitstr, buffer);
		packstr(step_ptr->tres_fmt_alloc_str, buffer);
		pack16(step_ptr->start_protocol_ver, buffer);

		packstr(step_ptr->cpus_per_tres, buffer);
		packstr(step_ptr->mem_per_tres, buffer);
		packstr(step_ptr->submit_line, buffer);
		packstr(step_ptr->tres_bind, buffer);
		packstr(step_ptr->tres_freq, buffer);
		packstr(step_ptr->tres_per_step, buffer);
		packstr(step_ptr->tres_per_node, buffer);
		packstr(step_ptr->tres_per_socket, buffer);
		packstr(step_ptr->tres_per_task, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, args->proto_version);
	}

	args->steps_packed++;

	return 0;
}

static void _pack_acct_policy_limit(acct_policy_limit_set_t *limit_set,
				    buf_t *buffer, int tres_cnt,
				    uint16_t protocol_version)
{
	xassert(limit_set);

	pack16(limit_set->qos, buffer);
	pack16(limit_set->time, buffer);
	pack16_array(limit_set->tres, tres_cnt, buffer);
}

/*
 * _dump_job_details - dump the state of a specific job details to
 *	a buffer
 * IN detail_ptr - pointer to job details for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 *
 * WARNING: this contains sensitive data. e.g., the x11_magic_cookie.
 * DO NOT use this in client-facing RPCs.
 */
static void _dump_job_details(job_details_t *detail_ptr, buf_t *buffer,
			      uint16_t protocol_version)
{
	/*
	 * Some job fields can change in the course of scheduling, so we
	 * report the original values supplied by the user rather than
	 * an intermediate value that might be set by our scheduling
	 * logic (e.g. to enforce a partition, association or QOS limit).
	 *
	 * Fields subject to change and their original values are as follows:
	 * min_cpus		orig_min_cpus
	 * max_cpus		orig_max_cpus
	 * cpus_per_task 	orig_cpus_per_task
	 * pn_min_cpus		orig_pn_min_cpus
	 * pn_min_memory	orig_pn_min_memory
	 * dependency		orig_dependency
	 */
	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		job_record_pack_details_common(detail_ptr, buffer,
					       protocol_version);

		pack32(detail_ptr->orig_min_cpus, buffer);
		pack32(detail_ptr->orig_max_cpus, buffer);
		pack32(detail_ptr->min_nodes, buffer);
		pack32(detail_ptr->max_nodes, buffer);
		pack32(detail_ptr->num_tasks, buffer);

		packstr(detail_ptr->acctg_freq, buffer);
		pack16(detail_ptr->contiguous, buffer);
		pack16(detail_ptr->core_spec, buffer);
		pack16(detail_ptr->orig_cpus_per_task, buffer);
		pack32(detail_ptr->task_dist, buffer);

		pack8(detail_ptr->share_res, buffer);
		pack8(detail_ptr->whole_node, buffer);

		packstr(detail_ptr->cpu_bind, buffer);
		pack16(detail_ptr->cpu_bind_type, buffer);
		packstr(detail_ptr->mem_bind, buffer);
		pack16(detail_ptr->mem_bind_type, buffer);

		pack8(detail_ptr->open_mode, buffer);
		pack8(detail_ptr->overcommit, buffer);
		pack8(detail_ptr->prolog_running, buffer);

		pack32(detail_ptr->orig_pn_min_cpus, buffer);
		pack64(detail_ptr->orig_pn_min_memory, buffer);
		pack16(detail_ptr->oom_kill_step, buffer);
		pack32(detail_ptr->pn_min_tmp_disk, buffer);

		packstr(detail_ptr->req_nodes, buffer);
		packstr(detail_ptr->exc_nodes, buffer);
		packstr(detail_ptr->features, buffer);
		packstr(detail_ptr->prefer, buffer);
		if (detail_ptr->features_use == detail_ptr->features)
			pack8(1, buffer);
		else if (detail_ptr->features_use == detail_ptr->prefer)
			pack8(2, buffer);
		else
			pack8(0, buffer);
		pack_dep_list(detail_ptr->depend_list, buffer,
			      protocol_version);
		packstr(detail_ptr->orig_dependency, buffer);

		packstr(detail_ptr->std_err, buffer);
		packstr(detail_ptr->std_in, buffer);
		packstr(detail_ptr->std_out, buffer);
		packstr(detail_ptr->submit_line, buffer);

		pack_multi_core_data(detail_ptr->mc_ptr, buffer,
				     protocol_version);
		packstr_array(detail_ptr->argv, detail_ptr->argc, buffer);
		packstr_array(detail_ptr->env_sup, detail_ptr->env_cnt, buffer);

		pack_cron_entry(detail_ptr->crontab_entry,
				protocol_version,
				buffer);
		packstr(detail_ptr->env_hash, buffer);
		packstr(detail_ptr->script_hash, buffer);
		pack16(detail_ptr->segment_size, buffer);
		pack16(detail_ptr->resv_port_cnt, buffer);
		packstr(detail_ptr->qos_req, buffer);

		pack16(detail_ptr->x11, buffer);
		packstr(detail_ptr->x11_magic_cookie, buffer);
		packstr(detail_ptr->x11_target, buffer);
		pack16(detail_ptr->x11_target_port, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {

		pack32(detail_ptr->orig_min_cpus, buffer);	/* subject to change */
		pack32(detail_ptr->orig_max_cpus, buffer);	/* subject to change */
		pack32(detail_ptr->min_nodes, buffer);
		pack32(detail_ptr->max_nodes, buffer);
		pack32(detail_ptr->num_tasks, buffer);

		packstr(detail_ptr->acctg_freq, buffer);
		pack16(detail_ptr->contiguous, buffer);
		pack16(detail_ptr->core_spec, buffer);
		pack16(detail_ptr->orig_cpus_per_task, buffer);	/* subject to change */
		pack32(detail_ptr->nice, buffer);
		pack16(detail_ptr->ntasks_per_node, buffer);
		pack16(detail_ptr->requeue, buffer);
		pack32(detail_ptr->task_dist, buffer);

		pack8(detail_ptr->share_res, buffer);
		pack8(detail_ptr->whole_node, buffer);

		packstr(detail_ptr->cpu_bind, buffer);
		pack16(detail_ptr->cpu_bind_type, buffer);
		packstr(detail_ptr->mem_bind, buffer);
		pack16(detail_ptr->mem_bind_type, buffer);

		pack8(detail_ptr->open_mode, buffer);
		pack8(detail_ptr->overcommit, buffer);
		pack8(detail_ptr->prolog_running, buffer);

		pack32(detail_ptr->orig_pn_min_cpus, buffer);	/* subject to change */
		pack64(detail_ptr->orig_pn_min_memory, buffer);	/* subject to change */
		pack32(detail_ptr->pn_min_tmp_disk, buffer);
		pack32(detail_ptr->cpu_freq_min, buffer);
		pack32(detail_ptr->cpu_freq_max, buffer);
		pack32(detail_ptr->cpu_freq_gov, buffer);
		pack_time(detail_ptr->begin_time, buffer);
		pack_time(detail_ptr->accrue_time, buffer);
		pack_time(detail_ptr->submit_time, buffer);

		packstr(detail_ptr->req_nodes, buffer);
		packstr(detail_ptr->exc_nodes, buffer);
		packstr(detail_ptr->features, buffer);
		packstr(detail_ptr->cluster_features, buffer);
		packstr(detail_ptr->prefer, buffer);
		if (detail_ptr->features_use == detail_ptr->features)
			pack8(1, buffer);
		else if (detail_ptr->features_use == detail_ptr->prefer)
			pack8(2, buffer);
		else
			pack8(0, buffer);
		pack_bit_str_hex(detail_ptr->job_size_bitmap, buffer);
		pack_dep_list(detail_ptr->depend_list, buffer,
			      protocol_version);
		packstr(detail_ptr->dependency, buffer);
		packstr(detail_ptr->orig_dependency, buffer);	/* subject to change */

		packstr(detail_ptr->std_err, buffer);
		packstr(detail_ptr->std_in, buffer);
		packstr(detail_ptr->std_out, buffer);
		packstr(detail_ptr->submit_line, buffer);
		packstr(detail_ptr->work_dir, buffer);

		pack_multi_core_data(detail_ptr->mc_ptr, buffer,
				     protocol_version);
		packstr_array(detail_ptr->argv, detail_ptr->argc, buffer);
		packstr_array(detail_ptr->env_sup, detail_ptr->env_cnt, buffer);

		pack_cron_entry(detail_ptr->crontab_entry,
				protocol_version,
				buffer);
		packstr(detail_ptr->env_hash, buffer);
		packstr(detail_ptr->script_hash, buffer);
		pack16(detail_ptr->segment_size, buffer);
		pack16(detail_ptr->resv_port_cnt, buffer);
	}
}

static void _pack_step_state(void *object, uint16_t protocol_version,
			     buf_t *buffer)
{
	step_record_t *step_ptr = object;
	slurm_node_alias_addrs_t *alias_addrs_tmp = NULL;

	pack32(step_ptr->step_id.step_id, buffer);
	pack32(step_ptr->step_id.step_het_comp, buffer);
	pack16(step_ptr->cyclic_alloc, buffer);
	pack32(step_ptr->srun_pid, buffer);
	pack16(step_ptr->port, buffer);
	pack16(step_ptr->cpus_per_task, buffer);
	packstr(step_ptr->container, buffer);
	packstr(step_ptr->container_id, buffer);
	pack16(step_ptr->resv_port_cnt, buffer);
	pack16(step_ptr->state, buffer);
	pack16(step_ptr->start_protocol_ver, buffer);

	pack32(step_ptr->flags, buffer);

	pack32_array(step_ptr->cpu_alloc_reps,
		     step_ptr->cpu_alloc_array_cnt, buffer);
	pack16_array(step_ptr->cpu_alloc_values,
		     step_ptr->cpu_alloc_array_cnt, buffer);
	pack32(step_ptr->cpu_count, buffer);
	pack64(step_ptr->pn_min_memory, buffer);
	pack32(step_ptr->exit_code, buffer);
	if (step_ptr->exit_code != NO_VAL) {
		pack_bit_str_hex(step_ptr->exit_node_bitmap, buffer);
	}
	pack_bit_str_hex(step_ptr->core_bitmap_job, buffer);
	pack32(step_ptr->time_limit, buffer);
	pack32(step_ptr->cpu_freq_min, buffer);
	pack32(step_ptr->cpu_freq_max, buffer);
	pack32(step_ptr->cpu_freq_gov, buffer);

	pack_time(step_ptr->start_time, buffer);
	pack_time(step_ptr->pre_sus_time, buffer);
	pack_time(step_ptr->tot_sus_time, buffer);

	packstr(step_ptr->host,  buffer);
	packstr(step_ptr->resv_ports, buffer);
	packstr(step_ptr->name, buffer);
	packstr(step_ptr->network, buffer);

	(void) gres_step_state_pack(step_ptr->gres_list_req, buffer,
				    &step_ptr->step_id,
				    protocol_version);
	(void) gres_step_state_pack(step_ptr->gres_list_alloc, buffer,
				    &step_ptr->step_id,
				    protocol_version);
	/*
	 * Don't dump alias_addrs
	 */
	if (step_ptr->step_layout) {
		alias_addrs_tmp = step_ptr->step_layout->alias_addrs;
		step_ptr->step_layout->alias_addrs = NULL;
	}
	pack_slurm_step_layout(step_ptr->step_layout, buffer,
			       protocol_version);
	if (step_ptr->step_layout)
		step_ptr->step_layout->alias_addrs = alias_addrs_tmp;

	if (step_ptr->switch_step) {
		pack8(1, buffer);
		switch_g_pack_stepinfo(step_ptr->switch_step, buffer,
				       protocol_version);
	} else
		pack8(0, buffer);

	select_g_select_jobinfo_pack(step_ptr->select_jobinfo, buffer,
				     protocol_version);
	packstr(step_ptr->tres_alloc_str, buffer);
	packstr(step_ptr->tres_fmt_alloc_str, buffer);

	packstr(step_ptr->cpus_per_tres, buffer);
	packstr(step_ptr->mem_per_tres, buffer);
	packstr(step_ptr->submit_line, buffer);
	packstr(step_ptr->tres_bind, buffer);
	packstr(step_ptr->tres_freq, buffer);
	packstr(step_ptr->tres_per_step, buffer);
	packstr(step_ptr->tres_per_node, buffer);
	packstr(step_ptr->tres_per_socket, buffer);
	packstr(step_ptr->tres_per_task, buffer);
	jobacctinfo_pack(step_ptr->jobacct, protocol_version,
	                 PROTOCOL_TYPE_SLURM, buffer);

	if (step_ptr->memory_allocated &&
	    step_ptr->step_layout &&
	    step_ptr->step_layout->node_cnt)
		pack64_array(step_ptr->memory_allocated,
			     step_ptr->step_layout->node_cnt, buffer);
	else
		pack64_array(step_ptr->memory_allocated, 0, buffer);
}

/*
 * dump_job_step_state - dump the state of a specific job step to a buffer,
 *	load with load_step_state
 * IN step_ptr - pointer to job step for which information is to be dumped
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
extern int dump_job_step_state(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	buf_t *buffer = (buf_t *) arg;

	pack16((uint16_t) STEP_FLAG, buffer);

	_pack_step_state(step_ptr, SLURM_PROTOCOL_VERSION, buffer);

	return 0;
}

/*
 * Create a new job step from data in a buffer (as created by
 *	dump_job_step_state)
 * IN/OUT - job_ptr - point to a job for which the step is to be loaded.
 * IN/OUT buffer - location to get data from, pointers advanced
 */
/* NOTE: assoc_mgr tres and assoc read lock must be locked before calling */
extern int load_step_state(job_record_t *job_ptr, buf_t *buffer,
			   uint16_t protocol_version)
{
	step_record_t *step_ptr = NULL;
	bitstr_t *exit_node_bitmap = NULL, *core_bitmap_job = NULL;
	uint8_t uint8_tmp;
	uint16_t cyclic_alloc, port;
	uint16_t start_protocol_ver = SLURM_MIN_PROTOCOL_VERSION;
	uint16_t cpus_per_task, resv_port_cnt, state;
	uint32_t cpu_count, exit_code, srun_pid = 0, flags = 0;
	uint32_t cpu_alloc_array_cnt = 0;
	uint32_t *cpu_alloc_reps = NULL;
	uint16_t *cpu_alloc_values = NULL;
	uint32_t time_limit, cpu_freq_min, cpu_freq_max, cpu_freq_gov;
	uint32_t tmp32;
	uint64_t pn_min_memory;
	uint64_t *memory_allocated = NULL;
	time_t start_time, pre_sus_time, tot_sus_time;
	char *host = NULL, *container = NULL, *container_id = NULL;
	char *core_job = NULL, *submit_line = NULL;
	char *resv_ports = NULL, *name = NULL, *network = NULL;
	char *tres_alloc_str = NULL, *tres_fmt_alloc_str = NULL;
	char *cpus_per_tres = NULL, *mem_per_tres = NULL, *tres_bind = NULL;
	char *tres_freq = NULL, *tres_per_step = NULL, *tres_per_node = NULL;
	char *tres_per_socket = NULL, *tres_per_task = NULL;
	dynamic_plugin_data_t *switch_tmp = NULL;
	slurm_step_layout_t *step_layout = NULL;
	list_t *gres_list_req = NULL, *gres_list_alloc = NULL;
	dynamic_plugin_data_t *select_jobinfo = NULL;
	jobacctinfo_t *jobacct = NULL;
	slurm_step_id_t step_id = {
		.job_id = job_ptr->job_id,
		.step_het_comp = NO_VAL,
	};

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		safe_unpack32(&step_id.step_id, buffer);
		safe_unpack32(&step_id.step_het_comp, buffer);
		safe_unpack16(&cyclic_alloc, buffer);
		safe_unpack32(&srun_pid, buffer);
		safe_unpack16(&port, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpackstr(&container, buffer);
		safe_unpackstr(&container_id, buffer);
		safe_unpack16(&resv_port_cnt, buffer);
		safe_unpack16(&state, buffer);
		safe_unpack16(&start_protocol_ver, buffer);

		safe_unpack32(&flags, buffer);

		safe_unpack32_array(&cpu_alloc_reps,
				    &cpu_alloc_array_cnt, buffer);
		safe_unpack16_array(&cpu_alloc_values, &tmp32, buffer);
		xassert(tmp32 == cpu_alloc_array_cnt);
		safe_unpack32(&cpu_count, buffer);
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack32(&exit_code, buffer);
		if (exit_code != NO_VAL) {
			unpack_bit_str_hex(&exit_node_bitmap, buffer);
		}
		unpack_bit_str_hex(&core_bitmap_job, buffer);

		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);

		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);

		safe_unpackstr(&host, buffer);
		safe_unpackstr(&resv_ports, buffer);
		safe_unpackstr(&name, buffer);
		safe_unpackstr(&network, buffer);

		if (gres_step_state_unpack(&gres_list_req, buffer,
					   &step_id, protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_step_state_unpack(&gres_list_alloc, buffer,
					   &step_id, protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		if (unpack_slurm_step_layout(&step_layout, buffer,
					     protocol_version))
			goto unpack_error;

		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp &&
		    (switch_g_unpack_stepinfo(&switch_tmp, buffer,
					      protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
		safe_unpackstr(&tres_alloc_str, buffer);
		safe_unpackstr(&tres_fmt_alloc_str, buffer);

		safe_unpackstr(&cpus_per_tres, buffer);
		safe_unpackstr(&mem_per_tres, buffer);
		safe_unpackstr(&submit_line, buffer);
		safe_unpackstr(&tres_bind, buffer);
		safe_unpackstr(&tres_freq, buffer);
		safe_unpackstr(&tres_per_step, buffer);
		safe_unpackstr(&tres_per_node, buffer);
		safe_unpackstr(&tres_per_socket, buffer);
		safe_unpackstr(&tres_per_task, buffer);
		if (jobacctinfo_unpack(&jobacct, protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer, true))
			goto unpack_error;
		safe_unpack64_array(&memory_allocated, &tmp32, buffer);
		if (tmp32 == 0)
			xfree(memory_allocated);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&step_id.step_id, buffer);
		safe_unpack32(&step_id.step_het_comp, buffer);
		safe_unpack16(&cyclic_alloc, buffer);
		safe_unpack32(&srun_pid, buffer);
		safe_unpack16(&port, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpackstr(&container, buffer);
		safe_unpackstr(&container_id, buffer);
		safe_unpack16(&resv_port_cnt, buffer);
		safe_unpack16(&state, buffer);
		safe_unpack16(&start_protocol_ver, buffer);

		safe_unpack32(&flags, buffer);

		safe_unpack32(&cpu_count, buffer);
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack32(&exit_code, buffer);
		if (exit_code != NO_VAL) {
			unpack_bit_str_hex(&exit_node_bitmap, buffer);
		}
		unpack_bit_str_hex(&core_bitmap_job, buffer);

		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);

		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);

		safe_unpackstr(&host, buffer);
		safe_unpackstr(&resv_ports, buffer);
		safe_unpackstr(&name, buffer);
		safe_unpackstr(&network, buffer);

		if (gres_step_state_unpack(&gres_list_req, buffer,
					   &step_id, protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_step_state_unpack(&gres_list_alloc, buffer,
					   &step_id, protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		if (unpack_slurm_step_layout(&step_layout, buffer,
					     protocol_version))
			goto unpack_error;

		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp &&
		    (switch_g_unpack_stepinfo(&switch_tmp, buffer,
					      protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
		safe_unpackstr(&tres_alloc_str, buffer);
		safe_unpackstr(&tres_fmt_alloc_str, buffer);

		safe_unpackstr(&cpus_per_tres, buffer);
		safe_unpackstr(&mem_per_tres, buffer);
		safe_unpackstr(&submit_line, buffer);
		safe_unpackstr(&tres_bind, buffer);
		safe_unpackstr(&tres_freq, buffer);

		safe_unpackstr(&tres_per_step, buffer);
		slurm_format_tres_string(&tres_per_step, "gres");

		safe_unpackstr(&tres_per_node, buffer);
		slurm_format_tres_string(&tres_per_node, "gres");

		safe_unpackstr(&tres_per_socket, buffer);
		slurm_format_tres_string(&tres_per_socket, "gres");

		safe_unpackstr(&tres_per_task, buffer);
		slurm_format_tres_string(&tres_per_task, "gres");

		if (jobacctinfo_unpack(&jobacct, protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer, true))
			goto unpack_error;
		safe_unpack64_array(&memory_allocated, &tmp32, buffer);
		if (tmp32 == 0)
			xfree(memory_allocated);
	} else {
		error("load_step_state: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	/* validity test as possible */
	if (cyclic_alloc > 1) {
		error("Invalid data for %pJ StepId=%u: cyclic_alloc=%u",
		      job_ptr, step_id.step_id, cyclic_alloc);
		goto unpack_error;
	}

	step_ptr = create_step_record(job_ptr, start_protocol_ver);
	if (step_ptr == NULL)
		goto unpack_error;

	/* set new values */
	memcpy(&step_ptr->step_id, &step_id, sizeof(step_ptr->step_id));

	step_ptr->container = container;
	step_ptr->container_id = container_id;
	step_ptr->cpu_alloc_array_cnt = cpu_alloc_array_cnt;
	xfree(step_ptr->cpu_alloc_reps);
	step_ptr->cpu_alloc_reps = cpu_alloc_reps;
	cpu_alloc_reps = NULL;
	xfree(step_ptr->cpu_alloc_values);
	step_ptr->cpu_alloc_values = cpu_alloc_values;
	cpu_alloc_values = NULL;
	step_ptr->cpu_count    = cpu_count;
	step_ptr->cpus_per_task= cpus_per_task;
	step_ptr->cyclic_alloc = cyclic_alloc;
	step_ptr->resv_port_cnt= resv_port_cnt;
	step_ptr->resv_ports   = resv_ports;
	step_ptr->memory_allocated = memory_allocated;
	memory_allocated = NULL;
	step_ptr->name         = name;
	step_ptr->network      = network;
	step_ptr->flags        = flags;
	step_ptr->gres_list_req = gres_list_req;
	step_ptr->gres_list_alloc = gres_list_alloc;
	step_ptr->srun_pid     = srun_pid;
	step_ptr->port         = port;
	step_ptr->pn_min_memory= pn_min_memory;
	step_ptr->host         = host;
	host                   = NULL;  /* re-used, nothing left to free */
	step_ptr->start_time   = start_time;
	step_ptr->time_limit   = time_limit;
	step_ptr->pre_sus_time = pre_sus_time;
	step_ptr->tot_sus_time = tot_sus_time;

	if (!select_jobinfo)
		select_jobinfo = select_g_select_jobinfo_alloc();
	step_ptr->select_jobinfo = select_jobinfo;
	select_jobinfo = NULL;

	slurm_step_layout_destroy(step_ptr->step_layout);
	step_ptr->step_layout  = step_layout;

	if ((step_ptr->step_id.step_id == SLURM_EXTERN_CONT) && switch_tmp) {
		switch_g_free_stepinfo(switch_tmp);
		switch_tmp = NULL;
	} else
		step_ptr->switch_step = switch_tmp;

	xfree(step_ptr->tres_alloc_str);
	step_ptr->tres_alloc_str     = tres_alloc_str;
	tres_alloc_str = NULL;

	xfree(step_ptr->cpus_per_tres);
	step_ptr->cpus_per_tres = cpus_per_tres;
	cpus_per_tres = NULL;
	xfree(step_ptr->mem_per_tres);
	step_ptr->mem_per_tres = mem_per_tres;
	mem_per_tres = NULL;
	xfree(step_ptr->submit_line);
	step_ptr->submit_line = submit_line;
	submit_line = NULL;
	xfree(step_ptr->tres_bind);
	step_ptr->tres_bind = tres_bind;
	tres_bind = NULL;
	xfree(step_ptr->tres_freq);
	step_ptr->tres_freq = tres_freq;
	tres_freq = NULL;
	xfree(step_ptr->tres_per_step);
	step_ptr->tres_per_step = tres_per_step;
	tres_per_step = NULL;
	xfree(step_ptr->tres_per_node);
	step_ptr->tres_per_node = tres_per_node;
	tres_per_node = NULL;
	xfree(step_ptr->tres_per_socket);
	step_ptr->tres_per_socket = tres_per_socket;
	tres_per_socket = NULL;
	xfree(step_ptr->tres_per_task);
	step_ptr->tres_per_task = tres_per_task;
	tres_per_task = NULL;

	xfree(step_ptr->tres_fmt_alloc_str);
	step_ptr->tres_fmt_alloc_str = tres_fmt_alloc_str;
	tres_fmt_alloc_str = NULL;

	step_ptr->cpu_freq_min = cpu_freq_min;
	step_ptr->cpu_freq_max = cpu_freq_max;
	step_ptr->cpu_freq_gov = cpu_freq_gov;
	step_ptr->state        = state;

	step_ptr->start_protocol_ver = start_protocol_ver;

	step_ptr->exit_code    = exit_code;

	if (exit_node_bitmap) {
		step_ptr->exit_node_bitmap = exit_node_bitmap;
		exit_node_bitmap = NULL;
	}

	if (core_bitmap_job) {
		step_ptr->core_bitmap_job = core_bitmap_job;
		core_bitmap_job = NULL;
	}

	if (jobacct) {
		jobacctinfo_destroy(step_ptr->jobacct);
		step_ptr->jobacct = jobacct;
	}

	info("Recovered %pS", step_ptr);
	return SLURM_SUCCESS;

unpack_error:
	xfree(container);
	xfree(container_id);
	xfree(cpu_alloc_reps);
	xfree(cpu_alloc_values);
	xfree(host);
	xfree(resv_ports);
	xfree(name);
	xfree(network);
	FREE_NULL_LIST(gres_list_req);
	FREE_NULL_LIST(gres_list_alloc);
	FREE_NULL_BITMAP(exit_node_bitmap);
	FREE_NULL_BITMAP(core_bitmap_job);
	if (jobacct)
		jobacctinfo_destroy(jobacct);
	xfree(core_job);
	if (switch_tmp)
		switch_g_free_stepinfo(switch_tmp);
	slurm_step_layout_destroy(step_layout);
	select_g_select_jobinfo_free(select_jobinfo);
	xfree(tres_alloc_str);
	xfree(tres_fmt_alloc_str);
	xfree(cpus_per_tres);
	xfree(mem_per_tres);
	xfree(memory_allocated);
	xfree(submit_line);
	xfree(tres_bind);
	xfree(tres_freq);
	xfree(tres_per_step);
	xfree(tres_per_node);
	xfree(tres_per_socket);
	xfree(tres_per_task);

	return SLURM_ERROR;
}

static int _unpack_acct_policy_limit_members(
	acct_policy_limit_set_t *limit_set,
	int tres_cnt,
	buf_t *buffer, uint16_t protocol_version)
{
	uint32_t tmp32;

	xassert(limit_set);

	safe_unpack16(&limit_set->qos, buffer);
	safe_unpack16(&limit_set->time, buffer);
	xfree(limit_set->tres);
	safe_unpack16_array(&limit_set->tres, &tmp32, buffer);

	/*
	 * Because the tres array could have grown or the tres could have moved
	 * positions, the array needs to be rebuilt and the old values need to
	 * be copied into their new spots.
	 */
	if ((tmp32 < tres_cnt) || assoc_mgr_tres_pos_changed())
		update_job_limit_set_tres(&limit_set->tres, tres_cnt);

	return SLURM_SUCCESS;

unpack_error:
	xfree(limit_set->tres);

	return SLURM_ERROR;
}

static int _comp_node_inx(const void *n1, const void *n2)
{
	const node_inx_cnt_t *node1 = n1;
	const node_inx_cnt_t *node2 = n2;

	return slurm_sort_int_list_asc(&node1->node_index, &node2->node_index);
}

extern int job_record_calc_arbitrary_tpn(job_record_t *job_ptr)
{
	uint16_t *arbitrary_tasks_np = NULL;
	int rc = SLURM_SUCCESS;
	int cur_node = 0;
	int num_nodes = job_ptr->details->min_nodes;
	char *host, *prev_host = NULL;
	node_inx_cnt_t *node_inx_cnts;
	hostlist_t *hl = hostlist_create(job_ptr->details->req_nodes);
	hostlist_sort(hl);

	arbitrary_tasks_np = xcalloc(num_nodes, sizeof(uint16_t));
	node_inx_cnts = xcalloc(num_nodes, sizeof(node_inx_cnt_t));

	while ((host = hostlist_shift(hl))) {
		if (!prev_host || !xstrcmp(host, prev_host)) {
			node_inx_cnts[cur_node].node_count += 1;
		} else {
			node_inx_cnts[cur_node].node_index =
				node_name_get_inx(prev_host);
			cur_node++;
			if (cur_node >= num_nodes) {
				free(host);
				free(prev_host);
				error("Minimum number of nodes (%d) for %pJ is not sufficient for the requested arbitrary node list (%s).",
				      num_nodes, job_ptr,
				      job_ptr->details->req_nodes);
				rc = ESLURM_INVALID_NODE_COUNT;
				goto cleanup;
			}

			node_inx_cnts[cur_node].node_count += 1;
		}

		free(prev_host);
		prev_host = host;
	}

	if ((cur_node + 1) != num_nodes) {
		free(prev_host);
		error("Minimum number of nodes (%d) for %pJ is too large for the requested arbitrary node list (%s).",
		      num_nodes, job_ptr, job_ptr->details->req_nodes);
		rc = ESLURM_INVALID_NODE_COUNT;
		goto cleanup;
	}

	node_inx_cnts[cur_node].node_index = node_name_get_inx(prev_host);
	free(prev_host);

	qsort(node_inx_cnts, num_nodes, sizeof(node_inx_cnt_t), _comp_node_inx);

	for (int i = 0; i < num_nodes; i++)
		arbitrary_tasks_np[i] = node_inx_cnts[i].node_count;

	job_ptr->details->arbitrary_tpn = arbitrary_tasks_np;
	arbitrary_tasks_np = NULL;

cleanup:
	xfree(arbitrary_tasks_np);
	hostlist_destroy(hl);
	xfree(node_inx_cnts);

	return rc;
}

/* _load_job_details - Unpack a job details information from buffer */
static int _load_job_details(job_record_t *job_ptr, buf_t *buffer,
			     uint16_t protocol_version)
{
	char *acctg_freq = NULL, *req_nodes = NULL, *exc_nodes = NULL;
	char *features = NULL, *cpu_bind = NULL, *dependency = NULL;
	char *orig_dependency = NULL, *mem_bind = NULL;
	char *cluster_features = NULL;
	char *err = NULL, *in = NULL, *out = NULL, *work_dir = NULL;
	char **argv = (char **) NULL, **env_sup = (char **) NULL;
	char *submit_line = NULL, *prefer = NULL;
	char *env_hash = NULL, *script_hash = NULL, *qos_req = NULL;
	char *x11_magic_cookie = NULL, *x11_target = NULL;
	uint32_t min_nodes, max_nodes;
	uint32_t min_cpus = 1, max_cpus = NO_VAL;
	uint32_t pn_min_cpus, pn_min_tmp_disk;
	uint64_t pn_min_memory;
	uint16_t oom_kill_step = NO_VAL16;
	uint32_t cpu_freq_min = NO_VAL;
	uint32_t cpu_freq_max = NO_VAL;
	uint32_t cpu_freq_gov = NO_VAL, nice = 0;
	uint32_t num_tasks, argc = 0, env_cnt = 0, task_dist;
	uint16_t contiguous, core_spec = NO_VAL16;
	uint16_t ntasks_per_node, ntasks_per_tres = 0, cpus_per_task, requeue;
	uint16_t cpu_bind_type, mem_bind_type;
	uint16_t segment_size = 0;
	uint16_t resv_port_cnt = NO_VAL16;
	uint16_t x11 = 0, x11_target_port = 0;
	uint8_t open_mode, overcommit, prolog_running;
	uint8_t share_res, whole_node, features_use = 0;
	time_t begin_time, accrue_time = 0, submit_time;
	int i;
	list_t *depend_list = NULL;
	multi_core_data_t *mc_ptr;
	cron_entry_t *crontab_entry = NULL;
	bitstr_t *job_size_bitmap = NULL;

	/* unpack the job's details from the buffer */
	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		/* job_record_pack_details_common */
		safe_unpack_time(&accrue_time, buffer);
		safe_unpack_time(&begin_time, buffer);
		safe_unpackstr(&cluster_features, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpackstr(&dependency, buffer);
		unpack_bit_str_hex(&job_size_bitmap, buffer);
		safe_unpack32(&nice, buffer);
		safe_unpack16(&ntasks_per_node, buffer);
		safe_unpack16(&ntasks_per_tres, buffer);
		safe_unpack16(&requeue, buffer);
		safe_unpack_time(&submit_time, buffer);
		safe_unpackstr(&work_dir, buffer);
		/**********************************/

		safe_unpack32(&min_cpus, buffer);
		safe_unpack32(&max_cpus, buffer);
		safe_unpack32(&min_nodes, buffer);
		safe_unpack32(&max_nodes, buffer);
		safe_unpack32(&num_tasks, buffer);

		safe_unpackstr(&acctg_freq, buffer);
		safe_unpack16(&contiguous, buffer);
		safe_unpack16(&core_spec, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack32(&task_dist, buffer);

		safe_unpack8(&share_res, buffer);
		safe_unpack8(&whole_node, buffer);

		safe_unpackstr(&cpu_bind, buffer);
		safe_unpack16(&cpu_bind_type, buffer);
		safe_unpackstr(&mem_bind, buffer);
		safe_unpack16(&mem_bind_type, buffer);

		safe_unpack8(&open_mode, buffer);
		safe_unpack8(&overcommit, buffer);
		safe_unpack8(&prolog_running, buffer);

		safe_unpack32(&pn_min_cpus, buffer);
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack16(&oom_kill_step, buffer);
		safe_unpack32(&pn_min_tmp_disk, buffer);

		safe_unpackstr(&req_nodes, buffer);
		safe_unpackstr(&exc_nodes, buffer);
		safe_unpackstr(&features, buffer);
		safe_unpackstr(&prefer, buffer);
		safe_unpack8(&features_use, buffer);

		unpack_dep_list(&depend_list, buffer, protocol_version);
		safe_unpackstr(&orig_dependency, buffer);

		safe_unpackstr(&err, buffer);
		safe_unpackstr(&in, buffer);
		safe_unpackstr(&out, buffer);
		safe_unpackstr(&submit_line, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&argv, &argc, buffer);
		safe_unpackstr_array(&env_sup, &env_cnt, buffer);

		if (unpack_cron_entry((void **) &crontab_entry,
				      protocol_version, buffer))
			goto unpack_error;
		safe_unpackstr(&env_hash, buffer);
		safe_unpackstr(&script_hash, buffer);
		safe_unpack16(&segment_size, buffer);
		safe_unpack16(&resv_port_cnt, buffer);
		safe_unpackstr(&qos_req, buffer);

		safe_unpack16(&x11, buffer);
		safe_unpackstr(&x11_magic_cookie, buffer);
		safe_unpackstr(&x11_target, buffer);
		safe_unpack16(&x11_target_port, buffer);
	} else if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		safe_unpack32(&min_cpus, buffer);
		safe_unpack32(&max_cpus, buffer);
		safe_unpack32(&min_nodes, buffer);
		safe_unpack32(&max_nodes, buffer);
		safe_unpack32(&num_tasks, buffer);

		safe_unpackstr(&acctg_freq, buffer);
		safe_unpack16(&contiguous, buffer);
		safe_unpack16(&core_spec, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack32(&nice, buffer);
		safe_unpack16(&ntasks_per_node, buffer);
		safe_unpack16(&requeue, buffer);
		safe_unpack32(&task_dist, buffer);

		safe_unpack8(&share_res, buffer);
		safe_unpack8(&whole_node, buffer);

		safe_unpackstr(&cpu_bind, buffer);
		safe_unpack16(&cpu_bind_type, buffer);
		safe_unpackstr(&mem_bind, buffer);
		safe_unpack16(&mem_bind_type, buffer);

		safe_unpack8(&open_mode, buffer);
		safe_unpack8(&overcommit, buffer);
		safe_unpack8(&prolog_running, buffer);

		safe_unpack32(&pn_min_cpus, buffer);
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack32(&pn_min_tmp_disk, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);
		safe_unpack_time(&begin_time, buffer);
		safe_unpack_time(&accrue_time, buffer);
		safe_unpack_time(&submit_time, buffer);

		safe_unpackstr(&req_nodes, buffer);
		safe_unpackstr(&exc_nodes, buffer);
		safe_unpackstr(&features, buffer);
		safe_unpackstr(&cluster_features, buffer);
		safe_unpackstr(&prefer, buffer);
		safe_unpack8(&features_use, buffer);
		unpack_bit_str_hex(&job_size_bitmap, buffer);

		unpack_dep_list(&depend_list, buffer, protocol_version);
		safe_unpackstr(&dependency, buffer);
		safe_unpackstr(&orig_dependency, buffer);

		safe_unpackstr(&err, buffer);
		safe_unpackstr(&in, buffer);
		safe_unpackstr(&out, buffer);
		safe_unpackstr(&submit_line, buffer);
		safe_unpackstr(&work_dir, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&argv, &argc, buffer);
		safe_unpackstr_array(&env_sup, &env_cnt, buffer);

		if (unpack_cron_entry((void **) &crontab_entry,
				      protocol_version, buffer))
			goto unpack_error;
		safe_unpackstr(&env_hash, buffer);
		safe_unpackstr(&script_hash, buffer);
		safe_unpack16(&segment_size, buffer);
		safe_unpack16(&resv_port_cnt, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint16_t plane_size;
		safe_unpack32(&min_cpus, buffer);
		safe_unpack32(&max_cpus, buffer);
		safe_unpack32(&min_nodes, buffer);
		safe_unpack32(&max_nodes, buffer);
		safe_unpack32(&num_tasks, buffer);

		safe_unpackstr(&acctg_freq, buffer);
		safe_unpack16(&contiguous, buffer);
		safe_unpack16(&core_spec, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack32(&nice, buffer);
		safe_unpack16(&ntasks_per_node, buffer);
		safe_unpack16(&requeue, buffer);
		safe_unpack32(&task_dist, buffer);

		safe_unpack8(&share_res, buffer);
		safe_unpack8(&whole_node, buffer);

		safe_unpackstr(&cpu_bind, buffer);
		safe_unpack16(&cpu_bind_type, buffer);
		safe_unpackstr(&mem_bind, buffer);
		safe_unpack16(&mem_bind_type, buffer);
		safe_unpack16(&plane_size, buffer);

		safe_unpack8(&open_mode, buffer);
		safe_unpack8(&overcommit, buffer);
		safe_unpack8(&prolog_running, buffer);

		safe_unpack32(&pn_min_cpus, buffer);
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack32(&pn_min_tmp_disk, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);
		safe_unpack_time(&begin_time, buffer);
		safe_unpack_time(&accrue_time, buffer);
		safe_unpack_time(&submit_time, buffer);

		safe_unpackstr(&req_nodes, buffer);
		safe_unpackstr(&exc_nodes, buffer);
		safe_unpackstr(&features, buffer);
		safe_unpackstr(&cluster_features, buffer);
		safe_unpackstr(&prefer, buffer);
		safe_unpack8(&features_use, buffer);
		unpack_bit_str_hex(&job_size_bitmap, buffer);

		unpack_dep_list(&depend_list, buffer, protocol_version);
		safe_unpackstr(&dependency, buffer);
		safe_unpackstr(&orig_dependency, buffer);

		safe_unpackstr(&err, buffer);
		safe_unpackstr(&in, buffer);
		safe_unpackstr(&out, buffer);
		safe_unpackstr(&submit_line, buffer);
		safe_unpackstr(&work_dir, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&argv, &argc, buffer);
		safe_unpackstr_array(&env_sup, &env_cnt, buffer);

		if (unpack_cron_entry((void **) &crontab_entry,
				      protocol_version, buffer))
			goto unpack_error;
		safe_unpackstr(&env_hash, buffer);
		safe_unpackstr(&script_hash, buffer);
	} else {
		error("_load_job_details: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	/* validity test as possible */
	if (contiguous > 1) {
		error("Invalid data for %pJ: contiguous=%u",
		      job_ptr, contiguous);
		goto unpack_error;
	}
	if ((requeue > 1) || (overcommit > 1)) {
		error("Invalid data for %pJ: requeue=%u overcommit=%u",
		      job_ptr, requeue, overcommit);
		goto unpack_error;
	}
	if (prolog_running > 4) {
		error("Invalid data for %pJ: prolog_running=%u",
		      job_ptr, prolog_running);
		goto unpack_error;
	}

	/* free any left-over detail data */
	xfree(job_ptr->details->acctg_freq);
	xfree(job_ptr->details->arbitrary_tpn);
	for (i=0; i<job_ptr->details->argc; i++)
		xfree(job_ptr->details->argv[i]);
	xfree(job_ptr->details->argv);
	xfree(job_ptr->details->cpu_bind);
	FREE_NULL_LIST(job_ptr->details->depend_list);
	xfree(job_ptr->details->dependency);
	xfree(job_ptr->details->orig_dependency);
	xfree(job_ptr->details->std_err);
	xfree(job_ptr->details->env_hash);
	for (i=0; i<job_ptr->details->env_cnt; i++)
		xfree(job_ptr->details->env_sup[i]);
	xfree(job_ptr->details->env_sup);
	xfree(job_ptr->details->exc_nodes);
	xfree(job_ptr->details->features);
	xfree(job_ptr->details->cluster_features);
	xfree(job_ptr->details->prefer);
	xfree(job_ptr->details->std_in);
	xfree(job_ptr->details->mem_bind);
	xfree(job_ptr->details->script_hash);
	xfree(job_ptr->details->std_out);
	xfree(job_ptr->details->submit_line);
	xfree(job_ptr->details->req_nodes);
	xfree(job_ptr->details->work_dir);
	xfree(job_ptr->details->qos_req);
	xfree(job_ptr->details->x11_magic_cookie);
	xfree(job_ptr->details->x11_target);

	/* now put the details into the job record */
	job_ptr->details->acctg_freq = acctg_freq;
	job_ptr->details->argc = argc;
	job_ptr->details->argv = argv;
	job_ptr->details->accrue_time = accrue_time;
	job_ptr->details->begin_time = begin_time;
	job_ptr->details->contiguous = contiguous;
	job_ptr->details->core_spec = core_spec;
	job_ptr->details->cpu_bind = cpu_bind;
	job_ptr->details->cpu_bind_type = cpu_bind_type;
	job_ptr->details->cpu_freq_min = cpu_freq_min;
	job_ptr->details->cpu_freq_max = cpu_freq_max;
	job_ptr->details->cpu_freq_gov = cpu_freq_gov;
	if (cpus_per_task != NO_VAL16)
		job_ptr->details->cpus_per_task = cpus_per_task;
	else
		job_ptr->details->cpus_per_task = 1;
	job_ptr->details->orig_cpus_per_task = cpus_per_task;
	job_ptr->details->crontab_entry = crontab_entry;
	job_ptr->details->depend_list = depend_list;
	job_ptr->details->dependency = dependency;
	job_ptr->details->orig_dependency = orig_dependency;
	job_ptr->details->env_cnt = env_cnt;
	job_ptr->details->env_sup = env_sup;
	job_ptr->details->std_err = err;
	job_ptr->details->exc_nodes = exc_nodes;
	job_ptr->details->features = features;
	job_ptr->details->cluster_features = cluster_features;
	job_ptr->details->prefer = prefer;
	job_ptr->details->env_hash = env_hash;
	job_ptr->details->job_size_bitmap = job_size_bitmap;

	job_ptr->details->script_hash = script_hash;
	job_ptr->details->qos_req = qos_req;
	job_ptr->details->x11 = x11;
	job_ptr->details->x11_magic_cookie = x11_magic_cookie;
	job_ptr->details->x11_target = x11_target;
	job_ptr->details->x11_target_port = x11_target_port;

	switch (features_use) {
	case 0:
		break;
	case 1:
		job_ptr->details->features_use = job_ptr->details->features;
		break;
	case 2:
		job_ptr->details->features_use = job_ptr->details->prefer;
		break;
	default:
		error("unknown detail_use given %d", features_use);
		break;
	}

	job_ptr->details->std_in = in;
	job_ptr->details->pn_min_cpus = pn_min_cpus;
	job_ptr->details->orig_pn_min_cpus = pn_min_cpus;
	job_ptr->details->pn_min_memory = pn_min_memory;
	job_ptr->details->oom_kill_step = oom_kill_step;
	job_ptr->details->orig_pn_min_memory = pn_min_memory;
	job_ptr->details->pn_min_tmp_disk = pn_min_tmp_disk;
	job_ptr->details->max_cpus = max_cpus;
	job_ptr->details->orig_max_cpus = max_cpus;
	job_ptr->details->max_nodes = max_nodes;
	job_ptr->details->mc_ptr = mc_ptr;
	job_ptr->details->mem_bind = mem_bind;
	job_ptr->details->mem_bind_type = mem_bind_type;
	job_ptr->details->min_cpus = min_cpus;
	job_ptr->details->orig_min_cpus = min_cpus;
	job_ptr->details->min_nodes = min_nodes;
	job_ptr->details->nice = nice;
	job_ptr->details->ntasks_per_node = ntasks_per_node;
	job_ptr->details->ntasks_per_tres = ntasks_per_tres;
	job_ptr->details->num_tasks = num_tasks;
	job_ptr->details->open_mode = open_mode;
	job_ptr->details->std_out = out;
	job_ptr->details->submit_line = submit_line;
	job_ptr->details->overcommit = overcommit;
	job_ptr->details->prolog_running = prolog_running;
	job_ptr->details->req_nodes = req_nodes;
	job_ptr->details->requeue = requeue;
	job_ptr->details->resv_port_cnt = resv_port_cnt;
	job_ptr->details->segment_size = segment_size;
	job_ptr->details->share_res = share_res;
	job_ptr->details->submit_time = submit_time;
	job_ptr->details->task_dist = task_dist;
	job_ptr->details->whole_node = whole_node;
	job_ptr->details->work_dir = work_dir;

	if (((job_ptr->details->task_dist & SLURM_DIST_STATE_BASE) ==
	     SLURM_DIST_ARBITRARY) && job_record_calc_arbitrary_tpn(job_ptr))
		return SLURM_ERROR;

	return SLURM_SUCCESS;

unpack_error:

/*	for (i=0; i<argc; i++)
	xfree(argv[i]);  Don't trust this on unpack error */
	xfree(acctg_freq);
	xfree(argv);
	xfree(cpu_bind);
	free_cron_entry(crontab_entry);
	xfree(dependency);
	xfree(orig_dependency);
/*	for (i=0; i<env_cnt; i++)
	xfree(env_sup[i]);  Don't trust this on unpack error */
	xfree(env_hash);
	xfree(env_sup);
	xfree(err);
	xfree(exc_nodes);
	xfree(features);
	xfree(cluster_features);
	xfree(prefer);
	xfree(in);
	FREE_NULL_BITMAP(job_size_bitmap);
	xfree(mem_bind);
	xfree(out);
	xfree(qos_req);
	xfree(req_nodes);
	xfree(script_hash);
	xfree(submit_line);
	xfree(work_dir);
	xfree(x11_magic_cookie);
	xfree(x11_target);
	return SLURM_ERROR;
}

static void _dump_job_fed_details(job_fed_details_t *fed_details_ptr,
				  bool for_state,
				  buf_t *buffer,
				  uint16_t protocol_version)
{
	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		if (!fed_details_ptr) {
			packbool(false, buffer);
			return;
		}

		packbool(true, buffer);
		if (for_state) {
			pack32(fed_details_ptr->cluster_lock, buffer);
		}
		packstr(fed_details_ptr->origin_str, buffer);
		pack64(fed_details_ptr->siblings_active, buffer);
		packstr(fed_details_ptr->siblings_active_str, buffer);
		pack64(fed_details_ptr->siblings_viable, buffer);
		packstr(fed_details_ptr->siblings_viable_str, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!fed_details_ptr) {
			pack16(0, buffer);
			return;
		}

		pack16(1, buffer);
		pack32(fed_details_ptr->cluster_lock, buffer);
		packstr(fed_details_ptr->origin_str, buffer);
		pack64(fed_details_ptr->siblings_active, buffer);
		packstr(fed_details_ptr->siblings_active_str, buffer);
		pack64(fed_details_ptr->siblings_viable, buffer);
		packstr(fed_details_ptr->siblings_viable_str, buffer);
	}
}

static int _load_job_fed_details(job_fed_details_t **fed_details_pptr,
				 buf_t *buffer, uint16_t protocol_version)
{
	bool need_unpack = true;
	job_fed_details_t *fed_details_ptr = NULL;

	xassert(fed_details_pptr);

	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		safe_unpackbool(&need_unpack, buffer);
		if (!need_unpack)
			goto end_unpack;

		*fed_details_pptr = xmalloc(sizeof(job_fed_details_t));
		fed_details_ptr = *fed_details_pptr;
		safe_unpack32(&fed_details_ptr->cluster_lock, buffer);
		safe_unpackstr(&fed_details_ptr->origin_str, buffer);
		safe_unpack64(&fed_details_ptr->siblings_active, buffer);
		safe_unpackstr(&fed_details_ptr->siblings_active_str, buffer);
		safe_unpack64(&fed_details_ptr->siblings_viable, buffer);
		safe_unpackstr(&fed_details_ptr->siblings_viable_str, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint16_t tmp_uint16;
		safe_unpack16(&tmp_uint16, buffer);
		if (tmp_uint16) {
			*fed_details_pptr = xmalloc(sizeof(job_fed_details_t));
			fed_details_ptr = *fed_details_pptr;
			safe_unpack32(&fed_details_ptr->cluster_lock, buffer);
			safe_unpackstr(&fed_details_ptr->origin_str, buffer);
			safe_unpack64(&fed_details_ptr->siblings_active,
				      buffer);
			safe_unpackstr(&fed_details_ptr->siblings_active_str,
				       buffer);
			safe_unpack64(&fed_details_ptr->siblings_viable,
				      buffer);
			safe_unpackstr(&fed_details_ptr->siblings_viable_str,
				       buffer);
		}
	} else
		goto unpack_error;
end_unpack:
	return SLURM_SUCCESS;

unpack_error:
	job_record_free_fed_details(fed_details_pptr);
	*fed_details_pptr = NULL;

	return SLURM_ERROR;
}

extern void job_record_pack_details_common(
	job_details_t *detail_ptr, buf_t *buffer, uint16_t protocol_version)
{
	xassert(detail_ptr);
	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		pack_time(detail_ptr->accrue_time, buffer);
		pack_time(detail_ptr->begin_time, buffer);
		packstr(detail_ptr->cluster_features, buffer);
		pack32(detail_ptr->cpu_freq_gov, buffer);
		pack32(detail_ptr->cpu_freq_max, buffer);
		pack32(detail_ptr->cpu_freq_min, buffer);
		packstr(detail_ptr->dependency, buffer);
		pack_bit_str_hex(detail_ptr->job_size_bitmap, buffer);
		pack32(detail_ptr->nice, buffer);
		pack16(detail_ptr->ntasks_per_node, buffer);
		pack16(detail_ptr->ntasks_per_tres, buffer);
		pack16(detail_ptr->requeue,   buffer);
		pack_time(detail_ptr->submit_time, buffer);
		packstr(detail_ptr->work_dir, buffer);
	}
}

extern void job_record_pack_common(job_record_t *dump_job_ptr,
				   bool for_state,
				   buf_t *buffer,
				   uint16_t protocol_version)
{
	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		packstr(dump_job_ptr->account, buffer);
		packstr(dump_job_ptr->admin_comment, buffer);
		packstr(dump_job_ptr->alloc_node, buffer);
		pack32(dump_job_ptr->alloc_sid, buffer);
		pack32(dump_job_ptr->array_job_id, buffer);
		pack32(dump_job_ptr->array_task_id, buffer);
		pack32(dump_job_ptr->assoc_id, buffer);

		packstr(dump_job_ptr->batch_features, buffer);
		pack16(dump_job_ptr->batch_flag, buffer);
		packstr(dump_job_ptr->batch_host, buffer);
		pack64(dump_job_ptr->bit_flags, buffer);
		packstr(dump_job_ptr->burst_buffer, buffer);
		packstr(dump_job_ptr->burst_buffer_state, buffer);
		packdouble(dump_job_ptr->billable_tres, buffer);

		packstr(dump_job_ptr->comment, buffer);
		packstr(dump_job_ptr->container, buffer);
		packstr(dump_job_ptr->container_id, buffer);
		packstr(dump_job_ptr->cpus_per_tres, buffer);

		pack_time(dump_job_ptr->deadline, buffer);
		pack32(dump_job_ptr->delay_boot, buffer);
		pack32(dump_job_ptr->derived_ec, buffer);

		pack32(dump_job_ptr->exit_code, buffer);
		packstr(dump_job_ptr->extra, buffer);

		packstr(dump_job_ptr->failed_node, buffer);
		_dump_job_fed_details(dump_job_ptr->fed_details,
				      for_state,
				      buffer,
				      protocol_version);

		packstr(dump_job_ptr->gres_used, buffer);
		pack32(dump_job_ptr->group_id, buffer);

		pack32(dump_job_ptr->het_job_id, buffer);
		packstr(dump_job_ptr->het_job_id_set, buffer);
		pack32(dump_job_ptr->het_job_offset, buffer);

		pack32(dump_job_ptr->job_id, buffer);
		pack32(dump_job_ptr->job_state, buffer);

		pack_time(dump_job_ptr->last_sched_eval, buffer);
		packstr(dump_job_ptr->licenses, buffer);

		pack16(dump_job_ptr->mail_type, buffer);
		packstr(dump_job_ptr->mail_user, buffer);
		packstr(dump_job_ptr->mcs_label, buffer);
		packstr(dump_job_ptr->mem_per_tres, buffer);

		packstr(dump_job_ptr->name, buffer);
		packstr(dump_job_ptr->network, buffer);

		pack_time(dump_job_ptr->preempt_time, buffer);
		pack_time(dump_job_ptr->pre_sus_time, buffer);
		pack32(dump_job_ptr->priority, buffer);
		pack32(dump_job_ptr->profile, buffer);

		pack8(dump_job_ptr->reboot, buffer);
		pack32(dump_job_ptr->req_switch, buffer);
		pack_time(dump_job_ptr->resize_time, buffer);
		pack16(dump_job_ptr->restart_cnt, buffer);
		packstr(dump_job_ptr->resv_name, buffer);
		packstr(dump_job_ptr->resv_ports, buffer);

		packstr(dump_job_ptr->selinux_context, buffer);
		pack32(dump_job_ptr->site_factor, buffer);
		pack16(dump_job_ptr->start_protocol_ver, buffer);
		packstr(dump_job_ptr->state_desc, buffer);
		pack32(dump_job_ptr->state_reason, buffer);
		pack_time(dump_job_ptr->suspend_time, buffer);
		packstr(dump_job_ptr->system_comment, buffer);

		pack32(dump_job_ptr->time_min, buffer);
		packstr(dump_job_ptr->tres_bind, buffer);
		packstr(dump_job_ptr->tres_fmt_alloc_str, buffer);
		packstr(dump_job_ptr->tres_fmt_req_str, buffer);
		packstr(dump_job_ptr->tres_freq, buffer);
		packstr(dump_job_ptr->tres_per_job, buffer);
		packstr(dump_job_ptr->tres_per_node, buffer);
		packstr(dump_job_ptr->tres_per_socket, buffer);
		packstr(dump_job_ptr->tres_per_task, buffer);

		pack32(dump_job_ptr->user_id, buffer);
		packstr(dump_job_ptr->user_name, buffer);

		pack32(dump_job_ptr->wait4switch, buffer);
		packstr(dump_job_ptr->wckey, buffer);
	}
}

extern int job_record_unpack_common(job_record_t *job_ptr,
				    buf_t *buffer,
				    uint16_t protocol_version)
{
	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		safe_unpackstr(&job_ptr->account, buffer);
		safe_unpackstr(&job_ptr->admin_comment, buffer);
		safe_unpackstr(&job_ptr->alloc_node, buffer);
		safe_unpack32(&job_ptr->alloc_sid, buffer);
		safe_unpack32(&job_ptr->array_job_id, buffer);
		safe_unpack32(&job_ptr->array_task_id, buffer);
		safe_unpack32(&job_ptr->assoc_id, buffer);

		safe_unpackstr(&job_ptr->batch_features, buffer);
		safe_unpack16(&job_ptr->batch_flag, buffer);
		safe_unpackstr(&job_ptr->batch_host, buffer);
		safe_unpack64(&job_ptr->bit_flags, buffer);
		job_ptr->bit_flags &= ~BACKFILL_TEST;
		job_ptr->bit_flags &= ~BF_WHOLE_NODE_TEST;
		safe_unpackstr(&job_ptr->burst_buffer, buffer);
		safe_unpackstr(&job_ptr->burst_buffer_state, buffer);
		safe_unpackdouble(&job_ptr->billable_tres, buffer);

		safe_unpackstr(&job_ptr->comment, buffer);
		safe_unpackstr(&job_ptr->container, buffer);
		safe_unpackstr(&job_ptr->container_id, buffer);
		safe_unpackstr(&job_ptr->cpus_per_tres, buffer);

		safe_unpack_time(&job_ptr->deadline, buffer);
		safe_unpack32(&job_ptr->delay_boot, buffer);
		safe_unpack32(&job_ptr->derived_ec, buffer);

		safe_unpack32(&job_ptr->exit_code, buffer);
		safe_unpackstr(&job_ptr->extra, buffer);

		safe_unpackstr(&job_ptr->failed_node, buffer);
		if (_load_job_fed_details(&job_ptr->fed_details,
					  buffer,
					  protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&job_ptr->gres_used, buffer);
		safe_unpack32(&job_ptr->group_id, buffer);

		safe_unpack32(&job_ptr->het_job_id, buffer);
		safe_unpackstr(&job_ptr->het_job_id_set, buffer);
		safe_unpack32(&job_ptr->het_job_offset, buffer);

		safe_unpack32(&job_ptr->job_id, buffer);
		safe_unpack32(&job_ptr->job_state, buffer);

		safe_unpack_time(&job_ptr->last_sched_eval, buffer);
		safe_unpackstr(&job_ptr->licenses, buffer);

		safe_unpack16(&job_ptr->mail_type, buffer);
		safe_unpackstr(&job_ptr->mail_user, buffer);
		safe_unpackstr(&job_ptr->mcs_label, buffer);
		safe_unpackstr(&job_ptr->mem_per_tres, buffer);

		safe_unpackstr(&job_ptr->name, buffer);
		safe_unpackstr(&job_ptr->network, buffer);

		safe_unpack_time(&job_ptr->preempt_time, buffer);
		safe_unpack_time(&job_ptr->pre_sus_time, buffer);
		safe_unpack32(&job_ptr->priority, buffer);
		safe_unpack32(&job_ptr->profile, buffer);

		safe_unpack8(&job_ptr->reboot, buffer);
		safe_unpack32(&job_ptr->req_switch, buffer);
		safe_unpack_time(&job_ptr->resize_time, buffer);
		safe_unpack16(&job_ptr->restart_cnt, buffer);
		safe_unpackstr(&job_ptr->resv_name, buffer);
		safe_unpackstr(&job_ptr->resv_ports, buffer);

		safe_unpackstr(&job_ptr->selinux_context, buffer);
		safe_unpack32(&job_ptr->site_factor, buffer);
		safe_unpack16(&job_ptr->start_protocol_ver, buffer);
		safe_unpackstr(&job_ptr->state_desc, buffer);
		safe_unpack32(&job_ptr->state_reason, buffer);
		safe_unpack_time(&job_ptr->suspend_time, buffer);
		safe_unpackstr(&job_ptr->system_comment, buffer);

		safe_unpack32(&job_ptr->time_min, buffer);
		safe_unpackstr(&job_ptr->tres_bind, buffer);
		safe_unpackstr(&job_ptr->tres_fmt_alloc_str, buffer);
		safe_unpackstr(&job_ptr->tres_fmt_req_str, buffer);
		safe_unpackstr(&job_ptr->tres_freq, buffer);
		safe_unpackstr(&job_ptr->tres_per_job, buffer);
		safe_unpackstr(&job_ptr->tres_per_node, buffer);
		safe_unpackstr(&job_ptr->tres_per_socket, buffer);
		safe_unpackstr(&job_ptr->tres_per_task, buffer);

		safe_unpack32(&job_ptr->user_id, buffer);
		safe_unpackstr(&job_ptr->user_name, buffer);

		safe_unpack32(&job_ptr->wait4switch, buffer);
		safe_unpackstr(&job_ptr->wckey, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

/*
 * WARNING: this contains sensitive data. e.g., the x11_magic_cookie.
 * DO NOT use this in client-facing RPCs.
 */
extern int job_record_pack(job_record_t *dump_job_ptr,
			   int tres_cnt,
			   buf_t *buffer,
			   uint16_t protocol_version)
{
	job_details_t *detail_ptr;
	uint32_t tmp_32;

	xassert(dump_job_ptr->magic == JOB_MAGIC);

	/* Don't pack "unlinked" job. */
	if (dump_job_ptr->job_id == NO_VAL)
		return 0;

	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		job_record_pack_common(dump_job_ptr, true, buffer,
				       protocol_version);

		if (dump_job_ptr->array_recs) {
			if (dump_job_ptr->array_recs->task_id_bitmap) {
				tmp_32 = bit_size(dump_job_ptr->array_recs->
						  task_id_bitmap);
			} else
				tmp_32 = 0;
			pack32(tmp_32, buffer);
			if (tmp_32)
				packstr(dump_job_ptr->array_recs->task_id_str,
					buffer);
			pack32(dump_job_ptr->array_recs->array_flags, buffer);
			pack32(dump_job_ptr->array_recs->max_run_tasks, buffer);
			pack32(dump_job_ptr->array_recs->tot_run_tasks, buffer);
			pack32(dump_job_ptr->array_recs->min_exit_code, buffer);
			pack32(dump_job_ptr->array_recs->max_exit_code, buffer);
			pack32(dump_job_ptr->array_recs->tot_comp_tasks,
			       buffer);
		} else {
			tmp_32 = NO_VAL;
			pack32(tmp_32, buffer);
		}

		pack32(dump_job_ptr->time_limit, buffer);
		pack32(dump_job_ptr->total_cpus, buffer);
		if (dump_job_ptr->total_nodes)
			pack32(dump_job_ptr->total_nodes, buffer);
		else
			pack32(dump_job_ptr->node_cnt_wag, buffer);
		pack32(dump_job_ptr->cpu_cnt, buffer);
		pack64(dump_job_ptr->db_index, buffer);
		pack32(dump_job_ptr->resv_id, buffer);
		pack32(dump_job_ptr->next_step_id, buffer);
		pack32(dump_job_ptr->qos_id, buffer);
		pack32(dump_job_ptr->db_flags, buffer);

		pack_time(dump_job_ptr->prolog_launch_time, buffer);
		pack_time(dump_job_ptr->start_time, buffer);
		pack_time(dump_job_ptr->end_time, buffer);
		pack_time(dump_job_ptr->end_time_exp, buffer);
		pack_time(dump_job_ptr->tot_sus_time, buffer);

		pack16(dump_job_ptr->direct_set_prio, buffer);
		pack16(dump_job_ptr->kill_on_node_fail, buffer);
		pack32(dump_job_ptr->state_reason_prev_db, buffer);
		pack16(dump_job_ptr->wait_all_nodes, buffer);
		pack16(dump_job_ptr->warn_flags, buffer);
		pack16(dump_job_ptr->warn_signal, buffer);
		pack16(dump_job_ptr->warn_time, buffer);

		_pack_acct_policy_limit(&dump_job_ptr->limit_set, buffer,
					tres_cnt, protocol_version);

		packstr(dump_job_ptr->resp_host, buffer);

		pack16(dump_job_ptr->alloc_resp_port, buffer);
		pack16(dump_job_ptr->other_port, buffer);
		pack16(dump_job_ptr->resv_port_cnt, buffer);

		if (IS_JOB_COMPLETING(dump_job_ptr))
			packstr(dump_job_ptr->nodes_completing, buffer);
		if (dump_job_ptr->state_reason == WAIT_PROLOG)
			packstr(dump_job_ptr->nodes_pr, buffer);
		packstr(dump_job_ptr->nodes, buffer);
		pack32(dump_job_ptr->node_cnt, buffer);

		pack_bit_str_hex(dump_job_ptr->node_bitmap, buffer);

		packstr(dump_job_ptr->partition, buffer);
		packstr(dump_job_ptr->lic_req, buffer);

		select_g_select_jobinfo_pack(dump_job_ptr->select_jobinfo,
					     buffer, protocol_version);
		switch_g_pack_jobinfo(dump_job_ptr->switch_jobinfo, buffer,
				      protocol_version);
		pack_job_resources(dump_job_ptr->job_resrcs, buffer,
				   protocol_version);

		packstr_array(dump_job_ptr->spank_job_env,
			      dump_job_ptr->spank_job_env_size, buffer);

		(void) gres_job_state_pack(dump_job_ptr->gres_list_req, buffer,
					   dump_job_ptr->job_id, true,
					   protocol_version);

		(void) gres_job_state_pack(dump_job_ptr->gres_list_alloc,
					   buffer, dump_job_ptr->job_id,
					   true, protocol_version);

		/* Dump job details, if available */
		detail_ptr = dump_job_ptr->details;
		if (detail_ptr) {
			xassert(detail_ptr->magic == DETAILS_MAGIC);
			packbool(true, buffer);
			_dump_job_details(detail_ptr, buffer, protocol_version);
		} else
			packbool(false, buffer); /* no details */

		/* Dump job steps */
		(void) slurm_pack_list(dump_job_ptr->step_list,
				       _pack_step_state, buffer,
				       protocol_version);

		packstr(dump_job_ptr->tres_alloc_str, buffer);
		packstr(dump_job_ptr->tres_req_str, buffer);
		packstr(dump_job_ptr->clusters, buffer);

		packstr(dump_job_ptr->origin_cluster, buffer);

		if (dump_job_ptr->id) {
			packbool(true, buffer);
			pack_identity(dump_job_ptr->id, buffer,
				      protocol_version);
		} else {
			packbool(false, buffer);
		}
	} else if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		/* Dump basic job info */
		pack32(dump_job_ptr->array_job_id, buffer);
		pack32(dump_job_ptr->array_task_id, buffer);
		if (dump_job_ptr->array_recs) {
			if (dump_job_ptr->array_recs->task_id_bitmap) {
				tmp_32 = bit_size(dump_job_ptr->array_recs->
						  task_id_bitmap);
			} else
				tmp_32 = 0;
			pack32(tmp_32, buffer);
			if (tmp_32)
				packstr(dump_job_ptr->array_recs->task_id_str,
					buffer);
			pack32(dump_job_ptr->array_recs->array_flags, buffer);
			pack32(dump_job_ptr->array_recs->max_run_tasks, buffer);
			pack32(dump_job_ptr->array_recs->tot_run_tasks, buffer);
			pack32(dump_job_ptr->array_recs->min_exit_code, buffer);
			pack32(dump_job_ptr->array_recs->max_exit_code, buffer);
			pack32(dump_job_ptr->array_recs->tot_comp_tasks,
			       buffer);
		} else {
			tmp_32 = NO_VAL;
			pack32(tmp_32, buffer);
		}

		pack32(dump_job_ptr->assoc_id, buffer);
		packstr(dump_job_ptr->batch_features, buffer);
		packstr(dump_job_ptr->container, buffer);
		packstr(dump_job_ptr->container_id, buffer);
		pack32(dump_job_ptr->delay_boot, buffer);
		packstr(dump_job_ptr->failed_node, buffer);
		pack32(dump_job_ptr->job_id, buffer);
		pack32(dump_job_ptr->user_id, buffer);
		pack32(dump_job_ptr->group_id, buffer);
		pack32(dump_job_ptr->time_limit, buffer);
		pack32(dump_job_ptr->time_min, buffer);
		pack32(dump_job_ptr->priority, buffer);
		pack32(dump_job_ptr->alloc_sid, buffer);
		pack32(dump_job_ptr->total_cpus, buffer);
		if (dump_job_ptr->total_nodes)
			pack32(dump_job_ptr->total_nodes, buffer);
		else
			pack32(dump_job_ptr->node_cnt_wag, buffer);
		pack32(dump_job_ptr->cpu_cnt, buffer);
		pack32(dump_job_ptr->exit_code, buffer);
		pack32(dump_job_ptr->derived_ec, buffer);
		pack64(dump_job_ptr->db_index, buffer);
		pack32(dump_job_ptr->resv_id, buffer);
		pack32(dump_job_ptr->next_step_id, buffer);
		pack32(dump_job_ptr->het_job_id, buffer);
		packstr(dump_job_ptr->het_job_id_set, buffer);
		pack32(dump_job_ptr->het_job_offset, buffer);
		pack32(dump_job_ptr->qos_id, buffer);
		pack32(dump_job_ptr->req_switch, buffer);
		pack32(dump_job_ptr->wait4switch, buffer);
		pack32(dump_job_ptr->profile, buffer);
		pack32(dump_job_ptr->db_flags, buffer);

		pack_time(dump_job_ptr->last_sched_eval, buffer);
		pack_time(dump_job_ptr->preempt_time, buffer);
		pack_time(dump_job_ptr->prolog_launch_time, buffer);
		pack_time(dump_job_ptr->start_time, buffer);
		pack_time(dump_job_ptr->end_time, buffer);
		pack_time(dump_job_ptr->end_time_exp, buffer);
		pack_time(dump_job_ptr->suspend_time, buffer);
		pack_time(dump_job_ptr->pre_sus_time, buffer);
		pack_time(dump_job_ptr->resize_time, buffer);
		pack_time(dump_job_ptr->tot_sus_time, buffer);
		pack_time(dump_job_ptr->deadline, buffer);

		pack32(dump_job_ptr->site_factor, buffer);
		pack16(dump_job_ptr->direct_set_prio, buffer);
		pack32(dump_job_ptr->job_state, buffer);
		pack16(dump_job_ptr->kill_on_node_fail, buffer);
		pack16(dump_job_ptr->batch_flag, buffer);
		pack16(dump_job_ptr->mail_type, buffer);
		pack32(dump_job_ptr->state_reason, buffer);
		pack32(dump_job_ptr->state_reason_prev_db, buffer);
		pack8(dump_job_ptr->reboot, buffer);
		pack16(dump_job_ptr->restart_cnt, buffer);
		pack16(dump_job_ptr->wait_all_nodes, buffer);
		pack16(dump_job_ptr->warn_flags, buffer);
		pack16(dump_job_ptr->warn_signal, buffer);
		pack16(dump_job_ptr->warn_time, buffer);

		_pack_acct_policy_limit(&dump_job_ptr->limit_set, buffer,
					tres_cnt, protocol_version);

		packstr(dump_job_ptr->state_desc, buffer);
		packstr(dump_job_ptr->resp_host, buffer);

		pack16(dump_job_ptr->alloc_resp_port, buffer);
		pack16(dump_job_ptr->other_port, buffer);
		packstr(dump_job_ptr->resv_ports, buffer);
		pack16(dump_job_ptr->resv_port_cnt, buffer);
		pack8(0, buffer); /* was power_flags */
		pack16(dump_job_ptr->start_protocol_ver, buffer);
		packdouble(dump_job_ptr->billable_tres, buffer);

		if (IS_JOB_COMPLETING(dump_job_ptr)) {
			packstr(dump_job_ptr->nodes_completing, buffer);
		}
		if (dump_job_ptr->state_reason == WAIT_PROLOG) {
			packstr(dump_job_ptr->nodes_pr, buffer);
		}
		packstr(dump_job_ptr->nodes, buffer);
		pack32(dump_job_ptr->node_cnt, buffer);
		pack_bit_str_hex(dump_job_ptr->node_bitmap, buffer);
		packstr(dump_job_ptr->partition, buffer);
		packstr(dump_job_ptr->name, buffer);
		packstr(dump_job_ptr->user_name, buffer);
		packstr(dump_job_ptr->wckey, buffer);
		packstr(dump_job_ptr->alloc_node, buffer);
		packstr(dump_job_ptr->account, buffer);
		packstr(dump_job_ptr->admin_comment, buffer);
		packstr(dump_job_ptr->comment, buffer);
		packstr(dump_job_ptr->extra, buffer);
		packstr(dump_job_ptr->gres_used, buffer);
		packstr(dump_job_ptr->network, buffer);
		packstr(dump_job_ptr->licenses, buffer);
		packstr(dump_job_ptr->lic_req, buffer);
		packstr(dump_job_ptr->mail_user, buffer);
		packstr(dump_job_ptr->mcs_label, buffer);
		packstr(dump_job_ptr->resv_name, buffer);
		packstr(dump_job_ptr->batch_host, buffer);
		packstr(dump_job_ptr->burst_buffer, buffer);
		packstr(dump_job_ptr->burst_buffer_state, buffer);
		packstr(dump_job_ptr->system_comment, buffer);

		select_g_select_jobinfo_pack(dump_job_ptr->select_jobinfo,
					     buffer, protocol_version);
		switch_g_pack_jobinfo(dump_job_ptr->switch_jobinfo, buffer,
				      protocol_version);
		pack_job_resources(dump_job_ptr->job_resrcs, buffer,
				   protocol_version);

		packstr_array(dump_job_ptr->spank_job_env,
			      dump_job_ptr->spank_job_env_size, buffer);

		(void) gres_job_state_pack(dump_job_ptr->gres_list_req, buffer,
					   dump_job_ptr->job_id, true,
					   protocol_version);

		(void) gres_job_state_pack(dump_job_ptr->gres_list_alloc,
					   buffer, dump_job_ptr->job_id,
					   true, protocol_version);

		/* Dump job details, if available */
		detail_ptr = dump_job_ptr->details;
		if (detail_ptr) {
			xassert (detail_ptr->magic == DETAILS_MAGIC);
			pack16((uint16_t) DETAILS_FLAG, buffer);
			_dump_job_details(detail_ptr, buffer, protocol_version);
		} else
			pack16((uint16_t) 0, buffer);	/* no details flag */

		/* Dump job steps */
		list_for_each_ro(dump_job_ptr->step_list, dump_job_step_state,
				 buffer);

		pack16((uint16_t) 0, buffer);	/* no step flag */
		pack64(dump_job_ptr->bit_flags, buffer);
		packstr(dump_job_ptr->tres_alloc_str, buffer);
		packstr(dump_job_ptr->tres_fmt_alloc_str, buffer);
		packstr(dump_job_ptr->tres_req_str, buffer);
		packstr(dump_job_ptr->tres_fmt_req_str, buffer);

		packstr(dump_job_ptr->clusters, buffer);
		_dump_job_fed_details(dump_job_ptr->fed_details, true,
				      buffer, protocol_version);

		packstr(dump_job_ptr->origin_cluster, buffer);

		packstr(dump_job_ptr->cpus_per_tres, buffer);
		packstr(dump_job_ptr->mem_per_tres, buffer);
		packstr(dump_job_ptr->tres_bind, buffer);
		packstr(dump_job_ptr->tres_freq, buffer);
		packstr(dump_job_ptr->tres_per_job, buffer);
		packstr(dump_job_ptr->tres_per_node, buffer);
		packstr(dump_job_ptr->tres_per_socket, buffer);
		packstr(dump_job_ptr->tres_per_task, buffer);

		packstr(dump_job_ptr->selinux_context, buffer);

		if (dump_job_ptr->id) {
			pack8(1, buffer);
			pack_identity(dump_job_ptr->id, buffer,
				      protocol_version);
		} else {
			pack8(0, buffer);
		}
	}

	return SLURM_SUCCESS;
}

extern int job_record_unpack(job_record_t **out,
			     int tres_cnt,
			     buf_t *buffer,
			     uint16_t protocol_version)
{
	uint32_t count;
	uint8_t uint8_tmp, identity_flag;
	uint16_t details, step_flag;
	int error_code;
	bool need_unpack = false;

	job_record_t *job_ptr = job_record_create();
	*out = job_ptr;

	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		job_record_unpack_common(job_ptr, buffer, protocol_version);

		/* validity test as possible */
		if (job_ptr->job_id == 0) {
			verbose("Invalid job_id %u", job_ptr->job_id);
			goto unpack_error;
		}

		/* Job Array record */
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			job_ptr->array_recs =
				xmalloc(sizeof(job_array_struct_t));
			if (count) {
				job_ptr->array_recs->task_id_bitmap =
					bit_alloc(count);
				safe_unpackstr(
					&job_ptr->array_recs->task_id_str,
					buffer);
				if (job_ptr->array_recs->task_id_str) {
					if (bit_unfmt_hexmask(
						    job_ptr->array_recs->
						    task_id_bitmap,
						    job_ptr->array_recs->
						    task_id_str) == -1)
						error("%s: bit_unfmt_hexmask error on '%s'",
						      __func__,
						      job_ptr->array_recs->
						      task_id_str);
				}
				job_ptr->array_recs->task_cnt =
					bit_set_count(job_ptr->array_recs->
						      task_id_bitmap);
			}
			safe_unpack32(&job_ptr->array_recs->array_flags,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->max_run_tasks,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->tot_run_tasks,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->min_exit_code,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->max_exit_code,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->tot_comp_tasks,
				      buffer);
		}

		safe_unpack32(&job_ptr->time_limit, buffer);
		safe_unpack32(&job_ptr->total_cpus, buffer);
		safe_unpack32(&job_ptr->total_nodes, buffer);
		safe_unpack32(&job_ptr->cpu_cnt, buffer);
		safe_unpack64(&job_ptr->db_index, buffer);
		safe_unpack32(&job_ptr->resv_id, buffer);
		safe_unpack32(&job_ptr->next_step_id, buffer);
		safe_unpack32(&job_ptr->qos_id, buffer);
		safe_unpack32(&job_ptr->db_flags, buffer);

		safe_unpack_time(&job_ptr->prolog_launch_time, buffer);
		safe_unpack_time(&job_ptr->start_time, buffer);
		safe_unpack_time(&job_ptr->end_time, buffer);
		safe_unpack_time(&job_ptr->end_time_exp, buffer);
		safe_unpack_time(&job_ptr->tot_sus_time, buffer);

		safe_unpack16(&job_ptr->direct_set_prio, buffer);
		safe_unpack16(&job_ptr->kill_on_node_fail, buffer);
		safe_unpack32(&job_ptr->state_reason_prev_db, buffer);
		safe_unpack16(&job_ptr->wait_all_nodes, buffer);
		safe_unpack16(&job_ptr->warn_flags, buffer);
		safe_unpack16(&job_ptr->warn_signal, buffer);
		safe_unpack16(&job_ptr->warn_time, buffer);

		_unpack_acct_policy_limit_members(&job_ptr->limit_set, tres_cnt,
						  buffer, protocol_version);

		safe_unpackstr(&job_ptr->resp_host, buffer);

		safe_unpack16(&job_ptr->alloc_resp_port, buffer);
		safe_unpack16(&job_ptr->other_port, buffer);
		safe_unpack16(&job_ptr->resv_port_cnt, buffer);

		if (job_ptr->job_state & JOB_COMPLETING)
			safe_unpackstr(&job_ptr->nodes_completing, buffer);
		if (job_ptr->state_reason == WAIT_PROLOG)
			safe_unpackstr(&job_ptr->nodes_pr, buffer);
		safe_unpackstr(&job_ptr->nodes, buffer);
		safe_unpack32(&job_ptr->node_cnt, buffer);
		unpack_bit_str_hex(&job_ptr->node_bitmap, buffer);
		safe_unpackstr(&job_ptr->partition, buffer);
		if (job_ptr->partition == NULL) {
			error("No partition for JobId=%u", job_ptr->job_id);
			goto unpack_error;
		}
		safe_unpackstr(&job_ptr->lic_req, buffer);

		if (select_g_select_jobinfo_unpack(&job_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		if (switch_g_unpack_jobinfo(&job_ptr->switch_jobinfo,
					    buffer, protocol_version))
			goto unpack_error;
		if (unpack_job_resources(&job_ptr->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;

		safe_unpackstr_array(&job_ptr->spank_job_env,
				     &job_ptr->spank_job_env_size, buffer);

		if (gres_job_state_unpack(&job_ptr->gres_list_req, buffer,
					  job_ptr->job_id, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_job_state_log(job_ptr->gres_list_req, job_ptr->job_id);

		if (gres_job_state_unpack(&job_ptr->gres_list_alloc, buffer,
					  job_ptr->job_id, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_job_state_log(job_ptr->gres_list_alloc, job_ptr->job_id);

		safe_unpackbool(&need_unpack, buffer);
		if (need_unpack &&
		    (_load_job_details(job_ptr, buffer, protocol_version) !=
		     SLURM_SUCCESS)) {
			goto unpack_error;
		}

		/*
		 * slurm_list_pack was used here but the step needs the job_ptr
		 * so we have to make a copy of slurm_unpack_list logic so we
		 * can deal with the extra pointer.
		 */
		safe_unpack32(&count, buffer);

		if (count > NO_VAL)
			goto unpack_error;

		if (count != NO_VAL) {
			for (int i = 0; i < count; i++) {
				if (load_step_state(job_ptr, buffer,
						    protocol_version) !=
				    SLURM_SUCCESS)
					goto unpack_error;
			}
		}

		safe_unpackstr(&job_ptr->tres_alloc_str, buffer);
		safe_unpackstr(&job_ptr->tres_req_str, buffer);
		safe_unpackstr(&job_ptr->clusters, buffer);

		safe_unpackstr(&job_ptr->origin_cluster, buffer);

		safe_unpackbool(&need_unpack, buffer);
		if (need_unpack) {
			if (unpack_identity(&job_ptr->id, buffer,
					    protocol_version))
				goto unpack_error;
			assoc_mgr_set_uid(job_ptr->user_id,
					  job_ptr->id->pw_name);
		}
	} else if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		safe_unpack32(&job_ptr->array_job_id, buffer);
		safe_unpack32(&job_ptr->array_task_id, buffer);

		/* Job Array record */
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			job_ptr->array_recs =
				xmalloc(sizeof(job_array_struct_t));
			if (count) {
				job_ptr->array_recs->task_id_bitmap =
					bit_alloc(count);
				safe_unpackstr(
					&job_ptr->array_recs->task_id_str,
					buffer);
				if (job_ptr->array_recs->task_id_str) {
					if (bit_unfmt_hexmask(
						    job_ptr->array_recs->
						    task_id_bitmap,
						    job_ptr->array_recs->
						    task_id_str) == -1)
						error("%s: bit_unfmt_hexmask error on '%s'",
						      __func__,
						      job_ptr->array_recs->
						      task_id_str);
				}
				job_ptr->array_recs->task_cnt =
					bit_set_count(job_ptr->array_recs->
						      task_id_bitmap);
			}
			safe_unpack32(&job_ptr->array_recs->array_flags,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->max_run_tasks,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->tot_run_tasks,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->min_exit_code,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->max_exit_code,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->tot_comp_tasks,
				      buffer);
		}

		safe_unpack32(&job_ptr->assoc_id, buffer);
		safe_unpackstr(&job_ptr->batch_features, buffer);
		safe_unpackstr(&job_ptr->container, buffer);
		safe_unpackstr(&job_ptr->container_id, buffer);
		safe_unpack32(&job_ptr->delay_boot, buffer);
		safe_unpackstr(&job_ptr->failed_node, buffer);
		safe_unpack32(&job_ptr->job_id, buffer);

		/* validity test as possible */
		if (job_ptr->job_id == 0) {
			verbose("Invalid job_id %u", job_ptr->job_id);
			goto unpack_error;
		}

		safe_unpack32(&job_ptr->user_id, buffer);
		safe_unpack32(&job_ptr->group_id, buffer);
		safe_unpack32(&job_ptr->time_limit, buffer);
		safe_unpack32(&job_ptr->time_min, buffer);
		safe_unpack32(&job_ptr->priority, buffer);
		safe_unpack32(&job_ptr->alloc_sid, buffer);
		safe_unpack32(&job_ptr->total_cpus, buffer);
		safe_unpack32(&job_ptr->total_nodes, buffer);
		safe_unpack32(&job_ptr->cpu_cnt, buffer);
		safe_unpack32(&job_ptr->exit_code, buffer);
		safe_unpack32(&job_ptr->derived_ec, buffer);
		safe_unpack64(&job_ptr->db_index, buffer);
		safe_unpack32(&job_ptr->resv_id, buffer);
		safe_unpack32(&job_ptr->next_step_id, buffer);
		safe_unpack32(&job_ptr->het_job_id, buffer);
		safe_unpackstr(&job_ptr->het_job_id_set, buffer);
		safe_unpack32(&job_ptr->het_job_offset, buffer);
		safe_unpack32(&job_ptr->qos_id, buffer);
		safe_unpack32(&job_ptr->req_switch, buffer);
		safe_unpack32(&job_ptr->wait4switch, buffer);
		safe_unpack32(&job_ptr->profile, buffer);
		safe_unpack32(&job_ptr->db_flags, buffer);

		safe_unpack_time(&job_ptr->last_sched_eval, buffer);
		safe_unpack_time(&job_ptr->preempt_time, buffer);
		safe_unpack_time(&job_ptr->prolog_launch_time, buffer);
		safe_unpack_time(&job_ptr->start_time, buffer);
		safe_unpack_time(&job_ptr->end_time, buffer);
		safe_unpack_time(&job_ptr->end_time_exp, buffer);
		safe_unpack_time(&job_ptr->suspend_time, buffer);
		safe_unpack_time(&job_ptr->pre_sus_time, buffer);
		safe_unpack_time(&job_ptr->resize_time, buffer);
		safe_unpack_time(&job_ptr->tot_sus_time, buffer);
		safe_unpack_time(&job_ptr->deadline, buffer);

		safe_unpack32(&job_ptr->site_factor, buffer);
		safe_unpack16(&job_ptr->direct_set_prio, buffer);
		safe_unpack32(&job_ptr->job_state, buffer);
		safe_unpack16(&job_ptr->kill_on_node_fail, buffer);
		safe_unpack16(&job_ptr->batch_flag, buffer);
		safe_unpack16(&job_ptr->mail_type, buffer);
		safe_unpack32(&job_ptr->state_reason, buffer);
		safe_unpack32(&job_ptr->state_reason_prev_db, buffer);
		safe_unpack8 (&job_ptr->reboot, buffer);
		safe_unpack16(&job_ptr->restart_cnt, buffer);
		safe_unpack16(&job_ptr->wait_all_nodes, buffer);
		safe_unpack16(&job_ptr->warn_flags, buffer);
		safe_unpack16(&job_ptr->warn_signal, buffer);
		safe_unpack16(&job_ptr->warn_time, buffer);

		_unpack_acct_policy_limit_members(&job_ptr->limit_set, tres_cnt,
						  buffer, protocol_version);

		safe_unpackstr(&job_ptr->state_desc, buffer);
		safe_unpackstr(&job_ptr->resp_host, buffer);

		safe_unpack16(&job_ptr->alloc_resp_port, buffer);
		safe_unpack16(&job_ptr->other_port, buffer);
		safe_unpackstr(&job_ptr->resv_ports, buffer);
		safe_unpack16(&job_ptr->resv_port_cnt, buffer);
		safe_unpack8(&uint8_tmp, buffer); /* was power_flags */
		safe_unpack16(&job_ptr->start_protocol_ver, buffer);
		safe_unpackdouble(&job_ptr->billable_tres, buffer);

		if (job_ptr->job_state & JOB_COMPLETING)
			safe_unpackstr(&job_ptr->nodes_completing, buffer);
		if (job_ptr->state_reason == WAIT_PROLOG)
			safe_unpackstr(&job_ptr->nodes_pr, buffer);
		safe_unpackstr(&job_ptr->nodes, buffer);
		safe_unpack32(&job_ptr->node_cnt, buffer);
		unpack_bit_str_hex(&job_ptr->node_bitmap, buffer);
		safe_unpackstr(&job_ptr->partition, buffer);
		if (job_ptr->partition == NULL) {
			error("No partition for JobId=%u", job_ptr->job_id);
			goto unpack_error;
		}
		safe_unpackstr(&job_ptr->name, buffer);
		safe_unpackstr(&job_ptr->user_name, buffer);
		safe_unpackstr(&job_ptr->wckey, buffer);
		safe_unpackstr(&job_ptr->alloc_node, buffer);
		safe_unpackstr(&job_ptr->account, buffer);
		safe_unpackstr(&job_ptr->admin_comment, buffer);
		safe_unpackstr(&job_ptr->comment, buffer);
		safe_unpackstr(&job_ptr->extra, buffer);
		safe_unpackstr(&job_ptr->gres_used, buffer);
		safe_unpackstr(&job_ptr->network, buffer);
		safe_unpackstr(&job_ptr->licenses, buffer);
		safe_unpackstr(&job_ptr->lic_req, buffer);
		safe_unpackstr(&job_ptr->mail_user, buffer);
		safe_unpackstr(&job_ptr->mcs_label, buffer);
		safe_unpackstr(&job_ptr->resv_name, buffer);
		safe_unpackstr(&job_ptr->batch_host, buffer);
		safe_unpackstr(&job_ptr->burst_buffer, buffer);
		safe_unpackstr(&job_ptr->burst_buffer_state, buffer);
		safe_unpackstr(&job_ptr->system_comment, buffer);

		if (select_g_select_jobinfo_unpack(&job_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		if (switch_g_unpack_jobinfo(&job_ptr->switch_jobinfo,
					    buffer, protocol_version))
			goto unpack_error;
		if (unpack_job_resources(&job_ptr->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;

		safe_unpackstr_array(&job_ptr->spank_job_env,
				     &job_ptr->spank_job_env_size, buffer);

		if (gres_job_state_unpack(&job_ptr->gres_list_req, buffer,
					  job_ptr->job_id, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_job_state_log(job_ptr->gres_list_req, job_ptr->job_id);

		if (gres_job_state_unpack(&job_ptr->gres_list_alloc, buffer,
					  job_ptr->job_id, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_job_state_log(job_ptr->gres_list_alloc, job_ptr->job_id);

		safe_unpack16(&details, buffer);
		if ((details == DETAILS_FLAG) &&
		    (_load_job_details(job_ptr, buffer, protocol_version))) {
			goto unpack_error;
		}
		safe_unpack16(&step_flag, buffer);

		while (step_flag == STEP_FLAG) {
			/*
			 * No need to put these into accounting if they
			 * haven't been since all information will be
			 * put in when the job is finished.
			 */
			if ((error_code = load_step_state(job_ptr, buffer,
							  protocol_version)))
				goto unpack_error;
			safe_unpack16(&step_flag, buffer);
		}
		safe_unpack64(&job_ptr->bit_flags, buffer);
		job_ptr->bit_flags &= ~BACKFILL_TEST;
		job_ptr->bit_flags &= ~BF_WHOLE_NODE_TEST;
		safe_unpackstr(&job_ptr->tres_alloc_str, buffer);
		safe_unpackstr(&job_ptr->tres_fmt_alloc_str, buffer);
		safe_unpackstr(&job_ptr->tres_req_str, buffer);
		safe_unpackstr(&job_ptr->tres_fmt_req_str, buffer);
		safe_unpackstr(&job_ptr->clusters, buffer);
		if ((error_code = _load_job_fed_details(&job_ptr->fed_details,
							buffer,
							protocol_version)))
			goto unpack_error;

		safe_unpackstr(&job_ptr->origin_cluster, buffer);

		safe_unpackstr(&job_ptr->cpus_per_tres, buffer);
		safe_unpackstr(&job_ptr->mem_per_tres, buffer);
		safe_unpackstr(&job_ptr->tres_bind, buffer);
		safe_unpackstr(&job_ptr->tres_freq, buffer);
		safe_unpackstr(&job_ptr->tres_per_job, buffer);
		safe_unpackstr(&job_ptr->tres_per_node, buffer);
		safe_unpackstr(&job_ptr->tres_per_socket, buffer);
		safe_unpackstr(&job_ptr->tres_per_task, buffer);

		safe_unpackstr(&job_ptr->selinux_context, buffer);

		safe_unpack8(&identity_flag, buffer);
		if (identity_flag) {
			if (unpack_identity(&job_ptr->id, buffer,
					    protocol_version))
				goto unpack_error;
			assoc_mgr_set_uid(job_ptr->user_id,
					  job_ptr->id->pw_name);
		}
	} else if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		safe_unpack32(&job_ptr->array_job_id, buffer);
		safe_unpack32(&job_ptr->array_task_id, buffer);

		/* Job Array record */
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			job_ptr->array_recs =
				xmalloc(sizeof(job_array_struct_t));
			if (count) {
				job_ptr->array_recs->task_id_bitmap =
					bit_alloc(count);
				safe_unpackstr(
					&job_ptr->array_recs->task_id_str,
					buffer);
				if (job_ptr->array_recs->task_id_str) {
					if (bit_unfmt_hexmask(
						    job_ptr->array_recs->
						    task_id_bitmap,
						    job_ptr->array_recs->
						    task_id_str) == -1)
						error("%s: bit_unfmt_hexmask error on '%s'",
						      __func__,
						      job_ptr->array_recs->
						      task_id_str);
				}
				job_ptr->array_recs->task_cnt =
					bit_set_count(job_ptr->array_recs->
						      task_id_bitmap);
			}
			safe_unpack32(&job_ptr->array_recs->array_flags,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->max_run_tasks,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->tot_run_tasks,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->min_exit_code,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->max_exit_code,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->tot_comp_tasks,
				      buffer);
		}

		safe_unpack32(&job_ptr->assoc_id, buffer);
		safe_unpackstr(&job_ptr->batch_features, buffer);
		safe_unpackstr(&job_ptr->container, buffer);
		safe_unpackstr(&job_ptr->container_id, buffer);
		safe_unpack32(&job_ptr->delay_boot, buffer);
		safe_unpackstr(&job_ptr->failed_node, buffer);
		safe_unpack32(&job_ptr->job_id, buffer);

		/* validity test as possible */
		if (job_ptr->job_id == 0) {
			verbose("Invalid job_id %u", job_ptr->job_id);
			goto unpack_error;
		}

		safe_unpack32(&job_ptr->user_id, buffer);
		safe_unpack32(&job_ptr->group_id, buffer);
		safe_unpack32(&job_ptr->time_limit, buffer);
		safe_unpack32(&job_ptr->time_min, buffer);
		safe_unpack32(&job_ptr->priority, buffer);
		safe_unpack32(&job_ptr->alloc_sid, buffer);
		safe_unpack32(&job_ptr->total_cpus, buffer);
		safe_unpack32(&job_ptr->total_nodes, buffer);
		safe_unpack32(&job_ptr->cpu_cnt, buffer);
		safe_unpack32(&job_ptr->exit_code, buffer);
		safe_unpack32(&job_ptr->derived_ec, buffer);
		safe_unpack64(&job_ptr->db_index, buffer);
		safe_unpack32(&job_ptr->resv_id, buffer);
		safe_unpack32(&job_ptr->next_step_id, buffer);
		safe_unpack32(&job_ptr->het_job_id, buffer);
		safe_unpackstr(&job_ptr->het_job_id_set, buffer);
		safe_unpack32(&job_ptr->het_job_offset, buffer);
		safe_unpack32(&job_ptr->qos_id, buffer);
		safe_unpack32(&job_ptr->req_switch, buffer);
		safe_unpack32(&job_ptr->wait4switch, buffer);
		safe_unpack32(&job_ptr->profile, buffer);
		safe_unpack32(&job_ptr->db_flags, buffer);

		safe_unpack_time(&job_ptr->last_sched_eval, buffer);
		safe_unpack_time(&job_ptr->preempt_time, buffer);
		safe_unpack_time(&job_ptr->prolog_launch_time, buffer);
		safe_unpack_time(&job_ptr->start_time, buffer);
		safe_unpack_time(&job_ptr->end_time, buffer);
		safe_unpack_time(&job_ptr->end_time_exp, buffer);
		safe_unpack_time(&job_ptr->suspend_time, buffer);
		safe_unpack_time(&job_ptr->pre_sus_time, buffer);
		safe_unpack_time(&job_ptr->resize_time, buffer);
		safe_unpack_time(&job_ptr->tot_sus_time, buffer);
		safe_unpack_time(&job_ptr->deadline, buffer);

		safe_unpack32(&job_ptr->site_factor, buffer);
		safe_unpack16(&job_ptr->direct_set_prio, buffer);
		safe_unpack32(&job_ptr->job_state, buffer);
		safe_unpack16(&job_ptr->kill_on_node_fail, buffer);
		safe_unpack16(&job_ptr->batch_flag, buffer);
		safe_unpack16(&job_ptr->mail_type, buffer);
		safe_unpack32(&job_ptr->state_reason, buffer);
		safe_unpack32(&job_ptr->state_reason_prev_db, buffer);
		safe_unpack8 (&job_ptr->reboot, buffer);
		safe_unpack16(&job_ptr->restart_cnt, buffer);
		safe_unpack16(&job_ptr->wait_all_nodes, buffer);
		safe_unpack16(&job_ptr->warn_flags, buffer);
		safe_unpack16(&job_ptr->warn_signal, buffer);
		safe_unpack16(&job_ptr->warn_time, buffer);

		_unpack_acct_policy_limit_members(&job_ptr->limit_set, tres_cnt,
						  buffer, protocol_version);

		safe_unpackstr(&job_ptr->state_desc, buffer);
		safe_unpackstr(&job_ptr->resp_host, buffer);

		safe_unpack16(&job_ptr->alloc_resp_port, buffer);
		safe_unpack16(&job_ptr->other_port, buffer);
		safe_unpack8(&uint8_tmp, buffer); /* was power_flags */
		safe_unpack16(&job_ptr->start_protocol_ver, buffer);
		safe_unpackdouble(&job_ptr->billable_tres, buffer);

		if (job_ptr->job_state & JOB_COMPLETING)
			safe_unpackstr(&job_ptr->nodes_completing, buffer);
		if (job_ptr->state_reason == WAIT_PROLOG)
			safe_unpackstr(&job_ptr->nodes_pr, buffer);
		safe_unpackstr(&job_ptr->nodes, buffer);
		safe_unpackstr(&job_ptr->partition, buffer);
		if (job_ptr->partition == NULL) {
			error("No partition for JobId=%u", job_ptr->job_id);
			goto unpack_error;
		}
		safe_unpackstr(&job_ptr->name, buffer);
		safe_unpackstr(&job_ptr->user_name, buffer);
		safe_unpackstr(&job_ptr->wckey, buffer);
		safe_unpackstr(&job_ptr->alloc_node, buffer);
		safe_unpackstr(&job_ptr->account, buffer);
		safe_unpackstr(&job_ptr->admin_comment, buffer);
		safe_unpackstr(&job_ptr->comment, buffer);
		safe_unpackstr(&job_ptr->extra, buffer);
		safe_unpackstr(&job_ptr->gres_used, buffer);
		safe_unpackstr(&job_ptr->network, buffer);
		safe_unpackstr(&job_ptr->licenses, buffer);
		safe_unpackstr(&job_ptr->lic_req, buffer);
		safe_unpackstr(&job_ptr->mail_user, buffer);
		safe_unpackstr(&job_ptr->mcs_label, buffer);
		safe_unpackstr(&job_ptr->resv_name, buffer);
		safe_unpackstr(&job_ptr->batch_host, buffer);
		safe_unpackstr(&job_ptr->burst_buffer, buffer);
		safe_unpackstr(&job_ptr->burst_buffer_state, buffer);
		safe_unpackstr(&job_ptr->system_comment, buffer);

		if (select_g_select_jobinfo_unpack(&job_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		if (unpack_job_resources(&job_ptr->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;

		safe_unpackstr_array(&job_ptr->spank_job_env,
				     &job_ptr->spank_job_env_size, buffer);

		if (gres_job_state_unpack(&job_ptr->gres_list_req, buffer,
					  job_ptr->job_id, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_job_state_log(job_ptr->gres_list_req, job_ptr->job_id);

		if (gres_job_state_unpack(&job_ptr->gres_list_alloc, buffer,
					  job_ptr->job_id, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_job_state_log(job_ptr->gres_list_alloc, job_ptr->job_id);

		safe_unpack16(&details, buffer);
		if ((details == DETAILS_FLAG) &&
		    (_load_job_details(job_ptr, buffer, protocol_version))) {
			goto unpack_error;
		}
		safe_unpack16(&step_flag, buffer);

		while (step_flag == STEP_FLAG) {
			/*
			 * No need to put these into accounting if they
			 * haven't been since all information will be
			 * put in when the job is finished.
			 */
			if ((error_code = load_step_state(job_ptr, buffer,
							  protocol_version)))
				goto unpack_error;
			safe_unpack16(&step_flag, buffer);
		}
		safe_unpack64(&job_ptr->bit_flags, buffer);
		job_ptr->bit_flags &= ~BACKFILL_TEST;
		job_ptr->bit_flags &= ~BF_WHOLE_NODE_TEST;
		safe_unpackstr(&job_ptr->tres_alloc_str, buffer);
		safe_unpackstr(&job_ptr->tres_fmt_alloc_str, buffer);
		safe_unpackstr(&job_ptr->tres_req_str, buffer);
		safe_unpackstr(&job_ptr->tres_fmt_req_str, buffer);
		safe_unpackstr(&job_ptr->clusters, buffer);
		if ((error_code = _load_job_fed_details(&job_ptr->fed_details,
							buffer,
							protocol_version)))
			goto unpack_error;

		safe_unpackstr(&job_ptr->origin_cluster, buffer);

		safe_unpackstr(&job_ptr->cpus_per_tres, buffer);
		safe_unpackstr(&job_ptr->mem_per_tres, buffer);
		safe_unpackstr(&job_ptr->tres_bind, buffer);
		safe_unpackstr(&job_ptr->tres_freq, buffer);
		safe_unpackstr(&job_ptr->tres_per_job, buffer);
		safe_unpackstr(&job_ptr->tres_per_node, buffer);
		safe_unpackstr(&job_ptr->tres_per_socket, buffer);
		safe_unpackstr(&job_ptr->tres_per_task, buffer);

		safe_unpackstr(&job_ptr->selinux_context, buffer);

		safe_unpack8(&identity_flag, buffer);
		if (identity_flag) {
			if (unpack_identity(&job_ptr->id, buffer,
					    protocol_version))
				goto unpack_error;
			assoc_mgr_set_uid(job_ptr->user_id,
					  job_ptr->id->pw_name);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&job_ptr->array_job_id, buffer);
		safe_unpack32(&job_ptr->array_task_id, buffer);

		/* Job Array record */
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			job_ptr->array_recs =
				xmalloc(sizeof(job_array_struct_t));

			if (count) {
				job_ptr->array_recs->task_id_bitmap =
					bit_alloc(count);
				safe_unpackstr(
					&job_ptr->array_recs->task_id_str,
					buffer);
				if (job_ptr->array_recs->task_id_str) {
					if (bit_unfmt_hexmask(
						    job_ptr->array_recs->
						    task_id_bitmap,
						    job_ptr->array_recs->
						    task_id_str) == -1)
						error("%s: bit_unfmt_hexmask error on '%s'",
						      __func__,
						      job_ptr->array_recs->
						      task_id_str);
				}
				job_ptr->array_recs->task_cnt =
					bit_set_count(job_ptr->array_recs->
						      task_id_bitmap);
			}
			safe_unpack32(&job_ptr->array_recs->array_flags,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->max_run_tasks,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->tot_run_tasks,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->min_exit_code,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->max_exit_code,
				      buffer);
			safe_unpack32(&job_ptr->array_recs->tot_comp_tasks,
				      buffer);
		}

		safe_unpack32(&job_ptr->assoc_id, buffer);
		safe_unpackstr(&job_ptr->batch_features, buffer);
		safe_unpackstr(&job_ptr->container, buffer);
		safe_unpackstr(&job_ptr->container_id, buffer);
		safe_unpack32(&job_ptr->delay_boot, buffer);
		safe_unpackstr(&job_ptr->failed_node, buffer);
		safe_unpack32(&job_ptr->job_id, buffer);

		/* validity test as possible */
		if (job_ptr->job_id == 0) {
			verbose("Invalid job_id %u", job_ptr->job_id);
			goto unpack_error;
		}

		safe_unpack32(&job_ptr->user_id, buffer);
		safe_unpack32(&job_ptr->group_id, buffer);
		safe_unpack32(&job_ptr->time_limit, buffer);
		safe_unpack32(&job_ptr->time_min, buffer);
		safe_unpack32(&job_ptr->priority, buffer);
		safe_unpack32(&job_ptr->alloc_sid, buffer);
		safe_unpack32(&job_ptr->total_cpus, buffer);
		safe_unpack32(&job_ptr->total_nodes, buffer);
		safe_unpack32(&job_ptr->cpu_cnt, buffer);
		safe_unpack32(&job_ptr->exit_code, buffer);
		safe_unpack32(&job_ptr->derived_ec, buffer);
		safe_unpack64(&job_ptr->db_index, buffer);
		safe_unpack32(&job_ptr->resv_id, buffer);
		safe_unpack32(&job_ptr->next_step_id, buffer);
		safe_unpack32(&job_ptr->het_job_id, buffer);
		safe_unpackstr(&job_ptr->het_job_id_set, buffer);
		safe_unpack32(&job_ptr->het_job_offset, buffer);
		safe_unpack32(&job_ptr->qos_id, buffer);
		safe_unpack32(&job_ptr->req_switch, buffer);
		safe_unpack32(&job_ptr->wait4switch, buffer);
		safe_unpack32(&job_ptr->profile, buffer);
		safe_unpack32(&job_ptr->db_flags, buffer);

		safe_unpack_time(&job_ptr->last_sched_eval, buffer);
		safe_unpack_time(&job_ptr->preempt_time, buffer);
		safe_unpack_time(&job_ptr->start_time, buffer);
		safe_unpack_time(&job_ptr->end_time, buffer);
		safe_unpack_time(&job_ptr->end_time_exp, buffer);
		safe_unpack_time(&job_ptr->suspend_time, buffer);
		safe_unpack_time(&job_ptr->pre_sus_time, buffer);
		safe_unpack_time(&job_ptr->resize_time, buffer);
		safe_unpack_time(&job_ptr->tot_sus_time, buffer);
		safe_unpack_time(&job_ptr->deadline, buffer);

		safe_unpack32(&job_ptr->site_factor, buffer);
		safe_unpack16(&job_ptr->direct_set_prio, buffer);
		safe_unpack32(&job_ptr->job_state, buffer);
		safe_unpack16(&job_ptr->kill_on_node_fail, buffer);
		safe_unpack16(&job_ptr->batch_flag, buffer);
		safe_unpack16(&job_ptr->mail_type, buffer);
		safe_unpack32(&job_ptr->state_reason, buffer);
		safe_unpack32(&job_ptr->state_reason_prev_db, buffer);
		safe_unpack8 (&job_ptr->reboot, buffer);
		safe_unpack16(&job_ptr->restart_cnt, buffer);
		safe_unpack16(&job_ptr->wait_all_nodes, buffer);
		safe_unpack16(&job_ptr->warn_flags, buffer);
		safe_unpack16(&job_ptr->warn_signal, buffer);
		safe_unpack16(&job_ptr->warn_time, buffer);

		_unpack_acct_policy_limit_members(&job_ptr->limit_set, tres_cnt,
						  buffer, protocol_version);

		safe_unpackstr(&job_ptr->state_desc, buffer);
		safe_unpackstr(&job_ptr->resp_host, buffer);

		safe_unpack16(&job_ptr->alloc_resp_port, buffer);
		safe_unpack16(&job_ptr->other_port, buffer);
		safe_unpack8(&uint8_tmp, buffer); /* was power_flags */
		safe_unpack16(&job_ptr->start_protocol_ver, buffer);
		safe_unpackdouble(&job_ptr->billable_tres, buffer);

		if (job_ptr->job_state & JOB_COMPLETING)
			safe_unpackstr(&job_ptr->nodes_completing, buffer);
		if (job_ptr->state_reason == WAIT_PROLOG)
			safe_unpackstr(&job_ptr->nodes_pr, buffer);
		safe_unpackstr(&job_ptr->nodes, buffer);
		safe_unpackstr(&job_ptr->partition, buffer);
		if (job_ptr->partition == NULL) {
			error("No partition for JobId=%u", job_ptr->job_id);
			goto unpack_error;
		}
		safe_unpackstr(&job_ptr->name, buffer);
		safe_unpackstr(&job_ptr->user_name, buffer);
		safe_unpackstr(&job_ptr->wckey, buffer);
		safe_unpackstr(&job_ptr->alloc_node, buffer);
		safe_unpackstr(&job_ptr->account, buffer);
		safe_unpackstr(&job_ptr->admin_comment, buffer);
		safe_unpackstr(&job_ptr->comment, buffer);
		safe_unpackstr(&job_ptr->extra, buffer);
		safe_unpackstr(&job_ptr->gres_used, buffer);
		safe_unpackstr(&job_ptr->network, buffer);
		safe_unpackstr(&job_ptr->licenses, buffer);
		safe_unpackstr(&job_ptr->lic_req, buffer);
		safe_unpackstr(&job_ptr->mail_user, buffer);
		safe_unpackstr(&job_ptr->mcs_label, buffer);
		safe_unpackstr(&job_ptr->resv_name, buffer);
		safe_unpackstr(&job_ptr->batch_host, buffer);
		safe_unpackstr(&job_ptr->burst_buffer, buffer);
		safe_unpackstr(&job_ptr->burst_buffer_state, buffer);
		safe_unpackstr(&job_ptr->system_comment, buffer);

		if (select_g_select_jobinfo_unpack(&job_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		if (unpack_job_resources(&job_ptr->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;

		safe_unpackstr_array(&job_ptr->spank_job_env,
				     &job_ptr->spank_job_env_size, buffer);

		if (gres_job_state_unpack(&job_ptr->gres_list_req, buffer,
					  job_ptr->job_id, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_job_state_log(job_ptr->gres_list_req, job_ptr->job_id);

		if (gres_job_state_unpack(&job_ptr->gres_list_alloc, buffer,
					  job_ptr->job_id, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_job_state_log(job_ptr->gres_list_alloc, job_ptr->job_id);

		safe_unpack16(&details, buffer);
		if ((details == DETAILS_FLAG) &&
		    (_load_job_details(job_ptr, buffer, protocol_version))) {
			goto unpack_error;
		}
		safe_unpack16(&step_flag, buffer);

		while (step_flag == STEP_FLAG) {
			/*
			 * No need to put these into accounting if they
			 * haven't been since all information will be
			 * put in when the job is finished.
			 */
			if ((error_code = load_step_state(job_ptr, buffer,
							  protocol_version)))
				goto unpack_error;
			safe_unpack16(&step_flag, buffer);
		}
		safe_unpack64(&job_ptr->bit_flags, buffer);
		job_ptr->bit_flags &= ~BACKFILL_TEST;
		job_ptr->bit_flags &= ~BF_WHOLE_NODE_TEST;
		safe_unpackstr(&job_ptr->tres_alloc_str, buffer);
		safe_unpackstr(&job_ptr->tres_fmt_alloc_str, buffer);
		safe_unpackstr(&job_ptr->tres_req_str, buffer);
		safe_unpackstr(&job_ptr->tres_fmt_req_str, buffer);
		safe_unpackstr(&job_ptr->clusters, buffer);
		if ((error_code = _load_job_fed_details(&job_ptr->fed_details,
							buffer,
							protocol_version)))
			goto unpack_error;

		safe_unpackstr(&job_ptr->origin_cluster, buffer);

		safe_unpackstr(&job_ptr->cpus_per_tres, buffer);
		slurm_format_tres_string(&job_ptr->cpus_per_tres, "gres");

		safe_unpackstr(&job_ptr->mem_per_tres, buffer);
		slurm_format_tres_string(&job_ptr->mem_per_tres, "gres");

		safe_unpackstr(&job_ptr->tres_bind, buffer);
		safe_unpackstr(&job_ptr->tres_freq, buffer);

		safe_unpackstr(&job_ptr->tres_per_job, buffer);
		slurm_format_tres_string(&job_ptr->tres_per_job, "gres");

		safe_unpackstr(&job_ptr->tres_per_node, buffer);
		slurm_format_tres_string(&job_ptr->tres_per_node, "gres");

		safe_unpackstr(&job_ptr->tres_per_socket, buffer);
		slurm_format_tres_string(&job_ptr->tres_per_socket, "gres");

		safe_unpackstr(&job_ptr->tres_per_task, buffer);
		slurm_format_tres_string(&job_ptr->tres_per_task, "gres");

		safe_unpackstr(&job_ptr->selinux_context, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	job_record_delete(job_ptr);
	*out = NULL;
	return SLURM_ERROR;
}

extern step_record_t *create_step_record(job_record_t *job_ptr,
					 uint16_t protocol_version)
{
	step_record_t *step_ptr;

	xassert(job_ptr);
	/* NOTE: Reserve highest step ID values for
	 * SLURM_EXTERN_CONT and SLURM_BATCH_SCRIPT and any other
	 * special step that may come our way. */
	if (job_ptr->next_step_id >= SLURM_MAX_NORMAL_STEP_ID) {
		/* avoid step records in the accounting database */
		info("%pJ has reached step id limit", job_ptr);
		return NULL;
	}

	step_ptr = xmalloc(sizeof(*step_ptr));

	step_ptr->job_ptr    = job_ptr;
	step_ptr->exit_code  = NO_VAL;
	step_ptr->time_limit = INFINITE;
	step_ptr->jobacct    = jobacctinfo_create(NULL);
	step_ptr->requid     = -1;
	if (protocol_version)
		step_ptr->start_protocol_ver = protocol_version;
	else
		step_ptr->start_protocol_ver = job_ptr->start_protocol_ver;

	step_ptr->magic = STEP_MAGIC;
	list_append(job_ptr->step_list, step_ptr);

	return step_ptr;
}

extern int find_step_id(void *object, void *key)
{
	step_record_t *step_ptr = (step_record_t *)object;
	slurm_step_id_t *step_id = (slurm_step_id_t *)key;

	return verify_step_id(&step_ptr->step_id, step_id);
}

step_record_t *find_step_record(job_record_t *job_ptr, slurm_step_id_t *step_id)
{
	if (job_ptr == NULL)
		return NULL;

	return list_find_first(job_ptr->step_list, find_step_id, step_id);
}

extern void update_job_limit_set_tres(uint16_t **limits_pptr, int tres_cnt)
{
	int i, old_pos;
	int new_size = sizeof(uint16_t) * tres_cnt;

	xassert(limits_pptr);

	*limits_pptr = xrealloc(*limits_pptr, new_size);

	if (assoc_mgr_tres_pos_changed()) {
		uint16_t *limits_ptr, tmp_tres[tres_cnt];
		limits_ptr = *limits_pptr;

		for (i = 0; i < tres_cnt; i++) {
			if ((old_pos = assoc_mgr_get_old_tres_pos(i)) == -1)
				tmp_tres[i] = 0;
			else
				tmp_tres[i] = limits_ptr[old_pos];
		}
		memcpy(limits_ptr, tmp_tres, new_size);
	}
}

extern void job_record_set_sluid(job_record_t *job_ptr)
{
	job_ptr->db_index = generate_sluid();
	job_ptr->db_flags &= ~SLURMDB_JOB_FLAG_START_R;
}
