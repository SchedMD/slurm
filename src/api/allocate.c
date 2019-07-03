/*****************************************************************************\
 *  allocate.c - allocate nodes for a job or step with supplied contraints
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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
#include <netinet/in.h>			/* for ntohs() */
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getsid(pid_t pid);		/* missing from <unistd.h> */
#endif

#include "slurm/slurm.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/hostlist.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define BUFFER_SIZE 1024
#define MAX_ALLOC_WAIT 60	/* seconds */
#define MIN_ALLOC_WAIT  5	/* seconds */

typedef struct {
	slurm_addr_t address;
	int fd;
	char *hostname;
	uint16_t port;
} listen_t;

typedef struct {
	slurmdb_cluster_rec_t *cluster;
	job_desc_msg_t        *req;
	List                   resp_msg_list;
} load_willrun_req_struct_t;

typedef struct {
	int                      rc;
	will_run_response_msg_t *willrun_resp_msg;
} load_willrun_resp_struct_t;

static int _handle_rc_msg(slurm_msg_t *msg);
static listen_t *_create_allocation_response_socket(void);
static void _destroy_allocation_response_socket(listen_t *listen);
static void _wait_for_allocation_response(uint32_t job_id,
					  const listen_t *listen,
					  uint16_t msg_type, int timeout,
					  void **resp);
static int _job_will_run_cluster(job_desc_msg_t *req,
				 will_run_response_msg_t **will_run_resp,
				 slurmdb_cluster_rec_t *cluster);

/*
 * slurm_allocate_resources - allocate resources for a job request
 * IN job_desc_msg - description of resource allocation request
 * OUT slurm_alloc_msg - response to request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_resource_allocation_response_msg()
 */
int
slurm_allocate_resources (job_desc_msg_t *req,
			  resource_allocation_response_msg_t **resp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	/*
	 * set Node and session id for this request
	 */
	if (req->alloc_sid == NO_VAL)
		req->alloc_sid = getsid(0);

	req_msg.msg_type = REQUEST_RESOURCE_ALLOCATION;
	req_msg.data     = req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					    working_cluster_rec);

	if (rc == SLURM_ERROR)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		*resp = NULL;
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		*resp = (resource_allocation_response_msg_t *) resp_msg.data;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_allocate_resources_blocking
 *	allocate resources for a job request.  This call will block until
 *	the allocation is granted, or the specified timeout limit is reached.
 * IN req - description of resource allocation request
 * IN timeout - amount of time, in seconds, to wait for a response before
 * 	giving up.
 *	A timeout of zero will wait indefinitely.
 * IN pending_callback - If the allocation cannot be granted immediately,
 *      the controller will put the job in the PENDING state.  If
 *      pending callback is not NULL, it will be called with the job_id
 *      of the pending job as the sole parameter.
 *
 * RET allocation structure on success, NULL on error set errno to
 *	indicate the error (errno will be ETIMEDOUT if the timeout is reached
 *      with no allocation granted)
 * NOTE: free the response using slurm_free_resource_allocation_response_msg()
 */
resource_allocation_response_msg_t *
slurm_allocate_resources_blocking (const job_desc_msg_t *user_req,
				   time_t timeout,
				   void(*pending_callback)(uint32_t job_id))
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	resource_allocation_response_msg_t *resp = NULL;
	uint32_t job_id;
	job_desc_msg_t *req;
	listen_t *listen = NULL;
	int errnum = SLURM_SUCCESS;
	bool already_done = false;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	/* make a copy of the user's job description struct so that we
	 * can make changes before contacting the controller */
	req = (job_desc_msg_t *)xmalloc(sizeof(job_desc_msg_t));
	if (req == NULL)
		return NULL;
	memcpy(req, user_req, sizeof(job_desc_msg_t));

	/*
	 * set Node and session id for this request
	 */
	if (req->alloc_sid == NO_VAL)
		req->alloc_sid = getsid(0);

	if (!req->immediate) {
		listen = _create_allocation_response_socket();
		if (listen == NULL) {
			xfree(req);
			return NULL;
		}
		req->alloc_resp_port = listen->port;
	}

	req_msg.msg_type = REQUEST_RESOURCE_ALLOCATION;
	req_msg.data     = req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					    working_cluster_rec);

	if (rc == SLURM_ERROR) {
		int errnum = errno;
		destroy_forward(&req_msg.forward);
		destroy_forward(&resp_msg.forward);
		if (!req->immediate)
			_destroy_allocation_response_socket(listen);
		xfree(req);
		errno = errnum;
		return NULL;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0) {
			/* will reach this when the allocation fails */
			errnum = errno;
		} else {
			/* shouldn't get here */
			errnum = SLURM_ERROR;
		}
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		/* Yay, the controller has acknowledged our request!
		 * Test if we have an allocation yet? */
		resp = (resource_allocation_response_msg_t *) resp_msg.data;
		if (resp->node_cnt > 0) {
			/* yes, allocation has been granted */
			errno = SLURM_SUCCESS;
		} else if (!req->immediate) {
			if (resp->error_code != SLURM_SUCCESS)
				info("%s", slurm_strerror(resp->error_code));
			/* no, we need to wait for a response */

			/* print out any user messages before we wait. */
			print_multi_line_string(resp->job_submit_user_msg,
						-1, LOG_LEVEL_INFO);

			job_id = resp->job_id;
			slurm_free_resource_allocation_response_msg(resp);
			if (pending_callback != NULL)
				pending_callback(job_id);
			_wait_for_allocation_response(job_id, listen,
						RESPONSE_RESOURCE_ALLOCATION,
						timeout, (void **) &resp);
			/* If NULL, we didn't get the allocation in
			   the time desired, so just free the job id */
			if ((resp == NULL) && (errno != ESLURM_ALREADY_DONE)) {
				errnum = errno;
				slurm_complete_job(job_id, -1);
			}
			if ((resp == NULL) && (errno == ESLURM_ALREADY_DONE))
				already_done = true;
		}
		break;
	default:
		errnum = SLURM_UNEXPECTED_MSG_ERROR;
		resp = NULL;
	}

	destroy_forward(&req_msg.forward);
	destroy_forward(&resp_msg.forward);
	if (!req->immediate)
		_destroy_allocation_response_socket(listen);
	xfree(req);
	if (!resp && already_done && (errnum == SLURM_SUCCESS))
		errnum = ESLURM_ALREADY_DONE;
	errno = errnum;
	return resp;
}

static void *_load_willrun_thread(void *args)
{
	load_willrun_req_struct_t *load_args =
		(load_willrun_req_struct_t *)args;
	slurmdb_cluster_rec_t *cluster       = load_args->cluster;
	will_run_response_msg_t *new_msg = NULL;
	load_willrun_resp_struct_t *resp;

	_job_will_run_cluster(load_args->req, &new_msg, cluster);

	resp = xmalloc(sizeof(load_willrun_resp_struct_t));
	resp->rc               = errno;
	resp->willrun_resp_msg = new_msg;
	list_append(load_args->resp_msg_list, resp);
	xfree(args);

	return (void *) NULL;
}

static int _fed_job_will_run(job_desc_msg_t *req,
			     will_run_response_msg_t **will_run_resp,
			     slurmdb_federation_rec_t *fed)
{
	List resp_msg_list;
	int pthread_count = 0, i;
	pthread_t *load_thread = 0;
	load_willrun_req_struct_t *load_args;
	ListIterator iter;
	will_run_response_msg_t *earliest_resp = NULL;
	load_willrun_resp_struct_t *tmp_resp;
	slurmdb_cluster_rec_t *cluster;
	List req_clusters = NULL;

	xassert(req);
	xassert(will_run_resp);

	*will_run_resp = NULL;

	/*
	 * If a subset of clusters was specified then only do a will_run to
	 * those clusters, otherwise check all clusters in the federation.
	 */
	if (req->clusters && xstrcasecmp(req->clusters, "all")) {
		req_clusters = list_create(slurm_destroy_char);
		slurm_addto_char_list(req_clusters, req->clusters);
	}

	/* Spawn one pthread per cluster to collect job information */
	resp_msg_list = list_create(NULL);
	load_thread = xmalloc(sizeof(pthread_t) *
			      list_count(fed->cluster_list));
	iter = list_iterator_create(fed->cluster_list);
	while ((cluster = (slurmdb_cluster_rec_t *)list_next(iter))) {
		if ((cluster->control_host == NULL) ||
		    (cluster->control_host[0] == '\0'))
			continue;	/* Cluster down */

		if (req_clusters &&
		    !list_find_first(req_clusters, slurm_find_char_in_list,
				     cluster->name))
			continue;

		load_args = xmalloc(sizeof(load_willrun_req_struct_t));
		load_args->cluster       = cluster;
		load_args->req           = req;
		load_args->resp_msg_list = resp_msg_list;
		slurm_thread_create(&load_thread[pthread_count],
				    _load_willrun_thread, load_args);
		pthread_count++;
	}
	list_iterator_destroy(iter);
	FREE_NULL_LIST(req_clusters);

	/* Wait for all pthreads to complete */
	for (i = 0; i < pthread_count; i++)
		pthread_join(load_thread[i], NULL);
	xfree(load_thread);

	iter = list_iterator_create(resp_msg_list);
	while ((tmp_resp = (load_willrun_resp_struct_t *)list_next(iter))) {
		if (!tmp_resp->willrun_resp_msg)
			slurm_seterrno(tmp_resp->rc);
		else if ((!earliest_resp) ||
			 (tmp_resp->willrun_resp_msg->start_time <
			  earliest_resp->start_time)) {
			slurm_free_will_run_response_msg(earliest_resp);
			earliest_resp = tmp_resp->willrun_resp_msg;
			tmp_resp->willrun_resp_msg = NULL;
		}

		slurm_free_will_run_response_msg(tmp_resp->willrun_resp_msg);
		xfree(tmp_resp);
	}
	list_iterator_destroy(iter);
	FREE_NULL_LIST(resp_msg_list);

	*will_run_resp = earliest_resp;

	if (!earliest_resp)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/* Get total node count and lead job ID from RESPONSE_JOB_PACK_ALLOCATION */
static void _pack_alloc_test(List resp, uint32_t *node_cnt, uint32_t *job_id)
{
	resource_allocation_response_msg_t *alloc;
	uint32_t inx = 0, pack_node_cnt = 0, pack_job_id = 0;
	ListIterator iter;

	xassert(resp);
	iter = list_iterator_create(resp);
	while ((alloc = (resource_allocation_response_msg_t *)list_next(iter))){
		pack_node_cnt += alloc->node_cnt;
		if (pack_job_id == 0)
			pack_job_id = alloc->job_id;
		print_multi_line_string(alloc->job_submit_user_msg,
					inx, LOG_LEVEL_INFO);
		inx++;
	}
	list_iterator_destroy(iter);

	*job_id   = pack_job_id;
	*node_cnt = pack_node_cnt;
}

/*
 * slurm_allocate_pack_job_blocking
 *	allocate resources for a list of job requests.  This call will block
 *	until the entire allocation is granted, or the specified timeout limit
 *	is reached.
 * IN req - List of resource allocation requests
 * IN timeout - amount of time, in seconds, to wait for a response before
 * 	giving up.
 *	A timeout of zero will wait indefinitely.
 * IN pending_callback - If the allocation cannot be granted immediately,
 *      the controller will put the job in the PENDING state.  If
 *      pending callback is not NULL, it will be called with the job_id
 *      of the pending job as the sole parameter.
 *
 * RET List of allocation structures on success, NULL on error set errno to
 *	indicate the error (errno will be ETIMEDOUT if the timeout is reached
 *      with no allocation granted)
 * NOTE: free the response using list_destroy()
 */
List slurm_allocate_pack_job_blocking(List job_req_list, time_t timeout,
				      void(*pending_callback)(uint32_t job_id))
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	List resp = NULL;
	job_desc_msg_t *req;
	listen_t *listen = NULL;
	int errnum = SLURM_SUCCESS;
	ListIterator iter;
	bool immediate_flag = false;
	uint32_t node_cnt = 0, job_id = 0;
	bool already_done = false;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	/*
	 * set node name and session ID for this request
	 */

	if (!immediate_flag) {
		listen = _create_allocation_response_socket();
		if (listen == NULL)
			return NULL;
	}

	iter = list_iterator_create(job_req_list);
	while ((req = (job_desc_msg_t *) list_next(iter))) {
		if (req->alloc_sid == NO_VAL)
			req->alloc_sid = getsid(0);
		if (listen)
			req->alloc_resp_port = listen->port;

		if (req->immediate)
			immediate_flag = true;
	}
	list_iterator_destroy(iter);

	req_msg.msg_type = REQUEST_JOB_PACK_ALLOCATION;
	req_msg.data     = job_req_list;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					    working_cluster_rec);

	if (rc == SLURM_ERROR) {
		int errnum = errno;
		destroy_forward(&req_msg.forward);
		destroy_forward(&resp_msg.forward);
		if (listen)
			_destroy_allocation_response_socket(listen);
		errno = errnum;
		return NULL;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0) {
			/* will reach this when the allocation fails */
			errnum = errno;
		} else {
			/* shouldn't get here */
			errnum = SLURM_ERROR;
		}
		break;
	case RESPONSE_JOB_PACK_ALLOCATION:
		/* Yay, the controller has acknowledged our request!
		 * Test if we have an allocation yet? */
		resp = (List) resp_msg.data;
		_pack_alloc_test(resp, &node_cnt, &job_id);
		if (node_cnt > 0) {
			/* yes, allocation has been granted */
			errno = SLURM_SUCCESS;
		} else if (immediate_flag) {
			debug("Immediate allocation not granted");
		} else {
			/* no, logs user messages and wait for a response */
			FREE_NULL_LIST(resp);
			if (pending_callback != NULL)
				pending_callback(job_id);
			_wait_for_allocation_response(job_id, listen,
						RESPONSE_JOB_PACK_ALLOCATION,
						timeout, (void **) &resp);
			/* If NULL, we didn't get the allocation in
			 * the time desired, so just free the job id */
			if ((resp == NULL) && (errno != ESLURM_ALREADY_DONE)) {
				errnum = errno;
				slurm_complete_job(job_id, -1);
			}
			if ((resp == NULL) && (errno == ESLURM_ALREADY_DONE))
				already_done = true;
		}
		break;
	default:
		errnum = SLURM_UNEXPECTED_MSG_ERROR;
	}

	destroy_forward(&req_msg.forward);
	destroy_forward(&resp_msg.forward);
	if (listen)
		_destroy_allocation_response_socket(listen);
	if (!resp && already_done && (errnum == SLURM_SUCCESS))
		errnum = ESLURM_ALREADY_DONE;
	errno = errnum;

	return resp;
}

/*
 * slurm_job_will_run - determine if a job would execute immediately if
 *	submitted now
 * IN job_desc_msg - description of resource allocation request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
int slurm_job_will_run(job_desc_msg_t *req)
{
	will_run_response_msg_t *will_run_resp = NULL;
	char buf[64];
	int rc;
	char *cluster_name = NULL;
	void *ptr = NULL;

	if (working_cluster_rec)
		cluster_name = working_cluster_rec->name;
	else
		cluster_name = slurmctld_conf.cluster_name;
	if (!slurm_load_federation(&ptr) &&
	    cluster_in_federation(ptr, cluster_name))
		rc = _fed_job_will_run(req, &will_run_resp, ptr);
	else
		rc = slurm_job_will_run2(req, &will_run_resp);

	if (will_run_resp)
		print_multi_line_string(
			will_run_resp->job_submit_user_msg,
			-1, LOG_LEVEL_INFO);

	if ((rc == 0) && will_run_resp) {
		slurm_make_time_str(&will_run_resp->start_time,
				    buf, sizeof(buf));
		if (will_run_resp->part_name) {
			info("Job %u to start at %s using %u processors on nodes %s in partition %s",
			     will_run_resp->job_id, buf,
			     will_run_resp->proc_cnt,
			     will_run_resp->node_list,
			     will_run_resp->part_name);
		} else {
			/*
			 * Partition name not provided from slurmctld v17.11
			 * or earlier. Remove this in the future.
			 */
			info("Job %u to start at %s using %u processors on nodes %s",
			     will_run_resp->job_id, buf,
			     will_run_resp->proc_cnt,
			     will_run_resp->node_list);
		}
		if (will_run_resp->preemptee_job_id) {
			ListIterator itr;
			uint32_t *job_id_ptr;
			char *job_list = NULL, *sep = "";
			itr = list_iterator_create(will_run_resp->
						   preemptee_job_id);
			while ((job_id_ptr = list_next(itr))) {
				if (job_list)
					sep = ",";
				xstrfmtcat(job_list, "%s%u", sep, *job_id_ptr);
			}
			list_iterator_destroy(itr);
			info("  Preempts: %s", job_list);
			xfree(job_list);
		}

		slurm_free_will_run_response_msg(will_run_resp);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
}

/*
 * slurm_pack_job_will_run - determine if a heterogeneous job would execute
 *	immediately if submitted now
 * IN job_req_list - List of job_desc_msg_t structures describing the resource
 *		allocation request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_pack_job_will_run(List job_req_list)
{
	job_desc_msg_t *req;
	will_run_response_msg_t *will_run_resp;
	char buf[64], *sep = "";
	int rc = SLURM_SUCCESS, inx = 0;
	ListIterator iter, itr;
	time_t first_start = (time_t) 0;
	uint32_t first_job_id = 0, tot_proc_count = 0, *job_id_ptr;
	hostset_t hs = NULL;
	char *job_list = NULL;

	if (!job_req_list || (list_count(job_req_list) == 0)) {
		error("No job descriptors input");
		return SLURM_ERROR;
	}

	iter = list_iterator_create(job_req_list);
	while ((req = (job_desc_msg_t *) list_next(iter))) {
		will_run_resp = NULL;
		rc = slurm_job_will_run2(req, &will_run_resp);

		if (will_run_resp)
			print_multi_line_string(
				will_run_resp->job_submit_user_msg,
				inx, LOG_LEVEL_INFO);

		if ((rc == SLURM_SUCCESS) && will_run_resp) {
			if (first_job_id == 0)
				first_job_id = will_run_resp->job_id;
			if ((first_start == 0) ||
			    (first_start < will_run_resp->start_time))
				first_start = will_run_resp->start_time;
			tot_proc_count += will_run_resp->proc_cnt;
			if (hs)
				hostset_insert(hs, will_run_resp->node_list);
			else
				hs = hostset_create(will_run_resp->node_list);

			if (will_run_resp->preemptee_job_id) {
				itr = list_iterator_create(will_run_resp->
							   preemptee_job_id);
				while ((job_id_ptr = list_next(itr))) {
					if (job_list)
						sep = ",";
					xstrfmtcat(job_list, "%s%u", sep,
						   *job_id_ptr);
				}
				list_iterator_destroy(itr);
			}

			slurm_free_will_run_response_msg(will_run_resp);
		}
		if (rc != SLURM_SUCCESS)
			break;
		inx++;
	}
	list_iterator_destroy(iter);


	if (rc == SLURM_SUCCESS) {
		char node_list[1028] = "";

		if (hs)
			hostset_ranged_string(hs, sizeof(node_list), node_list);
		slurm_make_time_str(&first_start, buf, sizeof(buf));
		info("Job %u to start at %s using %u processors on %s",
		     first_job_id, buf, tot_proc_count, node_list);
		if (job_list)
			info("  Preempts: %s", job_list);
	}

	if (hs)
		hostset_destroy(hs);
	xfree(job_list);

	return rc;
}

/*
 * slurm_job_will_run2 - determine if a job would execute immediately if
 * 	submitted now
 * IN job_desc_msg - description of resource allocation request
 * OUT will_run_resp - job run time data
 * 	free using slurm_free_will_run_response_msg()
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
int slurm_job_will_run2 (job_desc_msg_t *req,
			 will_run_response_msg_t **will_run_resp)
{
	return _job_will_run_cluster(req, will_run_resp, working_cluster_rec);
}

static int _job_will_run_cluster(job_desc_msg_t *req,
				 will_run_response_msg_t **will_run_resp,
				 slurmdb_cluster_rec_t *cluster)
{
	slurm_msg_t req_msg, resp_msg;
	int rc;
	/* req.immediate = true;    implicit */

	slurm_msg_t_init(&req_msg);
	req_msg.msg_type = REQUEST_JOB_WILL_RUN;
	req_msg.data     = req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg, cluster);

	if (rc < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		break;
	case RESPONSE_JOB_WILL_RUN:
		*will_run_resp = (will_run_response_msg_t *) resp_msg.data;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_job_step_create - create a job step for a given job id
 * IN slurm_step_alloc_req_msg - description of job step request
 * OUT slurm_step_alloc_resp_msg - response to request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_job_step_create_response_msg
 */
int
slurm_job_step_create (job_step_create_request_msg_t *req,
                       job_step_create_response_msg_t **resp)
{
	slurm_msg_t req_msg, resp_msg;
	int delay = 0, rc, retry = 0;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_JOB_STEP_CREATE;
	req_msg.data     = req;

re_send:
	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		rc = _handle_rc_msg(&resp_msg);
		if ((rc < 0) && (errno == EAGAIN)) {
			if (retry++ == 0) {
				verbose("Slurm is busy, step creation delayed");
				delay = (getpid() % 10) + 10;	/* 10-19 secs */
			}
			sleep(delay);
			goto re_send;
		}
		if (rc < 0)
			return SLURM_ERROR;
		*resp = NULL;
		break;
	case RESPONSE_JOB_STEP_CREATE:
		*resp = (job_step_create_response_msg_t *) resp_msg.data;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS ;
}

/*
 * slurm_allocation_lookup - retrieve info for an existing resource allocation
 * 			     without the addrs and such
 * IN jobid - job allocation identifier
 * OUT info - job allocation information
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_resource_allocation_response_msg()
 */
extern int slurm_allocation_lookup(uint32_t jobid,
				   resource_allocation_response_msg_t **info)
{
	job_alloc_info_msg_t req;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	memset(&req, 0, sizeof(req));
	req.job_id = jobid;
	req.req_cluster  = slurmctld_conf.cluster_name;
	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_JOB_ALLOCATION_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	req.req_cluster = NULL;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		*info = NULL;
		break;
	case RESPONSE_JOB_ALLOCATION_INFO:
		*info = (resource_allocation_response_msg_t *) resp_msg.data;
		return SLURM_SUCCESS;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_pack_job_lookup - retrieve info for an existing heterogeneous job
 * 			   allocation without the addrs and such
 * IN jobid - job allocation identifier
 * OUT info - job allocation information
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: returns information an individual job as well
 * NOTE: free the response using list_destroy()
 */
extern int slurm_pack_job_lookup(uint32_t jobid, List *info)
{
	job_alloc_info_msg_t req;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	memset(&req, 0, sizeof(req));
	req.job_id = jobid;
	req.req_cluster  = slurmctld_conf.cluster_name;
	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_JOB_PACK_ALLOC_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	req.req_cluster = NULL;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		*info = NULL;
		break;
	case RESPONSE_JOB_PACK_ALLOCATION:
		*info = (List) resp_msg.data;
		return SLURM_SUCCESS;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_sbcast_lookup - retrieve info for an existing resource allocation
 *	including a credential needed for sbcast
 * IN job_id - job allocation identifier (or pack job ID)
 * IN pack_job_offset - pack job  index (or NO_VAL if not pack job)
 * IN step_id - step allocation identifier (or NO_VAL for entire job)
 * OUT info - job allocation information including a credential for sbcast
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the "resp" using slurm_free_sbcast_cred_msg
 */
extern int slurm_sbcast_lookup(uint32_t job_id, uint32_t pack_job_offset,
			       uint32_t step_id, job_sbcast_cred_msg_t **info)
{
	step_alloc_info_msg_t req;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	memset(&req, 0, sizeof(req));
	req.job_id = job_id;
	req.pack_job_offset = pack_job_offset;
	req.step_id = step_id;
	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_JOB_SBCAST_CRED;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg,
					   &resp_msg,working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		*info = NULL;
		break;
	case RESPONSE_JOB_SBCAST_CRED:
		*info = (job_sbcast_cred_msg_t *)resp_msg.data;
		return SLURM_SUCCESS;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/*
 *  Handle a return code message type.
 *    if return code is nonzero, sets errno to return code and returns < 0.
 *    Otherwise, returns 0 (SLURM_SUCCES)
 */
static int
_handle_rc_msg(slurm_msg_t *msg)
{
	int rc = ((return_code_msg_t *) msg->data)->return_code;
	slurm_free_return_code_msg(msg->data);

	if (rc)
		slurm_seterrno_ret(rc);
	else
		return SLURM_SUCCESS;
}

/*
 * Read a Slurm hostfile specified by "filename".  "filename" must contain
 * a list of Slurm NodeNames, one per line.  Reads up to "n" number of hostnames
 * from the file. Returns a string representing a hostlist ranged string of
 * the contents of the file.  This is a helper function, it does not
 * contact any Slurm daemons.
 *
 * Returns a string representing the hostlist.  Returns NULL if there are fewer
 * than "n" hostnames in the file, or if an error occurs.  If "n" ==
 * NO_VAL then the entire file is read in
 *
 * Returned string must be freed with free().
 */
char *slurm_read_hostfile(const char *filename, int n)
{
	FILE *fp = NULL;
	char in_line[BUFFER_SIZE];	/* input line */
	int i, j;
	int line_size;
	int line_num = 0;
	hostlist_t hostlist = NULL;
	char *nodelist = NULL, *end_part = NULL;
	char *asterisk, *tmp_text = NULL, *save_ptr = NULL, *host_name;
	int total_file_len = 0;

	if (filename == NULL || strlen(filename) == 0)
		return NULL;

	if ((fp = fopen(filename, "r")) == NULL) {
		error("slurm_allocate_resources error opening file %s, %m",
		      filename);
		return NULL;
	}

	hostlist = hostlist_create(NULL);
	if (hostlist == NULL) {
		fclose(fp);
		return NULL;
	}

	while (fgets(in_line, BUFFER_SIZE, fp) != NULL) {

		line_size = strlen(in_line);
		for (i = 0; i < line_size; i++) {
			if (in_line[i] == '\n') {
				in_line[i] = '\0';
				break;
			}
			if (in_line[i] == '\0')
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {
				for (j = i; j < line_size; j++) {
					in_line[j - 1] = in_line[j];
				}
				line_size--;
				continue;
			}
			in_line[i] = '\0';
			break;
		}

		/*
		 * Get the string length again just to in case it changed from
		 * the above loop
		 */
		line_size = strlen(in_line);
		total_file_len += line_size;

		/*
		 * If there was an end section from before set it up to be on
		 * the front of this next chunk.
		 */
		if (end_part) {
			tmp_text = end_part;
			end_part = NULL;
		}

		if (line_size == (BUFFER_SIZE - 1)) {
			/*
			 * If we filled up the buffer get the end past the last
			 * comma.  We will tack it on the next pass through.
			 */
			char *last_comma = strrchr(in_line, ',');
			if (!last_comma) {
				error("Line %d, of hostfile %s too long",
				      line_num, filename);
				fclose(fp);
				hostlist_destroy(hostlist);
				return NULL;
			}
			end_part = xstrdup(last_comma + 1);
			*last_comma = '\0';
		} else
			line_num++;

		xstrcat(tmp_text, in_line);

		/* Skip this line */
		if (tmp_text[0] == '\0')
			continue;

		if (!isalpha(tmp_text[0]) && !isdigit(tmp_text[0])) {
			error("Invalid hostfile %s contents on line %d",
			      filename, line_num);
			fclose(fp);
			hostlist_destroy(hostlist);
			xfree(end_part);
			xfree(tmp_text);
			return NULL;
		}

		host_name = strtok_r(tmp_text, ",", &save_ptr);
		while (host_name) {
			if ((asterisk = strchr(host_name, '*')) &&
			    (i = atoi(asterisk + 1))) {
				asterisk[0] = '\0';

				/*
				 * Don't forget the extra space potentially
				 * needed
				 */
				total_file_len += strlen(host_name) * i;

				for (j = 0; j < i; j++)
					hostlist_push_host(hostlist, host_name);
			} else {
				hostlist_push_host(hostlist, host_name);
			}
			host_name = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp_text);

		if ((n != (int)NO_VAL) && (hostlist_count(hostlist) == n))
			break;
	}
	fclose(fp);

	if (hostlist_count(hostlist) <= 0) {
		error("Hostlist is empty!");
		goto cleanup_hostfile;
	}
	if (hostlist_count(hostlist) < n) {
		error("Too few NodeNames in Slurm Hostfile");
		goto cleanup_hostfile;
	}

	total_file_len += 1024;
	nodelist = (char *)malloc(total_file_len);
	if (!nodelist) {
		error("Nodelist xmalloc failed");
		goto cleanup_hostfile;
	}

	if (hostlist_ranged_string(hostlist, total_file_len, nodelist) == -1) {
		error("Hostlist is too long for the allocate RPC!");
		free(nodelist);
		nodelist = NULL;
		goto cleanup_hostfile;
	}

	debug2("Hostlist from SLURM_HOSTFILE = %s", nodelist);

cleanup_hostfile:
	hostlist_destroy(hostlist);
	xfree(end_part);
	xfree(tmp_text);

	return nodelist;
}

/***************************************************************************
 * Support functions for slurm_allocate_resources_blocking()
 ***************************************************************************/
static listen_t *_create_allocation_response_socket(void)
{
	listen_t *listen = NULL;
	uint16_t *ports;

	listen = xmalloc(sizeof(listen_t));

	if ((ports = slurm_get_srun_port_range()))
		listen->fd = slurm_init_msg_engine_ports(ports);
	else
		listen->fd = slurm_init_msg_engine_port(0);

	if (listen->fd < 0) {
		error("slurm_init_msg_engine_port error %m");
		return NULL;
	}

	if (slurm_get_stream_addr(listen->fd, &listen->address) < 0) {
		error("slurm_get_stream_addr error %m");
		close(listen->fd);
		return NULL;
	}
	listen->hostname = xshort_hostname();
	/* FIXME - screw it!  I can't seem to get the port number through
	   slurm_* functions */
	listen->port = ntohs(listen->address.sin_port);
	fd_set_nonblocking(listen->fd);

	return listen;
}

static void _destroy_allocation_response_socket(listen_t *listen)
{
	xassert(listen != NULL);

	close(listen->fd);
	if (listen->hostname)
		xfree(listen->hostname);
	xfree(listen);
}

/* process RPC from slurmctld
 * IN msg: message received
 * OUT resp: resource allocation response message or List of them
 * RET 1 if resp is filled in, 0 otherwise */
static int
_handle_msg(slurm_msg_t *msg, uint16_t msg_type, void **resp)
{
	uid_t req_uid;
	uid_t uid       = getuid();
	uid_t slurm_uid = (uid_t) slurm_get_slurm_user_id();
	int rc = 0;

	req_uid = g_slurm_auth_get_uid(msg->auth_cred);

	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u",
			(unsigned int) req_uid);
		return 0;
	}

	if (msg->msg_type == msg_type) {
		debug2("resource allocation response received");
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		*resp = msg->data;    /* transfer payload to response */
		msg->data = NULL;
		rc = 1;
	} else if (msg->msg_type == SRUN_JOB_COMPLETE) {
		info("Job has been cancelled");
	} else {
		error("%s: received spurious message type: %u",
		      __func__, msg->msg_type);
	}
	return rc;
}

/* Accept RPC from slurmctld and process it.
 * IN slurmctld_fd: file descriptor for slurmctld communications
 * IN msg_type: RESPONSE_RESOURCE_ALLOCATION or RESPONSE_JOB_PACK_ALLOCATION
 * OUT resp: resource allocation response message or List
 * RET 1 if resp is filled in, 0 otherwise */
static int
_accept_msg_connection(int listen_fd, uint16_t msg_type, void **resp)
{
	int	     conn_fd;
	slurm_msg_t  *msg = NULL;
	slurm_addr_t cli_addr;
	char         host[256];
	uint16_t     port;
	int          rc = 0;

	conn_fd = slurm_accept_msg_conn(listen_fd, &cli_addr);
	if (conn_fd < 0) {
		error("Unable to accept connection: %m");
		return rc;
	}

	slurm_get_addr(&cli_addr, &port, host, sizeof(host));
	debug2("got message connection from %s:%hu", host, port);

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);

	if ((rc = slurm_receive_msg(conn_fd, msg, 0)) != 0) {
		slurm_free_msg(msg);

		if (errno == EINTR) {
			close(conn_fd);
			*resp = NULL;
			return 0;
		}

		error("%s[%s]: %m", __func__, host);
		close(conn_fd);
		return SLURM_ERROR;
	}

	rc = _handle_msg(msg, msg_type, resp); /* xfer payload */
	slurm_free_msg(msg);

	close(conn_fd);
	return rc;
}

/* Wait up to sleep_time for RPC from slurmctld indicating resource allocation
 * has occured.
 * IN sleep_time: delay in seconds (0 means unbounded wait)
 * RET -1: error, 0: timeout, 1:ready to read */
static int _wait_for_alloc_rpc(const listen_t *listen, int sleep_time)
{
	struct pollfd fds[1];
	int rc;
	int timeout_ms;

	if (listen == NULL) {
		error("Listening port not found");
		sleep(MAX(sleep_time, 1));
		return -1;
	}

	fds[0].fd = listen->fd;
	fds[0].events = POLLIN;

	if (sleep_time != 0)
		timeout_ms = sleep_time * 1000;
	else
		timeout_ms = -1;

	while ((rc = poll(fds, 1, timeout_ms)) < 0) {
		switch (errno) {
		case EAGAIN:
		case EINTR:
			return -1;
		case EBADF:
		case ENOMEM:
		case EINVAL:
		case EFAULT:
			error("poll: %m");
			return -1;
		default:
			error("poll: %m. Continuing...");
		}
	}

	if (rc == 0) { /* poll timed out */
		errno = ETIMEDOUT;
	} else if (fds[0].revents & POLLIN) {
		return 1;
	}

	return 0;
}

static void _wait_for_allocation_response(uint32_t job_id,
					  const listen_t *listen,
					  uint16_t msg_type, int timeout,
					  void **resp)
{
	int errnum, rc;

	info("job %u queued and waiting for resources", job_id);
	*resp = NULL;
	if ((rc = _wait_for_alloc_rpc(listen, timeout)) == 1)
		rc = _accept_msg_connection(listen->fd, msg_type, resp);
	if (rc <= 0) {
		errnum = errno;
		/* Maybe the resource allocation response RPC got lost
		 * in the mail; surely it should have arrived by now.
		 * Let's see if the controller thinks that the allocation
		 * has been granted.
		 */
		if (msg_type == RESPONSE_RESOURCE_ALLOCATION) {
			if (slurm_allocation_lookup(job_id,
					(resource_allocation_response_msg_t **)
					resp) >= 0)
				return;
		} else if (msg_type == RESPONSE_JOB_PACK_ALLOCATION) {
			if (slurm_pack_job_lookup(job_id, (List *) resp) >= 0)
				return;
		} else {
			error("%s: Invalid msg_type (%u)", __func__, msg_type);
		}

		if (slurm_get_errno() == ESLURM_JOB_PENDING) {
			debug3("Still waiting for allocation");
			errno = errnum;
			return;
		} else {
			debug3("Unable to confirm allocation for job %u: %m",
			       job_id);
			return;
		}
	}
	info("job %u has been allocated resources", job_id);
	return;
}
