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
#include "src/common/slurm_protocol_pack.h"

#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"

#define DETAILS_FLAG 0xdddd

extern job_record_t *create_job_record(void)
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
	if (step_ptr->switch_job) {
		if (step_ptr->step_layout)
			switch_g_job_step_complete(
				step_ptr->switch_job,
				step_ptr->step_layout->node_list);
		switch_g_free_jobinfo (step_ptr->switch_job);
	}
	resv_port_free(step_ptr);

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

/*
 * _delete_job_details - delete a job's detail record and clear it's pointer
 * IN job_entry - pointer to job_record to clear the record of
 */
static void _delete_job_details(job_record_t *job_entry)
{
	int i;

	if (job_entry->details == NULL)
		return;

	xassert (job_entry->details->magic == DETAILS_MAGIC);

	xfree(job_entry->details->acctg_freq);
	for (i=0; i<job_entry->details->argc; i++)
		xfree(job_entry->details->argv[i]);
	xfree(job_entry->details->argv);
	xfree(job_entry->details->cpu_bind);
	free_cron_entry(job_entry->details->crontab_entry);
	FREE_NULL_LIST(job_entry->details->depend_list);
	xfree(job_entry->details->dependency);
	xfree(job_entry->details->orig_dependency);
	xfree(job_entry->details->env_hash);
	for (i=0; i<job_entry->details->env_cnt; i++)
		xfree(job_entry->details->env_sup[i]);
	xfree(job_entry->details->env_sup);
	xfree(job_entry->details->std_err);
	FREE_NULL_BITMAP(job_entry->details->exc_node_bitmap);
	xfree(job_entry->details->exc_nodes);
	FREE_NULL_LIST(job_entry->details->feature_list);
	xfree(job_entry->details->features);
	xfree(job_entry->details->cluster_features);
	FREE_NULL_BITMAP(job_entry->details->job_size_bitmap);
	xfree(job_entry->details->std_in);
	xfree(job_entry->details->mc_ptr);
	xfree(job_entry->details->mem_bind);
	FREE_NULL_LIST(job_entry->details->prefer_list);
	xfree(job_entry->details->prefer);
	xfree(job_entry->details->req_context);
	xfree(job_entry->details->std_out);
	xfree(job_entry->details->submit_line);
	FREE_NULL_BITMAP(job_entry->details->req_node_bitmap);
	xfree(job_entry->details->req_nodes);
	xfree(job_entry->details->script);
	xfree(job_entry->details->script_hash);
	xfree(job_entry->details->arbitrary_tpn);
	xfree(job_entry->details->work_dir);
	xfree(job_entry->details->x11_magic_cookie);
	xfree(job_entry->details->x11_target);
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
		xfree(job_ptr->part_prio->priority_array_parts);
		xfree(job_ptr->part_prio);
	}
	slurm_destroy_priority_factors(job_ptr->prio_factors);
	xfree(job_ptr->resp_host);
	FREE_NULL_LIST(job_ptr->resv_list);
	xfree(job_ptr->resv_name);
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
 */
static void _dump_job_details(job_details_t *detail_ptr, buf_t *buffer)
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
	pack16(detail_ptr->plane_size, buffer);

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
	pack_dep_list(detail_ptr->depend_list, buffer, SLURM_PROTOCOL_VERSION);
	packstr(detail_ptr->dependency, buffer);
	packstr(detail_ptr->orig_dependency, buffer);	/* subject to change */

	packstr(detail_ptr->std_err, buffer);
	packstr(detail_ptr->std_in, buffer);
	packstr(detail_ptr->std_out, buffer);
	packstr(detail_ptr->submit_line, buffer);
	packstr(detail_ptr->work_dir, buffer);

	pack_multi_core_data(detail_ptr->mc_ptr, buffer,
			     SLURM_PROTOCOL_VERSION);
	packstr_array(detail_ptr->argv, detail_ptr->argc, buffer);
	packstr_array(detail_ptr->env_sup, detail_ptr->env_cnt, buffer);

	pack_cron_entry(detail_ptr->crontab_entry, SLURM_PROTOCOL_VERSION,
			buffer);
	packstr(detail_ptr->env_hash, buffer);
	packstr(detail_ptr->script_hash, buffer);
}

static void _dump_job_fed_details(job_fed_details_t *fed_details_ptr,
				  buf_t *buffer)
{
	if (fed_details_ptr) {
		pack16(1, buffer);
		pack32(fed_details_ptr->cluster_lock, buffer);
		packstr(fed_details_ptr->origin_str, buffer);
		pack64(fed_details_ptr->siblings_active, buffer);
		packstr(fed_details_ptr->siblings_active_str, buffer);
		pack64(fed_details_ptr->siblings_viable, buffer);
		packstr(fed_details_ptr->siblings_viable_str, buffer);
	} else {
		pack16(0, buffer);
	}
}

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

	if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
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
				packstr(dump_job_ptr->array_recs->task_id_str, buffer);
			pack32(dump_job_ptr->array_recs->array_flags,    buffer);
			pack32(dump_job_ptr->array_recs->max_run_tasks,  buffer);
			pack32(dump_job_ptr->array_recs->tot_run_tasks,  buffer);
			pack32(dump_job_ptr->array_recs->min_exit_code,  buffer);
			pack32(dump_job_ptr->array_recs->max_exit_code,  buffer);
			pack32(dump_job_ptr->array_recs->tot_comp_tasks, buffer);
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
					tres_cnt, SLURM_PROTOCOL_VERSION);

		packstr(dump_job_ptr->state_desc, buffer);
		packstr(dump_job_ptr->resp_host, buffer);

		pack16(dump_job_ptr->alloc_resp_port, buffer);
		pack16(dump_job_ptr->other_port, buffer);
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
					     buffer, SLURM_PROTOCOL_VERSION);
		pack_job_resources(dump_job_ptr->job_resrcs, buffer,
				   SLURM_PROTOCOL_VERSION);

		packstr_array(dump_job_ptr->spank_job_env,
			      dump_job_ptr->spank_job_env_size, buffer);

		(void) gres_job_state_pack(dump_job_ptr->gres_list_req, buffer,
					   dump_job_ptr->job_id, true,
					   SLURM_PROTOCOL_VERSION);

		(void) gres_job_state_pack(dump_job_ptr->gres_list_alloc,
					   buffer, dump_job_ptr->job_id,
					   true, SLURM_PROTOCOL_VERSION);

		/* Dump job details, if available */
		detail_ptr = dump_job_ptr->details;
		if (detail_ptr) {
			xassert (detail_ptr->magic == DETAILS_MAGIC);
			pack16((uint16_t) DETAILS_FLAG, buffer);
			_dump_job_details(detail_ptr, buffer);
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
		_dump_job_fed_details(dump_job_ptr->fed_details, buffer);

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
				      SLURM_PROTOCOL_VERSION);
		} else {
			pack8(0, buffer);
		}
	}

	return SLURM_SUCCESS;
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
	slurm_node_alias_addrs_t *alias_addrs_tmp;

	if (step_ptr->state < JOB_RUNNING)
		return 0;

	pack16((uint16_t) STEP_FLAG, buffer);

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
				    SLURM_PROTOCOL_VERSION);
	(void) gres_step_state_pack(step_ptr->gres_list_alloc, buffer,
				    &step_ptr->step_id,
				    SLURM_PROTOCOL_VERSION);
	/*
	 * Don't dump alias_addrs
	 */
	alias_addrs_tmp = step_ptr->step_layout->alias_addrs;
	step_ptr->step_layout->alias_addrs = NULL;
	pack_slurm_step_layout(step_ptr->step_layout, buffer,
			       SLURM_PROTOCOL_VERSION);
	step_ptr->step_layout->alias_addrs = alias_addrs_tmp;

	if (step_ptr->switch_job) {
		pack8(1, buffer);
		switch_g_pack_jobinfo(step_ptr->switch_job, buffer,
				      SLURM_PROTOCOL_VERSION);
	} else
		pack8(0, buffer);

	select_g_select_jobinfo_pack(step_ptr->select_jobinfo, buffer,
				     SLURM_PROTOCOL_VERSION);
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
	jobacctinfo_pack(step_ptr->jobacct, SLURM_PROTOCOL_VERSION,
	                 PROTOCOL_TYPE_SLURM, buffer);

	if (step_ptr->memory_allocated &&
	    step_ptr->step_layout &&
	    step_ptr->step_layout->node_cnt)
		pack64_array(step_ptr->memory_allocated,
			     step_ptr->step_layout->node_cnt, buffer);
	else
		pack64_array(step_ptr->memory_allocated, 0, buffer);

	return 0;
}
