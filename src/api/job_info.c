/*****************************************************************************\
 *  job_info.c - get/print the job state information of slurm
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"
#include "slurm/slurm_errno.h"

#include "src/common/cpu_frequency.h"
#include "src/common/forward.h"
#include "src/common/job_state_reason.h"
#include "src/common/macros.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/interfaces/select.h"
#include "src/interfaces/auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"
#include "src/common/uthash.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Use a hash table to identify duplicate job records across the clusters in
 * a federation */
#define JOB_HASH_SIZE 1000

/* Data structures for pthreads used to gather job information from multiple
 * clusters in parallel */
typedef struct load_job_req_struct {
	slurmdb_cluster_rec_t *cluster;
	bool local_cluster;
	slurm_msg_t *req_msg;
	list_t *resp_msg_list;
} load_job_req_struct_t;

typedef struct load_job_resp_struct {
	job_info_msg_t *new_msg;
} load_job_resp_struct_t;

typedef struct load_job_prio_resp_struct {
	bool local_cluster;
	priority_factors_response_msg_t *new_msg;
} load_job_prio_resp_struct_t;

/* Perform file name substitutions
 * %A - Job array's master job allocation number.
 * %a - Job array ID (index) number.
 * %j - Job ID
 * %u - User name
 * %x - Job name
 */
static void _fname_format(char *buf, int buf_size, job_info_t * job_ptr,
			  char *fname)
{
	char *q, *p, *tmp, *tmp2 = NULL, *user;
	unsigned int wid, offset;

	tmp = xstrdup(fname);
	q = p  = tmp;
	while (*p != '\0') {
		if (*p == '%') {
			offset = 1;
			wid = 0;
			if (*(p + 1) == '%') {
				p++;
				xmemcat(tmp2, q, p);
				q = ++p;
				continue;
			}
			if (isdigit(*(p + 1))) {
				unsigned long in_width = 0;
				if ((in_width = strtoul(p + 1, &p, 10)) > 9) {
					/* Remove % and double digit 10 */
					wid = 10;
					offset = 3;
				} else {
					wid = (unsigned int)in_width;
					offset = 2;
				}
				if (*p == '\0')
					break;
			} else
				p++;

			switch (*p) {
				case 'A': /* Array job ID */
					xmemcat(tmp2, q, p - offset);
					q = p + 1;
					if (job_ptr->array_task_id == NO_VAL) {
						/* Not a job array */
						xstrfmtcat(tmp2, "%0*u", wid,
							   job_ptr->job_id);
					} else {
						xstrfmtcat(tmp2, "%0*u", wid,
							job_ptr->array_job_id);
					}
					break;
				case 'a': /* Array task ID */
					xmemcat(tmp2, q, p - offset);
					xstrfmtcat(tmp2, "%0*u", wid,
						   job_ptr->array_task_id);
					q = p + 1;
					break;
				case 'b': /* Array task ID modulo 10 */
					xmemcat(tmp2, q, p - offset);
					xstrfmtcat(tmp2, "%0*u", wid,
						   job_ptr->array_task_id % 10);
					q = p + 1;
					break;
				case 'j': /* Job ID */
					xmemcat(tmp2, q, p - offset);
					xstrfmtcat(tmp2, "%0*u", wid,
						   job_ptr->job_id);
					q = p + 1;
					break;
				case 'u': /* User name */
					xmemcat(tmp2, q, p - offset);
					user = uid_to_string(
						(uid_t) job_ptr->user_id);
					xstrfmtcat(tmp2, "%s", user);
					xfree(user);
					q = p + 1;
					break;
				case 'x':
					xmemcat(tmp2, q, p - offset);
					xstrfmtcat(tmp2, "%s", job_ptr->name);
					q = p + 1;
					break;
			}
		} else
			p++;
	}
	if (p != q)
		xmemcat(tmp2, q, p);
	xfree(tmp);

	if (tmp2[0] == '/')
		snprintf(buf, buf_size, "%s", tmp2);
	else
		snprintf(buf, buf_size, "%s/%s", job_ptr->work_dir, tmp2);
	xfree(tmp2);
}

/* Given a job record pointer, return its stderr path in buf */
extern void slurm_get_job_stderr(char *buf, int buf_size, job_info_t * job_ptr)
{
	if (job_ptr == NULL)
		snprintf(buf, buf_size, "%s", "job pointer is NULL");
	else if (job_ptr->std_err)
		_fname_format(buf, buf_size, job_ptr, job_ptr->std_err);
	else if (job_ptr->batch_flag == 0)
		snprintf(buf, buf_size, "%s", "");
	else if (job_ptr->std_out)
		_fname_format(buf, buf_size, job_ptr, job_ptr->std_out);
	else if (job_ptr->array_job_id) {
		snprintf(buf, buf_size, "%s/slurm-%u_%u.out",
			 job_ptr->work_dir,
			 job_ptr->array_job_id, job_ptr->array_task_id);
	} else {
		snprintf(buf, buf_size, "%s/slurm-%u.out",
			 job_ptr->work_dir, job_ptr->job_id);
	}
}

/* Given a job record pointer, return its stdin path in buf */
extern void slurm_get_job_stdin(char *buf, int buf_size, job_info_t * job_ptr)
{
	if (job_ptr == NULL)
		snprintf(buf, buf_size, "%s", "job pointer is NULL");
	else if (job_ptr->std_in)
		_fname_format(buf, buf_size, job_ptr, job_ptr->std_in);
	else if (job_ptr->batch_flag == 0)
		snprintf(buf, buf_size, "%s", "");
	else
		snprintf(buf, buf_size, "%s", "/dev/null");
}

/* Given a job record pointer, return its stdout path in buf */
extern void slurm_get_job_stdout(char *buf, int buf_size, job_info_t * job_ptr)
{
	if (job_ptr == NULL)
		snprintf(buf, buf_size, "%s", "job pointer is NULL");
	else if (job_ptr->std_out)
		_fname_format(buf, buf_size, job_ptr, job_ptr->std_out);
	else if (job_ptr->batch_flag == 0)
		snprintf(buf, buf_size, "%s", "");
	else if (job_ptr->array_job_id) {
		snprintf(buf, buf_size, "%s/slurm-%u_%u.out",
			 job_ptr->work_dir,
			 job_ptr->array_job_id, job_ptr->array_task_id);
	} else {
		snprintf(buf, buf_size, "%s/slurm-%u.out",
			 job_ptr->work_dir, job_ptr->job_id);
	}
}

/* Return true if the specified job id is local to a cluster
 * (not a federated job) */
static inline bool _test_local_job(uint32_t job_id)
{
	if ((job_id & (~MAX_JOB_ID)) == 0)
		return true;
	return false;
}

static int
_load_cluster_jobs(slurm_msg_t *req_msg, job_info_msg_t **job_info_msg_pptr,
		   slurmdb_cluster_rec_t *cluster)
{
	slurm_msg_t resp_msg;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&resp_msg);

	*job_info_msg_pptr = NULL;

	if (slurm_send_recv_controller_msg(req_msg, &resp_msg, cluster) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_INFO:
		*job_info_msg_pptr = (job_info_msg_t *)resp_msg.data;
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
		errno = rc;

	return rc;
}

/* Thread to read job information from some cluster */
static void *_load_job_thread(void *args)
{
	load_job_req_struct_t *load_args = (load_job_req_struct_t *) args;
	slurmdb_cluster_rec_t *cluster = load_args->cluster;
	job_info_msg_t *new_msg = NULL;
	int rc;

	if ((rc = _load_cluster_jobs(load_args->req_msg, &new_msg, cluster)) ||
	    !new_msg) {
		verbose("Error reading job information from cluster %s: %s",
			cluster->name, slurm_strerror(rc));
	} else {
		load_job_resp_struct_t *job_resp;
		job_resp = xmalloc(sizeof(load_job_resp_struct_t));
		job_resp->new_msg = new_msg;
		list_append(load_args->resp_msg_list, job_resp);
	}
	xfree(args);

	return NULL;
}


static int _sort_orig_clusters(const void *a, const void *b)
{
	slurm_job_info_t *job1 = (slurm_job_info_t *)a;
	slurm_job_info_t *job2 = (slurm_job_info_t *)b;

	if (!xstrcmp(job1->cluster, job1->fed_origin_str))
		return -1;
	if (!xstrcmp(job2->cluster, job2->fed_origin_str))
		return 1;
	return 0;
}

static int _load_fed_jobs(slurm_msg_t *req_msg,
			  job_info_msg_t **job_info_msg_pptr,
			  uint16_t show_flags, char *cluster_name,
			  slurmdb_federation_rec_t *fed)
{
	int i, j;
	load_job_resp_struct_t *job_resp;
	job_info_msg_t *orig_msg = NULL, *new_msg = NULL;
	uint32_t new_rec_cnt;
	uint32_t hash_inx, *hash_tbl_size = NULL, **hash_job_id = NULL;
	slurmdb_cluster_rec_t *cluster;
	list_itr_t *iter;
	int pthread_count = 0;
	pthread_t *load_thread = 0;
	load_job_req_struct_t *load_args;
	list_t *resp_msg_list, *thread_msgs_list;

	*job_info_msg_pptr = NULL;

	/* Spawn one pthread per cluster to collect job information */
	resp_msg_list = list_create(NULL);
	thread_msgs_list = list_create(xfree_ptr);
	load_thread = xmalloc(sizeof(pthread_t) *
			      list_count(fed->cluster_list));
	iter = list_iterator_create(fed->cluster_list);
	while ((cluster = (slurmdb_cluster_rec_t *) list_next(iter))) {
		slurm_msg_t *thread_msg;
		if ((cluster->control_host == NULL) ||
		    (cluster->control_host[0] == '\0'))
			continue;	/* Cluster down */

		/* Only show jobs from the local cluster */
		if ((show_flags & SHOW_LOCAL) &&
		    xstrcmp(cluster->name, cluster_name))
			continue;

		thread_msg = xmalloc(sizeof(*thread_msg));
		list_append(thread_msgs_list, thread_msg);
		memcpy(thread_msg, req_msg, sizeof(*req_msg));

		load_args = xmalloc(sizeof(load_job_req_struct_t));
		load_args->cluster = cluster;
		load_args->req_msg = thread_msg;
		load_args->resp_msg_list = resp_msg_list;
		slurm_thread_create(&load_thread[pthread_count],
				    _load_job_thread, load_args);
		pthread_count++;

	}
	list_iterator_destroy(iter);

	/* Wait for all pthreads to complete */
	for (i = 0; i < pthread_count; i++)
		slurm_thread_join(load_thread[i]);
	xfree(load_thread);
	FREE_NULL_LIST(thread_msgs_list);

	/* Merge the responses into a single response message */
	iter = list_iterator_create(resp_msg_list);
	while ((job_resp = (load_job_resp_struct_t *) list_next(iter))) {
		new_msg = job_resp->new_msg;
		if (!orig_msg) {
			orig_msg = new_msg;
			*job_info_msg_pptr = orig_msg;
		} else {
			/* Merge job records into a single response message */
			orig_msg->last_update = MIN(orig_msg->last_update,
						    new_msg->last_update);
			new_rec_cnt = orig_msg->record_count +
				      new_msg->record_count;
			if (new_msg->record_count) {
				orig_msg->job_array =
					xrealloc(orig_msg->job_array,
						 sizeof(slurm_job_info_t) *
						 new_rec_cnt);
				(void) memcpy(orig_msg->job_array +
					      orig_msg->record_count,
					      new_msg->job_array,
					      sizeof(slurm_job_info_t) *
					      new_msg->record_count);
				orig_msg->record_count = new_rec_cnt;
			}
			xfree(new_msg->job_array);
			xfree(new_msg);
		}
		xfree(job_resp);
	}
	list_iterator_destroy(iter);
	FREE_NULL_LIST(resp_msg_list);

	if (!orig_msg)
		slurm_seterrno_ret(ESLURM_INVALID_JOB_ID);

	/* Find duplicate job records and jobs local to other clusters and set
	 * their job_id == 0 so they get skipped in reporting */
	if ((show_flags & SHOW_SIBLING) == 0) {
		hash_tbl_size = xmalloc(sizeof(uint32_t) * JOB_HASH_SIZE);
		hash_job_id = xmalloc(sizeof(uint32_t *) * JOB_HASH_SIZE);
		for (i = 0; i < JOB_HASH_SIZE; i++) {
			hash_tbl_size[i] = 100;
			hash_job_id[i] = xmalloc(sizeof(uint32_t ) *
						 hash_tbl_size[i]);
		}
	}

	/* Put the origin jobs at top and remove duplicates. */
	qsort(orig_msg->job_array, orig_msg->record_count,
	      sizeof(slurm_job_info_t), _sort_orig_clusters);
	for (i = 0; orig_msg && i < orig_msg->record_count; i++) {
		slurm_job_info_t *job_ptr = &orig_msg->job_array[i];

		/*
		 * Only show non-federated jobs that are local. Non-federated
		 * jobs will not have a fed_origin_str.
		 */
		if (_test_local_job(job_ptr->job_id) &&
		    !job_ptr->fed_origin_str &&
		    xstrcmp(job_ptr->cluster, cluster_name)) {
			job_ptr->job_id = 0;
			continue;
		}

		if (show_flags & SHOW_SIBLING)
			continue;
		hash_inx = job_ptr->job_id % JOB_HASH_SIZE;
		for (j = 0;
		     (j < hash_tbl_size[hash_inx] && hash_job_id[hash_inx][j]);
		     j++) {
			if (job_ptr->job_id == hash_job_id[hash_inx][j]) {
				job_ptr->job_id = 0;
				break;
			}
		}
		if (job_ptr->job_id == 0) {
			continue;	/* Duplicate */
		} else if (j >= hash_tbl_size[hash_inx]) {
			hash_tbl_size[hash_inx] *= 2;
			xrealloc(hash_job_id[hash_inx],
				 sizeof(uint32_t) * hash_tbl_size[hash_inx]);
		}
		hash_job_id[hash_inx][j] = job_ptr->job_id;
	}
	if ((show_flags & SHOW_SIBLING) == 0) {
		for (i = 0; i < JOB_HASH_SIZE; i++)
			xfree(hash_job_id[i]);
		xfree(hash_tbl_size);
		xfree(hash_job_id);
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_job_batch_script - retrieve the batch script for a given jobid
 * returns SLURM_SUCCESS, or appropriate error code
 */
extern int slurm_job_batch_script(FILE *out, uint32_t jobid)
{
	job_id_msg_t msg;
	slurm_msg_t req, resp;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	memset(&msg, 0, sizeof(msg));
	msg.job_id = jobid;
	req.msg_type = REQUEST_BATCH_SCRIPT;
	req.data = &msg;

	if (slurm_send_recv_controller_msg(&req, &resp, working_cluster_rec) < 0)
		return SLURM_ERROR;

	if (resp.msg_type == RESPONSE_BATCH_SCRIPT) {
		if (fprintf(out, "%s", (char *) resp.data) < 0)
			rc = SLURM_ERROR;
		xfree(resp.data);
	} else if (resp.msg_type == RESPONSE_SLURM_RC) {
		rc = ((return_code_msg_t *) resp.data)->return_code;
		slurm_free_return_code_msg(resp.data);
		if (rc)
			slurm_seterrno_ret(rc);
	} else {
		rc = SLURM_ERROR;
	}

	return rc;
}

/*
 * slurm_load_jobs - issue RPC to get all job configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN/OUT job_info_msg_pptr - place to store a job configuration pointer
 * IN show_flags -  job filtering option: 0, SHOW_ALL, SHOW_DETAIL or SHOW_LOCAL
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int
slurm_load_jobs (time_t update_time, job_info_msg_t **job_info_msg_pptr,
		 uint16_t show_flags)
{
	slurm_msg_t req_msg;
	job_info_request_msg_t req;
	char *cluster_name = NULL;
	void *ptr = NULL;
	slurmdb_federation_rec_t *fed;
	int rc;

	if (working_cluster_rec)
		cluster_name = working_cluster_rec->name;
	else
		cluster_name = slurm_conf.cluster_name;

	if ((show_flags & SHOW_FEDERATION) && !(show_flags & SHOW_LOCAL) &&
	    (slurm_load_federation(&ptr) == SLURM_SUCCESS) &&
	    cluster_in_federation(ptr, cluster_name)) {
		/* In federation. Need full info from all clusters */
		update_time = (time_t) 0;
		show_flags &= (~SHOW_LOCAL);
	} else {
		/* Report local cluster info only */
		show_flags |= SHOW_LOCAL;
		show_flags &= (~SHOW_FEDERATION);
	}

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.last_update  = update_time;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_JOB_INFO;
	req_msg.data     = &req;

	if (show_flags & SHOW_FEDERATION) {
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_jobs(&req_msg, job_info_msg_pptr, show_flags,
				    cluster_name, fed);
	} else {
		rc = _load_cluster_jobs(&req_msg, job_info_msg_pptr,
					working_cluster_rec);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
}

/*
 * slurm_load_job_user - issue RPC to get slurm information about all jobs
 *	to be run as the specified user
 * IN/OUT job_info_msg_pptr - place to store a job configuration pointer
 * IN user_id - ID of user we want information for
 * IN show_flags - job filtering options
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int slurm_load_job_user (job_info_msg_t **job_info_msg_pptr,
				uint32_t user_id,
				uint16_t show_flags)
{
	slurm_msg_t req_msg;
	job_user_id_msg_t req;
	void *ptr = NULL;
	slurmdb_federation_rec_t *fed;
	int rc;

	if ((show_flags & SHOW_LOCAL) == 0) {
		if (slurm_load_federation(&ptr) ||
		    !cluster_in_federation(ptr, slurm_conf.cluster_name)) {
			/* Not in federation */
			show_flags |= SHOW_LOCAL;
		}
	}

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.show_flags   = show_flags;
	req.user_id      = user_id;
	req_msg.msg_type = REQUEST_JOB_USER_INFO;
	req_msg.data     = &req;

	/* With -M option, working_cluster_rec is set and  we only get
	 * information for that cluster */
	if (working_cluster_rec || !ptr || (show_flags & SHOW_LOCAL)) {
		rc = _load_cluster_jobs(&req_msg, job_info_msg_pptr,
					working_cluster_rec);
	} else {
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_jobs(&req_msg, job_info_msg_pptr, show_flags,
				    slurm_conf.cluster_name, fed);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
}

/*
 * slurm_load_job - issue RPC to get job information for one job ID
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * IN job_id -  ID of job we want information about
 * IN show_flags -  job filtering option: 0, SHOW_ALL or SHOW_DETAIL
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int
slurm_load_job (job_info_msg_t **job_info_msg_pptr, uint32_t job_id,
		uint16_t show_flags)
{
	slurm_msg_t req_msg;
	job_id_msg_t req;
	void *ptr = NULL;
	slurmdb_federation_rec_t *fed;
	int rc;

	if ((show_flags & SHOW_LOCAL) == 0) {
		if (slurm_load_federation(&ptr) ||
		    !cluster_in_federation(ptr, slurm_conf.cluster_name)) {
			/* Not in federation */
			show_flags |= SHOW_LOCAL;
		}
	}

	memset(&req, 0, sizeof(req));
	slurm_msg_t_init(&req_msg);
	req.job_id       = job_id;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_JOB_INFO_SINGLE;
	req_msg.data     = &req;

	/* With -M option, working_cluster_rec is set and  we only get
	 * information for that cluster */
	if (working_cluster_rec || !ptr || (show_flags & SHOW_LOCAL)) {
		rc = _load_cluster_jobs(&req_msg, job_info_msg_pptr,
					working_cluster_rec);
	} else {
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_jobs(&req_msg, job_info_msg_pptr, show_flags,
				    slurm_conf.cluster_name, fed);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
}

extern int slurm_load_job_state(int job_id_count,
				slurm_selected_step_t *job_ids,
				job_state_response_msg_t **jsr_pptr)
{
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	int rc = SLURM_SUCCESS;
	job_state_request_msg_t req = {
		.count = job_id_count,
		.job_ids = job_ids,
	};

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_JOB_STATE;
	req_msg.data = &req;

	if ((rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg, 0))) {
		error("%s: Unable to query jobs state: %s",
		      __func__, slurm_strerror(rc));
		return rc;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_STATE:
		*jsr_pptr = resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return rc;
}

/*
 * slurm_pid2jobid - issue RPC to get the slurm job_id given a process_id
 *	on this machine
 * IN job_pid     - process_id of interest on this machine
 * OUT job_id_ptr - place to store a slurm job_id
 * RET 0 or -1 on error
 */
extern int
slurm_pid2jobid (pid_t job_pid, uint32_t *jobid)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	job_id_request_msg_t req;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	char *this_addr;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	if (cluster_flags & CLUSTER_FLAG_MULTSD) {
		if ((this_addr = getenv("SLURMD_NODENAME"))) {
			if (slurm_conf_get_addr(this_addr, &req_msg.address,
						req_msg.flags)) {
				/*
				 * The node isn't in the conf, see if the
				 * controller has an address for it.
				 */
				slurm_node_alias_addrs_t *alias_addrs;
				if (!slurm_get_node_alias_addrs(this_addr,
								&alias_addrs)) {
					add_remote_nodes_to_conf_tbls(
						alias_addrs->node_list,
						alias_addrs->node_addrs);
				}
				slurm_free_node_alias_addrs(alias_addrs);
				slurm_conf_get_addr(this_addr, &req_msg.address,
						    req_msg.flags);
			}
		} else {
			this_addr = "localhost";
			slurm_set_addr(&req_msg.address, slurm_conf.slurmd_port,
				       this_addr);
		}
	} else {
		char this_host[256];
		/*
		 *  Set request message address to slurmd on localhost
		 */
		gethostname_short(this_host, sizeof(this_host));
		this_addr = slurm_conf_get_nodeaddr(this_host);
		if (this_addr == NULL)
			this_addr = xstrdup("localhost");
		slurm_set_addr(&req_msg.address, slurm_conf.slurmd_port,
			       this_addr);
		xfree(this_addr);
	}

	memset(&req, 0, sizeof(req));
	req.job_pid      = job_pid;
	req_msg.msg_type = REQUEST_JOB_ID;
	req_msg.data     = &req;
	slurm_msg_set_r_uid(&req_msg, SLURM_AUTH_UID_ANY);

	rc = slurm_send_recv_node_msg(&req_msg, &resp_msg, 0);

	if (rc != SLURM_SUCCESS) {
		if (resp_msg.auth_cred)
			auth_g_destroy(resp_msg.auth_cred);
		return SLURM_ERROR;
	}
	if (resp_msg.auth_cred)
		auth_g_destroy(resp_msg.auth_cred);
	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_ID:
		*jobid = ((job_id_response_msg_t *) resp_msg.data)->job_id;
		slurm_free_job_id_response_msg(resp_msg.data);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_get_rem_time - get the expected time remaining for a given job
 * IN jobid     - slurm job id
 * RET remaining time in seconds or -1 on error
 */
extern long slurm_get_rem_time(uint32_t jobid)
{
	time_t now = time(NULL);
	time_t end_time = 0;
	long rc;

	if (slurm_get_end_time(jobid, &end_time) != SLURM_SUCCESS)
		return -1L;

	rc = difftime(end_time, now);
	if (rc < 0)
		rc = 0L;
	return rc;
}

/* FORTRAN VERSIONS OF slurm_get_rem_time */
extern int32_t islurm_get_rem_time__(uint32_t *jobid)
{
	time_t now = time(NULL);
	time_t end_time = 0;
	int32_t rc;

	if ((jobid == NULL)
	    ||  (slurm_get_end_time(*jobid, &end_time)
		 != SLURM_SUCCESS))
		return 0;

	rc = difftime(end_time, now);
	if (rc < 0)
		rc = 0;
	return rc;
}
extern int32_t islurm_get_rem_time2__()
{
	uint32_t jobid;
	char *slurm_job_id = getenv("SLURM_JOB_ID");

	if (slurm_job_id == NULL)
		return 0;
	jobid = atol(slurm_job_id);
	return islurm_get_rem_time__(&jobid);
}


/*
 * slurm_get_end_time - get the expected end time for a given slurm job
 * IN jobid     - slurm job id
 * end_time_ptr - location in which to store scheduled end time for job
 * RET 0 or -1 on error
 */
extern int
slurm_get_end_time(uint32_t jobid, time_t *end_time_ptr)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_alloc_info_msg_t job_msg;
	srun_timeout_msg_t *timeout_msg;
	time_t now = time(NULL);
	static uint32_t jobid_cache = 0;
	static uint32_t jobid_env = 0;
	static time_t endtime_cache = 0;
	static time_t last_test_time = 0;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	if (!end_time_ptr)
		slurm_seterrno_ret(EINVAL);

	if (jobid == 0) {
		if (jobid_env) {
			jobid = jobid_env;
		} else {
			char *env = getenv("SLURM_JOB_ID");
			if (env) {
				jobid = (uint32_t) atol(env);
				jobid_env = jobid;
			}
		}
		if (jobid == 0) {
			errno = ESLURM_INVALID_JOB_ID;
			return SLURM_ERROR;
		}
	}

	/* Just use cached data if data less than 60 seconds old */
	if ((jobid == jobid_cache)
	&&  (difftime(now, last_test_time) < 60)) {
		*end_time_ptr  = endtime_cache;
		return SLURM_SUCCESS;
	}

	memset(&job_msg, 0, sizeof(job_msg));
	job_msg.job_id     = jobid;
	req_msg.msg_type   = REQUEST_JOB_END_TIME;
	req_msg.data       = &job_msg;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case SRUN_TIMEOUT:
		timeout_msg = (srun_timeout_msg_t *) resp_msg.data;
		last_test_time = time(NULL);
		jobid_cache    = jobid;
		endtime_cache  = timeout_msg->timeout;
		*end_time_ptr  = endtime_cache;
		slurm_free_srun_timeout_msg(resp_msg.data);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (endtime_cache)
			*end_time_ptr  = endtime_cache;
		else if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		if (endtime_cache)
			*end_time_ptr  = endtime_cache;
		else
			slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_job_node_ready - report if nodes are ready for job to execute now
 * IN job_id - slurm job id
 * RET: READY_* values as defined in slurm.h
 */
extern int slurm_job_node_ready(uint32_t job_id)
{
	slurm_msg_t req, resp;
	job_id_msg_t msg;
	int rc;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	memset(&msg, 0, sizeof(msg));
	msg.job_id   = job_id;
	req.msg_type = REQUEST_JOB_READY;
	req.data     = &msg;

	if (slurm_send_recv_controller_msg(&req, &resp, working_cluster_rec) <0)
		return READY_JOB_ERROR;

	if (resp.msg_type == RESPONSE_JOB_READY) {
		rc = ((return_code_msg_t *) resp.data)->return_code;
		slurm_free_return_code_msg(resp.data);
	} else if (resp.msg_type == RESPONSE_SLURM_RC) {
		int job_rc = ((return_code_msg_t *) resp.data) ->
				return_code;
		if ((job_rc == ESLURM_INVALID_PARTITION_NAME) ||
		    (job_rc == ESLURM_INVALID_JOB_ID))
			rc = READY_JOB_FATAL;
		else	/* EAGAIN */
			rc = READY_JOB_ERROR;
		slurm_free_return_code_msg(resp.data);
	} else if (resp.msg_type == RESPONSE_PROLOG_EXECUTING) {
		rc = READY_JOB_ERROR;
	} else {
		rc = READY_JOB_ERROR;
	}

	return rc;
}

/*
 * slurm_network_callerid - issue RPC to get the job id of a job from a remote
 * slurmd based upon network socket information.
 *
 * IN req - Information about network connection in question
 * OUT job_id -  ID of the job or NO_VAL
 * OUT node_name - name of the remote slurmd
 * IN node_name_size - size of the node_name buffer
 * RET SLURM_SUCCESS or SLURM_ERROR on error
 */
extern int
slurm_network_callerid (network_callerid_msg_t req, uint32_t *job_id,
	char *node_name, int node_name_size)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	network_callerid_resp_t *resp;
	slurm_addr_t addr;

	debug("slurm_network_callerid RPC: start");

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	/* ip_src is the IP we want to talk to. Hopefully there's a slurmd
	 * listening there */
	memset(&addr, 0, sizeof(addr));
	addr.ss_family = req.af;

	if (addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *) &addr;
		memcpy(&(in6->sin6_addr.s6_addr), req.ip_src, 16);
	        in6->sin6_port = htons(slurm_conf.slurmd_port);
	} else {
		struct sockaddr_in *in = (struct sockaddr_in *) &addr;
		memcpy(&(in->sin_addr.s_addr), req.ip_src, 4);
		in->sin_port = htons(slurm_conf.slurmd_port);
	}

	req_msg.address = addr;

	req_msg.msg_type = REQUEST_NETWORK_CALLERID;
	req_msg.data     = &req;
	slurm_msg_set_r_uid(&req_msg, SLURM_AUTH_UID_ANY);

	if (slurm_send_recv_node_msg(&req_msg, &resp_msg, 0) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
		case RESPONSE_NETWORK_CALLERID:
			resp = (network_callerid_resp_t*)resp_msg.data;
			*job_id = resp->job_id;
			strlcpy(node_name, resp->node_name, node_name_size);
			break;
		case RESPONSE_SLURM_RC:
			rc = ((return_code_msg_t *) resp_msg.data)->return_code;
			if (rc)
				slurm_seterrno_ret(rc);
			break;
		default:
			slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
			break;
	}

	slurm_free_network_callerid_msg(resp_msg.data);
	return SLURM_SUCCESS;
}

static int
_load_cluster_job_prio(slurm_msg_t *req_msg,
		       priority_factors_response_msg_t **factors_resp,
		       slurmdb_cluster_rec_t *cluster)
{
	slurm_msg_t resp_msg;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&resp_msg);

	if (slurm_send_recv_controller_msg(req_msg, &resp_msg, cluster) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_PRIORITY_FACTORS:
		*factors_resp =
			(priority_factors_response_msg_t *) resp_msg.data;
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
		errno = rc;

	return rc;
}

/* Sort responses so local cluster response is first */
static int _local_resp_first_prio(void *x, void *y)
{
	load_job_prio_resp_struct_t *resp_x = *(load_job_prio_resp_struct_t **)x;
	load_job_prio_resp_struct_t *resp_y = *(load_job_prio_resp_struct_t **)y;

	if (resp_x->local_cluster)
		return -1;
	if (resp_y->local_cluster)
		return 1;
	return 0;
}

/* Add cluster_name to job priority info records */
static void _add_cluster_name(priority_factors_response_msg_t *new_msg,
			      char *cluster_name)
{
	priority_factors_object_t *prio_obj;
	list_itr_t *iter;

	if (!new_msg || !new_msg->priority_factors_list)
		return;

	iter = list_iterator_create(new_msg->priority_factors_list);
	while ((prio_obj = (priority_factors_object_t *) list_next(iter)))
		prio_obj->cluster_name = xstrdup(cluster_name);
	list_iterator_destroy(iter);
}

/* Thread to read job priority factor information from some cluster */
static void *_load_job_prio_thread(void *args)
{
	load_job_req_struct_t *load_args = (load_job_req_struct_t *) args;
	slurmdb_cluster_rec_t *cluster = load_args->cluster;
	priority_factors_response_msg_t *new_msg = NULL;
	int rc;

	if ((rc = _load_cluster_job_prio(load_args->req_msg, &new_msg,
					 cluster)) || !new_msg) {
		verbose("Error reading job information from cluster %s: %s",
			cluster->name, slurm_strerror(rc));
	} else {
		load_job_prio_resp_struct_t *job_resp;
		_add_cluster_name(new_msg, cluster->name);


		job_resp = xmalloc(sizeof(load_job_prio_resp_struct_t));
		job_resp->local_cluster = load_args->local_cluster;
		job_resp->new_msg = new_msg;
		list_append(load_args->resp_msg_list, job_resp);
	}
	xfree(args);

	return NULL;
}

static int _load_fed_job_prio(slurm_msg_t *req_msg,
			      priority_factors_response_msg_t **factors_resp,
			      uint16_t show_flags, char *cluster_name,
			      slurmdb_federation_rec_t *fed)
{
	int i, j;
	int local_job_cnt = 0;
	load_job_prio_resp_struct_t *job_resp;
	priority_factors_response_msg_t *orig_msg = NULL, *new_msg = NULL;
	priority_factors_object_t *prio_obj;
	uint32_t hash_job_inx, *hash_tbl_size = NULL, **hash_job_id = NULL;
	uint32_t hash_part_inx, **hash_part_id = NULL;
	slurmdb_cluster_rec_t *cluster;
	list_itr_t *iter;
	int pthread_count = 0;
	pthread_t *load_thread = 0;
	load_job_req_struct_t *load_args;
	list_t *resp_msg_list;

	*factors_resp = NULL;

	/* Spawn one pthread per cluster to collect job information */
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

		load_args = xmalloc(sizeof(load_job_req_struct_t));
		load_args->cluster = cluster;
		load_args->local_cluster = local_cluster;
		load_args->req_msg = req_msg;
		load_args->resp_msg_list = resp_msg_list;
		slurm_thread_create(&load_thread[pthread_count],
				    _load_job_prio_thread, load_args);
		pthread_count++;

	}
	list_iterator_destroy(iter);

	/* Wait for all pthreads to complete */
	for (i = 0; i < pthread_count; i++)
		slurm_thread_join(load_thread[i]);
	xfree(load_thread);

	/* Move the response from the local cluster (if any) to top of list */
	list_sort(resp_msg_list, _local_resp_first_prio);

	/* Merge the responses into a single response message */
	iter = list_iterator_create(resp_msg_list);
	while ((job_resp = (load_job_prio_resp_struct_t *) list_next(iter))) {
		new_msg = job_resp->new_msg;
		if (!new_msg->priority_factors_list) {
			/* Just skip this one. */
		} else if (!orig_msg) {
			orig_msg = new_msg;
			if (job_resp->local_cluster) {
				local_job_cnt = list_count(
						new_msg->priority_factors_list);
			}
			*factors_resp = orig_msg;
		} else {
			/* Merge prio records into a single response message */
			list_transfer(orig_msg->priority_factors_list,
				      new_msg->priority_factors_list);
			FREE_NULL_LIST(new_msg->priority_factors_list);
			xfree(new_msg);
		}
		xfree(job_resp);
	}
	list_iterator_destroy(iter);
	FREE_NULL_LIST(resp_msg_list);

	/* In a single cluster scenario with no jobs, the priority_factors_list
	 * will be NULL. sprio will handle this above. If the user requests
	 * specific jobids it will give the corresponding error otherwise the
	 * header will be printed and no jobs will be printed out. */
	if (!*factors_resp) {
		*factors_resp =
			xmalloc(sizeof(priority_factors_response_msg_t));
		return SLURM_SUCCESS;
	}

	/* Find duplicate job records and jobs local to other clusters and set
	 * their job_id == 0 so they get skipped in reporting */
	if ((show_flags & SHOW_SIBLING) == 0) {
		hash_tbl_size = xmalloc(sizeof(uint32_t)   * JOB_HASH_SIZE);
		hash_job_id   = xmalloc(sizeof(uint32_t *) * JOB_HASH_SIZE);
		hash_part_id  = xmalloc(sizeof(uint32_t *) * JOB_HASH_SIZE);
		for (i = 0; i < JOB_HASH_SIZE; i++) {
			hash_tbl_size[i] = 100;
			hash_job_id[i]  = xmalloc(sizeof(uint32_t) *
						  hash_tbl_size[i]);
			hash_part_id[i] = xmalloc(sizeof(uint32_t) *
						  hash_tbl_size[i]);
		}
	}
	iter = list_iterator_create(orig_msg->priority_factors_list);
	i = 0;
	while ((prio_obj = (priority_factors_object_t *) list_next(iter))) {
		bool found_job = false, local_cluster = false;
		if (i++ < local_job_cnt) {
			local_cluster = true;
		} else if (_test_local_job(prio_obj->job_id)) {
			list_delete_item(iter);
			continue;
		}

		if (show_flags & SHOW_SIBLING)
			continue;
		hash_job_inx = prio_obj->job_id % JOB_HASH_SIZE;
		if (prio_obj->partition) {
			HASH_FCN(prio_obj->partition,
				 strlen(prio_obj->partition), hash_part_inx);
		} else {
			hash_part_inx = 0;
		}
		for (j = 0;
		     ((j < hash_tbl_size[hash_job_inx]) &&
		      hash_job_id[hash_job_inx][j]); j++) {
			if ((prio_obj->job_id ==
			     hash_job_id[hash_job_inx][j]) &&
			    (hash_part_inx == hash_part_id[hash_job_inx][j])) {
				found_job = true;
				break;
			}
		}
		if (found_job && local_cluster) {
			/* Local job in multiple partitions */
			continue;
		} if (found_job) {
			/* Duplicate remote job,
			 * possible in multiple partitions */
			list_delete_item(iter);
			continue;
		} else if (j >= hash_tbl_size[hash_job_inx]) {
			hash_tbl_size[hash_job_inx] *= 2;
			xrealloc(hash_job_id[hash_job_inx],
				 sizeof(uint32_t) * hash_tbl_size[hash_job_inx]);
		}
		hash_job_id[hash_job_inx][j]  = prio_obj->job_id;
		hash_part_id[hash_job_inx][j] = hash_part_inx;
	}
	list_iterator_destroy(iter);
	if ((show_flags & SHOW_SIBLING) == 0) {
		for (i = 0; i < JOB_HASH_SIZE; i++) {
			xfree(hash_job_id[i]);
			xfree(hash_part_id[i]);
		}
		xfree(hash_tbl_size);
		xfree(hash_job_id);
		xfree(hash_part_id);
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_load_job_prio - issue RPC to get job priority information for jobs
 * OUT factors_resp - job priority factors
 * IN show_flags -  job filtering option: 0 or SHOW_LOCAL
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_priority_factors_response_msg()
 */
extern int
slurm_load_job_prio(priority_factors_response_msg_t **factors_resp,
		    uint16_t show_flags)
{
	slurm_msg_t req_msg;
	void *ptr = NULL;
	slurmdb_federation_rec_t *fed;
	int rc;

	if ((show_flags & SHOW_FEDERATION) && !(show_flags & SHOW_LOCAL) &&
	    (slurm_load_federation(&ptr) == SLURM_SUCCESS) &&
	    cluster_in_federation(ptr, slurm_conf.cluster_name)) {
		/* In federation. Need full info from all clusters */
		show_flags &= (~SHOW_LOCAL);
	} else {
		/* Report local cluster info only */
		show_flags |= SHOW_LOCAL;
		show_flags &= (~SHOW_FEDERATION);
	}

	slurm_msg_t_init(&req_msg);
	req_msg.msg_type = REQUEST_PRIORITY_FACTORS;

	/* With -M option, working_cluster_rec is set and  we only get
	 * information for that cluster */
	if (show_flags & SHOW_FEDERATION) {
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_job_prio(&req_msg, factors_resp, show_flags,
					slurm_conf.cluster_name, fed);
	} else {
		rc = _load_cluster_job_prio(&req_msg, factors_resp,
					    working_cluster_rec);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
}
