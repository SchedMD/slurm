/*****************************************************************************\
 *  array_unique.c - handle --array-unique option for sacct.
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC
 *  Written by Ben Glines <ben.glines@schedmd.com>
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

#include "src/sacct/sacct.h"

static int _calc_alloc_cpus(slurmdb_job_rec_t *job)
{
	return slurmdb_find_tres_count_in_string((job->tres_alloc_str &&
						  job->tres_alloc_str[0]) ?
							 job->tres_alloc_str :
							 job->tres_req_str,
						 TRES_CPU);
}

static int _calc_nnodes(slurmdb_job_rec_t *job)
{
	uint32_t nnodes = NO_VAL;
	uint64_t tmp_uint64 = NO_VAL;
	char *tmp_char = NULL;
	nnodes = job->alloc_nodes;
	tmp_char = (job->tres_alloc_str && job->tres_alloc_str[0]) ?
			   job->tres_alloc_str :
			   job->tres_req_str;
	if (!nnodes && tmp_char) {
		if ((tmp_uint64 = slurmdb_find_tres_count_in_string(
			     tmp_char, TRES_NODE)) != INFINITE64)
			nnodes = tmp_uint64;
	}
	return nnodes;
}

static int _calc_planned(slurmdb_job_rec_t *job)
{
	time_t planned;
	if (!job->eligible || (job->eligible == INFINITE))
		planned = 0;
	else if (job->start)
		planned = job->start - job->eligible;
	else
		planned = time(NULL) - job->eligible;
	return planned;
}

static int _calc_planned_cpu(slurmdb_job_rec_t *job)
{
	time_t planned_cpu;
	if (!job->eligible || (job->eligible == INFINITE))
		planned_cpu = 0;
	else if (job->start)
		planned_cpu = (job->start - job->eligible) * job->req_cpus;
	else
		planned_cpu = (time(NULL) - job->eligible) * job->req_cpus;
	return planned_cpu;
}

static int _calc_req_nodes(slurmdb_job_rec_t *job)
{
	uint32_t req_nodes = NO_VAL;
	uint64_t tmp_uint64 = NO_VAL;
	char *tmp_char = NULL;

	tmp_char = job->tres_req_str;
	if ((tmp_uint64 = slurmdb_find_tres_count_in_string(
		     tmp_char, TRES_NODE)) != INFINITE64)
		req_nodes = tmp_uint64;
	return req_nodes;
}

static int _calc_uid(slurmdb_job_rec_t *job)
{
	struct passwd *pw = NULL;
	uint32_t uid = NO_VAL;

	if (params.use_local_uid && job->user && (pw = getpwnam(job->user)))
		uid = pw->pw_uid;
	else
		uid = job->uid;
	return uid;
}

static char *_calc_user(slurmdb_job_rec_t *job)
{
	struct passwd *pw = NULL;
	char *user = NULL;
	if (job->user)
		user = job->user;
	else if ((pw = getpwuid(job->uid)))
		user = pw->pw_name;
	return user;
}
static bool _is_print_field_unique(slurmdb_job_rec_t *job,
				   slurmdb_job_rec_t job_key,
				   sacct_print_types_t type)
{
	switch (type) {
	case PRINT_ACCOUNT:
		return xstrcmp(job->account, job_key.account);
	case PRINT_ADMIN_COMMENT:
		return xstrcmp(job->admin_comment, job_key.admin_comment);
	case PRINT_ALLOC_CPUS:
		return !(_calc_alloc_cpus(job) == _calc_alloc_cpus(&job_key));
	case PRINT_ALLOC_NODES:
		return !(job->alloc_nodes == job_key.alloc_nodes) ||
		       xstrcmp(job->tres_alloc_str, job_key.tres_alloc_str);
	case PRINT_ASSOCID:
		return !(job->associd == job_key.associd);
	case PRINT_BLOCKID:
		return xstrcmp(job->blockid, job_key.blockid);
	case PRINT_CLUSTER:
		return xstrcmp(job->cluster, job_key.cluster);
	case PRINT_COMMENT:
		return xstrcmp(job->derived_es, job_key.derived_es);
	case PRINT_CONSTRAINTS:
		return xstrcmp(job->constraints, job_key.constraints);
	case PRINT_CONTAINER:
		return xstrcmp(job->container, job_key.container);
	case PRINT_DB_INX:
		return !(job->db_index == job_key.db_index);
	case PRINT_ELIGIBLE:
		return difftime(job->eligible, job_key.eligible);
	case PRINT_EXTRA:
		return xstrcmp(job->extra, job_key.extra);
	case PRINT_GID:
	case PRINT_GROUP:
		return !(job->gid == job_key.gid);
	case PRINT_JOBNAME:
		return xstrcmp(job->jobname, job_key.jobname);
	case PRINT_LICENSES:
		return xstrcmp(job->licenses, job_key.jobname);
	case PRINT_MCS_LABEL:
		return xstrcmp(job->mcs_label, job_key.mcs_label);
	case PRINT_NNODES:
		return !(_calc_nnodes(job) == _calc_nnodes(&job_key));
	case PRINT_NODELIST:
		return xstrcmp(job->nodes, job_key.nodes);
	case PRINT_PARTITION:
		return xstrcmp(job->partition, job_key.partition);
	case PRINT_PLANNED:
		return difftime(_calc_planned(job), _calc_planned(&job_key));
	case PRINT_PLANNED_CPU:
	case PRINT_PLANNED_CPU_RAW:
		return difftime(_calc_planned_cpu(job),
				_calc_planned_cpu(&job_key));
	case PRINT_PRIO:
		return !(job->priority == job_key.priority);
	case PRINT_QOS:
	case PRINT_QOSRAW:
		return !(job->qosid == job_key.qosid);
	case PRINT_REASON:
		return !(job->state_reason_prev == job_key.state_reason_prev);
	case PRINT_REQ_CPUS:
		return !(job->req_cpus == job_key.req_cpus);
	case PRINT_REQ_MEM:
		return !(job->req_mem == job_key.req_mem);
	case PRINT_REQ_NODES:
		return !(_calc_req_nodes(job) == _calc_req_nodes(&job_key));
	case PRINT_RESERVATION:
		return xstrcmp(job->resv_name, job_key.resv_name);
	case PRINT_RESERVATION_ID:
		return !(job->resvid == job_key.resvid);
	case PRINT_START:
		return difftime(job->start, job_key.start);
	case PRINT_SUBMIT:
		return difftime(job->submit, job_key.submit);
	case PRINT_SYSTEM_COMMENT:
		return xstrcmp(job->system_comment, job_key.system_comment);
	case PRINT_TIMELIMIT:
	case PRINT_TIMELIMIT_RAW:
		return !(job->timelimit == job_key.timelimit);
	case PRINT_TRESR:
		return xstrcmp(job->tres_req_str, job_key.tres_req_str);
	case PRINT_UID:
		return !(_calc_uid(job) == _calc_uid(&job_key));
	case PRINT_USER:
		return xstrcmp(_calc_user(job), _calc_user(&job_key));
	case PRINT_WCKEY:
		return xstrcmp(job->wckey, job_key.wckey);
	case PRINT_WCKEYID:
		return !(job->wckeyid == job_key.wckeyid);
	case PRINT_WORK_DIR:
		return xstrcmp(job->work_dir, job_key.work_dir);
	default:
		return false;
	}
}

static bool _is_job_unique(slurmdb_job_rec_t *job,
			   sacct_combined_job_bitmap_t *comb_job_bitmap)
{
	list_itr_t *fields_itr;
	print_field_t *curr_field;

	fields_itr = list_iterator_create(print_fields_list);
	while ((curr_field = list_next(fields_itr))) {
		if (_is_print_field_unique(job, comb_job_bitmap->job_key,
					   curr_field->type)) {
			return true;
		}
	}
	return false;
}

static void _comb_job_bitmap_free(sacct_combined_job_bitmap_t *comb_job_bitmap)
{
	if (!comb_job_bitmap)
		return;

	FREE_NULL_BITMAP(comb_job_bitmap->bitmap);
	xfree(comb_job_bitmap);
}

static void _sort_job_into_unique_list(slurmdb_job_rec_t *job,
				       list_t **comb_job_bitmap_list)
{
	list_itr_t *comb_job_bitmap_list_itr;
	sacct_combined_job_bitmap_t *curr_comb_job_bitmap;

	/* Create bitmap list on the first pass */
	if (!(*comb_job_bitmap_list)) {
		sacct_combined_job_bitmap_t *comb_job_bitmap;
		bitstr_t *bitmap;

		*comb_job_bitmap_list = list_create(
			(ListDelF) _comb_job_bitmap_free);

		bitmap = bit_alloc(slurm_conf.max_array_sz);
		bit_set(bitmap, job->array_task_id);

		comb_job_bitmap = xmalloc(sizeof(*comb_job_bitmap));
		comb_job_bitmap->bitmap = bitmap;
		comb_job_bitmap->job_key = *job;

		list_append(*comb_job_bitmap_list, comb_job_bitmap);
		return;
	}

	/*
	 * Check if any of the existing comblicate job bitmaps match the current
	 * job
	 */
	comb_job_bitmap_list_itr = list_iterator_create(*comb_job_bitmap_list);
	while ((curr_comb_job_bitmap = list_next(comb_job_bitmap_list_itr))) {
		/* If job is not unique to bitmap, add it to the bitmap */
		if (!_is_job_unique(job, curr_comb_job_bitmap)) {
			if (job->array_task_str) {
				bitstr_t *bitmap;

				bitmap = bit_alloc(slurm_conf.max_array_sz);
				bit_unfmt_hexmask(bitmap, job->array_task_str);
				bit_or(curr_comb_job_bitmap->bitmap, bitmap);

				FREE_NULL_BITMAP(bitmap);
			} else {
				bit_set(curr_comb_job_bitmap->bitmap,
					job->array_task_id);
			}
			break;
		}

		/*
		 * If at the end of list, then job is unique. Add another bitmap
		 * to the list.
		 */
		if (!list_peek_next(comb_job_bitmap_list_itr)) {
			sacct_combined_job_bitmap_t *comb_job_bitmap;
			bitstr_t *bitmap;

			bitmap = bit_alloc(slurm_conf.max_array_sz);
			if (job->array_task_str) {
				bit_unfmt_hexmask(bitmap, job->array_task_str);
			} else {
				bit_set(bitmap, job->array_task_id);
			}

			comb_job_bitmap = xmalloc(
				sizeof(sacct_combined_job_bitmap_t));
			comb_job_bitmap->bitmap = bitmap;
			comb_job_bitmap->job_key = *job;

			list_append(*comb_job_bitmap_list, comb_job_bitmap);
			break;
		}
	}
	list_iterator_destroy(comb_job_bitmap_list_itr);
}

extern bool handle_job_for_array_unique(slurmdb_job_rec_t *job,
					uint32_t *prev_array_job_id)
{
	static list_t *comb_job_bitmap_list;
	/* Don't combine jobs that have already have steps */
	if (job->first_step_ptr)
		return false;

	/* if job is an array task, sort into list */
	if (job->array_job_id && (job->array_task_id != NO_VAL)) {
		_sort_job_into_unique_list(job, &comb_job_bitmap_list);

		*prev_array_job_id = job->array_job_id;
		return true;
	}

	/*
	 * Previous job was an array task, current is not. Print previous array
	 * tasks and consolidate with current job if part of the same array
	 * job
	 */
	if (*prev_array_job_id) {
		bool same_as_prev_job = (job->array_job_id ==
					 *prev_array_job_id);

		if (same_as_prev_job) {
			_sort_job_into_unique_list(job, &comb_job_bitmap_list);
		}

		print_unique_array_job_group(comb_job_bitmap_list,
					     job->array_max_tasks);
		*prev_array_job_id = 0;
		list_destroy(comb_job_bitmap_list);
		comb_job_bitmap_list = NULL;

		/* Job was combined with previous job, so don't print */
		if (same_as_prev_job)
			return true;
	}
	return false;
}
