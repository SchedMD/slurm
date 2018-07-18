/*****************************************************************************\
 **  spawn.c - PMI job spawn handling
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "src/common/slurm_xlator.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/common/slurm_protocol_interface.h"

#include "spawn.h"
#include "setup.h"
#include "tree.h"
#include "pmi.h"

static uint32_t spawn_seq = 1;	/* 0 if not spawned */
static pid_t *spawned_srun_pids = NULL;

typedef struct pending_spawn_req {
	uint32_t seq;
	int fd;
	int lrank;
	char *from_node;	/* for srun */
	struct pending_spawn_req *next;
} psr_t;

static psr_t *psr_list = NULL;

extern spawn_subcmd_t *
spawn_subcmd_new(void)
{
	spawn_subcmd_t *subcmd;

	subcmd = xmalloc(sizeof(spawn_subcmd_t));
	return subcmd;
}

extern void
spawn_subcmd_free(spawn_subcmd_t *subcmd)
{
	int i;

	if (subcmd) {
		xfree(subcmd->cmd);
		if (subcmd->argv) {
			for (i = 0; i < subcmd->argc; i ++) {
				xfree(subcmd->argv[i]);
			}
			xfree(subcmd->argv);
		}
		if (subcmd->info_keys) {
			for (i = 0; i < subcmd->info_cnt; i ++) {
				xfree(subcmd->info_keys[i]);
			}
			xfree(subcmd->info_keys);
		}
		if (subcmd->info_vals) {
			for (i = 0; i < subcmd->info_cnt; i ++) {
				xfree(subcmd->info_vals[i]);
			}
			xfree(subcmd->info_vals);
		}
		xfree(subcmd);
	}
}

extern spawn_req_t *
spawn_req_new(void)
{
	spawn_req_t *req;

	req = xmalloc(sizeof(spawn_req_t));
	req->seq = 0;
	req->from_node = xstrdup(tree_info.this_node);
	return req;
}

extern void
spawn_req_free(spawn_req_t *req)
{
	int i;

	if (req) {
		xfree(req->from_node);
		if (req->pp_keys) {
			for (i = 0; i < req->preput_cnt; i ++) {
				xfree(req->pp_keys[i]);
			}
			xfree(req->pp_keys);
		}
		if (req->pp_vals) {
			for (i = 0; i < req->preput_cnt; i ++) {
				xfree(req->pp_vals[i]);
			}
			xfree(req->pp_vals);
		}
		if (req->subcmds) {
			for (i = 0; i < req->subcmd_cnt; i ++) {
				spawn_subcmd_free(req->subcmds[i]);
			}
			xfree(req->subcmds);
		}
		xfree(req);
	}
}

extern void
spawn_req_pack(spawn_req_t *req, Buf buf)
{
	int i, j;
	spawn_subcmd_t *subcmd;
	void *auth_cred;
	char *auth_info = slurm_get_auth_info();

	auth_cred = g_slurm_auth_create(auth_info);
	xfree(auth_info);
	if (auth_cred == NULL) {
		error("authentication: %s",
		      g_slurm_auth_errstr(g_slurm_auth_errno(NULL)) );
		return;
	}
	(void) g_slurm_auth_pack(auth_cred, buf);
	(void) g_slurm_auth_destroy(auth_cred);

	pack32(req->seq, buf);
	packstr(req->from_node, buf);
	pack32(req->subcmd_cnt, buf);
	pack32(req->preput_cnt, buf);
	for (i = 0; i < req->preput_cnt; i ++) {
		packstr(req->pp_keys[i], buf);
		packstr(req->pp_vals[i], buf);
	}
	for (i = 0; i < req->subcmd_cnt; i ++) {
		subcmd = req->subcmds[i];

		packstr(subcmd->cmd, buf);
		pack32(subcmd->max_procs, buf);
		pack32(subcmd->argc, buf);
		for (j = 0; j < subcmd->argc; j ++) {
			packstr(subcmd->argv[j], buf);
		}
		pack32(subcmd->info_cnt, buf);
		for (j = 0; j < subcmd->info_cnt; j ++) {
			packstr(subcmd->info_keys[j], buf);
			packstr(subcmd->info_vals[j], buf);
		}
	}
}

extern int
spawn_req_unpack(spawn_req_t **req_ptr, Buf buf)
{
	spawn_req_t *req = NULL;
	spawn_subcmd_t *subcmd = NULL;
	uint32_t temp32;
	int i, j;
	void *auth_cred;
	char *auth_info;
	uid_t auth_uid, my_uid;

	auth_cred = g_slurm_auth_unpack(buf);
	if (auth_cred == NULL) {
		error("authentication: %s",
		      g_slurm_auth_errstr(g_slurm_auth_errno(NULL)) );
		return SLURM_ERROR;
	}
	auth_info = slurm_get_auth_info();
	auth_uid = g_slurm_auth_get_uid(auth_cred, auth_info);
	xfree(auth_info);
	(void) g_slurm_auth_destroy(auth_cred);
	my_uid = getuid();
	if ((auth_uid != 0) && (auth_uid != my_uid)) {
		error("mpi/pmi2: spawn request apparently from uid %u",
		      (uint32_t) auth_uid);
		return SLURM_ERROR;
	}

	req = xmalloc(sizeof(spawn_req_t));

	safe_unpack32(&req->seq, buf);
	safe_unpackstr_xmalloc(&req->from_node, &temp32, buf);
	safe_unpack32(&req->subcmd_cnt, buf);
	/* subcmd_cnt must be greater than 0 */
	req->subcmds = xmalloc(req->subcmd_cnt * sizeof(spawn_subcmd_t *));
	safe_unpack32(&req->preput_cnt, buf);
	if (req->preput_cnt > 0) {
		req->pp_keys = xmalloc(req->preput_cnt * sizeof(char *));
		req->pp_vals = xmalloc(req->preput_cnt * sizeof(char *));
		for (i = 0; i < req->preput_cnt; i ++) {
			safe_unpackstr_xmalloc(&req->pp_keys[i], &temp32, buf);
			safe_unpackstr_xmalloc(&req->pp_vals[i], &temp32, buf);
		}
	}
	for (i = 0; i < req->subcmd_cnt; i ++) {
		req->subcmds[i] = spawn_subcmd_new();
		subcmd = req->subcmds[i];

		safe_unpackstr_xmalloc(&(subcmd->cmd), &temp32, buf);
		safe_unpack32(&(subcmd->max_procs), buf);
		safe_unpack32(&(subcmd->argc), buf);
		if (subcmd->argc > 0) {
			subcmd->argv = xmalloc(subcmd->argc * sizeof(char *));
			for (j = 0; j < subcmd->argc; j ++) {
				safe_unpackstr_xmalloc(&(subcmd->argv[j]),
						       &temp32, buf);
			}
		}
		safe_unpack32(&(subcmd->info_cnt), buf);
		if (subcmd->info_cnt > 0) {
			subcmd->info_keys = xmalloc(subcmd->info_cnt *
						    sizeof(char *));
			subcmd->info_vals = xmalloc(subcmd->info_cnt *
						    sizeof(char *));
			for (j = 0; j < subcmd->info_cnt; j ++) {
				safe_unpackstr_xmalloc(&(subcmd->info_keys[j]),
						       &temp32, buf);
				safe_unpackstr_xmalloc(&(subcmd->info_vals[j]),
						       &temp32, buf);
			}
		}
	}
	*req_ptr = req;
	return SLURM_SUCCESS;

unpack_error:
	spawn_req_free(req);
	return SLURM_ERROR;
}

extern int
spawn_req_send_to_srun(spawn_req_t *req, spawn_resp_t **resp_ptr)
{
	Buf req_buf = NULL, resp_buf = NULL;
	int rc;
	uint16_t cmd;

	req_buf = init_buf(2048);
	cmd = TREE_CMD_SPAWN;
	pack16(cmd, req_buf);
	spawn_req_pack(req, req_buf);
	rc = tree_msg_to_srun_with_resp(get_buf_offset(req_buf),
					get_buf_data(req_buf), &resp_buf);
	free_buf(req_buf);

	if (rc == SLURM_SUCCESS) {
		rc = spawn_resp_unpack(resp_ptr, resp_buf);
		free_buf(resp_buf);
	}
	return rc;
}
/**************************************************************/

extern spawn_resp_t *
spawn_resp_new(void)
{
	spawn_resp_t *resp;

	resp = xmalloc(sizeof(spawn_resp_t));
	return resp;
}

extern void
spawn_resp_free(spawn_resp_t *resp)
{
	if (resp) {
		xfree(resp->jobid);
		xfree(resp->error_codes);
		xfree(resp);
	}
}

extern void
spawn_resp_pack(spawn_resp_t *resp, Buf buf)
{
	int i;

	pack32(resp->seq, buf);
	pack32((uint32_t)resp->rc, buf);
	pack16((uint16_t)resp->pmi_port, buf);
	packstr(resp->jobid, buf);
	pack32(resp->error_cnt, buf);
	for (i = 0; i < resp->error_cnt; i ++) {
		pack32((uint32_t)resp->error_codes[i], buf);
	}
}

extern int
spawn_resp_unpack(spawn_resp_t **resp_ptr, Buf buf)
{
	spawn_resp_t *resp = NULL;
	uint32_t temp32;
	int i;

	resp = xmalloc(sizeof(spawn_resp_t));

	safe_unpack32(&resp->seq, buf);
	safe_unpack32((uint32_t *)&resp->rc, buf);
	safe_unpack16((uint16_t *)&resp->pmi_port, buf);
	safe_unpackstr_xmalloc(&resp->jobid, &temp32, buf);
	safe_unpack32(&resp->error_cnt, buf);
	if (resp->error_cnt > 0) {
		resp->error_codes = xmalloc(resp->error_cnt * sizeof(int));
		for (i = 0; i < resp->error_cnt; i ++) {
			safe_unpack32((uint32_t *)&(resp->error_codes[i]), buf);
		}
	}
	*resp_ptr = resp;
	return SLURM_SUCCESS;

unpack_error:
	spawn_resp_free(resp);
	return SLURM_ERROR;
}

extern int
spawn_resp_send_to_stepd(spawn_resp_t *resp, char *node)
{
	Buf buf;
	int rc;
	uint16_t cmd;

	buf = init_buf(1024);

	cmd = TREE_CMD_SPAWN_RESP;
	pack16(cmd, buf);
	spawn_resp_pack(resp, buf);

	rc = slurm_forward_data(&node, tree_sock_addr,
				get_buf_offset(buf),
				get_buf_data(buf));
	free_buf(buf);
	return rc;
}

extern int
spawn_resp_send_to_srun(spawn_resp_t *resp)
{
	Buf buf;
	int rc;
	uint16_t cmd;

	buf = init_buf(1024);

	cmd = TREE_CMD_SPAWN_RESP;
	pack16(cmd, buf);
	spawn_resp_pack(resp, buf);

	rc = tree_msg_to_srun(get_buf_offset(buf), get_buf_data(buf));
	free_buf(buf);
	return rc;
}

extern int
spawn_resp_send_to_fd(spawn_resp_t *resp, int fd)
{
	Buf buf;
	int rc;

	buf = init_buf(1024);

	/* sync with spawn_req_send_to_srun */
/* 	cmd = TREE_CMD_SPAWN_RESP; */
/* 	pack16(cmd, buf); */
	spawn_resp_pack(resp, buf);
	rc = slurm_msg_sendto(fd, get_buf_data(buf), get_buf_offset(buf));
	free_buf(buf);

	return rc;
}

/**************************************************************/

extern int
spawn_psr_enqueue(uint32_t seq, int fd, int lrank, char *from_node)
{
	psr_t *psr;

	psr = xmalloc(sizeof(psr_t));
	psr->seq = seq;
	psr->fd = fd;
	psr->lrank = lrank;
	psr->from_node = xstrdup(from_node);
	psr->next = psr_list;
	psr_list = psr;
	return SLURM_SUCCESS;
}

extern int
spawn_psr_dequeue(uint32_t seq, int *fd, int *lrank, char **from_node)
{
	psr_t *psr, **pprev;

	pprev = &psr_list;
	psr = *pprev;
	while(psr != NULL) {
		if (psr->seq != seq) {
			pprev = &(psr->next);
			psr = *pprev;
			continue;
		}
		/* found. remove the psr. */
		*fd = psr->fd;
		*lrank = psr->lrank;
		*from_node = psr->from_node; /* take over ownership */
		*pprev = psr->next;
		xfree(psr);
		return SLURM_SUCCESS;
	}
	return SLURM_ERROR;
}

extern uint32_t
spawn_seq_next(void)
{
	return spawn_seq ++;
}

static int
_exec_srun_single(spawn_req_t *req, char **env)
{
	int argc, i, j;
	char **argv = NULL;
	spawn_subcmd_t *subcmd;

	debug3("mpi/mpi2: in _exec_srun_single");
	subcmd = req->subcmds[0];
	argc = subcmd->argc + 7;
	xrealloc(argv, (argc + 1) * sizeof(char *));

	j = 0;
	argv[j ++] = "srun";
	argv[j ++] = "--mpi=pmi2";
	if (job_info.srun_opt && job_info.srun_opt->srun_opt->no_alloc) {
		argv[j ++] = "--no-alloc";
		xstrfmtcat(argv[j ++], "--nodelist=%s",
			   job_info.srun_opt->nodelist);
	}

	xstrfmtcat(argv[j ++], "--ntasks=%d", subcmd->max_procs);
	/* TODO: inherit options from srun_opt. */
	for (i = 0; i < subcmd->info_cnt; i ++) {
		if (0) {

		} else if (! xstrcmp(subcmd->info_keys[i], "host")) {
			xstrfmtcat(argv[j ++], "--nodelist=%s",
				   subcmd->info_vals[i]);

		} else if (! xstrcmp(subcmd->info_keys[i], "arch")) {
			error("mpi/pmi2: spawn info key 'arch' not supported");

		} else if (! xstrcmp(subcmd->info_keys[i], "wdir")) {
			xstrfmtcat(argv[j ++], "--chdir=%s",
				   subcmd->info_vals[i]);

		} else if (! xstrcmp(subcmd->info_keys[i], "path")) {
			env_array_overwrite_fmt(&env, "PATH", "%s",
						subcmd->info_vals[i]);

		} else if (! xstrcmp(subcmd->info_keys[i], "file")) {
			error("mpi/pmi2: spawn info key 'file' not supported");

		} else if (! xstrcmp(subcmd->info_keys[i], "soft")) {
			error("mpi/pmi2: spawn info key 'soft' not supported");

		} else {
			error("mpi/pmi2: unknown spawn info key '%s' ignored",
				subcmd->info_keys[i]);
		}
	}
	argv[j ++] = subcmd->cmd;
	for (i = 0; i < subcmd->argc; i ++) {
		argv[j ++] = subcmd->argv[i];
	}
	argv[j ++] = NULL;

	{
		debug3("mpi/mpi2: to execve");
		for (i = 0; i < j; i ++) {
			debug3("mpi/pmi2:   argv[%d]=%s", i, argv[i]);
		}
	}
	execve(SLURM_PREFIX"/bin/srun", argv, env);
	error("mpi/pmi2: failed to exec srun: %m");
	return SLURM_ERROR;
}

static int
_exec_srun_multiple(spawn_req_t *req, char **env)
{
	int argc, ntasks, i, j, spawn_cnt, fd;
	char **argv = NULL, *buf = NULL;
	spawn_subcmd_t *subcmd = NULL;
	char fbuf[128];

	debug3("mpi/pmi2: in _exec_srun_multiple");
	/* create a tmp multi_prog file */
	/* TODO: how to delete the file? */
	sprintf(fbuf, "/tmp/%d.XXXXXX", getpid());
	fd = mkstemp(fbuf);
	if (fd < 0) {
		error("mpi/pmi2: failed to open multi-prog file %s: %m", fbuf);
		return SLURM_ERROR;
	}
	ntasks = 0;
	for (spawn_cnt = 0; spawn_cnt < req->subcmd_cnt; spawn_cnt ++) {
		subcmd = req->subcmds[spawn_cnt];
		/* TODO: write a wrapper program to handle the info */
		if (subcmd->info_cnt > 0) {
			error("mpi/pmi2: spawn info ignored");
		}
		if (subcmd->max_procs == 1) {
			xstrfmtcat(buf, "%d  %s", ntasks, subcmd->cmd);
		} else {
			xstrfmtcat(buf, "%d-%d  %s", ntasks,
				   ntasks + subcmd->max_procs - 1, subcmd->cmd);
		}
		for (i = 0; i < subcmd->argc; i ++) {
			xstrfmtcat(buf, " %s", subcmd->argv[i]);
		}
		xstrcat(buf, "\n");
		ntasks += subcmd->max_procs;
	}
	if (buf) {
		safe_write(fd, buf, strlen(buf));
		xfree(buf);
	}
	close(fd);

	argc = 7;
	xrealloc(argv, argc * sizeof(char *));

	j = 0;
	argv[j ++] = "srun";
	argv[j ++] = "--mpi=pmi2";
	xstrfmtcat(argv[j ++], "--ntasks=%d", ntasks);
	if (job_info.srun_opt && job_info.srun_opt->srun_opt->no_alloc) {
		argv[j ++] = "--no-alloc";
		xstrfmtcat(argv[j ++], "--nodelist=%s",
			   job_info.srun_opt->nodelist);
	}
	argv[j ++] = "--multi-prog";
	argv[j ++] = fbuf;
	argv[j ++] = NULL;

	debug3("mpi/mpi2: to execve");

	execve(SLURM_PREFIX"/bin/srun", argv, env);
	error("mpi/pmi2: failed to exec srun: %m");
	return SLURM_ERROR;
rwfail:
	error("mpi/pmi2: failed to generate multi-prog file");
	return SLURM_ERROR;
}

static void
_setup_exec_srun(spawn_req_t *req)
{
	char **env, env_key[32];
	int i, rc;
	spawn_resp_t *resp;

	debug3("mpi/pmi2: in _setup_exec_srun");

	/* setup environments */
	env = env_array_copy((const char **)job_info.job_env);
	/* TODO: unset some env-vars */

	env_array_overwrite_fmt(&env, "SLURM_JOB_ID", "%u", job_info.jobid);
	env_array_overwrite_fmt(&env, PMI2_SPAWNER_JOBID_ENV, "%s",
				job_info.pmi_jobid);
	env_array_overwrite_fmt(&env, PMI2_PMI_JOBID_ENV, "%s-%u",
				job_info.pmi_jobid, req->seq);
	env_array_overwrite_fmt(&env, PMI2_SPAWN_SEQ_ENV, "%u", req->seq);
	env_array_overwrite_fmt(&env, PMI2_SPAWNER_PORT_ENV, "%hu",
				tree_info.pmi_port);
	/* preput kvs */
	env_array_overwrite_fmt(&env, PMI2_PREPUT_CNT_ENV, "%d",
				req->preput_cnt);
	for (i = 0; i < req->preput_cnt; i ++) {
		snprintf(env_key, 32, PMI2_PPKEY_ENV"%d", i);
		env_array_overwrite_fmt(&env, env_key, "%s", req->pp_keys[i]);
		snprintf(env_key, 32, PMI2_PPVAL_ENV"%d", i);
		env_array_overwrite_fmt(&env, env_key, "%s", req->pp_vals[i]);
	}

	if (req->subcmd_cnt == 1) {
		/* no return if success */
		rc = _exec_srun_single(req, env);
	} else {
		/* no return if success */
		rc = _exec_srun_multiple(req, env);
	}

	resp = spawn_resp_new();
	resp->seq = req->seq;
	xstrfmtcat(resp->jobid, "%s-%u", job_info.pmi_jobid, req->seq);
	resp->error_cnt = 0;
	resp->rc = rc;

	/* fake a srun address */
	tree_info.srun_addr = xmalloc(sizeof(slurm_addr_t));
	slurm_set_addr(tree_info.srun_addr, tree_info.pmi_port,
		       "127.0.0.1");
	spawn_resp_send_to_srun(resp);
	spawn_resp_free(resp);
	exit(errno);
}

extern int
spawn_job_do_spawn(spawn_req_t *req)
{
	pid_t child_pid;

	child_pid = fork();
	if (child_pid < 0) {
		error("mpi/pmi2: failed to fork srun");
		return SLURM_ERROR;
	} else if (child_pid == 0) { /* child */
		_setup_exec_srun(req);
	} else {
		/* always serially executed, spawn_seq == req->seq + 1 */
		xrealloc(spawned_srun_pids, spawn_seq * sizeof(pid_t));
		spawned_srun_pids[req->seq] = child_pid;
		return SLURM_SUCCESS;
	}
	return SLURM_ERROR;
}

static int
_wait_for_all(void)
{
	pid_t child;
	int i, status, exited;

	exited = 0;
	for (i = 1; i < spawn_seq; i ++) { /* seq 0 not used */
		if (! spawned_srun_pids[i])
			continue;
		child = waitpid(spawned_srun_pids[i], &status, WNOHANG);
		if (child == spawned_srun_pids[i]) {
			spawned_srun_pids[i] = 0;
			exited ++;
		}
	}
	return exited;
}

extern void
spawn_job_wait(void)
{
	int exited, i, wait;

	if (job_info.srun_opt) {
		wait = job_info.srun_opt->srun_opt->max_wait;
	} else {
		wait = 0;
	}

	if (wait == 0)		/* TODO: wait indefinitely */
		wait = 60;
	exited = _wait_for_all();
	while(wait > 0 && exited != spawn_seq - 1) {
		sleep(1);
		exited += _wait_for_all();
		wait --;
	}
	for (i = 1; i < spawn_seq; i ++) {
		if (!spawned_srun_pids[i])
			continue;
		/* terminte it */
		kill(spawned_srun_pids[i], SIGTERM);
	}
}
