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
