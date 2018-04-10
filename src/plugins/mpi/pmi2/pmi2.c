/*****************************************************************************\
 **  pmi2.c - PMI2 client(task) command handling
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
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

#if defined(__FreeBSD__)
#include <sys/socket.h> /* AF_INET */
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include "src/common/slurm_xlator.h"
#include "src/common/log.h"

#include "pmi.h"
#include "client.h"
#include "spawn.h"
#include "kvs.h"
#include "info.h"
#include "setup.h"
#include "agent.h"
#include "nameserv.h"
#include "ring.h"

/* PMI2 command handlers */
static int _handle_fullinit(int fd, int lrank, client_req_t *req);
static int _handle_finalize(int fd, int lrank, client_req_t *req);
static int _handle_abort(int fd, int lrank, client_req_t *req);
static int _handle_job_getid(int fd, int lrank, client_req_t *req);
static int _handle_job_connect(int fd, int lrank, client_req_t *req);
static int _handle_job_disconnect(int fd, int lrank, client_req_t *req);
static int _handle_ring(int fd, int lrank, client_req_t *req);
static int _handle_kvs_put(int fd, int lrank, client_req_t *req);
static int _handle_kvs_fence(int fd, int lrank, client_req_t *req);
static int _handle_kvs_get(int fd, int lrank, client_req_t *req);
static int _handle_info_getnodeattr(int fd, int lrank, client_req_t *req);
static int _handle_info_putnodeattr(int fd, int lrank, client_req_t *req);
static int _handle_info_getjobattr(int fd, int lrank, client_req_t *req);
static int _handle_name_publish(int fd, int lrank, client_req_t *req);
static int _handle_name_unpublish(int fd, int lrank, client_req_t *req);
static int _handle_name_lookup(int fd, int lrank, client_req_t *req);
static int _handle_spawn(int fd, int lrank, client_req_t *req);


static struct {
	char *cmd;
	int (*handler)(int fd, int lrank, client_req_t *req);
} pmi2_cmd_handlers[] = {
	{ FULLINIT_CMD,          _handle_fullinit },
	{ FINALIZE_CMD,          _handle_finalize },
	{ ABORT_CMD,             _handle_abort },
	{ JOBGETID_CMD,          _handle_job_getid },
	{ JOBCONNECT_CMD,        _handle_job_connect },
	{ JOBDISCONNECT_CMD,     _handle_job_disconnect },
	{ RING_CMD,              _handle_ring },
	{ KVSPUT_CMD,            _handle_kvs_put },
	{ KVSFENCE_CMD,          _handle_kvs_fence },
	{ KVSGET_CMD,            _handle_kvs_get },
	{ GETNODEATTR_CMD,       _handle_info_getnodeattr },
	{ PUTNODEATTR_CMD,       _handle_info_putnodeattr },
	{ GETJOBATTR_CMD,        _handle_info_getjobattr },
	{ NAMEPUBLISH_CMD,       _handle_name_publish },
	{ NAMEUNPUBLISH_CMD,     _handle_name_unpublish },
	{ NAMELOOKUP_CMD,        _handle_name_lookup },
	{ SPAWN_CMD,             _handle_spawn },
	{ NULL, NULL},
};

static int
_handle_fullinit(int fd, int lrank, client_req_t *req)
{
	int pmi_jobid, pmi_rank;
	bool threaded;
	int found, rc = PMI2_SUCCESS;
	client_resp_t *resp;

	debug3("mpi/pmi2: _handle_fullinit");

	client_req_parse_body(req);

	found = client_req_get_int(req, PMIJOBID_KEY, &pmi_jobid);
	if (! found) {
		error(PMIJOBID_KEY" missing in fullinit command");
		rc = PMI2_ERR_INVALID_ARG;
		goto response;
	}
	found = client_req_get_int(req, PMIRANK_KEY, &pmi_rank);
	if (! found) {
		error(PMIRANK_KEY" missing in fullinit command");
		rc = PMI2_ERR_INVALID_ARG;
		goto response;
	}
	found = client_req_get_bool(req, THREADED_KEY, &threaded);
	if (! found) {
		error(THREADED_KEY" missing in fullinit command");
		rc = PMI2_ERR_INVALID_ARG;
		goto response;
	}

	/* TODO: use threaded */

response:
	resp = client_resp_new();
	/* what's the difference between DEBUGGED and VERBOSE? */
	/* TODO: APPNUM */
	client_resp_append(resp, CMD_KEY"="FULLINITRESP_CMD";" RC_KEY"=%d;"
			   PMIVERSION_KEY"=%d;" PMISUBVER_KEY"=%d;"
			   RANK_KEY"=%d;" SIZE_KEY"=%d;"
			   APPNUM_KEY"=-1;" DEBUGGED_KEY"="FALSE_VAL";"
			   PMIVERBOSE_KEY"=%s;",
			   rc,
			   PMI20_VERSION, PMI20_SUBVERSION,
			   job_info.gtids[lrank], job_info.ntasks,
			   (job_info.pmi_debugged ? TRUE_VAL : FALSE_VAL));
	if (job_info.spawner_jobid) {
		client_resp_append(resp, SPAWNERJOBID_KEY"=%s;",
				   job_info.spawner_jobid);
	}
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: fullinit done");
	return rc;
}

static int
_handle_finalize(int fd, int lrank, client_req_t *req)
{
	client_resp_t *resp;
	int rc = 0;

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="FINALIZERESP_CMD";"
			   RC_KEY"=%d;", rc);
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);
	/* shutdown the PMI fd */
	shutdown(fd, SHUT_RDWR);
	close(fd);
	task_finalize(lrank);
	return rc;
}

static int
_handle_abort(int fd, int lrank, client_req_t *req)
{
	int rc = SLURM_SUCCESS;
	bool is_world = false;

	debug3("mpi/pmi2: in _handle_abort");
	client_req_parse_body(req);
	client_req_get_bool(req, ISWORLD_KEY, &is_world);
	/* no response needed. just cancel the job step if required */
	if (is_world) {
		slurm_kill_job_step(job_info.jobid, job_info.stepid, SIGKILL);
	}
	return rc;
}

static int
_handle_job_getid(int fd, int lrank, client_req_t *req)
{
	int rc = SLURM_SUCCESS;
	client_resp_t *resp;

	debug3("mpi/pmi2: in _handle_job_getid");
	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="JOBGETIDRESP_CMD";" RC_KEY"=0;"
			   JOBID_KEY"=%s;", job_info.pmi_jobid);
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);
	debug3("mpi/pmi2: out _handle_job_getid");
	return rc;
}

static int
_handle_job_connect(int fd, int lrank, client_req_t *req)
{
	int rc = SLURM_SUCCESS;
	error("mpi/pmi2: job connect not implemented for now");
	return rc;
}

static int
_handle_job_disconnect(int fd, int lrank, client_req_t *req)
{
	int rc = SLURM_SUCCESS;
	error("mpi/pmi2: job disconnect not implemented for now");
	return rc;
}

static int
_handle_ring(int fd, int lrank, client_req_t *req)
{
	int rc = SLURM_SUCCESS;
        int count   = 0;
	char *left  = NULL;
        char *right = NULL;

	debug3("mpi/pmi2: in _handle_ring");

	/* extract left, right, and count values from ring payload */
	client_req_parse_body(req);
	client_req_get_int(req, RING_COUNT_KEY, &count);
	client_req_get_str(req, RING_LEFT_KEY,  &left);
	client_req_get_str(req, RING_RIGHT_KEY, &right);

	/* compute ring_id, we list all application tasks first,
         * followed by stepds, so here we just use the application
         * process rank */
	int ring_id = lrank;

        rc = pmix_ring_in(ring_id, count, left, right);

	xfree(left);
	xfree(right);

        /* the repsonse is sent back to client from the pmix_ring_out call */

	debug3("mpi/pmi2: out _handle_ring");
	return rc;
}

static int
_handle_kvs_put(int fd, int lrank, client_req_t *req)
{
	int rc = SLURM_SUCCESS;
	client_resp_t *resp;
	char *key = NULL, *val = NULL;

	debug3("mpi/pmi2: in _handle_kvs_put");
	client_req_parse_body(req);
	client_req_get_str(req, KEY_KEY, &key);
	client_req_get_str(req, VALUE_KEY, &val);

	/* no need to add k-v to hash. just get it ready to be up-forward */
	rc = temp_kvs_add(key, val);
	xfree(key);
	xfree(val);

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="KVSPUTRESP_CMD";" RC_KEY"=%d;", rc);
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_kvs_put");
	return rc;
}

static int
_handle_kvs_fence(int fd, int lrank, client_req_t *req)
{
	int rc = 0;

	debug3("mpi/pmi2: in _handle_kvs_fence, from task %d",
	       job_info.gtids[lrank]);
	if (tasks_to_wait == 0 && children_to_wait == 0) {
		tasks_to_wait = job_info.ltasks;
		children_to_wait = tree_info.num_children;
	}
	tasks_to_wait --;

	/* mutex protection is not required */
	if (tasks_to_wait == 0 && children_to_wait == 0) {
		rc = temp_kvs_send();
		if (rc != SLURM_SUCCESS) {
			error("mpi/pmi2: failed to send temp kvs to %s",
			      tree_info.parent_node ?: "srun");
			send_kvs_fence_resp_to_clients(
				rc,
				"mpi/pmi2: failed to send temp kvs");
			/* cancel the step to avoid tasks hang */
			slurm_kill_job_step(job_info.jobid, job_info.stepid,
					    SIGKILL);
		} else {
			waiting_kvs_resp = 1;
		}
	}
	debug3("mpi/pmi2: out _handle_kvs_fence, tasks_to_wait=%d, "
	       "children_to_wait=%d", tasks_to_wait, children_to_wait);
	return rc;
}


static int
_handle_kvs_get(int fd, int lrank, client_req_t *req)
{
	int rc;
	client_resp_t *resp;
	char *key = NULL, *val;

	debug3("mpi/pmi2: in _handle_kvs_get");

	client_req_parse_body(req);
	client_req_get_str(req, KEY_KEY, &key);

	val = kvs_get(key);
	xfree(key);

	resp = client_resp_new();
	if (val != NULL) {
		client_resp_append(resp, CMD_KEY"="KVSGETRESP_CMD";"
				   RC_KEY"=0;" FOUND_KEY"="TRUE_VAL";"
				   VALUE_KEY"=%s;", val);
	} else {
		client_resp_append(resp, CMD_KEY"="KVSGETRESP_CMD";"
				   RC_KEY"=0;" FOUND_KEY"="FALSE_VAL";");
	}
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_kvs_get");
	return rc;
}

static int
_handle_info_getnodeattr(int fd, int lrank, client_req_t *req)
{
	int rc = 0;
	client_resp_t *resp;
	char *key = NULL, *val;
	bool wait = false;

	debug3("mpi/pmi2: in _handle_info_getnodeattr from lrank %d", lrank);

	client_req_parse_body(req);
	client_req_get_str(req, KEY_KEY, &key);
	client_req_get_bool(req, WAIT_KEY, &wait);

	val = node_attr_get(key);

	if (val != NULL || (! wait)) {
		resp = client_resp_new();
		client_resp_append(resp, CMD_KEY"="GETNODEATTRRESP_CMD";"
				RC_KEY"=0;" );
		if (val == NULL) {
			client_resp_append(resp, FOUND_KEY"="FALSE_VAL";" );
		} else  {
			client_resp_append(resp, FOUND_KEY"="TRUE_VAL";"
					   VALUE_KEY"=%s;", val);
		}
		rc = client_resp_send(resp, fd);
		client_resp_free(resp);
	} else {
		rc = enqueue_nag_req(fd, lrank, key);
	}
	xfree(key);
	debug3("mpi/pmi2: out _handle_info_getnodeattr");
	return rc;
}

static int
_handle_info_putnodeattr(int fd, int lrank, client_req_t *req)
{
	char *key, *val;
	client_resp_t *resp;
	int rc = 0;

	debug3("mpi/pmi2: in _handle_info_putnodeattr");

	client_req_parse_body(req);
	client_req_get_str(req, KEY_KEY, &key);
	client_req_get_str(req, VALUE_KEY, &val);

	rc = node_attr_put(key, val);

	xfree(key);
	xfree(val);

	resp = client_resp_new();
	client_resp_append(resp,
			   CMD_KEY"="PUTNODEATTRRESP_CMD";" RC_KEY"=%d;", rc);
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_info_putnodeattr");
	return rc;
}

static int
_handle_info_getjobattr(int fd, int lrank, client_req_t *req)
{
	char *key = NULL, *val;
	client_resp_t *resp;
	int rc;

	debug3("mpi/pmi2: in _handle_info_getjobattr");
	client_req_parse_body(req);
	client_req_get_str(req, KEY_KEY, &key);

	val = job_attr_get(key);
	xfree(key);

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="GETJOBATTRRESP_CMD";" RC_KEY"=0;");
	if (val != NULL) {
		client_resp_append(resp,
				   FOUND_KEY"="TRUE_VAL";" VALUE_KEY"=%s;",
				   val);
	} else {
		client_resp_append(resp, FOUND_KEY"="FALSE_VAL";");
	}

	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_info_getjobattr");
	return rc;
}

static int
_handle_name_publish(int fd, int lrank, client_req_t *req)
{
	int rc;
	client_resp_t *resp;
	char *name = NULL, *port = NULL;

	debug3("mpi/pmi2: in _handle_publish_name");

	client_req_parse_body(req);
	client_req_get_str(req, NAME_KEY, &name);
	client_req_get_str(req, PORT_KEY, &port);

	rc = name_publish_up(name, port);
	xfree(name);
	xfree(port);

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="NAMEPUBLISHRESP_CMD";"
			   RC_KEY"=%d;", rc);
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_publish_name");
	return rc;
}

static int
_handle_name_unpublish(int fd, int lrank, client_req_t *req)
{
	int rc;
	client_resp_t *resp;
	char *name = NULL;

	debug3("mpi/pmi2: in _handle_unpublish_name");

	client_req_parse_body(req);
	client_req_get_str(req, NAME_KEY, &name);

	rc = name_unpublish_up(name);
	xfree(name);

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="NAMEUNPUBLISHRESP_CMD";"
			   RC_KEY"=%d;", rc);
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_unpublish_name");
	return rc;
}

static int
_handle_name_lookup(int fd, int lrank, client_req_t *req)
{
	int rc;
	client_resp_t *resp;
	char *name = NULL, *port = NULL;

	debug3("mpi/pmi2: in _handle_lookup_name");

	client_req_parse_body(req);
	client_req_get_str(req, NAME_KEY, &name);

	port = name_lookup_up(name);

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="NAMELOOKUPRESP_CMD";");
	if (port == NULL) {
		client_resp_append(resp, RC_KEY"=1;");
	} else {
		client_resp_append(resp, RC_KEY"=0;"VALUE_KEY"=%s;",
				   port);
	}
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	xfree(name);
	xfree(port);

	debug3("mpi/pmi2: out _handle_lookup_name");
	return rc;
}

static int
_handle_spawn(int fd, int lrank, client_req_t *req)
{
	int rc;
	spawn_req_t *spawn_req = NULL;
	spawn_resp_t *spawn_resp = NULL;
	client_resp_t *task_resp;

	debug3("mpi/pmi2: in _handle_spawn");

	client_req_parse_body(req);
	spawn_req = client_req_parse_spawn_req(req);
	if (spawn_req == NULL) {
		task_resp = client_resp_new();
		client_resp_append(task_resp, CMD_KEY"="SPAWNRESP_CMD";"
				   RC_KEY"=%d;"
				   ERRMSG_KEY"=invalid command;",
				   PMI2_ERR_INVALID_ARGS);
		client_resp_send(task_resp, fd);
		client_resp_free(task_resp);
		return SLURM_ERROR;
	}

	/* a resp will be send back from srun.
	 * this will not be forwarded to the tasks */
	rc = spawn_req_send_to_srun(spawn_req, &spawn_resp);
	if (spawn_resp->rc != SLURM_SUCCESS) {
		task_resp = client_resp_new();
		client_resp_append(task_resp, CMD_KEY"="SPAWNRESP_CMD";"
				   RC_KEY"=%d;"
				   ERRMSG_KEY"=spawn failed;",
				   spawn_resp->rc);
		client_resp_send(task_resp, fd);
		client_resp_free(task_resp);
		spawn_req_free(spawn_req);
		spawn_resp_free(spawn_resp);
		debug("mpi/pmi2: spawn failed");
		return SLURM_ERROR;
	}

	debug3("mpi/pmi2: spawn request sent to srun");
	spawn_psr_enqueue(spawn_resp->seq, fd, lrank, NULL);

	spawn_req_free(spawn_req);
	spawn_resp_free(spawn_resp);
	debug3("mpi/pmi2: out _handle_spawn");
	return rc;
}

/**************************************************/

extern int
handle_pmi2_cmd(int fd, int lrank)
{
	int i, len;
	char len_buf[7], *buf = NULL;
	client_req_t *req = NULL;
	int rc = SLURM_SUCCESS;

	debug3("mpi/pmi2: in handle_pmi2_cmd");

	safe_read(fd, len_buf, 6);
	len_buf[6] = '\0';
	len = atoi(len_buf);
	buf = xmalloc(len + 1);
	safe_read(fd, buf, len);
	buf[len] = '\0';

	debug2("mpi/pmi2: got client request: %s %s", len_buf, buf);

	if (!len) {
		/*
		 * This is an invalid request.
		 *
		 * The most likely cause of an invalid client request is a
		 * second PMI2_Init call from the client end. This arrives
		 * first as a "cmd=init" call. Ideally, we'd capture that
		 * request, and respond with "cmd=response_to_init" with the rc
		 * field set to PMI2_ERR_INIT and expect the client to cleanup
		 * and die correctly.
		 *
		 * However - Slurm's libpmi2 has historically ignored the rc
		 * value and immediately sends the FULLINIT_CMD regardless, and
		 * then waits for a response to that. Rather than construct
		 * two successive error messages, this call will send back
		 * "cmd=finalize-response" back that will trigger the desired
		 * error handling paths, and then tears down the connection
		 * for good measure.
		 */
		_handle_finalize(fd, 0, NULL);
		return SLURM_ERROR;
	}

	req = client_req_init(len, buf);
	if (req == NULL) {
		error("mpi/pmi2: invalid client request");
		return SLURM_ERROR;
	}

	i = 0;
	while (pmi2_cmd_handlers[i].cmd != NULL) {
		if (!xstrcmp(req->cmd, pmi2_cmd_handlers[i].cmd))
			break;
		i ++;
	}
	if (pmi2_cmd_handlers[i].cmd == NULL) {
		error("mpi/pmi2: invalid pmi2 command received: '%s'", req->cmd);
		rc = SLURM_ERROR;
	} else {
		rc = pmi2_cmd_handlers[i].handler(fd, lrank, req);
	}
	client_req_free(req);

	debug3("mpi/pmi2: out handle_pmi2_cmd");

	return rc;

rwfail:
	xfree(buf);
	return SLURM_ERROR;
}
