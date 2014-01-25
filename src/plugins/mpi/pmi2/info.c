/*****************************************************************************\
 **  info.c - job/node info related functions
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
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

#include "pmi.h"
#include "setup.h"
#include "client.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include "slurm/slurm.h"
#include "src/srun/libsrun/launch.h"
#include "src/common/switch.h"
#include "src/common/slurm_protocol_api.h"

#define NODE_ATTR_SIZE_INC 8

/* pending node attribute get request */
typedef struct nag_req {
	int fd;
	int rank;
	char key[PMI2_MAX_KEYLEN];
	struct nag_req *next;
} nag_req_t;
static nag_req_t *nag_req_list = NULL;

/* node attributes */
static int na_cnt = 0;
static int na_size = 0;
static char **node_attr = NULL;

#define KEY_INDEX(i) (i * 2)
#define VAL_INDEX(i) (i * 2 + 1)


static void inline
_free_nag_req(nag_req_t *req)
{
	xfree (req);
}

extern int
enqueue_nag_req(int fd, int rank, char *key)
{
	nag_req_t *req;

	req = xmalloc(sizeof(nag_req_t));
	req->fd = fd;
	req->rank = rank;
	strncpy(req->key, key, PMI2_MAX_KEYLEN);

	/* insert in the head */
	req->next = nag_req_list;
	nag_req_list = req;
	return SLURM_SUCCESS;
}

extern int
node_attr_put(char *key, char *val)
{
	nag_req_t *req = NULL, **pprev = NULL;
	client_resp_t *resp = NULL;
	int rc = SLURM_SUCCESS;

	debug3("mpi/pmi2: node_attr_put: %s=%s", key, val);

	if (na_cnt * 2 >= na_size) {
		na_size += NODE_ATTR_SIZE_INC;
		xrealloc(node_attr, na_size * sizeof(char*));
	}
	node_attr[KEY_INDEX(na_cnt)] = xstrdup(key);
	node_attr[VAL_INDEX(na_cnt)] = xstrdup(val);
	na_cnt ++;

	/* process pending requests */
	pprev = &nag_req_list;
	req = *pprev;
	while (req != NULL) {
		if (strncmp(key, req->key, PMI2_MAX_KEYLEN)) {
			pprev = &req->next;
			req = *pprev;
		} else {
			debug("mpi/pmi2: found pending request from rank %d",
			      req->rank);

			/* send response msg */
			if (! resp) {
				resp = client_resp_new();
				client_resp_append(resp,
						   CMD_KEY"="
						   GETNODEATTRRESP_CMD";"
						   RC_KEY"=0;"
						   FOUND_KEY"="TRUE_VAL";"
						   VALUE_KEY"=%s;", val);
			}
			rc = client_resp_send(resp, req->fd);
			if (rc != SLURM_SUCCESS) {
				error("mpi/pmi2: failed to send '"
				      GETNODEATTRRESP_CMD "' to task %d",
				      req->rank);
			}
			/* remove the request */
			*pprev = req->next;
			_free_nag_req(req);
			req = *pprev;
		}
	}
	if (resp) {
		client_resp_free (resp);
	}
	debug3("mpi/pmi2: out node_attr_put");
	return SLURM_SUCCESS;
}

/* returned value not dup-ed */
extern char *
node_attr_get(char *key)
{
	int i;
	char *val = NULL;

	debug3("mpi/pmi2: node_attr_get: key=%s", key);

	for (i = 0; i < na_cnt; i ++) {
		if (! strcmp(key, node_attr[KEY_INDEX(i)])) {
			val = node_attr[VAL_INDEX(i)];
			break;
		}
	}

	debug3("mpi/pmi2: out node_attr_get: val=%s", val);
	return val;
}

static char *job_attr_get_netinfo(char *key, char *attr) {
	char *taskid_str, *p;
	int taskid, nodeid;
	slurm_step_layout_t *sl;
	char *netinfo = NULL;

	/* check for switch/generic plugin and information */
	debug3("switch entering");
	if (!job_info.switch_job
		|| strcmp(slurm_get_switch_type(), "switch/generic")!=0) {
		debug3("job_attr_get() netinfo: no switch/generic info");
		return NULL;
	}
	debug3("switch ok");

	/* parse task id */
	taskid_str = key + sizeof(JOB_ATTR_NETINFO) - 1;
	taskid = strtol(taskid_str, &p, 10);
	if (p == taskid_str || errno == EINVAL || errno == ERANGE) {
		debug3("job_attr_get() netinfo: needs a task id");
		return NULL;
	}
	debug3("taskid ok");


	/* get node on wich the task runs */
	sl = slurm_job_step_layout_get(job_info.jobid,
				       job_info.stepid);
	if (!sl) {
		debug3("job_attr_get() netinfo: can't get layout");
		return NULL;
	}
	nodeid = slurm_step_layout_host_id(sl, taskid);
	debug3("node ok");


	/* get network information of node in netinfo, xmalloc'ed */
	switch_g_get_jobinfo(job_info.switch_job, nodeid, &netinfo);
	if (!netinfo) {
		debug3("job_attr_get() no switch job info for node %d", nodeid);
		return NULL;
	}
	snprintf(attr, PMI2_MAX_VALLEN, "%s", netinfo);
	xfree(netinfo);

	debug3("job_attr_get(): task_id_query=%d -> %s", taskid, attr);

	return attr;
}

/* returned value not dup-ed */
extern char *
job_attr_get(char *key)
{
	static char attr[PMI2_MAX_VALLEN];
	job_step_info_response_msg_t *stepmsg = NULL;

	if (!strcmp(key, JOB_ATTR_PROC_MAP)) {
		return job_info.proc_mapping;
	}

	if (!strcmp(key, JOB_ATTR_UNIV_SIZE)) {
		snprintf(attr, PMI2_MAX_VALLEN, "%d", job_info.ntasks);
		return attr;
	}

	if (!strcmp(key, JOB_ATTR_RESV_PORTS)) {
		if (0 != slurm_get_job_steps((time_t) 0, job_info.jobid,
					     job_info.stepid, &stepmsg,
					     SHOW_ALL)) {
			return NULL;
		}
		if (stepmsg == NULL || stepmsg->job_step_count != 1) {
			return NULL;
		}
		snprintf(attr, PMI2_MAX_VALLEN, "%s",
			 stepmsg->job_steps[0].resv_ports);
		slurm_free_job_step_info_response_msg(stepmsg);
		return attr;
	}

	if (strcmp(key, JOB_ATTR_NETINFO) >= 0) { // followed by task_id
		if ( NULL == job_attr_get_netinfo(key, attr)) {
			return NULL;
		}
		return attr;
	}


	return NULL;
}
