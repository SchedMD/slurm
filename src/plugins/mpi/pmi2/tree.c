/*****************************************************************************\
 **  tree.c - PMI tree communication handling code
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"

#include "kvs.h"
#include "spawn.h"
#include "client.h"
#include "setup.h"
#include "pmi.h"

static int _handle_kvs_fence(int fd, Buf buf);
static int _handle_kvs_fence_resp(int fd, Buf buf);
static int _handle_spawn(int fd, Buf buf);
static int _handle_spawn_resp(int fd, Buf buf);

static int (*tree_cmd_handlers[]) (int fd, Buf buf) = {
	_handle_kvs_fence,
	_handle_kvs_fence_resp,
	_handle_spawn,
	_handle_spawn_resp,
	NULL
};

static char *tree_cmd_names[] = {
	"TREE_CMD_KVS_FENCE",
	"TREE_CMD_KVS_FENCE_RESP",
	"TREE_CMD_SPAWN",
	"TREE_CMD_SPAWN_RESP",
	NULL,
};
	
static int
_handle_kvs_fence(int fd, Buf buf)
{
	uint32_t from_nodeid, num_children, temp32;
	char *from_node = NULL;

	safe_unpack32(&from_nodeid, buf);
	safe_unpackstr_xmalloc(&from_node, &temp32, buf);
	safe_unpack32(&num_children, buf);
	
	debug3("mpi/pmi2: in _handle_kvs_fence, from node %u(%s) representing"
	       " %u offspring", from_nodeid, from_node, num_children);

	if (tasks_to_wait == 0 && children_to_wait == 0) {
		tasks_to_wait = job_info.ltasks;
		children_to_wait = tree_info.num_children;
	}
	children_to_wait -= num_children;

	temp_kvs_merge(buf);

	if (children_to_wait == 0 && tasks_to_wait == 0) {
		temp_kvs_send();
	}
	debug3("mpi/pmi2: out _handle_kvs_fence, tasks_to_wait=%d, "
	       "children_to_wait=%d", tasks_to_wait, children_to_wait);
	return SLURM_SUCCESS;

unpack_error:
	error("mpi/pmi2: failed to unpack kvs fence message");
	return SLURM_ERROR;
}

static int
_handle_kvs_fence_resp(int fd, Buf buf)
{
	char *key, *val;
	int rc = 0, i = 0;
	client_resp_t *resp;
	uint32_t temp32;

	debug3("mpi/pmi2: in _handle_kvs_fence_resp");
	temp32 = remaining_buf(buf);
	debug3("mpi/pmi2: buf length: %u", temp32);
	/* put kvs into local hash */
	while (remaining_buf(buf) > 0) {
		safe_unpackstr_xmalloc(&key, &temp32, buf);
		safe_unpackstr_xmalloc(&val, &temp32, buf);
		kvs_put(key, val);
		//temp32 = remaining_buf(buf);
		xfree(key);
		xfree(val);
	}

resp:
	/* send fence_resp/barrier_out to tasks */
	resp = client_resp_new();
	if ( is_pmi11() ) {
		client_resp_append(resp, CMD_KEY"="BARRIEROUT_CMD" "
				   RC_KEY"=%d\n", rc);
	} else if (is_pmi20()) {
		client_resp_append(resp, CMD_KEY"="KVSFENCERESP_CMD";"
				   RC_KEY"=%d;", rc);
	}
	for (i = 0; i < job_info.ltasks; i ++) {
		client_resp_send(resp, STEPD_PMI_SOCK(i));
	}
	client_resp_free(resp);
	return rc;

unpack_error:
	error("mpi/pmi2: unpack kvs error in fence resp");
	rc = SLURM_ERROR;
	goto resp;
}

/* only called in srun */
static int
_handle_spawn(int fd, Buf buf)
{
	int rc;
	spawn_req_t *req = NULL;
	spawn_resp_t *resp = NULL;

	debug3("mpi/pmi2: in _handle_spawn");
	
	rc = spawn_req_unpack(&req, buf);
	if (rc != SLURM_SUCCESS) {
		error("mpi/pmi2: failed to unpack spawn request spawn cmd");
		/* We lack a hostname to send response below.
		 resp = spawn_resp_new();
		resp->rc = rc;
		rc = spawn_resp_send_to_stepd(resp, req->from_node);
		spawn_resp_free(resp); */
		return rc;
	}

	/* assign a sequence number */
	req->seq = spawn_seq_next();
	resp = spawn_resp_new();
	resp->seq = req->seq;
	resp->jobid = NULL;
	resp->error_cnt = 0;
	
	/* fork srun */
	rc = spawn_job_do_spawn(req);
	if (rc != SLURM_SUCCESS) {
		error("mpi/pmi2: failed to spawn job");
		resp->rc = rc;
	} else {
		spawn_psr_enqueue(resp->seq, -1, -1, req->from_node);
		resp->rc = SLURM_SUCCESS; /* temp resp */
	}

	spawn_resp_send_to_fd(resp, fd);

	spawn_req_free(req);
	spawn_resp_free(resp);

	debug3("mpi/pmi2: out _handle_spawn");
	return rc;
}

static int
_send_task_spawn_resp_pmi20(spawn_resp_t *spawn_resp, int task_fd,
			    int task_lrank)
{
	int i, rc;
	client_resp_t *task_resp;
	char *error_codes = NULL;
	
	task_resp = client_resp_new();
	client_resp_append(task_resp,
			   CMD_KEY"="SPAWNRESP_CMD";"
			   RC_KEY"=%d;"
			   JOBID_KEY"=%s;",
			   spawn_resp->rc,
			   spawn_resp->jobid);
	/* seems that simple2pmi does not consider rc */
	if (spawn_resp->rc != SLURM_SUCCESS) {
		xstrfmtcat(error_codes, "%d", spawn_resp->rc);
	}
	if (spawn_resp->error_cnt > 0) {
		if (error_codes) {
			xstrfmtcat(error_codes, ",%d", spawn_resp->error_codes[0]);
		} else {
			xstrfmtcat(error_codes, "%d", spawn_resp->error_codes[0]);
		}
			
		for (i = 1; i < spawn_resp->error_cnt; i ++) {
			xstrfmtcat(error_codes, ",%d",
				   spawn_resp->error_codes[i]);
		}
	}
	if (error_codes) {
		client_resp_append(task_resp, ERRCODES_KEY"=%s;",
				   error_codes);
		xfree(error_codes);
	}
		
	rc = client_resp_send(task_resp, task_fd);
	client_resp_free(task_resp);
	return rc;
}

static int
_send_task_spawn_resp_pmi11(spawn_resp_t *spawn_resp, int task_fd,
			    int task_lrank)
{
	int i, rc;
	client_resp_t *task_resp;
	char *error_codes = NULL;

	task_resp = client_resp_new();
	client_resp_append(task_resp,
			   CMD_KEY"="SPAWNRESULT_CMD" "
			   RC_KEY"=%d "
			   JOBID_KEY"=%s", /* JOBID_KEY is not required */
			   spawn_resp->rc,
			   spawn_resp->jobid);

	if (spawn_resp->rc != SLURM_SUCCESS) {
		xstrfmtcat(error_codes, "%d", spawn_resp->rc);
	}
	if (spawn_resp->error_cnt > 0) {
		if (error_codes) {
			xstrfmtcat(error_codes, ",%d", spawn_resp->error_codes[0]);
		} else {
			xstrfmtcat(error_codes, "%d", spawn_resp->error_codes[0]);
		}
			
		for (i = 1; i < spawn_resp->error_cnt; i ++) {
			xstrfmtcat(error_codes, ",%d",
				   spawn_resp->error_codes[i]);
		}
	}
	if (error_codes) {
		client_resp_append(task_resp, " "ERRCODES_KEY"=%s\n",
				   error_codes);
		xfree(error_codes);
	} else {
		client_resp_append(task_resp, "\n");
	}
	
	rc = client_resp_send(task_resp, task_fd);
	client_resp_free(task_resp);
	return rc;
}

/* called in stepd and srun */
static int
_handle_spawn_resp(int fd, Buf buf)
{
	int rc, task_fd, task_lrank;
	spawn_resp_t *spawn_resp;
	char *from_node = NULL;

	debug3("mpi/pmi2: in _handle_spawn_resp");
	
	rc = spawn_resp_unpack(&spawn_resp, buf);
	if (rc != SLURM_SUCCESS) {
		error("mpi/pmi2: failed to unpack spawn response tree cmd");
		return SLURM_ERROR;
	}

	rc = spawn_psr_dequeue(spawn_resp->seq, &task_fd, &task_lrank, &from_node);
	if (rc != SLURM_SUCCESS) {
		error("mpi/pmi2: spawn response not matched in psr list");
		return SLURM_ERROR;
	}

	if (from_node == NULL) { /* stepd */
		debug3("mpi/pmi2: spawned tasks of %s launched",
		       spawn_resp->jobid);
		if (is_pmi20()) {
			_send_task_spawn_resp_pmi20(spawn_resp, task_fd, task_lrank);
		} else if (is_pmi11()) {
			_send_task_spawn_resp_pmi11(spawn_resp, task_fd, task_lrank);
		}
	} else {		/* srun */
		debug3("mpi/pmi2: spawned tasks of %s launched",
		       spawn_resp->jobid);
		/* forward resp to stepd */
		spawn_resp_send_to_stepd(spawn_resp, from_node);
		xfree(from_node);
	}
	spawn_resp_free(spawn_resp);
	
	return rc;
}

/**************************************************************/
extern int
handle_tree_cmd(int fd)
{
	char *req_buf = NULL;
	uint32_t len;
	Buf buf = NULL;
	uint16_t cmd;
	int rc;

	debug3("mpi/pmi2: in handle_tree_cmd");

	safe_read(fd, &len, sizeof(uint32_t));
	len = ntohl(len);

	safe_read(fd, &cmd, sizeof(uint16_t));
	cmd = ntohs(cmd);
	if (cmd >= TREE_CMD_COUNT) {
		error("mpi/pmi2: invalid tree req command");
		return SLURM_ERROR;
	}

	len -= sizeof(cmd);
	req_buf = xmalloc(len + 1);
	safe_read(fd, req_buf, len);
	buf = create_buf(req_buf, len); /* req_buf taken by buf */

	debug3("mpi/pmi2: got tree cmd: %hu(%s)", cmd, tree_cmd_names[cmd]);
	rc = tree_cmd_handlers[cmd](fd, buf);
	free_buf (buf);
	debug3("mpi/pmi2: out handle_tree_cmd");
	return rc;
	
rwfail:
	xfree(req_buf);
	return SLURM_ERROR;
}

extern int
tree_msg_to_srun(uint32_t len, char *msg)
{
	int fd, rc;
	
	fd = _slurm_open_stream(tree_info.srun_addr, true);
	rc = _slurm_msg_sendto(fd, msg, len, SLURM_PROTOCOL_NO_SEND_RECV_FLAGS);
	close(fd);
	return rc;
}

extern int
tree_msg_to_srun_with_resp(uint32_t len, char *msg, Buf *resp_ptr)
{
	int fd, rc;
	Buf buf = NULL;
	char *data = NULL;

	xassert(resp_ptr != NULL);

	fd = _slurm_open_stream(tree_info.srun_addr, true);
	rc = _slurm_msg_sendto(fd, msg, len, SLURM_PROTOCOL_NO_SEND_RECV_FLAGS);
	if (rc == len) { 	/* all data sent */
		safe_read(fd, &len, sizeof(len));
		len = ntohl(len);
		data = xmalloc(len);
		safe_read(fd, data, len);
		buf = create_buf(data, len);
		*resp_ptr = buf;
		rc = SLURM_SUCCESS;
	} else {
		rc = SLURM_ERROR;
	}
	close(fd);
	return rc;
	
rwfail:
	close (fd);
	xfree(data);
	return SLURM_ERROR;
}

extern int
tree_msg_to_stepds(char *nodelist, uint32_t len, char *msg)
{
	int rc;
	rc = slurm_forward_data(nodelist,
				tree_sock_addr,
				len,
				msg);
	return rc;
}

