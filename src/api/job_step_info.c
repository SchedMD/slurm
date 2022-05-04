/*****************************************************************************\
 *  job_step_info.c - get/print the job step state information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"

#include "src/common/cpu_frequency.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Data structures for pthreads used to gather step information from multiple
 * clusters in parallel */
typedef struct load_step_req_struct {
	slurmdb_cluster_rec_t *cluster;
	bool local_cluster;
	slurm_msg_t *req_msg;
	List resp_msg_list;
} load_step_req_struct_t;

typedef struct load_step_resp_struct {
	bool local_cluster;
	job_step_info_response_msg_t *new_msg;
} load_step_resp_struct_t;

static int _nodes_in_list(char *node_list)
{
	hostset_t host_set = hostset_create(node_list);
	int count = hostset_count(host_set);
	hostset_destroy(host_set);
	return count;
}

static int _sort_pids_by_name(void *x, void *y)
{
	int diff = 0;
	job_step_pids_t *rec_a = *(job_step_pids_t **)x;
	job_step_pids_t *rec_b = *(job_step_pids_t **)y;

	if (!rec_a->node_name || !rec_b->node_name)
		return 0;

	diff = xstrcmp(rec_a->node_name, rec_b->node_name);
	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	return 0;
}

static int _sort_stats_by_name(void *x, void *y)
{
	job_step_stat_t *rec_a = *(job_step_stat_t **)x;
	job_step_stat_t *rec_b = *(job_step_stat_t **)y;

	if (!rec_a->step_pids || !rec_b->step_pids)
		return 0;

	return _sort_pids_by_name((void *)&rec_a->step_pids, (void *)&rec_b->step_pids);
}

/*
 * slurm_print_job_step_info_msg - output information about all Slurm
 *	job steps based upon message as loaded using slurm_get_job_steps
 * IN out - file to write to
 * IN job_step_info_msg_ptr - job step information message pointer
 * IN one_liner - print as a single line if true
 */
void
slurm_print_job_step_info_msg ( FILE* out,
		job_step_info_response_msg_t * job_step_info_msg_ptr,
		int one_liner )
{
	int i;
	job_step_info_t *job_step_ptr = job_step_info_msg_ptr->job_steps ;
	char time_str[32];

	slurm_make_time_str ((time_t *)&job_step_info_msg_ptr->last_update,
			time_str, sizeof(time_str));
	fprintf( out, "Job step data as of %s, record count %d\n",
		time_str, job_step_info_msg_ptr->job_step_count);

	for (i = 0; i < job_step_info_msg_ptr-> job_step_count; i++)
	{
		slurm_print_job_step_info ( out, & job_step_ptr[i],
					    one_liner ) ;
	}
}

/*
 * slurm_print_job_step_info - output information about a specific Slurm
 *	job step based upon message as loaded using slurm_get_job_steps
 * IN out - file to write to
 * IN job_ptr - an individual job step information record pointer
 * IN one_liner - print as a single line if true
 */
void
slurm_print_job_step_info ( FILE* out, job_step_info_t * job_step_ptr,
			    int one_liner )
{
	char *print_this = slurm_sprint_job_step_info(job_step_ptr, one_liner);
	fprintf ( out, "%s", print_this);
	xfree(print_this);
}

/*
 * slurm_sprint_job_step_info - output information about a specific Slurm
 *	job step based upon message as loaded using slurm_get_job_steps
 * IN job_ptr - an individual job step information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *
slurm_sprint_job_step_info ( job_step_info_t * job_step_ptr,
			    int one_liner )
{
	char tmp_node_cnt[40];
	char time_str[32];
	char limit_str[32];
	char tmp_line[128];
	char *out = NULL;
	char *line_end = (one_liner) ? " " : "\n   ";
	uint16_t flags = STEP_ID_FLAG_NONE;

	/****** Line 1 ******/
	slurm_make_time_str ((time_t *)&job_step_ptr->start_time, time_str,
		sizeof(time_str));
	if (job_step_ptr->time_limit == INFINITE)
		snprintf(limit_str, sizeof(limit_str), "UNLIMITED");
	else
		secs2time_str ((time_t)job_step_ptr->time_limit * 60,
				limit_str, sizeof(limit_str));

	if (job_step_ptr->array_job_id) {
		xstrfmtcat(out, "StepId=%u_%u.",
			   job_step_ptr->array_job_id,
			   job_step_ptr->array_task_id);
		flags = STEP_ID_FLAG_NO_PREFIX | STEP_ID_FLAG_NO_JOB;
	}

	log_build_step_id_str(&job_step_ptr->step_id,
			      tmp_line, sizeof(tmp_line),
			      flags);
	xstrfmtcat(out, "%s ", tmp_line);

	xstrfmtcat(out, "UserId=%u StartTime=%s TimeLimit=%s",
		   job_step_ptr->user_id, time_str, limit_str);

	/****** Line 2 ******/
	xstrcat(out, line_end);
	xstrfmtcat(out, "State=%s Partition=%s NodeList=%s",
		   job_state_string(job_step_ptr->state),
		   job_step_ptr->partition, job_step_ptr->nodes);

	/****** Line 3 ******/
	convert_num_unit((float)_nodes_in_list(job_step_ptr->nodes),
			 tmp_node_cnt, sizeof(tmp_node_cnt), UNIT_NONE,
			 NO_VAL, CONVERT_NUM_UNIT_EXACT);
	xstrcat(out, line_end);
	xstrfmtcat(out, "Nodes=%s CPUs=%u Tasks=%u Name=%s Network=%s",
		   tmp_node_cnt, job_step_ptr->num_cpus,
		   job_step_ptr->num_tasks, job_step_ptr->name,
		   job_step_ptr->network);

	/****** Line 4 ******/
	xstrcat(out, line_end);
	xstrfmtcat(out, "TRES=%s", job_step_ptr->tres_alloc_str);

	/****** Line 5 ******/
	xstrcat(out, line_end);
	xstrfmtcat(out, "ResvPorts=%s", job_step_ptr->resv_ports);

	/****** Line 6 ******/
	xstrcat(out, line_end);
	if (cpu_freq_debug(NULL, NULL, tmp_line, sizeof(tmp_line),
			   job_step_ptr->cpu_freq_gov,
			   job_step_ptr->cpu_freq_min,
			   job_step_ptr->cpu_freq_max, NO_VAL) != 0) {
		xstrcat(out, tmp_line);
	} else {
		xstrcat(out, "CPUFreqReq=Default");
	}

	if (job_step_ptr->task_dist) {
		char *name =
			slurm_step_layout_type_name(job_step_ptr->task_dist);
		xstrfmtcat(out, " Dist=%s", name);
		xfree(name);
	}

	/****** Line 7 ******/
	xstrcat(out, line_end);
	xstrfmtcat(out, "SrunHost:Pid=%s:%u",
		   job_step_ptr->srun_host, job_step_ptr->srun_pid);

	/****** Line (optional) ******/
	if (job_step_ptr->cpus_per_tres) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "CpusPerTres=%s", job_step_ptr->cpus_per_tres);
	}

	/****** Line (optional) ******/
	if (job_step_ptr->mem_per_tres) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "MemPerTres=%s", job_step_ptr->mem_per_tres);
	}

	/****** Line (optional) ******/
	if (job_step_ptr->tres_bind) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresBind=%s", job_step_ptr->tres_bind);
	}

	/****** Line (optional) ******/
	if (job_step_ptr->tres_freq) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresFreq=%s", job_step_ptr->tres_freq);
	}

	/****** Line (optional) ******/
	if (job_step_ptr->tres_per_step) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerStep=%s", job_step_ptr->tres_per_step);
	}

	/****** Line (optional) ******/
	if (job_step_ptr->tres_per_node) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerNode=%s", job_step_ptr->tres_per_node);
	}

	/****** Line (optional) ******/
	if (job_step_ptr->tres_per_socket) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerSocket=%s",
			   job_step_ptr->tres_per_socket);
	}

	/****** Line (optional) ******/
	if (job_step_ptr->tres_per_task) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerTask=%s", job_step_ptr->tres_per_task);
	}

	/****** END OF JOB RECORD ******/
	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");
	return out;
}

static int
_load_cluster_steps(slurm_msg_t *req_msg, job_step_info_response_msg_t **resp,
		    slurmdb_cluster_rec_t *cluster)
{
	slurm_msg_t resp_msg;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&resp_msg);

	*resp = NULL;

	if (slurm_send_recv_controller_msg(req_msg, &resp_msg, cluster) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_STEP_INFO:
		*resp = (job_step_info_response_msg_t *) resp_msg.data;
		resp_msg.data = NULL;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		break;
	default:
		rc = SLURM_UNEXPECTED_MSG_ERROR;
		break;
	}

	if (rc)
		slurm_seterrno_ret(rc);

	return rc;
}

/* Thread to read step information from some cluster */
static void *_load_step_thread(void *args)
{
	load_step_req_struct_t *load_args = (load_step_req_struct_t *)args;
	slurmdb_cluster_rec_t *cluster = load_args->cluster;
	job_step_info_response_msg_t *new_msg = NULL;
	int rc;

	if ((rc = _load_cluster_steps(load_args->req_msg, &new_msg, cluster)) ||
	    !new_msg) {
		verbose("Error reading step information from cluster %s: %s",
			cluster->name, slurm_strerror(rc));
	} else {
		load_step_resp_struct_t *step_resp;
		step_resp = xmalloc(sizeof(load_step_resp_struct_t));
		step_resp->local_cluster = load_args->local_cluster;
		step_resp->new_msg = new_msg;
		list_append(load_args->resp_msg_list, step_resp);
	}
	xfree(args);

	return (void *) NULL;
}

static int
_load_fed_steps(slurm_msg_t *req_msg, job_step_info_response_msg_t **resp,
		uint16_t show_flags, char *cluster_name,
		slurmdb_federation_rec_t *fed)
{
	int i;
	load_step_resp_struct_t *step_resp;
	job_step_info_response_msg_t *orig_msg = NULL, *new_msg = NULL;
	uint32_t new_rec_cnt;
	slurmdb_cluster_rec_t *cluster;
	ListIterator iter;
	int pthread_count = 0;
	pthread_t *load_thread = 0;
	load_step_req_struct_t *load_args;
	List resp_msg_list;

	*resp = NULL;

	/* Spawn one pthread per cluster to collect step information */
	resp_msg_list = list_create(NULL);
	load_thread = xmalloc(sizeof(pthread_t) *
			      list_count(fed->cluster_list));
	iter = list_iterator_create(fed->cluster_list);
	while ((cluster = (slurmdb_cluster_rec_t *) list_next(iter))) {
		bool local_cluster = false;
		if ((cluster->control_host == NULL) ||
		    (cluster->control_host[0] == '\0'))
			continue;	/* Cluster down */

		if (!xstrcmp(cluster->name, cluster_name))
			local_cluster = true;
		if ((show_flags & SHOW_LOCAL) && !local_cluster)
			continue;

		load_args = xmalloc(sizeof(load_step_req_struct_t));
		load_args->cluster = cluster;
		load_args->local_cluster = local_cluster;
		load_args->req_msg = req_msg;
		load_args->resp_msg_list = resp_msg_list;
		slurm_thread_create(&load_thread[pthread_count],
				    _load_step_thread, load_args);
		pthread_count++;

	}
	list_iterator_destroy(iter);

	/* Wait for all pthreads to complete */
	for (i = 0; i < pthread_count; i++)
		pthread_join(load_thread[i], NULL);
	xfree(load_thread);

	/* Merge the responses into a single response message */
	iter = list_iterator_create(resp_msg_list);
	while ((step_resp = (load_step_resp_struct_t *) list_next(iter))) {
		new_msg = step_resp->new_msg;
		if (!orig_msg) {
			orig_msg = new_msg;
			*resp = orig_msg;
		} else {
			/* Merge the step records into a single response message */
			orig_msg->last_update = MIN(orig_msg->last_update,
						    new_msg->last_update);
			new_rec_cnt = orig_msg->job_step_count +
				      new_msg->job_step_count;
			if (new_msg->job_step_count) {
				orig_msg->job_steps =
					xrealloc(orig_msg->job_steps,
						 sizeof(job_step_info_t) *
						 new_rec_cnt);
				(void) memcpy(orig_msg->job_steps +
					      orig_msg->job_step_count,
					      new_msg->job_steps,
					      sizeof(job_step_info_t) *
					      new_msg->job_step_count);
				orig_msg->job_step_count = new_rec_cnt;
			}
			xfree(new_msg->job_steps);
			xfree(new_msg);
		}
		xfree(step_resp);
	}
	list_iterator_destroy(iter);
	FREE_NULL_LIST(resp_msg_list);

	if (!orig_msg)
		slurm_seterrno_ret(ESLURM_INVALID_JOB_ID);

	return SLURM_SUCCESS;
}

/*
 * slurm_get_job_steps - issue RPC to get specific slurm job step
 *	configuration information if changed since update_time.
 *	a job_id value of NO_VAL implies all jobs, a step_id value of
 *	NO_VAL implies all steps
 * IN update_time - time of current configuration data
 * IN job_id - get information for specific job id, NO_VAL for all jobs
 * IN step_id - get information for specific job step id, NO_VAL for all
 *	job steps
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * IN show_flags - job step filtering options
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_job_step_info_response_msg
 */
int
slurm_get_job_steps (time_t update_time, uint32_t job_id, uint32_t step_id,
		     job_step_info_response_msg_t **resp, uint16_t show_flags)
{
	int rc;
	slurm_msg_t req_msg;
	job_step_info_request_msg_t req;
	slurmdb_federation_rec_t *fed;
	void *ptr = NULL;
	slurm_step_id_t tmp_step_id = {
		.job_id = job_id,
		.step_het_comp = NO_VAL,
		.step_id = step_id,
	};
	if ((show_flags & SHOW_LOCAL) == 0) {
		if (slurm_load_federation(&ptr) ||
		    !cluster_in_federation(ptr, slurm_conf.cluster_name)) {
			/* Not in federation */
			show_flags |= SHOW_LOCAL;
		} else {
			/* In federation. Need full info from all clusters */
			update_time = (time_t) 0;
		}
	}

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.last_update  = update_time;
	memcpy(&req.step_id, &tmp_step_id, sizeof(req.step_id));
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_JOB_STEP_INFO;
	req_msg.data     = &req;

	/* With -M option, working_cluster_rec is set and  we only get
	 * information for that cluster */
	if (working_cluster_rec || !ptr || (show_flags & SHOW_LOCAL)) {
		rc = _load_cluster_steps(&req_msg, resp, working_cluster_rec);
	} else {
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_steps(&req_msg, resp, show_flags,
				     slurm_conf.cluster_name, fed);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
}

extern slurm_step_layout_t *slurm_job_step_layout_get(slurm_step_id_t *step_id)
{
	slurm_step_id_t data;
	slurm_msg_t req, resp;
	int errnum;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	req.msg_type = REQUEST_STEP_LAYOUT;
	req.data = &data;
	memcpy(&data, step_id, sizeof(data));

	if (slurm_send_recv_controller_msg(&req, &resp, working_cluster_rec) <0)
		return NULL;

	switch (resp.msg_type) {
	case RESPONSE_STEP_LAYOUT:
		return (slurm_step_layout_t *)resp.data;
	case RESPONSE_SLURM_RC:
		errnum = ((return_code_msg_t *)resp.data)->return_code;
		slurm_free_return_code_msg(resp.data);
		errno = errnum;
		return NULL;
	default:
		errno = SLURM_UNEXPECTED_MSG_ERROR;
		return NULL;
	}
}

/*
 * slurm_job_step_stat - status a current step
 *
 * IN step_id
 * IN node_list, optional, if NULL then all nodes in step are returned.
 * IN use_protocol_ver protocol version to use.
 * OUT resp
 * RET SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurm_job_step_stat(slurm_step_id_t *step_id,
			       char *node_list,
			       uint16_t use_protocol_ver,
			       job_step_stat_response_msg_t **resp)
{
	slurm_msg_t req_msg;
	ListIterator itr;
	slurm_step_id_t req;
	List ret_list = NULL;
	ret_data_info_t *ret_data_info = NULL;
	int rc = SLURM_SUCCESS;
	slurm_step_layout_t *step_layout = NULL;
	job_step_stat_response_msg_t *resp_out;
	bool created = 0;

	xassert(resp);

	if (!node_list) {
		if (!(step_layout = slurm_job_step_layout_get(step_id))) {
			rc = errno;
			error("slurm_job_step_stat: "
			      "problem getting step_layout for %ps: %s",
			      step_id, slurm_strerror(rc));
			return rc;
		}
		node_list = step_layout->node_list;
		use_protocol_ver = MIN(SLURM_PROTOCOL_VERSION,
				       step_layout->start_protocol_ver);
	}

 	if (!*resp) {
		resp_out = xmalloc(sizeof(job_step_stat_response_msg_t));
		*resp = resp_out;
		created = 1;
	} else
		resp_out = *resp;

	debug("%s: getting pid information of job %ps on nodes %s",
	      __func__, step_id, node_list);

	slurm_msg_t_init(&req_msg);
	slurm_msg_set_r_uid(&req_msg, SLURM_AUTH_UID_ANY);

	memcpy(&req, step_id, sizeof(req));
	memcpy(&resp_out->step_id, step_id, sizeof(resp_out->step_id));

	req_msg.protocol_version = use_protocol_ver;
	req_msg.msg_type = REQUEST_JOB_STEP_STAT;
	req_msg.data = &req;

	if (!(ret_list = slurm_send_recv_msgs(node_list, &req_msg, 0))) {
		error("%s: got an error no list returned", __func__);
		rc = SLURM_ERROR;
		if (created) {
			slurm_job_step_stat_response_msg_free(resp_out);
			*resp = NULL;
		}
		goto cleanup;
	}

	itr = list_iterator_create(ret_list);
	while ((ret_data_info = list_next(itr))) {
		switch (ret_data_info->type) {
		case RESPONSE_JOB_STEP_STAT:
			if (!resp_out->stats_list)
				resp_out->stats_list = list_create(
					slurm_free_job_step_stat);
			list_push(resp_out->stats_list,
				  ret_data_info->data);
			ret_data_info->data = NULL;
 			break;
		case RESPONSE_SLURM_RC:
			rc = slurm_get_return_code(ret_data_info->type,
						   ret_data_info->data);
			if (rc == ESLURM_INVALID_JOB_ID) {
				debug("slurm_job_step_stat: job step %ps has already completed",
				      step_id);
			} else {
				error("slurm_job_step_stat: "
				      "there was an error with the request to "
				      "%s rc = %s",
				      ret_data_info->node_name,
				      slurm_strerror(rc));
			}
			break;
		default:
			rc = slurm_get_return_code(ret_data_info->type,
						   ret_data_info->data);
			error("slurm_job_step_stat: "
			      "unknown return given from %s: %d rc = %s",
			      ret_data_info->node_name, ret_data_info->type,
			      slurm_strerror(rc));
			break;
		}
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(ret_list);

	if (resp_out->stats_list)
		list_sort(resp_out->stats_list, (ListCmpF)_sort_stats_by_name);
cleanup:
	slurm_step_layout_destroy(step_layout);

	return rc;
}

/*
 * slurm_job_step_get_pids - get the complete list of pids for a given
 *      job step
 *
 * IN step_id
 * IN node_list, optional, if NULL then all nodes in step are returned.
 * OUT resp
 * RET SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurm_job_step_get_pids(slurm_step_id_t *step_id,
				   char *node_list,
				   job_step_pids_response_msg_t **resp)
{
	int rc = SLURM_SUCCESS;
	slurm_msg_t req_msg;
	slurm_step_id_t req;
	ListIterator itr;
	List ret_list = NULL;
	ret_data_info_t *ret_data_info = NULL;
	slurm_step_layout_t *step_layout = NULL;
	job_step_pids_response_msg_t *resp_out;
	bool created = 0;

	xassert(resp);

	if (!node_list) {
		if (!(step_layout = slurm_job_step_layout_get(step_id))) {
			rc = errno;
			error("slurm_job_step_get_pids: "
			      "problem getting step_layout for %ps: %s",
			      step_id, slurm_strerror(rc));
			return rc;
		}
		node_list = step_layout->node_list;
	}

	if (!*resp) {
		resp_out = xmalloc(sizeof(job_step_pids_response_msg_t));
		*resp = resp_out;
		created = 1;
	} else
		resp_out = *resp;

	debug("%s: getting pid information of job %ps on nodes %s",
	      __func__, step_id, node_list);

	slurm_msg_t_init(&req_msg);
	slurm_msg_set_r_uid(&req_msg, SLURM_AUTH_UID_ANY);

	memcpy(&req, step_id, sizeof(req));
	memcpy(&resp_out->step_id, step_id, sizeof(resp_out->step_id));

	req_msg.msg_type = REQUEST_JOB_STEP_PIDS;
	req_msg.data = &req;

	if (!(ret_list = slurm_send_recv_msgs(node_list, &req_msg, 0))) {
		error("%s: got an error no list returned", __func__);
		rc = SLURM_ERROR;
		if (created) {
			slurm_job_step_pids_response_msg_free(resp_out);
			*resp = NULL;
		}
		goto cleanup;
	}

	itr = list_iterator_create(ret_list);
	while((ret_data_info = list_next(itr))) {
		switch (ret_data_info->type) {
		case RESPONSE_JOB_STEP_PIDS:
			if (!resp_out->pid_list)
				resp_out->pid_list = list_create(
					slurm_free_job_step_pids);
			list_push(resp_out->pid_list,
				  ret_data_info->data);
			ret_data_info->data = NULL;
			break;
		case RESPONSE_SLURM_RC:
			rc = slurm_get_return_code(ret_data_info->type,
						   ret_data_info->data);
			error("%s: there was an error with the list pid request rc = %s",
			      __func__, slurm_strerror(rc));
			break;
		default:
			rc = slurm_get_return_code(ret_data_info->type,
						   ret_data_info->data);
			error("%s: unknown return given %d rc = %s",
			      __func__, ret_data_info->type,
			      slurm_strerror(rc));
			break;
		}
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(ret_list);

 	if (resp_out->pid_list)
		list_sort(resp_out->pid_list, (ListCmpF)_sort_pids_by_name);
cleanup:
	slurm_step_layout_destroy(step_layout);

	return rc;
}

extern void slurm_job_step_layout_free(slurm_step_layout_t *layout)
{
	slurm_step_layout_destroy(layout);
}

extern void slurm_job_step_pids_free(job_step_pids_t *object)
{
	slurm_free_job_step_pids(object);
}

extern void slurm_job_step_pids_response_msg_free(void *object)
{
	job_step_pids_response_msg_t *step_pids_msg =
		(job_step_pids_response_msg_t *) object;
	if (step_pids_msg) {
		FREE_NULL_LIST(step_pids_msg->pid_list);
		xfree(step_pids_msg);
	}
}

extern void slurm_job_step_stat_free(job_step_stat_t *object)
{
	slurm_free_job_step_stat(object);
}

extern void slurm_job_step_stat_response_msg_free(void *object)
{
	job_step_stat_response_msg_t *step_stat_msg =
		(job_step_stat_response_msg_t *) object;
	if (step_stat_msg) {
		FREE_NULL_LIST(step_stat_msg->stats_list);
		xfree(step_stat_msg);
	}
}
