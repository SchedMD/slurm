/*****************************************************************************\
 *  gres_select_util.c - filters used in the select plugin
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Derived in large part from code previously in common/gres.h
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

#include "src/common/slurm_xlator.h"

#include "gres_select_util.h"

#include "src/common/xstring.h"

/*
 * Set job default parameters in a given element of a list
 * IN job_gres_list - job's gres_list built by gres_job_state_validate()
 * IN gres_name - name of gres, apply defaults to all elements (e.g. updates to
 *		  gres_name="gpu" would apply to "gpu:tesla", "gpu:volta", etc.)
 * IN cpu_per_gpu - value to set as default
 * IN mem_per_gpu - value to set as default
 * OUT *cpus_per_tres - CpusPerTres string displayed by scontrol show job
 * OUT *mem_per_tres - MemPerTres string displayed by scontrol show job
 * IN/OUT *cpus_per_task - Increased if cpu_per_gpu * gres_per_task is more than
 *                         *cpus_per_task
 */
extern void gres_select_util_job_set_defs(List job_gres_list,
					  char *gres_name,
					  uint64_t cpu_per_gpu,
					  uint64_t mem_per_gpu,
					  char **cpus_per_tres,
					  char **mem_per_tres,
					  uint16_t *cpus_per_task)
{
	uint32_t plugin_id;
	ListIterator gres_iter;
	gres_state_t *gres_state_job = NULL;
	gres_job_state_t *gres_js;

	/*
	 * Currently only GPU supported, check how cpus_per_tres/mem_per_tres
	 * is handled in _fill_job_desc_from_sbatch_opts and
	 * _job_desc_msg_create_from_opts.
	 */
	xassert(!xstrcmp(gres_name, "gpu"));

	if (!job_gres_list)
		return;

	plugin_id = gres_build_id(gres_name);
	gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(gres_iter))) {
		if (gres_state_job->plugin_id != plugin_id)
			continue;
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (!gres_js)
			continue;
		gres_js->def_cpus_per_gres = cpu_per_gpu;
		gres_js->def_mem_per_gres = mem_per_gpu;
		if (!gres_js->cpus_per_gres) {
			xfree(*cpus_per_tres);
			if (cpu_per_gpu)
				xstrfmtcat(*cpus_per_tres, "gpu:%"PRIu64,
					   cpu_per_gpu);
		}
		if (!gres_js->mem_per_gres) {
			xfree(*mem_per_tres);
			if (mem_per_gpu)
				xstrfmtcat(*mem_per_tres, "gpu:%"PRIu64,
					   mem_per_gpu);
		}
		if (cpu_per_gpu && gres_js->gres_per_task) {
			*cpus_per_task = MAX(*cpus_per_task,
					     (gres_js->gres_per_task *
					      cpu_per_gpu));
		}
	}
	list_iterator_destroy(gres_iter);
}

/*
 * Determine the minimum number of CPUs required to satify the job's GRES
 *	request on one node
 * sockets_per_node IN - count of sockets per node in job allocation
 * tasks_per_node IN - count of tasks per node in job allocation
 * job_gres_list IN - job GRES specification
 * RET count of required CPUs for the job
 */
extern int gres_select_util_job_min_cpu_node(uint32_t sockets_per_node,
					     uint32_t tasks_per_node,
					     List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *gres_state_job;
	gres_job_state_t  *gres_js;
	int tmp, min_cpus = 0;
	uint16_t cpus_per_gres;

	if (!job_gres_list || (list_count(job_gres_list) == 0))
		return 0;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		uint64_t total_gres = 0;
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (gres_js->cpus_per_gres)
			cpus_per_gres = gres_js->cpus_per_gres;
		else
			cpus_per_gres = gres_js->def_cpus_per_gres;
		if (cpus_per_gres == 0)
			continue;
		if (gres_js->gres_per_node) {
			total_gres = gres_js->gres_per_node;
		} else if (gres_js->gres_per_socket) {
			total_gres = gres_js->gres_per_socket *
				sockets_per_node;
		} else if (gres_js->gres_per_task) {
			total_gres = gres_js->gres_per_task *
				tasks_per_node;
		} else
			total_gres = 1;
		tmp = cpus_per_gres * total_gres;
		min_cpus = MAX(min_cpus, tmp);
	}
	list_iterator_destroy(job_gres_iter);
	return min_cpus;
}

/*
 * Determine the minimum number of tasks required to satisfy the job's GRES
 *	request (based upon total GRES times ntasks_per_tres value). If
 *	ntasks_per_tres is not specified, returns 0.
 * node_count IN - count of nodes in job allocation
 * sockets_per_node IN - count of sockets per node in job allocation
 * ntasks_per_tres IN - # of tasks per GPU
 * gres_name IN - (optional) Filter GRES by name. If NULL, check all GRES
 * job_gres_list IN - job GRES specification
 * RET count of required tasks for the job
 */
extern int gres_select_util_job_min_tasks(uint32_t node_count,
					  uint32_t sockets_per_node,
					  uint16_t ntasks_per_tres,
					  char *gres_name,
					  List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	int tmp, min_tasks = 0;
	uint32_t plugin_id = 0;

	if (!ntasks_per_tres || (ntasks_per_tres == NO_VAL16))
		return 0;

	if (!job_gres_list || (list_count(job_gres_list) == 0))
		return 0;

	if (gres_name && (gres_name[0] != '\0'))
		plugin_id = gres_build_id(gres_name);

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = list_next(job_gres_iter))) {
		uint64_t total_gres = 0;
		/* Filter on GRES name, if specified */
		if (plugin_id && (plugin_id != gres_state_job->plugin_id))
			continue;

		gres_js = (gres_job_state_t *)gres_state_job->gres_data;

		if (gres_js->gres_per_job) {
			total_gres = gres_js->gres_per_job;
		} else if (gres_js->gres_per_node) {
			total_gres = gres_js->gres_per_node * node_count;
		} else if (gres_js->gres_per_socket) {
			total_gres = gres_js->gres_per_socket * node_count *
				sockets_per_node;
		} else if (gres_js->gres_per_task) {
			error("%s: gres_per_task and ntasks_per_tres conflict",
			      __func__);
		} else
			continue;

		tmp = ntasks_per_tres * total_gres;
		min_tasks = MAX(min_tasks, tmp);
	}
	list_iterator_destroy(job_gres_iter);
	return min_tasks;
}

/*
 * Set per-node memory limits based upon GRES assignments
 * RET TRUE if mem-per-tres specification used to set memory limits
 */
extern bool gres_select_util_job_mem_set(List job_gres_list,
					 job_resources_t *job_res)
{
	ListIterator job_gres_iter;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	bool rc = false, first_set = true;
	uint64_t gres_cnt, mem_size, mem_per_gres;
	int i, i_first, i_last, node_off;

	if (!job_gres_list)
		return false;

	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first < 0)
		return false;
	i_last = bit_fls(job_res->node_bitmap);

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = list_next(job_gres_iter))) {
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (gres_js->mem_per_gres)
			mem_per_gres = gres_js->mem_per_gres;
		else
			mem_per_gres = gres_js->def_mem_per_gres;
		/*
		 * The logic below is correct because the only mem_per_gres
		 * is --mem-per-gpu adding another option will require change
		 * to take MAX of mem_per_gres for all types.
		 * Similar logic is in _step_alloc() (which is called by
		 * gres_ctld_step_alloc()), which would also need to be changed
		 * if another mem_per_gres option was added.
		 */
		if ((mem_per_gres == 0) || !gres_js->gres_cnt_node_select)
			continue;
		rc = true;
		node_off = -1;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			node_off++;
			if (job_res->whole_node == 1) {
				gres_state_t *gres_state_node;
				gres_node_state_t *gres_ns;

				gres_state_node = list_find_first(
					node_record_table_ptr[i]->gres_list,
					gres_find_id,
					&gres_state_job->plugin_id);
				if (!gres_state_node)
					continue;
				gres_ns = gres_state_node->gres_data;
				gres_cnt = gres_ns->gres_cnt_avail;
			} else
				gres_cnt =
					gres_js->gres_cnt_node_select[i];
			mem_size = mem_per_gres * gres_cnt;
			if (first_set)
				job_res->memory_allocated[node_off] = mem_size;
			else
				job_res->memory_allocated[node_off] += mem_size;
		}
		first_set = false;
	}
	list_iterator_destroy(job_gres_iter);

	return rc;
}

/*
 * Determine the minimum number of CPUs required to satify the job's GRES
 *	request (based upon total GRES times cpus_per_gres value)
 * node_count IN - count of nodes in job allocation
 * sockets_per_node IN - count of sockets per node in job allocation
 * task_count IN - count of tasks in job allocation
 * job_gres_list IN - job GRES specification
 * RET count of required CPUs for the job
 */
extern int gres_select_util_job_min_cpus(uint32_t node_count,
					 uint32_t sockets_per_node,
					 uint32_t task_count,
					 List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *gres_state_job;
	gres_job_state_t  *gres_js;
	int tmp, min_cpus = 0;
	uint16_t cpus_per_gres;

	if (!job_gres_list || (list_count(job_gres_list) == 0))
		return 0;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		uint64_t total_gres = 0;
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (gres_js->cpus_per_gres)
			cpus_per_gres = gres_js->cpus_per_gres;
		else
			cpus_per_gres = gres_js->def_cpus_per_gres;
		if (cpus_per_gres == 0)
			continue;
		if (gres_js->gres_per_job) {
			total_gres = gres_js->gres_per_job;
		} else if (gres_js->gres_per_node) {
			total_gres = gres_js->gres_per_node *
				node_count;
		} else if (gres_js->gres_per_socket) {
			total_gres = gres_js->gres_per_socket *
				node_count * sockets_per_node;
		} else if (gres_js->gres_per_task) {
			total_gres = gres_js->gres_per_task * task_count;
		} else
			continue;
		tmp = cpus_per_gres * total_gres;
		min_cpus = MAX(min_cpus, tmp);
	}
	list_iterator_destroy(job_gres_iter);
	return min_cpus;
}

/*
 * Determine if the job GRES specification includes a mem-per-tres specification
 * RET largest mem-per-tres specification found
 */
extern uint64_t gres_select_util_job_mem_max(List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	uint64_t mem_max = 0, mem_per_gres;

	if (!job_gres_list)
		return 0;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (gres_js->mem_per_gres)
			mem_per_gres = gres_js->mem_per_gres;
		else
			mem_per_gres = gres_js->def_mem_per_gres;
		mem_max = MAX(mem_max, mem_per_gres);
	}
	list_iterator_destroy(job_gres_iter);

	return mem_max;
}

/*
 * Determine if job GRES specification includes a tres-per-task specification
 * RET TRUE if any GRES requested by the job include a tres-per-task option
 */
extern bool gres_select_util_job_tres_per_task(List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	bool have_gres_per_task = false;

	if (!job_gres_list)
		return false;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = list_next(job_gres_iter))) {
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (gres_js->gres_per_task) {
			have_gres_per_task = true;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);

	return have_gres_per_task;
}

/*
 * Return the maximum number of tasks that can be started on a node with
 * sock_gres_list (per-socket GRES details for some node)
 */
extern uint32_t gres_select_util_get_task_limit(List sock_gres_list)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	uint32_t max_tasks = NO_VAL;
	uint64_t task_limit;

	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = list_next(sock_gres_iter))) {
		gres_job_state_t *gres_js;
		xassert(sock_gres->gres_state_job);
		gres_js = sock_gres->gres_state_job->gres_data;
		if (gres_js->gres_per_task == 0)
			continue;
		task_limit = sock_gres->total_cnt / gres_js->gres_per_task;
		max_tasks = MIN(max_tasks, task_limit);
	}
	list_iterator_destroy(sock_gres_iter);

	return max_tasks;
}

static int _accumulate_gres_device_req(void *x, void *arg)
{
	gres_state_t *gres_state_job = x, *new_gres_state_job;
	List new_gres_list = arg;

	if ((new_gres_state_job = list_find_first(
		     new_gres_list,
		     gres_find_id,
		     &gres_state_job->plugin_id))) {
		gres_job_state_t *accum_gres_js =
			new_gres_state_job->gres_data;
		gres_job_state_t *gres_js = gres_state_job->gres_data;

		/*
		 * Add up gres counts but cpus_per_gres and mem_per_gres should
		 * be same.
		 */
		accum_gres_js->gres_per_job += gres_js->gres_per_job;
		accum_gres_js->gres_per_node += gres_js->gres_per_node;
		accum_gres_js->gres_per_socket += gres_js->gres_per_socket;
		accum_gres_js->gres_per_task += gres_js->gres_per_task;
		accum_gres_js->total_gres += gres_js->total_gres;
	} else {
		gres_job_state_t *gres_js = gres_job_state_dup(
			gres_state_job->gres_data);
		/*
		 * The type id or name should never be set here as we should
		 * only have counters here for the gres_per_* counters based on
		 * cpus/mem per_gres.
		 */
		xfree(gres_js->type_name);
		gres_js->type_id = 0;

		new_gres_state_job = gres_create_state(
			gres_state_job, GRES_STATE_SRC_STATE_PTR,
			GRES_STATE_TYPE_JOB, gres_js);
		list_append(new_gres_list, new_gres_state_job);
	}

	return 0;
}


/*
 * Create a (partial) copy of a job's gres state accumlating the gres_per_*
 * requirements to accuratly calculate cpus_per_gres
 * IN gres_list - List of Gres records
 * RET The copy of list or NULL on failure
 */
extern List gres_select_util_create_list_req_accum(List gres_list)
{
	List new_gres_list;

	if (!gres_list)
		return NULL;

	new_gres_list = list_create(gres_job_list_delete);

	(void) list_for_each(gres_list, _accumulate_gres_device_req,
			     new_gres_list);

	return new_gres_list;
}
