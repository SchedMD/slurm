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
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"

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
