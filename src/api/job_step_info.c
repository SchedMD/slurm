/*****************************************************************************\
 *  job_step_info.c - get/print the job step state information of slurm
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

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

	diff = strcmp(rec_a->node_name, rec_b->node_name);
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
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	/****** Line 1 ******/
	slurm_make_time_str ((time_t *)&job_step_ptr->start_time, time_str,
		sizeof(time_str));
	if (job_step_ptr->time_limit == INFINITE)
		sprintf(limit_str, "UNLIMITED");
	else
		secs2time_str ((time_t)job_step_ptr->time_limit * 60,
				limit_str, sizeof(limit_str));
	if (job_step_ptr->array_job_id) {
		if (job_step_ptr->step_id == INFINITE) {	/* Pending */
			snprintf(tmp_line, sizeof(tmp_line), "StepId=%u_%u.TBD ",
				 job_step_ptr->array_job_id,
				 job_step_ptr->array_task_id);
		} else {
			snprintf(tmp_line, sizeof(tmp_line), "StepId=%u_%u.%u ",
				 job_step_ptr->array_job_id,
				 job_step_ptr->array_task_id,
				 job_step_ptr->step_id);
		}
		out = xstrdup(tmp_line);
	} else {
		if (job_step_ptr->step_id == INFINITE) {	/* Pending */
			snprintf(tmp_line, sizeof(tmp_line), "StepId=%u.TBD ",
				 job_step_ptr->job_id);
		} else {
			snprintf(tmp_line, sizeof(tmp_line), "StepId=%u.%u ",
				 job_step_ptr->job_id, job_step_ptr->step_id);
		}
		out = xstrdup(tmp_line);
	}
	snprintf(tmp_line, sizeof(tmp_line),
		 "UserId=%u StartTime=%s TimeLimit=%s",
		 job_step_ptr->user_id, time_str, limit_str);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 2 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "State=%s ",
		 job_state_string(job_step_ptr->state));
	xstrcat(out, tmp_line);
	if (cluster_flags & CLUSTER_FLAG_BG) {
		char *io_nodes;
		select_g_select_jobinfo_get(job_step_ptr->select_jobinfo,
					    SELECT_JOBDATA_IONODES,
					    &io_nodes);
		if (io_nodes) {
			snprintf(tmp_line, sizeof(tmp_line),
				 "Partition=%s MidplaneList=%s[%s] Gres=%s",
				 job_step_ptr->partition,
				 job_step_ptr->nodes, io_nodes,
				 job_step_ptr->gres);
			xfree(io_nodes);
		} else
			snprintf(tmp_line, sizeof(tmp_line),
				 "Partition=%s MidplaneList=%s Gres=%s",
				 job_step_ptr->partition,
				 job_step_ptr->nodes,
				 job_step_ptr->gres);
	} else {
		snprintf(tmp_line, sizeof(tmp_line),
			"Partition=%s NodeList=%s Gres=%s",
			job_step_ptr->partition, job_step_ptr->nodes,
			job_step_ptr->gres);
	}
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 3 ******/
	if (cluster_flags & CLUSTER_FLAG_BGQ) {
		uint32_t nodes = 0;
		select_g_select_jobinfo_get(job_step_ptr->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &nodes);
		convert_num_unit((float)nodes, tmp_node_cnt,
				 sizeof(tmp_node_cnt), UNIT_NONE);
	} else {
		convert_num_unit((float)_nodes_in_list(job_step_ptr->nodes),
				 tmp_node_cnt, sizeof(tmp_node_cnt),
				 UNIT_NONE);
	}

	snprintf(tmp_line, sizeof(tmp_line),
		"Nodes=%s CPUs=%u Tasks=%u Name=%s Network=%s",
		 tmp_node_cnt, job_step_ptr->num_cpus, job_step_ptr->num_tasks,
		 job_step_ptr->name, job_step_ptr->network);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 4 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		"ResvPorts=%s Checkpoint=%u CheckpointDir=%s",
		 job_step_ptr->resv_ports,
		 job_step_ptr->ckpt_interval, job_step_ptr->ckpt_dir);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 5 ******/
	if (job_step_ptr->cpu_freq == NO_VAL) {
		snprintf(tmp_line, sizeof(tmp_line),
			 "CPUFreqReq=Default\n\n");
	} else if (job_step_ptr->cpu_freq & CPU_FREQ_RANGE_FLAG) {
		char buf[32];
		cpu_freq_to_string(buf, sizeof(buf), job_step_ptr->cpu_freq);
		snprintf(tmp_line, sizeof(tmp_line), "CPUFreqReq=%s\n\n", buf);
	} else {
		snprintf(tmp_line, sizeof(tmp_line),
			 "CPUFreqReq=%u\n\n", job_step_ptr->cpu_freq);
	}
	xstrcat(out, tmp_line);

	return out;
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
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the response using slurm_free_job_step_info_response_msg
 */
int
slurm_get_job_steps (time_t update_time, uint32_t job_id, uint32_t step_id,
		     job_step_info_response_msg_t **resp, uint16_t show_flags)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	job_step_info_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.last_update  = update_time;
	req.job_id	= job_id;
	req.step_id	= step_id;
	req.show_flags	= show_flags;
	req_msg.msg_type = REQUEST_JOB_STEP_INFO;
	req_msg.data	= &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_STEP_INFO:
		*resp = (job_step_info_response_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*resp = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

extern slurm_step_layout_t *
slurm_job_step_layout_get(uint32_t job_id, uint32_t step_id)
{
	job_step_id_msg_t data;
	slurm_msg_t req, resp;
	int errnum;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	req.msg_type = REQUEST_STEP_LAYOUT;
	req.data = &data;
	data.job_id = job_id;
	data.step_id = step_id;

	if (slurm_send_recv_controller_msg(&req, &resp) < 0) {
		return NULL;
	}

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
 * IN job_id
 * IN step_id
 * IN node_list, optional, if NULL then all nodes in step are returned.
 * OUT resp
 * RET SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurm_job_step_stat(uint32_t job_id, uint32_t step_id,
			       char *node_list,
			       job_step_stat_response_msg_t **resp)
{
	slurm_msg_t req_msg;
	ListIterator itr;
	job_step_id_msg_t req;
	List ret_list = NULL;
	ret_data_info_t *ret_data_info = NULL;
	int rc = SLURM_SUCCESS;
	slurm_step_layout_t *step_layout = NULL;
	job_step_stat_response_msg_t *resp_out;
	bool created = 0;

	xassert(resp);

	if (!node_list) {
		if (!(step_layout =
		      slurm_job_step_layout_get(job_id, step_id))) {
			rc = errno;
			error("slurm_job_step_stat: "
			      "problem getting step_layout for %u.%u: %s",
			      job_id, step_id, slurm_strerror(rc));
			return rc;
		}
		node_list = step_layout->node_list;
	}

 	if (!*resp) {
		resp_out = xmalloc(sizeof(job_step_stat_response_msg_t));
		*resp = resp_out;
		created = 1;
	} else
		resp_out = *resp;

        debug("slurm_job_step_stat: "
	      "getting pid information of job %u.%u on nodes %s",
              job_id, step_id, node_list);

	slurm_msg_t_init(&req_msg);

	memset(&req, 0, sizeof(job_step_id_msg_t));
	resp_out->job_id = req.job_id = job_id;
	resp_out->step_id = req.step_id = step_id;

	req_msg.msg_type = REQUEST_JOB_STEP_STAT;
        req_msg.data = &req;

        if (!(ret_list = slurm_send_recv_msgs(node_list, &req_msg, 0, false))) {
                error("slurm_job_step_stat: got an error no list returned");
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
				debug("slurm_job_step_stat: job step %u.%u "
				      "has already completed",
				      job_id, step_id);
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
	list_destroy(ret_list);

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
 * IN job_id
 * IN step_id
 * IN node_list, optional, if NULL then all nodes in step are returned.
 * OUT resp
 * RET SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurm_job_step_get_pids(uint32_t job_id, uint32_t step_id,
				   char *node_list,
				   job_step_pids_response_msg_t **resp)
{
        int rc = SLURM_SUCCESS;
        slurm_msg_t req_msg;
        job_step_id_msg_t req;
        ListIterator itr;
        List ret_list = NULL;
        ret_data_info_t *ret_data_info = NULL;
	slurm_step_layout_t *step_layout = NULL;
	job_step_pids_response_msg_t *resp_out;
	bool created = 0;

	xassert(resp);

	if (!node_list) {
		if (!(step_layout =
		     slurm_job_step_layout_get(job_id, step_id))) {
			rc = errno;
			error("slurm_job_step_get_pids: "
			      "problem getting step_layout for %u.%u: %s",
			      job_id, step_id, slurm_strerror(rc));
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

        debug("slurm_job_step_get_pids: "
	      "getting pid information of job %u.%u on nodes %s",
              job_id, step_id, node_list);

	slurm_msg_t_init(&req_msg);

	memset(&req, 0, sizeof(job_step_id_msg_t));
        resp_out->job_id = req.job_id = job_id;
	resp_out->step_id = req.step_id = step_id;

	req_msg.msg_type = REQUEST_JOB_STEP_PIDS;
        req_msg.data = &req;

        if (!(ret_list = slurm_send_recv_msgs(node_list,
					     &req_msg, 0, false))) {
                error("slurm_job_step_get_pids: got an error no list returned");
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
                              error("slurm_job_step_get_pids: "
				    "there was an error with the "
				    "list pid request rc = %s",
                                    slurm_strerror(rc));
                              break;
                      default:
                              rc = slurm_get_return_code(ret_data_info->type,
                                                         ret_data_info->data);
                              error("slurm_job_step_get_pids: "
				    "unknown return given %d rc = %s",
                                    ret_data_info->type, slurm_strerror(rc));
                              break;
                }
        }
        list_iterator_destroy(itr);
        list_destroy(ret_list);

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
		if (step_pids_msg->pid_list)
			list_destroy(step_pids_msg->pid_list);
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
		if (step_stat_msg->stats_list)
			list_destroy(step_stat_msg->stats_list);
		xfree(step_stat_msg);
	}
}

