/*****************************************************************************\
 **  tree.c - PMI tree communication handling code
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
 *  Portions copyright (C) 2014 Institute of Semiconductor Physics
 *                     Siberian Branch of Russian Academy of Science
 *  Written by Artem Y. Polyakov <artpol84@gmail.com>.
 *  All rights reserved.
 *  Portions copyright (C) 2015 Mellanox Technologies Inc.
 *  Written by Artem Y. Polyakov <artemp@mellanox.com>.
 *  All rights reserved.
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
#include <stdlib.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#include "kvs.h"
#include "spawn.h"
#include "client.h"
#include "setup.h"
#include "pmi.h"
#include "nameserv.h"
#include "ring.h"

static int _handle_kvs_fence(int fd, Buf buf);
static int _handle_kvs_fence_resp(int fd, Buf buf);
static int _handle_spawn(int fd, Buf buf);
static int _handle_spawn_resp(int fd, Buf buf);
static int _handle_name_publish(int fd, Buf buf);
static int _handle_name_unpublish(int fd, Buf buf);
static int _handle_name_lookup(int fd, Buf buf);
static int _handle_ring(int fd, Buf buf);
static int _handle_ring_resp(int fd, Buf buf);

static uint32_t  spawned_srun_ports_size = 0;
static uint16_t *spawned_srun_ports = NULL;


static int (*tree_cmd_handlers[]) (int fd, Buf buf) = {
	_handle_kvs_fence,
	_handle_kvs_fence_resp,
	_handle_spawn,
	_handle_spawn_resp,
	_handle_name_publish,
	_handle_name_unpublish,
	_handle_name_lookup,
	_handle_ring,
	_handle_ring_resp,
	NULL
};

static char *tree_cmd_names[] = {
	"TREE_CMD_KVS_FENCE",
	"TREE_CMD_KVS_FENCE_RESP",
	"TREE_CMD_SPAWN",
	"TREE_CMD_SPAWN_RESP",
	"TREE_CMD_NAME_PUBLISH",
	"TREE_CMD_NAME_UNPUBLISH",
	"TREE_CMD_NAME_LOOKUP",
	"TREE_CMD_RING",
	"TREE_CMD_RING_RESP",
	NULL,
};

static int
_handle_kvs_fence(int fd, Buf buf)
{
	uint32_t from_nodeid, num_children, temp32, seq;
	char *from_node = NULL;
	int rc = SLURM_SUCCESS;

	safe_unpack32(&from_nodeid, buf);
	safe_unpackstr_xmalloc(&from_node, &temp32, buf);
	safe_unpack32(&num_children, buf);
	safe_unpack32(&seq, buf);

	debug3("mpi/pmi2: in _handle_kvs_fence, from node %u(%s) representing"
	       " %u offspring, seq=%u", from_nodeid, from_node, num_children,
	       seq);
	if (seq != kvs_seq) {
		error("mpi/pmi2: invalid kvs seq from node %u(%s) ignored, "
		      "expect %u got %u",
		      from_nodeid, from_node, kvs_seq, seq);
		goto out;
	}
	if (seq == tree_info.children_kvs_seq[from_nodeid]) {
		info("mpi/pmi2: duplicate KVS_FENCE request from node %u(%s) "
		      "ignored, seq=%u", from_nodeid, from_node, seq);
		goto out;
	}
	tree_info.children_kvs_seq[from_nodeid] = seq;

	if (tasks_to_wait == 0 && children_to_wait == 0) {
		tasks_to_wait = job_info.ltasks;
		children_to_wait = tree_info.num_children;
	}
	children_to_wait -= num_children;

	temp_kvs_merge(buf);

	if ((children_to_wait == 0) && (tasks_to_wait == 0)) {
		rc = temp_kvs_send();
		if (rc != SLURM_SUCCESS) {
			if (in_stepd()) {
				error("mpi/pmi2: failed to send temp kvs"
				      " to %s",
				      tree_info.parent_node ?: "srun");
				send_kvs_fence_resp_to_clients(
					rc,
					"mpi/pmi2: failed to send temp kvs");
			} else {
				error("mpi/pmi2: failed to send temp kvs"
				      " to compute nodes");
			}
			/* cancel the step to avoid tasks hang */
			slurm_kill_job_step(job_info.jobid, job_info.stepid,
					    SIGKILL);
		} else {
			if (in_stepd())
				waiting_kvs_resp = 1;
		}
	}
	debug3("mpi/pmi2: out _handle_kvs_fence, tasks_to_wait=%d, "
	       "children_to_wait=%d", tasks_to_wait, children_to_wait);
out:
	xfree(from_node);
	return rc;

unpack_error:
	error("mpi/pmi2: failed to unpack kvs fence message");
	rc = SLURM_ERROR;
	goto out;
}

static int
_handle_kvs_fence_resp(int fd, Buf buf)
{
	char *key, *val, *errmsg = NULL;
	int rc = SLURM_SUCCESS;
	uint32_t temp32, seq;

	debug3("mpi/pmi2: in _handle_kvs_fence_resp");

	safe_unpack32(&seq, buf);
	if (seq == kvs_seq - 2) {
		debug("mpi/pmi2: duplicate KVS_FENCE_RESP "
		      "seq %d kvs_seq %d from srun ignored", seq, kvs_seq);
		return rc;
	} else if (seq != kvs_seq - 1) {
		error("mpi/pmi2: invalid kvs seq from srun, expect %u got %u",
		      kvs_seq - 1, seq);
		rc = SLURM_ERROR;;
		errmsg = "mpi/pmi2: invalid kvs seq from srun";
		goto resp;
	}
	if (! waiting_kvs_resp) {
		debug("mpi/pmi2: duplicate KVS_FENCE_RESP from srun ignored");
		return rc;
	} else {
		waiting_kvs_resp = 0;
	}

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
	send_kvs_fence_resp_to_clients(rc, errmsg);
	if (rc != SLURM_SUCCESS) {
		slurm_kill_job_step(job_info.jobid, job_info.stepid, SIGKILL);
	}
	return rc;

unpack_error:
	error("mpi/pmi2: unpack kvs error in fence resp");
	rc = SLURM_ERROR;
	errmsg = "mpi/pmi2: unpack kvs error in fence resp";
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
		spawned_srun_ports = xrealloc(spawned_srun_ports,
					      spawn_resp->seq *
					      sizeof(uint16_t));
		spawned_srun_ports_size = spawn_resp->seq; /* seq start from 1 */
		spawned_srun_ports[spawn_resp->seq - 1] = spawn_resp->pmi_port;
		/* forward resp to stepd */
		spawn_resp_send_to_stepd(spawn_resp, from_node);
		xfree(from_node);
	}
	spawn_resp_free(spawn_resp);

	return rc;
}


/* name serv handlers called only in srun */
static int
_handle_name_publish(int fd, Buf buf)
{
	int rc;
	uint32_t tmp32;
	char *name = NULL, *port = NULL;
	Buf resp_buf = NULL;

	debug3("mpi/pmi2: in _handle_name_publish");

	safe_unpackstr_xmalloc(&name, &tmp32, buf);
	safe_unpackstr_xmalloc(&port, &tmp32, buf);

	if (tree_info.srun_addr)
		rc = name_publish_up(name, port);
	else
		rc = name_publish_local(name, port);
out:
	xfree(name);
	xfree(port);
	resp_buf = init_buf(32);
	pack32((uint32_t) rc, resp_buf);
	rc = slurm_msg_sendto(fd, get_buf_data(resp_buf),
			      get_buf_offset(resp_buf));
	free_buf(resp_buf);

	debug3("mpi/pmi2: out _handle_name_publish");
	return rc;

unpack_error:
	rc = SLURM_ERROR;
	goto out;
}

static int
_handle_name_unpublish(int fd, Buf buf)
{
	int rc;
	uint32_t tmp32;
	char *name = NULL;
	Buf resp_buf = NULL;

	debug3("mpi/pmi2: in _handle_name_unpublish");

	safe_unpackstr_xmalloc(&name, &tmp32, buf);

	if (tree_info.srun_addr)
		rc = name_unpublish_up(name);
	else
		rc = name_unpublish_local(name);
out:
	xfree(name);
	resp_buf = init_buf(32);
	pack32((uint32_t) rc, resp_buf);
	rc = slurm_msg_sendto(fd, get_buf_data(resp_buf),
			      get_buf_offset(resp_buf));
	free_buf(resp_buf);

	debug3("mpi/pmi2: out _handle_name_unpublish");
	return rc;

unpack_error:
	rc = SLURM_ERROR;
	goto out;
}

static int
_handle_name_lookup(int fd, Buf buf)
{
	int rc = SLURM_SUCCESS, rc2;
	uint32_t tmp32;
	char *name = NULL, *port = NULL;
	Buf resp_buf = NULL;

	debug3("mpi/pmi2: in _handle_name_lookup");

	safe_unpackstr_xmalloc(&name, &tmp32, buf);

	if (tree_info.srun_addr)
		port = name_lookup_up(name);
	else
		port = name_lookup_local(name);
out:
	resp_buf = init_buf(1024);
	packstr(port, resp_buf);
	rc2 = slurm_msg_sendto(fd, get_buf_data(resp_buf),
			       get_buf_offset(resp_buf));
	rc = MAX(rc, rc2);
	free_buf(resp_buf);
	xfree(name);
	xfree(port);

	debug3("mpi/pmi2: out _handle_name_lookup");
	return rc;

unpack_error:
	rc = SLURM_ERROR;
	goto out;
}

/* handles ring_in message from one of our stepd children */
static int
_handle_ring(int fd, Buf buf)
{
	uint32_t rank, count, temp32;
	char *left  = NULL;
	char *right = NULL;
	int ring_id;
	int rc = SLURM_SUCCESS;

        debug3("mpi/pmi2: in _handle_ring");

	/* TODO: do we need ntoh translation? */

	/* data consists of:
         *   uint32_t rank  - tree rank of stepd process that sent message
         *   uint32_t count - ring in count value
         *   string   left  - ring in left value
         *   string   right - ring in right value */
	safe_unpack32(&rank,  buf);
	safe_unpack32(&count, buf);
	safe_unpackstr_xmalloc(&left,  &temp32, buf);
	safe_unpackstr_xmalloc(&right, &temp32, buf);

	/* lookup ring_id for this child */
	ring_id = pmix_ring_id_by_rank(rank);

	/* check that we got a valid child id */
	if (ring_id == -1) {
		error("mpi/pmi2: received ring_in message from unknown child %d", rank);
		rc = SLURM_ERROR;
		goto out;
	}

	/* execute ring in operation */
	rc = pmix_ring_in(ring_id, count, left, right);

out:
	/* free strings unpacked from message */
	xfree(left);
	xfree(right);
        debug3("mpi/pmi2: out _handle_ring");
	return rc;

unpack_error:
	error("mpi/pmi2: failed to unpack ring in message");
	rc = SLURM_ERROR;
	goto out;
}

/* handles ring_out messages coming in from parent in stepd tree */
static int
_handle_ring_resp(int fd, Buf buf)
{
	uint32_t count, temp32;
	char *left  = NULL;
	char *right = NULL;
	int rc = SLURM_SUCCESS;

        debug3("mpi/pmi2: in _handle_ring_resp");

	/* TODO: need ntoh translation? */
	/* data consists of:
         *   uint32_t count - ring out count value
         *   string   left  - ring out left value
         *   string   right - ring out right value */
	safe_unpack32(&count, buf);
	safe_unpackstr_xmalloc(&left,  &temp32, buf);
	safe_unpackstr_xmalloc(&right, &temp32, buf);

	/* execute ring out operation */
	rc = pmix_ring_out(count, left, right);

out:
	/* free strings unpacked from message */
	xfree(left);
	xfree(right);
        debug3("mpi/pmi2: out _handle_ring_resp");
	return rc;

unpack_error:
	error("mpi/pmi2: failed to unpack ring out message");
	rc = SLURM_ERROR;
	goto out;
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

	fd = slurm_open_stream(tree_info.srun_addr, true);
	if (fd < 0)
		return SLURM_ERROR;
	rc = slurm_msg_sendto(fd, msg, len);
	if (rc == len) /* all data sent */
		rc = SLURM_SUCCESS;
	else
		rc = SLURM_ERROR;
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

	fd = slurm_open_stream(tree_info.srun_addr, true);
	if (fd < 0)
		return SLURM_ERROR;
	rc = slurm_msg_sendto(fd, msg, len);
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
tree_msg_to_spawned_sruns(uint32_t len, char *msg)
{
	int i = 0, rc = SLURM_SUCCESS, fd = -1, sent=0;
	slurm_addr_t srun_addr;

	for (i = 0; i < spawned_srun_ports_size; i ++) {
		if (spawned_srun_ports[i] == 0)
			continue;

		slurm_set_addr(&srun_addr, spawned_srun_ports[i], "127.0.0.1");
		fd = slurm_open_stream(&srun_addr, true);
		if (fd < 0)
			return SLURM_ERROR;
		sent = slurm_msg_sendto(fd, msg, len);
		if (sent != len)
			rc = SLURM_ERROR;
		close(fd);
	}
	return rc;
}
