/*****************************************************************************\
 **  pmi1.c - PMI1 client(task) command handling
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

#include "config.h"

#if defined(__FreeBSD__)
#include <sys/socket.h> /* AF_INET */
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/xmalloc.h"
#include "src/common/log.h"

#include "pmi.h"
#include "client.h"
#include "spawn.h"
#include "setup.h"
#include "kvs.h"
#include "agent.h"
#include "nameserv.h"

/* client command handlers */
static int _handle_get_maxes(int fd, int lrank, client_req_t *req);
static int _handle_get_universe_size(int fd, int lrank, client_req_t *req);
static int _handle_get_appnum(int fd, int lrank, client_req_t *req);
static int _handle_barrier_in(int fd, int lrank, client_req_t *req);
static int _handle_finalize(int fd, int lrank, client_req_t *req);
static int _handle_abort(int fd, int lrank, client_req_t *req);
static int _handle_get_my_kvsname(int fd, int lrank, client_req_t *req);
static int _handle_create_kvs(int fd, int lrank, client_req_t *req);
static int _handle_destroy_kvs(int fd, int lrank, client_req_t *req);
static int _handle_put(int fd, int lrank, client_req_t *req);
static int _handle_get(int fd, int lrank, client_req_t *req);
static int _handle_getbyidx(int fd, int lrank ,client_req_t *req);
static int _handle_publish_name(int fd, int lrank, client_req_t *req);
static int _handle_unpublish_name(int fd, int lrank, client_req_t *req);
static int _handle_lookup_name(int fd, int lrank, client_req_t *req);
static int _handle_mcmd(int fd, int lrank, client_req_t *req);

static struct {
	char *cmd;
	int (*handler)(int fd, int lrank, client_req_t *req);
} pmi1_cmd_handlers[] = {
	{ GETMAXES_CMD,          _handle_get_maxes },
	{ GETUNIVSIZE_CMD,       _handle_get_universe_size },
	{ GETAPPNUM_CMD,         _handle_get_appnum },
	{ BARRIERIN_CMD,         _handle_barrier_in },
	{ FINALIZE_CMD,          _handle_finalize },
	{ ABORT_CMD,             _handle_abort },
	{ GETMYKVSNAME_CMD,      _handle_get_my_kvsname },
	{ CREATEKVS_CMD,         _handle_create_kvs },
	{ DESTROYKVS_CMD,        _handle_destroy_kvs },
	{ PUT_CMD,               _handle_put },
	{ GET_CMD,               _handle_get },
	{ GETBYIDX_CMD,          _handle_getbyidx },
	{ PUBLISHNAME_CMD,       _handle_publish_name },
	{ UNPUBLISHNAME_CMD,     _handle_unpublish_name },
	{ LOOKUPNAME_CMD,        _handle_lookup_name },
	{ MCMD_CMD,              _handle_mcmd },
	{ NULL, NULL},
};

static spawn_req_t *pmi1_spawn = NULL;

static int
_handle_get_maxes(int fd, int lrank, client_req_t *req)
{
	int rc = 0;
	client_resp_t *resp;

	debug3("mpi/pmi2: in _handle_get_maxes");

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="MAXES_CMD" " RC_KEY"=%d "
			   KVSNAMEMAX_KEY"=%d " KEYLENMAX_KEY"=%d "
			   VALLENMAX_KEY"=%d\n",
			   rc, MAXKVSNAME, MAXKEYLEN, MAXVALLEN);
	(void) client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_get_maxes");
	return SLURM_SUCCESS;
}

static int
_handle_get_universe_size(int fd, int lrank, client_req_t *req)
{
	int rc = 0;
	client_resp_t *resp;

	debug3("mpi/pmi2: in _handle_get_universe_size");

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="UNIVSIZE_CMD" " RC_KEY"=%d "
			   SIZE_KEY"=%d\n",
			   rc, job_info.ntasks);
	(void) client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_get_universe_size");
	return SLURM_SUCCESS;
}

static int
_handle_get_appnum(int fd, int lrank, client_req_t *req)
{
	int rc = 0;
	client_resp_t *resp;

	debug3("mpi/pmi2: in _handle_get_appnum");

	resp = client_resp_new();
	/*
	 * TODO: spawn_multiple: order number of command
	 *       spawn: 0
	 *       otherwise: -1, since no way to get the order
	 *         number from multi-prog conf
	 */
	client_resp_append(resp, CMD_KEY"="APPNUM_CMD" " RC_KEY"=%d "
			   APPNUM_KEY"=-1\n", rc);
	(void) client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_get_appnum");
	return SLURM_SUCCESS;
}

static int
_handle_barrier_in(int fd, int lrank, client_req_t *req)
{
	int rc = 0;

	debug3("mpi/pmi2: in _handle_barrier_in, from task %d",
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
	debug3("mpi/pmi2: out _handle_barrier_in, tasks_to_wait=%d, "
	       "children_to_wait=%d", tasks_to_wait, children_to_wait);
	return rc;
}

static int
_handle_finalize(int fd, int lrank, client_req_t *req)
{
	client_resp_t *resp;
	int rc = 0;

	debug3("mpi/pmi2: in _handle_finalize");
	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="FINALIZEACK_CMD" "
			RC_KEY"=%d\n", rc);
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);
	debug3("mpi/pmi2: out _handle_finalize");
	/* shutdown the PMI fd */
	shutdown(fd, SHUT_RDWR);
	close(fd);
	task_finalize(lrank);
	return rc;
}

static int
_handle_abort(int fd, int lrank, client_req_t *req)
{
	debug3("mpi/pmi2: in _handle_abort");
	/* no response needed. just cancel the job */
	slurm_kill_job_step(job_info.jobid, job_info.stepid, SIGKILL);
	debug3("mpi/pmi2: out _handle_abort");
	return SLURM_SUCCESS;
}

static int
_handle_get_my_kvsname(int fd, int lrank, client_req_t *req)
{
	client_resp_t *resp;
	int rc = 0;

	debug3("mpi/pmi2: in _handle_get_my_kvsname");
	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="GETMYKVSNAMERESP_CMD" "
			   RC_KEY"=%d " KVSNAME_KEY"=%u.%u\n",
			   rc, job_info.jobid, job_info.stepid);
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);
	debug3("mpi/pmi2: out _handle_get_my_kvsname");
	return rc;
}

static int
_handle_create_kvs(int fd, int lrank, client_req_t *req)
{
	/* not used in MPICH2 */
	error("mpi/pmi2: PMI1 request of '" CREATEKVS_CMD "' not supported");
	return SLURM_ERROR;
}

static int
_handle_destroy_kvs(int fd, int lrank, client_req_t *req)
{
	/* not used in MPICH2 */
	error("mpi/pmi2: PMI1 request of '" DESTROYKVS_CMD "' not supported");
	return SLURM_ERROR;
}


static int
_handle_put(int fd, int lrank, client_req_t *req)
{
	int rc = SLURM_SUCCESS;
	client_resp_t *resp;
	char *kvsname = NULL, *key = NULL, *val = NULL;

	debug3("mpi/pmi2: in _handle_put");

	client_req_parse_body(req);
	client_req_get_str(req, KVSNAME_KEY, &kvsname); /* not used */
	client_req_get_str(req, KEY_KEY, &key);
	client_req_get_str(req, VALUE_KEY, &val);
	xfree(kvsname);
	
	/* no need to add k-v to hash. just get it ready to be up-forward */
	rc = temp_kvs_add(key, val);
	xfree(key);
	xfree(val);
	if (rc == SLURM_SUCCESS)
		rc = 0;
	else
		rc = 1;

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="PUTRESULT_CMD" " RC_KEY"=%d\n", rc);
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_put");
	return rc;
}

static int
_handle_get(int fd, int lrank, client_req_t *req)
{
	int rc;
	client_resp_t *resp;
	char *kvsname = NULL, *key = NULL, *val = NULL;

	debug3("mpi/pmi2: in _handle_get");

	client_req_parse_body(req);
	client_req_get_str(req, KVSNAME_KEY, &kvsname); /* not used */
	client_req_get_str(req, KEY_KEY, &key);
	xfree(kvsname);
	
	val = kvs_get(key);
	xfree(key);
	
	resp = client_resp_new();
	if (val != NULL) {
		client_resp_append(resp, CMD_KEY"="GETRESULT_CMD" "
				   RC_KEY"=0 " VALUE_KEY"=%s\n", val);
	} else {
		client_resp_append(resp, CMD_KEY"="GETRESULT_CMD" "
				   RC_KEY"=1\n");
	}
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_get");
	return rc;
}


static int
_handle_getbyidx(int fd, int lrank, client_req_t *req)
{
	/* not used in MPICH2 */
	error("mpi/pmi2: PMI1 request of '" GETBYIDX_CMD "' not supported");
	
	return SLURM_ERROR;
}

static int
_handle_publish_name(int fd, int lrank, client_req_t *req)
{
	int rc;
	client_resp_t *resp;
	char *service = NULL, *port = NULL;

	debug3("mpi/pmi2: in _handle_publish_name");

	client_req_parse_body(req);
	client_req_get_str(req, SERVICE_KEY, &service);
	client_req_get_str(req, PORT_KEY, &port);
	
	rc = name_publish_up(service, port);
	xfree(service);
	xfree(port);

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="PUBLISHRESULT_CMD" "
			   INFO_KEY"=%s\n",
			   rc == SLURM_SUCCESS ? "ok" : "fail");
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_publish_name");
	return rc;
}

static int
_handle_unpublish_name(int fd, int lrank, client_req_t *req)
{
	int rc;
	client_resp_t *resp;
	char *service = NULL;

	debug3("mpi/pmi2: in _handle_unpublish_name");

	client_req_parse_body(req);
	client_req_get_str(req, SERVICE_KEY, &service);
	
	rc = name_unpublish_up(service);
	xfree(service);

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="UNPUBLISHRESULT_CMD" "
			   INFO_KEY"=%s\n",
			   rc == SLURM_SUCCESS ? "ok" : "fail");
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	debug3("mpi/pmi2: out _handle_unpublish_name");
	return rc;
}

/*
 * this design is not scalable: each task that calls MPI_Lookup_name()
 * will generate a RPC to srun.
 */
static int
_handle_lookup_name(int fd, int lrank, client_req_t *req)
{
	int rc;
	client_resp_t *resp;
	char *service = NULL, *port = NULL;

	debug3("mpi/pmi2: in _handle_lookup_name");

	client_req_parse_body(req);
	client_req_get_str(req, SERVICE_KEY, &service);

	port = name_lookup_up(service);

	resp = client_resp_new();
	client_resp_append(resp, CMD_KEY"="LOOKUPRESULT_CMD" ");
	if (port == NULL) {
		client_resp_append(resp, INFO_KEY"=fail\n");
	} else {
		client_resp_append(resp, INFO_KEY"=ok "PORT_KEY"=%s\n",
				   port);
	}
	rc = client_resp_send(resp, fd);
	client_resp_free(resp);

	xfree(service);
	xfree(port);

	debug3("mpi/pmi2: out _handle_lookup_name");
	return rc;
}

static int
_handle_mcmd(int fd, int lrank, client_req_t *req)
{
	spawn_subcmd_t *subcmd = NULL;
	spawn_resp_t *spawn_resp = NULL;
	client_resp_t *task_resp = NULL;
	int spawnssofar = 0, rc = SLURM_SUCCESS, i;
	char buf[64];

	debug3("mpi/pmi2: in _handle_mcmd");

	client_req_parse_body(req);
	subcmd = client_req_parse_spawn_subcmd(req);

	debug3("mpi/pmi2: got subcmd");

	client_req_get_int(req, SPAWNSSOFAR_KEY, &spawnssofar);
	if (spawnssofar == 1) {
		pmi1_spawn = spawn_req_new();
		client_req_get_int(req, TOTSPAWNS_KEY,
				   (int *)&pmi1_spawn->subcmd_cnt);
		pmi1_spawn->subcmds = xmalloc(pmi1_spawn->subcmd_cnt *
					      sizeof(spawn_subcmd_t *));
		client_req_get_int(req, PREPUTNUM_KEY,
				   (int *)&pmi1_spawn->preput_cnt);
		pmi1_spawn->pp_keys =
			xmalloc(pmi1_spawn->preput_cnt * sizeof(char *));
		pmi1_spawn->pp_vals =
			xmalloc(pmi1_spawn->preput_cnt * sizeof(char *));
		for (i = 0; i < pmi1_spawn->preput_cnt; i ++) {
			snprintf(buf, 64, PREPUTKEY_KEY"%d", i);
			client_req_get_str(req, buf, &pmi1_spawn->pp_keys[i]);
			snprintf(buf, 64, PREPUTVAL_KEY"%d", i);
			client_req_get_str(req, buf, &pmi1_spawn->pp_vals[i]);
		}
	}
	pmi1_spawn->subcmds[spawnssofar - 1] = subcmd;

	if (spawnssofar == pmi1_spawn->subcmd_cnt) {
		debug3("mpi/pmi2: got whole spawn req");
		/* a resp will be send back from srun.
		   this will not be forwarded to the tasks */
		rc = spawn_req_send_to_srun(pmi1_spawn, &spawn_resp);
		if (spawn_resp->rc != SLURM_SUCCESS) {
			task_resp = client_resp_new();
			client_resp_append(task_resp, CMD_KEY"="SPAWNRESP_CMD";"
					   RC_KEY"=%d;"
					   ERRMSG_KEY"=spawn failed;",
					   spawn_resp->rc);
			client_resp_send(task_resp, fd);
			client_resp_free(task_resp);

			spawn_resp_free(spawn_resp);
			spawn_req_free(pmi1_spawn);
			pmi1_spawn = NULL;
			error("mpi/pmi2: spawn failed");
			rc = SLURM_ERROR;
			goto out;
		}

		debug("mpi/pmi2: spawn request sent to srun");
		spawn_psr_enqueue(spawn_resp->seq, fd, lrank, NULL);

		spawn_resp_free(spawn_resp);
		spawn_req_free(pmi1_spawn);
		pmi1_spawn = NULL;
	}
out:
	debug3("mpi/pmi2: out _handle_mcmd");
	return rc;
}

/**************************************************/

/* from src/pmi/simple/simeple_pmiutil.c */
#define MAX_READLINE 1024

/* buf will be xfree-ed */
static int
_handle_pmi1_cmd_buf(int fd, int lrank, int buf_len, char *buf)
{
	client_req_t *req = NULL;
	int i = 0, rc;

	debug3("mpi/pmi2: got client request: %s", buf);

	/* buf taken by req */
	req = client_req_init(buf_len, buf);
	if (req == NULL) {
		error("mpi/pmi2: invalid client request");
		return SLURM_ERROR;
	}

	i = 0;
	while (pmi1_cmd_handlers[i].cmd != NULL) {
		if (!xstrcmp(req->cmd, pmi1_cmd_handlers[i].cmd))
			break;
		i ++;
	}
	if (pmi1_cmd_handlers[i].cmd == NULL) {
		error("mpi/pmi2: invalid pmi1 command received: '%s'", req->cmd);
		rc = SLURM_ERROR;
	} else {
		rc = pmi1_cmd_handlers[i].handler(fd, lrank, req);
	}
	client_req_free(req);	/* free buf */

	return rc;
}

/* *pbuf not xfree-ed */
static int
_handle_pmi1_mcmd_buf(int fd, int lrank, int buf_size, int buf_len, char **pbuf)
{
	int n, len, endcmd_len, not_end;
	char *cmd_buf = NULL, *tmp_buf = NULL, *tmp_ptr = NULL, *buf;
	int rc = SLURM_SUCCESS;

	/* read until "endcmd\n" */
	buf = *pbuf;
	n = buf_len;
	endcmd_len = strlen(ENDCMD_KEY"\n");
	not_end = xstrncmp(&buf[n - endcmd_len], ENDCMD_KEY"\n", endcmd_len);
	while(not_end) {
		if (n == buf_size) {
			buf_size += MAX_READLINE;
			xrealloc(buf, buf_size + 1);
			*pbuf = buf;
		}
		while((len = read(fd, &buf[n], buf_size - n)) < 0
		      && errno == EINTR );
		if (len < 0) {
			error("mpi/pmi2: failed to read PMI1 request");
			return SLURM_ERROR;
		} else if (len == 0) {
			debug("mpi/pmi2: read partial mcmd: %s", buf);
			usleep(100);
		} else {
			n += len;
			not_end = xstrncmp(&buf[n - endcmd_len],
					   ENDCMD_KEY"\n", endcmd_len);
		}
	}
	buf[n] = '\0';

	/* there maybe multiple subcmds in the buffer */
	tmp_buf = buf;
	tmp_ptr = NULL;
	while (tmp_buf[0] != '\0') {
		tmp_ptr = strstr(tmp_buf, ENDCMD_KEY"\n");
		if (tmp_ptr == NULL) {
			error("mpi/pmi2: this is impossible");
			rc = SLURM_ERROR;
			break;
		}
		*tmp_ptr = '\0';
		n = tmp_ptr - tmp_buf;
		cmd_buf = xstrdup(tmp_buf);
		rc = _handle_pmi1_cmd_buf(fd, lrank, n, cmd_buf);
		if (rc != SLURM_SUCCESS)
			break;
		tmp_buf = tmp_ptr + endcmd_len;
	}

	return rc;
}

extern int
handle_pmi1_cmd(int fd, int lrank)
{
	char *buf = NULL;
	int n, len, size, rc = SLURM_SUCCESS;

	debug3("mpi/pmi2: in handle_pmi1_cmd");

	/* TODO: read until newline */
	size = MAX_READLINE;
	buf = xmalloc(size + 1);
	while ( (n = read(fd, buf, size)) < 0 && errno == EINTR );
	if (n < 0) {
		error("mpi/pmi2: failed to read PMI1 request");
		xfree(buf);
		return SLURM_ERROR;
	} else if (n == 0) {
		error("mpi/pmi2: read length 0");
		xfree(buf);
		return SLURM_ERROR;
	}

	len = strlen(MCMD_KEY"=");
	if (! xstrncmp(buf, MCMD_KEY"=", len)) {
		rc = _handle_pmi1_mcmd_buf(fd, lrank, size, n, &buf);
		xfree(buf);
	} else {
		buf[n] = '\0';
		rc = _handle_pmi1_cmd_buf(fd, lrank, n, buf);
	}
	debug3("mpi/pmi2: out handle_pmi1_cmd");
	return rc;
}
