/*****************************************************************************\
 **  client.c - PMI2 client wire protocol message handling
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

#include <stdlib.h>

#include "src/common/slurm_xlator.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "client.h"
#include "setup.h"
#include "pmi.h"

#define KEY_INDEX(i) (i * 2)
#define VAL_INDEX(i) (i * 2 + 1)
#define MP_KEY(msg, index) (msg->pairs[KEY_INDEX(index)])
#define MP_VAL(req, index) (req->pairs[VAL_INDEX(index)])

#define REQ_PAIR_SIZE_INC 32

static int pmi_version = 0;
static int pmi_subversion = 0;

extern int
is_pmi11(void)
{
	return (pmi_version == PMI11_VERSION &&
		pmi_subversion == PMI11_SUBVERSION);
}

extern int
is_pmi20(void)
{
	return (pmi_version == PMI20_VERSION &&
		pmi_subversion == PMI20_SUBVERSION);
}

extern int
get_pmi_version(int *version, int *subversion)
{
	if (pmi_version) {
		*version = pmi_version;
		*subversion = pmi_subversion;
		return SLURM_SUCCESS;
	} else
		return SLURM_ERROR;
}

extern int
set_pmi_version(int version, int subversion)
{
	if ( (version == PMI11_VERSION && subversion == PMI11_SUBVERSION) ||
	     (version == PMI20_VERSION && subversion == PMI20_SUBVERSION) ) {

		if (pmi_version && (pmi_version != version ||
				    pmi_subversion != subversion)) {
			error("mpi/pmi2: inconsistent client PMI version: "
			      "%d.%d(req) <> %d.%d(orig)", version, subversion,
			      pmi_version, pmi_subversion);
			return SLURM_ERROR;
		} else if (! pmi_version) {
			verbose("mpi/pmi2: got client PMI1 init, version=%d.%d",
				version, subversion);
			pmi_version = version;
			pmi_subversion = subversion;
		}
	} else {
		error("mpi/pmi2: unsupported PMI version: %d.%d", version,
		      subversion);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

static int
_parse_cmd(client_req_t *req)
{
	int i = 0, len = 0;

	len = strlen (MCMD_KEY"=");
	if (! xstrncmp(req->buf, MCMD_KEY"=", len)) {
		req->cmd = MCMD_KEY; /* XXX: mcmd=spawn */
		req->sep = '\n';
		req->term = '\n';
		return SLURM_SUCCESS;
	}

	len = strlen (CMD_KEY"=");
	if (xstrncmp(req->buf, CMD_KEY"=", len)) {
		error("mpi/pmi2: request not begin with '" CMD_KEY "='");
		error("mpi/pmi2: full request is: %s", req->buf);
		return SLURM_ERROR;
	}
	req->cmd = &req->buf[len];

	i = len;
	if (is_pmi11()) {
		req->sep = ' ';
		req->term = '\n';
		while (req->buf[i] != req->sep &&
		       req->buf[i] != req->term &&
		       i < req->buf_len) {
			i ++;
		}
	} else if (is_pmi20()) {
		req->sep = ';';
		req->term = ';';
		while (req->buf[i] != req->sep &&
		       req->buf[i] != req->term &&
		       i < req->buf_len) {
			i ++;
		}
	}
	if (i >= req->buf_len) {
		error ("mpi/pmi2: cmd not properly terminated in client request");
		return SLURM_ERROR;
	}
	req->buf[i] = '\0';	/* make it nul terminated */

	req->parse_idx = i + 1;

	/* TODO: concat processing */

	return SLURM_SUCCESS;
}


extern client_req_t *
client_req_init(uint32_t len, char *buf)
{
	client_req_t *req = NULL;

	/* buf always '\0' terminated */
	req = xmalloc(sizeof(client_req_t));
	req->buf = buf;
	req->buf_len = len;
	req->parse_idx = 0;

	if (_parse_cmd(req) != SLURM_SUCCESS) {
		xfree(req);
	}

	return req;
}

extern void
client_req_free(client_req_t *req)
{
	if (req) {
		xfree(req->buf);
		xfree(req->pairs);
		xfree(req);
	}
}


/*
 * No escape of ';' supported for now, hence no ';' in value.
 * TODO: concat command processing
 */
extern int
client_req_parse_body(client_req_t *req)
{
	int i = 0, rc = SLURM_SUCCESS;
	char *key, *val;

	/* skip cmd */
	i = req->parse_idx;

	while (i < req->buf_len) {
		/* search for key */
		key = &req->buf[i];
		while (req->buf[i] != '=' && i < req->buf_len) {
			i ++;
		}
		if (i >= req->buf_len) {
			error("mpi/pmi2: no value for key %s in req", key);
			rc = SLURM_ERROR;
			break;
		}
		req->buf[i] = '\0'; /* make it nul terminated */
		i ++;
		debug3("mpi/pmi2: client req key %s", key);

		/* search for val */
		val = &req->buf[i];
		while (req->buf[i] != req->sep &&
		       req->buf[i] != req->term &&
		       i < req->buf_len) {
			i ++;
		}
		if (i >= req->buf_len) {
			error("mpi/pmi2: value not properly terminated in "
			      "client request");
			rc = SLURM_ERROR;
			break;
		}
		req->buf[i] = '\0'; /* make it nul terminated */
		i ++;
		debug3("mpi/pmi2: client req val %s", val);
		/*
		 * append pair.
		 * there may be duplicate keys in the pairs, such as in the
		 * spawn cmd. Hence the order of the pairs is of significance.
		 */
		if (2 * (req->pairs_cnt + 2) > req->pairs_size) {
			req->pairs_size += REQ_PAIR_SIZE_INC;
			xrealloc(req->pairs, req->pairs_size * sizeof(char *));
		}
		req->pairs[KEY_INDEX(req->pairs_cnt)] = key;
		req->pairs[VAL_INDEX(req->pairs_cnt)] = val;
		req->pairs_cnt ++;
	}
	/* add a pair of NULL at the end, without increasing req->pairs_cnt */
	req->pairs[KEY_INDEX(req->pairs_cnt)] = NULL;
	req->pairs[VAL_INDEX(req->pairs_cnt)] = NULL;

	return rc;
}

extern spawn_req_t *
client_req_parse_spawn_req(client_req_t *req)
{
	spawn_req_t *spawn_req = NULL;
	spawn_subcmd_t *subcmd = NULL;
	int i = 0, j = 0, pi = 0;

	/* req body already parsed */
	pi = 0;

	if (req->pairs_cnt - pi < 5) {
		/* NCMDS, PREPUTCOUNT, SUBCMD, MAXPROCS, ARGC */
		error("mpi/pmi2: wrong number of key-val pairs in spawn cmd");
		return NULL;
	}

	spawn_req = spawn_req_new();

	/* ncmds */
	if (xstrcmp(MP_KEY(req, pi), NCMDS_KEY)) {
		error("mpi/pmi2: '" NCMDS_KEY "' expected in spawn cmd");
		goto req_err;
	}
	spawn_req->subcmd_cnt = atoi(MP_VAL(req, pi));
	spawn_req->subcmds = xmalloc(spawn_req->subcmd_cnt *
				     sizeof(spawn_subcmd_t *));
	pi ++;
	/* preputcount */
	if (xstrcmp(MP_KEY(req, pi), PREPUTCOUNT_KEY)) {
		error("mpi/pmi2: '" PREPUTCOUNT_KEY "' expected in spawn cmd");
		goto req_err;
	}
	spawn_req->preput_cnt = atoi(MP_VAL(req, pi));
	pi ++;
	if (req->pairs_cnt - pi <
	    ( (2 * spawn_req->preput_cnt) + (3 * spawn_req->subcmd_cnt) )) {
		/* <PPKEY, PPVAL>, <SUBCMD, MAXPROCS, ARGC> */
		error("mpi/pmi2: wrong number of key-val pairs in spawn cmd");
		goto req_err;
	}
	spawn_req->pp_keys = xmalloc(spawn_req->preput_cnt * sizeof(char *));
	spawn_req->pp_vals = xmalloc(spawn_req->preput_cnt * sizeof(char *));
	/* ppkey,ppval */
	for (i = 0; i < spawn_req->preput_cnt; i ++) {
		/* ppkey */
		if (xstrncmp(MP_KEY(req, pi), PPKEY_KEY, strlen(PPKEY_KEY)) ||
		    atoi((MP_KEY(req, pi) + strlen(PPKEY_KEY))) != i) {
			error("mpi/pmi2: '" PPKEY_KEY
			      "%d' expected in spawn cmd", i);
			goto req_err;
		}
		spawn_req->pp_keys[i] = xstrdup(MP_VAL(req, pi));
		pi ++;
		/* ppval */
		if (xstrncmp(MP_KEY(req, pi), PPVAL_KEY, strlen(PPVAL_KEY)) ||
		    atoi((MP_KEY(req, pi) + strlen(PPVAL_KEY))) != i) {
			error("mpi/pmi2: '" PPVAL_KEY
			      "%d' expected in spawn cmd", i);
			goto req_err;
		}
		spawn_req->pp_vals[i] = xstrdup(MP_VAL(req, pi));
		pi ++;
	}
	/* subcmd */
	for (i = 0; i < spawn_req->subcmd_cnt; i ++) {
		spawn_req->subcmds[i] = spawn_subcmd_new();
		subcmd = spawn_req->subcmds[i];
		/* subcmd */
		if (xstrcmp(MP_KEY(req, pi), SUBCMD_KEY)) {
			error("mpi/pmi2: '" SUBCMD_KEY
			      "' expected in spawn cmd");
			goto req_err;
		}
		subcmd->cmd = xstrdup(MP_VAL(req, pi));
		pi ++;
		/* maxprocs */
		if (xstrcmp(MP_KEY(req, pi), MAXPROCS_KEY)) {
			error("mpi/pmi2: '" MAXPROCS_KEY
			      "' expected in spawn cmd");
			goto req_err;

		}
		subcmd->max_procs = atoi(MP_VAL(req, pi));
		pi ++;
		/* argc */
		if (xstrcmp(MP_KEY(req, pi), ARGC_KEY)) {
			error("mpi/pmi2: '" ARGC_KEY
			      "' expected in spawn cmd");
			goto req_err;

		}
		subcmd->argc = atoi(MP_VAL(req, pi));
		pi ++;
		if (req->pairs_cnt - pi <
		    ( subcmd->argc + (3 * (spawn_req->subcmd_cnt - i - 1))) ) {
			/* <ARGV>, <SUBCMD, MAXPROCS, ARGC> */
			error("mpi/pmi2: wrong number of key-val pairs"
			      " in spawn cmd");
			goto req_err;
		}
		debug("mpi/pmi2: argc = %d", subcmd->argc);
		if (subcmd->argc > 0) {
			subcmd->argv = xmalloc(subcmd->argc * sizeof(char *));
		}
		/* argv */
		for (j = 0; j < subcmd->argc; j ++) {
			if (xstrncmp(MP_KEY(req, pi), ARGV_KEY,
				     strlen(ARGV_KEY)) ||
			    atoi((MP_KEY(req, pi) + strlen(ARGV_KEY))) != j) {
				error("mpi/pmi2: '" ARGV_KEY
				      "%d' expected in spawn cmd", j);
				goto req_err;
			}
			subcmd->argv[j] = xstrdup(MP_VAL(req, pi));
			pi ++;
		}
		debug("mpi/pmi2: got argv");
		/* infokeycount, optional */
		if (pi == req->pairs_cnt) {
			if (i != spawn_req->subcmd_cnt - 1) {
				error("mpi/pmi2: wrong number of key-val pairs"
				      "in spawn cmd");
				goto req_err;
			}
			break;
		} else if (xstrcmp(MP_KEY(req, pi), INFOKEYCOUNT_KEY)) {
			subcmd->info_cnt = 0;
			continue;
		}
		subcmd->info_cnt = atoi(MP_VAL(req, pi));
		pi ++;
		if (req->pairs_cnt - pi <
		    ( (2 * subcmd->info_cnt) +
		      (3 * (spawn_req->subcmd_cnt - i - 1)) )) {
			/* <INFOKEY, INFOVAL>, <SUBCMD, MAXPROCS, ARGC> */
			error("mpi/pmi2: wrong number of key-val pairs"
			      " in spawn cmd");
			goto req_err;
		}
		if (subcmd->info_cnt > 0) {
			subcmd->info_keys = xmalloc(subcmd->info_cnt *
						    sizeof(char *));
			subcmd->info_vals = xmalloc(subcmd->info_cnt *
						    sizeof(char *));
		}
		/* infokey,infoval */
		for (j = 0; j < subcmd->info_cnt; j ++) {
			/* infokey */
			if (xstrncmp(MP_KEY(req, pi), INFOKEY_KEY,
				     strlen(INFOKEY_KEY)) ||
			    atoi((MP_KEY(req, pi) +
				  strlen(INFOKEY_KEY))) != j) {
				error("mpi/pmi2: '" INFOKEY_KEY
				      "%d' expected in spawn cmd", j);
				goto req_err;
			}
			subcmd->info_keys[j] = xstrdup(MP_VAL(req, pi));
			pi ++;
			/* infoval */
			if (xstrncmp(MP_KEY(req, pi), INFOVAL_KEY,
				     strlen(INFOVAL_KEY)) ||
			    atoi((MP_KEY(req, pi) +
				  strlen(INFOVAL_KEY))) != j) {
				error("mpi/pmi2: '" INFOVAL_KEY
				      "%d' expected in spawn cmd", j);
				goto req_err;
			}
			subcmd->info_vals[j] = xstrdup(MP_VAL(req, pi));
			pi ++;
		}
	}

	debug("mpi/pmi2: out client_req_parse_spawn");
	return spawn_req;

req_err:
	spawn_req_free(spawn_req);
	return NULL;
}

extern spawn_subcmd_t *
client_req_parse_spawn_subcmd(client_req_t *req)
{
	spawn_subcmd_t *subcmd = NULL;
	char buf[PMI2_MAX_KEYLEN];
	int i = 0;

	subcmd = xmalloc(sizeof(spawn_subcmd_t));

	client_req_get_str(req, EXECNAME_KEY, &subcmd->cmd);
	client_req_get_int(req, NPROCS_KEY, (int *)&subcmd->max_procs);
	client_req_get_int(req, ARGCNT_KEY, (int *)&subcmd->argc);
	subcmd->argv = xmalloc(subcmd->argc * sizeof(char *));
	for (i = 0; i < subcmd->argc; i ++) {
		snprintf(buf, PMI2_MAX_KEYLEN, "arg%d", i + 1);
		client_req_get_str(req, buf, &(subcmd->argv[i]));
	}
	client_req_get_int(req, INFONUM_KEY, (int *)&subcmd->info_cnt);
	subcmd->info_keys = xmalloc(subcmd->info_cnt * sizeof(char *));
	subcmd->info_vals = xmalloc(subcmd->info_cnt * sizeof(char *));
	for (i = 0; i < subcmd->info_cnt; i ++) {
		snprintf(buf, PMI2_MAX_KEYLEN, "info_key_%d", i);
		client_req_get_str(req, buf, &(subcmd->info_keys[i]));
		snprintf(buf, PMI2_MAX_KEYLEN, "info_val_%d", i);
		client_req_get_str(req, buf, &(subcmd->info_vals[i]));
	}
	return subcmd;
}

/************************************************************************/

/* returned value not dup-ed */
static char *
_client_req_get_val(client_req_t *req, const char *key)
{
	int i;

	for (i = 0; i < req->pairs_cnt; i ++) {
		if (! xstrcmp(key, req->pairs[KEY_INDEX(i)]))
			return req->pairs[VAL_INDEX(i)];
	}
	return NULL;
}

/* return true if found */
extern bool
client_req_get_str(client_req_t *req, const char *key, char **pval)
{
	char *val;

	val = _client_req_get_val(req, key);
	if (val == NULL)
		return false;

	*pval = xstrdup(val);
	return true;
}

extern bool
client_req_get_int(client_req_t *req, const char *key, int *pval)
{
	char *val;

	val = _client_req_get_val(req, key);
	if (val == NULL)
		return false;

	*pval = atoi(val);
	return true;
}

extern bool
client_req_get_bool(client_req_t *req, const char *key, bool *pval)
{
	char *val;

	val = _client_req_get_val(req, key);
	if (val == NULL)
		return false;

	if (!xstrcasecmp(val, TRUE_VAL))
		*pval = true;
	else
		*pval = false;
	return true;
}

/* ************************************************************ */

extern client_resp_t *
client_resp_new(void)
{
	client_resp_t *resp;

	resp = xmalloc(sizeof(client_resp_t));
	return resp;
}

extern int
client_resp_send(client_resp_t *resp, int fd)
{
	char len_buf[7];
	int len;

	len = strlen(resp->buf);

	if ( is_pmi20() ) {
		snprintf(len_buf, 7, "%-6d", len);
		debug2("mpi/pmi2: client_resp_send: %s%s", len_buf, resp->buf);
		safe_write(fd, len_buf, 6);
	} else if ( is_pmi11() ) {
		debug2("mpi/pmi2: client_resp_send: %s", resp->buf);
	}
	safe_write(fd, resp->buf, len);

	return SLURM_SUCCESS;

rwfail:
	return SLURM_ERROR;
}

extern void
client_resp_free(client_resp_t *resp)
{
	if (resp) {
		xfree(resp->buf);
		xfree(resp);
	}
}

/* caller must free the result */
static char *
_str_replace(char *str, char src, char dst)
{
	char *res, *ptr;

	res = xstrdup(str);
	ptr = res;
	while (*ptr) {
		if (*ptr == src)
			*ptr = dst;
		ptr ++;
	}
	return res;
}
/* send fence_resp/barrier_out to tasks */
extern int
send_kvs_fence_resp_to_clients(int rc, char *errmsg)
{
	int i = 0;
	client_resp_t *resp;
	char *msg;

	resp = client_resp_new();
	if ( is_pmi11() ) {
		if (rc != 0 && errmsg != NULL) {
			// XXX: pmi1.1 does not check the rc
			msg = _str_replace(errmsg, ' ', '_');
			client_resp_append(resp, CMD_KEY"="BARRIEROUT_CMD" "
					   RC_KEY"=%d "MSG_KEY"=%s\n",
					   rc, msg);
			xfree(msg);
		} else {
			client_resp_append(resp, CMD_KEY"="BARRIEROUT_CMD" "
					   RC_KEY"=%d\n", rc);
		}
	} else if (is_pmi20()) {
		if (rc != 0 && errmsg != NULL) {
			// TODO: pmi2.0 accept escaped ';' (";;")
			msg = _str_replace(errmsg, ';', '_');
			client_resp_append(resp, CMD_KEY"="KVSFENCERESP_CMD";"
					   RC_KEY"=%d;"ERRMSG_KEY"=%s;",
					   rc, msg);
			xfree(msg);
		} else {
			client_resp_append(resp, CMD_KEY"="KVSFENCERESP_CMD";"
					   RC_KEY"=%d;", rc);
		}
	}
	for (i = 0; i < job_info.ltasks; i ++) {
		rc = client_resp_send(resp, STEPD_PMI_SOCK(i));
	}
	client_resp_free(resp);
	return rc;
}
