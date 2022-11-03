/*****************************************************************************\
 *  burst_buffer_common.c - Common logic for managing burst_buffers
 *
 *  NOTE: These functions are designed so they can be used by multiple burst
 *  buffer plugins at the same time, so the state information is largely in the
 *  individual plugin and passed as a pointer argument to these functions.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <ctype.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/mman.h>	/* memfd_create */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#define POLLRDHUP POLLHUP
#include <signal.h>
#endif

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/assoc_mgr.h"
#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/run_command.h"
#include "src/interfaces/accounting_storage.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"

#include "burst_buffer_common.h"

/* Maximum poll wait time for child processes, in milliseconds */
#define MAX_POLL_WAIT 500

static void	_bb_job_del2(bb_job_t *bb_job);
static uid_t *	_parse_users(char *buf);
static char *	_print_users(uid_t *buf);

/* Translate comma delimitted list of users into a UID array,
 * Return value must be xfreed */
static uid_t *_parse_users(char *buf)
{
	char *tmp, *tok, *save_ptr = NULL;
	int inx = 0, array_size;
	uid_t *user_array = NULL;

	if (!buf)
		return user_array;
	tmp = xstrdup(buf);
	array_size = 1;
	user_array = xmalloc(sizeof(uid_t) * array_size);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		if ((uid_from_string(tok, user_array + inx) == -1) ||
		    (user_array[inx] == 0)) {
			error("%s: ignoring invalid user: %s", __func__, tok);
		} else {
			if (++inx >= array_size) {
				array_size *= 2;
				user_array = xrealloc(user_array,
						      sizeof(uid_t)*array_size);
			}
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);
	return user_array;
}

/* Translate an array of (zero terminated) UIDs into a string with colon
 * delimited UIDs
 * Return value must be xfreed */
static char *_print_users(uid_t *buf)
{
	char *user_elem, *user_str = NULL;
	int i;

	if (!buf)
		return user_str;
	for (i = 0; buf[i]; i++) {
		user_elem = uid_to_string(buf[i]);
		if (!user_elem)
			continue;
		if (user_str)
			xstrcat(user_str, ",");
		xstrcat(user_str, user_elem);
		xfree(user_elem);
	}
	return user_str;
}

/* Allocate burst buffer hash tables */
extern void bb_alloc_cache(bb_state_t *state_ptr)
{
	state_ptr->bb_ahash = xmalloc(sizeof(bb_alloc_t *) * BB_HASH_SIZE);
	state_ptr->bb_jhash = xmalloc(sizeof(bb_job_t *)   * BB_HASH_SIZE);
	state_ptr->bb_uhash = xmalloc(sizeof(bb_user_t *)  * BB_HASH_SIZE);
}

/* Clear all cached burst buffer records, freeing all memory. */
extern void bb_clear_cache(bb_state_t *state_ptr)
{
	bb_alloc_t *bb_current,   *bb_next;
	bb_job_t   *job_current,  *job_next;
	bb_user_t  *user_current, *user_next;
	int i;

	if (state_ptr->bb_ahash) {
		for (i = 0; i < BB_HASH_SIZE; i++) {
			bb_current = state_ptr->bb_ahash[i];
			while (bb_current) {
				xassert(bb_current->magic == BB_ALLOC_MAGIC);
				bb_next = bb_current->next;
				bb_free_alloc_buf(bb_current);
				bb_current = bb_next;
			}
		}
		xfree(state_ptr->bb_ahash);
	}

	if (state_ptr->bb_jhash) {
		for (i = 0; i < BB_HASH_SIZE; i++) {
			job_current = state_ptr->bb_jhash[i];
			while (job_current) {
				xassert(job_current->magic == BB_JOB_MAGIC);
				job_next = job_current->next;
				_bb_job_del2(job_current);
				job_current = job_next;
			}
		}
		xfree(state_ptr->bb_jhash);
	}

	if (state_ptr->bb_uhash) {
		for (i = 0; i < BB_HASH_SIZE; i++) {
			user_current = state_ptr->bb_uhash[i];
			while (user_current) {
				xassert(user_current->magic == BB_USER_MAGIC);
				user_next = user_current->next;
				xfree(user_current);
				user_current = user_next;
			}
		}
		xfree(state_ptr->bb_uhash);
	}

	xfree(state_ptr->name);
	FREE_NULL_LIST(state_ptr->persist_resv_rec);
}

/* Clear configuration parameters, free memory
 * config_ptr IN - Initial configuration to be cleared
 * fini IN - True if shutting down, do more complete clean-up */
extern void bb_clear_config(bb_config_t *config_ptr, bool fini)
{
	int i;

	xassert(config_ptr);
	xfree(config_ptr->allow_users);
	xfree(config_ptr->allow_users_str);
	xfree(config_ptr->create_buffer);
	xfree(config_ptr->default_pool);
	xfree(config_ptr->deny_users);
	xfree(config_ptr->deny_users_str);
	xfree(config_ptr->destroy_buffer);
	xfree(config_ptr->directive_str);
	config_ptr->flags = 0;
	xfree(config_ptr->get_sys_state);
	xfree(config_ptr->get_sys_status);
	config_ptr->granularity = 1;
	if (fini) {
		for (i = 0; i < config_ptr->pool_cnt; i++)
			xfree(config_ptr->pool_ptr[i].name);
		xfree(config_ptr->pool_ptr);
		config_ptr->pool_cnt = 0;
	} else {
		for (i = 0; i < config_ptr->pool_cnt; i++)
			config_ptr->pool_ptr[i].total_space = 0;
	}
	config_ptr->other_timeout = 0;
	config_ptr->stage_in_timeout = 0;
	config_ptr->stage_out_timeout = 0;
	xfree(config_ptr->start_stage_in);
	xfree(config_ptr->start_stage_out);
	xfree(config_ptr->stop_stage_in);
	xfree(config_ptr->stop_stage_out);
	config_ptr->validate_timeout = 0;
}

/* Find a per-job burst buffer record for a specific job.
 * If not found, return NULL. */
extern bb_alloc_t *bb_find_alloc_rec(bb_state_t *state_ptr,
				     job_record_t *job_ptr)
{
	bb_alloc_t *bb_alloc = NULL;

	xassert(job_ptr);
	xassert(state_ptr);
	bb_alloc = state_ptr->bb_ahash[job_ptr->user_id % BB_HASH_SIZE];
	while (bb_alloc) {
		if (bb_alloc->job_id == job_ptr->job_id) {
			if (bb_alloc->user_id == job_ptr->user_id) {
				xassert(bb_alloc->magic == BB_ALLOC_MAGIC);
				return bb_alloc;
			}
			error("%s: Slurm state inconsistent with burst buffer. %pJ has UserID mismatch (%u != %u)",
			      __func__, job_ptr,
			      bb_alloc->user_id, job_ptr->user_id);
			/* This has been observed when slurmctld crashed and
			 * the job state recovered was missing some jobs
			 * which already had burst buffers configured. */
		}
		bb_alloc = bb_alloc->next;
	}
	return bb_alloc;
}

/* Find a burst buffer record by name
 * bb_name IN - Buffer's name
 * user_id IN - Possible user ID, advisory use only
 * RET the buffer or NULL if not found */
extern bb_alloc_t *bb_find_name_rec(char *bb_name, uint32_t user_id,
				    bb_state_t *state_ptr)
{
	bb_alloc_t *bb_alloc = NULL;
	int i, hash_inx = user_id % BB_HASH_SIZE;

	/* Try this user ID first */
	bb_alloc = state_ptr->bb_ahash[hash_inx];
	while (bb_alloc) {
		if (!xstrcmp(bb_alloc->name, bb_name))
			return bb_alloc;
		bb_alloc = bb_alloc->next;
	}

	/* Now search all other records */
	for (i = 0; i < BB_HASH_SIZE; i++) {
		if (i == hash_inx)
			continue;
		bb_alloc = state_ptr->bb_ahash[i];
		while (bb_alloc) {
			if (!xstrcmp(bb_alloc->name, bb_name)) {
				xassert(bb_alloc->magic == BB_ALLOC_MAGIC);
				return bb_alloc;
			}
			bb_alloc = bb_alloc->next;
		}
	}

	return bb_alloc;
}

/* Find a per-user burst buffer record for a specific user ID */
extern bb_user_t *bb_find_user_rec(uint32_t user_id, bb_state_t *state_ptr)
{
	int inx = user_id % BB_HASH_SIZE;
	bb_user_t *user_ptr;

	xassert(state_ptr);
	xassert(state_ptr->bb_uhash);
	user_ptr = state_ptr->bb_uhash[inx];
	while (user_ptr) {
		if (user_ptr->user_id == user_id)
			return user_ptr;
		user_ptr = user_ptr->next;
	}
	user_ptr = xmalloc(sizeof(bb_user_t));
	user_ptr->magic = BB_USER_MAGIC;
	user_ptr->next = state_ptr->bb_uhash[inx];
	/* user_ptr->size = 0;	initialized by xmalloc */
	user_ptr->user_id = user_id;
	state_ptr->bb_uhash[inx] = user_ptr;
	return user_ptr;
}

#ifdef HAVE_MEMFD_CREATE
char *_handle_replacement(job_record_t *job_ptr)
{
	char *replaced = NULL, *p, *q;

	if (!job_ptr->burst_buffer)
		return xstrdup("");

	/* throw a script header on in case something downstream cares */
	xstrcat(replaced, "#!/bin/sh\n");

	p = q = job_ptr->burst_buffer;

	while (*p != '\0') {
		if (*p == '%') {
			xmemcat(replaced, q, p);
			p++;

			switch (*p) {
			case '%':	/* '%%' -> '%' */
				xstrcatchar(replaced, '%');
				break;
			case 'A':	/* '%A' => array master job id */
				xstrfmtcat(replaced, "%u",
					   job_ptr->array_job_id);
				break;
			case 'a':	/* '%a' => array task id */
				xstrfmtcat(replaced, "%u",
					   job_ptr->array_task_id);
				break;
			case 'd':	/* '%d' => workdir */
				xstrcat(replaced, job_ptr->details->work_dir);
				break;
			case 'j':	/* '%j' => jobid */
				xstrfmtcat(replaced, "%u", job_ptr->job_id);
				break;
			case 'u':	/* '%u' => user name */
				if (!job_ptr->user_name)
					job_ptr->user_name =
						uid_to_string_or_null(
							job_ptr->user_id);
				xstrcat(replaced, job_ptr->user_name);
				break;
			case 'x':	/* '%x' => job name */
				xstrcat(replaced, job_ptr->name);
				break;
			default:
				break;
			}

			q = ++p;
		} else if (*p == '\\' && *(p+1) == '\\') {
			/* '\\' => stop further symbol processing */
			xstrcat(replaced, p);
			q = p;
			break;
		} else
			p++;
	}

	if (p != q)
		xmemcat(replaced, q, p);

	/* throw an extra terminating newline in for good measure */
	xstrcat(replaced, "\n");

	return replaced;
}
#endif

char *bb_handle_job_script(job_record_t *job_ptr, bb_job_t *bb_job)
{
	char *script = NULL;

	if (bb_job->memfd_path) {
		/*
		 * Already have an existing symbol-replaced script, so use it.
		 */
		return xstrdup(bb_job->memfd_path);
	}

	if (bb_job->need_symbol_replacement) {
#ifdef HAVE_MEMFD_CREATE
		/*
		 * Create a memfd-backed temporary file to write out the
		 * symbol-replaced BB script. memfd files will automatically be
		 * cleaned up on process termination. This will be recreated if
		 * the slurmctld restarts, otherwise kept in memory for the
		 * lifespan of the job.
		 */
		char *filename = NULL, *bb;
		pid_t pid = getpid();

		xstrfmtcat(filename, "bb_job_script.%u", job_ptr->job_id);

		bb_job->memfd = memfd_create(filename, MFD_CLOEXEC);
		if (bb_job->memfd < 0)
			fatal("%s: failed memfd_create: %m", __func__);
		xstrfmtcat(bb_job->memfd_path, "/proc/%lu/fd/%d",
			   (unsigned long) pid, bb_job->memfd);

		bb = _handle_replacement(job_ptr);
		safe_write(bb_job->memfd, bb, strlen(bb));
		xfree(bb);

		return xstrdup(bb_job->memfd_path);

	rwfail:
		xfree(bb);
		fatal("%s: could not write script file, likely out of memory",
		      __func__);
#else
		error("%s: symbol replacement requested, but not available as memfd_create() could not be found at compile time. "
		      "Falling back to the unreplaced job script.",
		      __func__);
#endif
	}

	xstrfmtcat(script, "%s/hash.%d/job.%u/script",
	           slurm_conf.state_save_location, (job_ptr->job_id % 10),
	           job_ptr->job_id);

	return script;
}

static uint64_t _atoi(char *tok)
{
	char *end_ptr = NULL;
	int64_t size_i;
	uint64_t mult, size_u = 0;

	size_i = (int64_t) strtoll(tok, &end_ptr, 10);
	if (size_i > 0) {
		size_u = (uint64_t) size_i;
		if ((mult = suffix_mult(end_ptr)) != NO_VAL64)
			size_u *= mult;
	}
	return size_u;
}

extern void bb_set_job_bb_state(job_record_t *job_ptr, bb_job_t *bb_job,
				int new_state)
{
	const char *new_state_str = NULL;

	xassert(bb_job);

	new_state_str = bb_state_string(new_state);
	bb_job->state = new_state;
	if (!job_ptr) {
		/* This should never happen, but handle it just in case. */
		error("%s: Could not find job_ptr for JobId=%u, unable to set new burst buffer state %s in job.",
		      __func__, bb_job->job_id, new_state_str);
		return;
	}

	log_flag(BURST_BUF, "Modify %pJ burst buffer state from %s to %s",
		 job_ptr, job_ptr->burst_buffer_state, new_state_str);
	xfree(job_ptr->burst_buffer_state);
	job_ptr->burst_buffer_state = xstrdup(new_state_str);
}

/* Set the bb_state's tres_id and tres_pos for limit enforcement.
 * Value is set to -1 if not found. */
extern void bb_set_tres_pos(bb_state_t *state_ptr)
{
	slurmdb_tres_rec_t tres_rec;
	int inx;

	xassert(state_ptr);
	memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
	tres_rec.type = "bb";
	tres_rec.name = state_ptr->name;
	inx = assoc_mgr_find_tres_pos(&tres_rec, false);
	state_ptr->tres_pos = inx;
	if (inx == -1) {
		debug3("%s: Tres %s not found by assoc_mgr",
		       __func__, state_ptr->name);
	} else {
		state_ptr->tres_id  = assoc_mgr_tres_array[inx]->id;
	}
}

/* Load and process configuration parameters */
extern void bb_load_config(bb_state_t *state_ptr, char *plugin_type)
{
	s_p_hashtbl_t *bb_hashtbl = NULL;
	char *bb_conf, *tmp = NULL, *value;
	char *colon, *save_ptr = NULL, *tok;
	uint32_t pool_cnt;
	int fd, i;
	static s_p_options_t bb_options[] = {
		{"AllowUsers", S_P_STRING},
		{"CreateBuffer", S_P_STRING},
		{"DefaultPool", S_P_STRING},
		{"DenyUsers", S_P_STRING},
		{"DestroyBuffer", S_P_STRING},
		{"Directive", S_P_STRING},
		{"Flags", S_P_STRING},
		{"GetSysState", S_P_STRING},
		{"GetSysStatus", S_P_STRING},
		{"Granularity", S_P_STRING},
		{"OtherTimeout", S_P_UINT32},
		{"Pools", S_P_STRING},
		{"StageInTimeout", S_P_UINT32},
		{"StageOutTimeout", S_P_UINT32},
		{"StartStageIn", S_P_STRING},
		{"StartStageOut", S_P_STRING},
		{"StopStageIn", S_P_STRING},
		{"StopStageOut", S_P_STRING},
		{"ValidateTimeout", S_P_UINT32},
		{NULL}
	};

	xfree(state_ptr->name);
	if (plugin_type) {
		tmp = strchr(plugin_type, '/');
		if (tmp)
			tmp++;
		else
			tmp = plugin_type;
		state_ptr->name = xstrdup(tmp);
	}

	/* Set default configuration */
	bb_clear_config(&state_ptr->bb_config, false);
	state_ptr->bb_config.flags |= BB_FLAG_DISABLE_PERSISTENT;
	state_ptr->bb_config.other_timeout = DEFAULT_OTHER_TIMEOUT;
	state_ptr->bb_config.stage_in_timeout = DEFAULT_STATE_IN_TIMEOUT;
	state_ptr->bb_config.stage_out_timeout = DEFAULT_STATE_OUT_TIMEOUT;
	state_ptr->bb_config.validate_timeout = DEFAULT_VALIDATE_TIMEOUT;

	/* First look for "burst_buffer.conf" then with "type" field,
	 * for example "burst_buffer_datawarp.conf" */
	bb_conf = get_extra_conf_path("burst_buffer.conf");
	fd = open(bb_conf, 0);
	if (fd >= 0) {
		close(fd);
	} else {
		char *new_path = NULL;
		xfree(bb_conf);
		xstrfmtcat(new_path, "burst_buffer_%s.conf", state_ptr->name);
		bb_conf = get_extra_conf_path(new_path);
		fd = open(bb_conf, 0);
		if (fd < 0) {
			info("%s: Unable to find configuration file %s or "
			     "burst_buffer.conf", __func__, new_path);
			xfree(bb_conf);
			xfree(new_path);
			return;
		}
		close(fd);
		xfree(new_path);
	}

	bb_hashtbl = s_p_hashtbl_create(bb_options);
	if (s_p_parse_file(bb_hashtbl, NULL, bb_conf, false, NULL)
	    == SLURM_ERROR) {
		fatal("%s: something wrong with opening/reading %s: %m",
		      __func__, bb_conf);
	}
	if (s_p_get_string(&state_ptr->bb_config.allow_users_str, "AllowUsers",
			   bb_hashtbl)) {
		state_ptr->bb_config.allow_users = _parse_users(
					state_ptr->bb_config.allow_users_str);
	}
	s_p_get_string(&state_ptr->bb_config.create_buffer, "CreateBuffer",
		       bb_hashtbl);
	s_p_get_string(&state_ptr->bb_config.default_pool, "DefaultPool",
		       bb_hashtbl);
	if (s_p_get_string(&state_ptr->bb_config.deny_users_str, "DenyUsers",
			   bb_hashtbl)) {
		state_ptr->bb_config.deny_users = _parse_users(
					state_ptr->bb_config.deny_users_str);
	}
	s_p_get_string(&state_ptr->bb_config.destroy_buffer, "DestroyBuffer",
		       bb_hashtbl);
	s_p_get_string(&state_ptr->bb_config.directive_str, "Directive",
		       bb_hashtbl);

	if (s_p_get_string(&tmp, "Flags", bb_hashtbl)) {
		state_ptr->bb_config.flags = slurm_bb_str2flags(tmp);
		xfree(tmp);
	}
	/* By default, disable persistent buffer creation by normal users */
	if (state_ptr->bb_config.flags & BB_FLAG_ENABLE_PERSISTENT)
		state_ptr->bb_config.flags &= (~BB_FLAG_DISABLE_PERSISTENT);

	s_p_get_string(&state_ptr->bb_config.get_sys_state, "GetSysState",
		       bb_hashtbl);
	s_p_get_string(&state_ptr->bb_config.get_sys_status, "GetSysStatus",
		       bb_hashtbl);
	if (s_p_get_string(&tmp, "Granularity", bb_hashtbl)) {
		state_ptr->bb_config.granularity = bb_get_size_num(tmp, 1);
		xfree(tmp);
		if (state_ptr->bb_config.granularity == 0) {
			error("%s: Granularity=0 is invalid", __func__);
			state_ptr->bb_config.granularity = 1;
		}
	}
	if (s_p_get_string(&tmp, "Pools", bb_hashtbl)) {
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			burst_buffer_pool_t *tmp_pool;
			int inx;
			colon = strchr(tok, ':');
			if (colon) {
				colon[0] = '\0';
				pool_cnt = _atoi(colon + 1);
			} else
				pool_cnt = 1;
			/* Make space */
			state_ptr->bb_config.pool_ptr = xrealloc(
				state_ptr->bb_config.pool_ptr,
				sizeof(burst_buffer_pool_t) *
				(state_ptr->bb_config.pool_cnt + 1));

			/* Initialize */
			inx = state_ptr->bb_config.pool_cnt;
			tmp_pool = &(state_ptr->bb_config.pool_ptr[inx]);
			tmp_pool->granularity = 1;
			tmp_pool->name = xstrdup(tok);
			tmp_pool->total_space = pool_cnt;
			tmp_pool->unfree_space = 0;
			tmp_pool->used_space = 0;
			state_ptr->bb_config.pool_cnt++;
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
	}

	(void) s_p_get_uint32(&state_ptr->bb_config.other_timeout,
			     "OtherTimeout", bb_hashtbl);
	(void) s_p_get_uint32(&state_ptr->bb_config.stage_in_timeout,
			    "StageInTimeout", bb_hashtbl);
	(void) s_p_get_uint32(&state_ptr->bb_config.stage_out_timeout,
			    "StageOutTimeout", bb_hashtbl);
	s_p_get_string(&state_ptr->bb_config.start_stage_in, "StartStageIn",
		       bb_hashtbl);
	s_p_get_string(&state_ptr->bb_config.start_stage_out, "StartStageOut",
			    bb_hashtbl);
	s_p_get_string(&state_ptr->bb_config.stop_stage_in, "StopStageIn",
		       bb_hashtbl);
	s_p_get_string(&state_ptr->bb_config.stop_stage_out, "StopStageOut",
		       bb_hashtbl);
	(void) s_p_get_uint32(&state_ptr->bb_config.validate_timeout,
			     "ValidateTimeout", bb_hashtbl);

	s_p_hashtbl_destroy(bb_hashtbl);
	xfree(bb_conf);

	if (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF) {
		value = _print_users(state_ptr->bb_config.allow_users);
		info("%s: AllowUsers:%s",  __func__, value);
		xfree(value);
		info("%s: CreateBuffer:%s",  __func__,
		     state_ptr->bb_config.create_buffer);
		info("%s: DefaultPool:%s",  __func__,
		     state_ptr->bb_config.default_pool);
		value = _print_users(state_ptr->bb_config.deny_users);
		info("%s: DenyUsers:%s",  __func__, value);
		xfree(value);
		info("%s: DestroyBuffer:%s",  __func__,
		     state_ptr->bb_config.destroy_buffer);
		info("%s: Directive:%s",
		     __func__, state_ptr->bb_config.directive_str);
		info("%s: Flags:%s",
		     __func__, slurm_bb_flags2str(state_ptr->bb_config.flags));
		info("%s: GetSysState:%s",  __func__,
		     state_ptr->bb_config.get_sys_state);
		info("%s: GetSysStatus:%s",  __func__,
		     state_ptr->bb_config.get_sys_status);
		info("%s: Granularity:%"PRIu64"",  __func__,
		     state_ptr->bb_config.granularity);
		for (i = 0; i < state_ptr->bb_config.pool_cnt; i++) {
			info("%s: Pool[%d]:%s:%"PRIu64"", __func__, i,
			     state_ptr->bb_config.pool_ptr[i].name,
			     state_ptr->bb_config.pool_ptr[i].total_space);
		}
		info("%s: OtherTimeout:%u", __func__,
		     state_ptr->bb_config.other_timeout);
		info("%s: StageInTimeout:%u", __func__,
		     state_ptr->bb_config.stage_in_timeout);
		info("%s: StageOutTimeout:%u", __func__,
		     state_ptr->bb_config.stage_out_timeout);
		info("%s: StartStageIn:%s",  __func__,
		     state_ptr->bb_config.start_stage_in);
		info("%s: StartStageOut:%s",  __func__,
		     state_ptr->bb_config.start_stage_out);
		info("%s: StopStageIn:%s",  __func__,
		     state_ptr->bb_config.stop_stage_in);
		info("%s: StopStageOut:%s",  __func__,
		     state_ptr->bb_config.stop_stage_out);
		info("%s: ValidateTimeout:%u", __func__,
		     state_ptr->bb_config.validate_timeout);
	}
}

extern int bb_open_state_file(const char *file_name, char **state_file)
{
	int state_fd;
	struct stat stat_buf;

	*state_file = xstrdup(slurm_conf.state_save_location);
	xstrfmtcat(*state_file, "/%s", file_name);
	state_fd = open(*state_file, O_RDONLY);
	if (state_fd < 0) {
		error("Could not open burst buffer state file %s: %m",
		      *state_file);
	} else if (fstat(state_fd, &stat_buf) < 0) {
		error("Could not stat burst buffer state file %s: %m",
		      *state_file);
		(void) close(state_fd);
	} else if (stat_buf.st_size < 4) {
		error("Burst buffer state file %s too small", *state_file);
		(void) close(state_fd);
	} else	/* Success */
		return state_fd;

	error("NOTE: Trying backup burst buffer state save file. Information may be lost!");
	xstrcat(*state_file, ".old");
	state_fd = open(*state_file, O_RDONLY);
	return state_fd;
}

static void _pack_alloc(struct bb_alloc *bb_alloc, buf_t *buffer,
			uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(bb_alloc->account,      buffer);
		pack32(bb_alloc->array_job_id,  buffer);
		pack32(bb_alloc->array_task_id, buffer);
		pack_time(bb_alloc->create_time, buffer);
		pack32(bb_alloc->job_id,        buffer);
		packstr(bb_alloc->name,         buffer);
		packstr(bb_alloc->partition,    buffer);
		packstr(bb_alloc->pool,   	buffer);
		packstr(bb_alloc->qos,          buffer);
		pack64(bb_alloc->size,          buffer);
		pack16(bb_alloc->state,         buffer);
		pack32(bb_alloc->user_id,       buffer);
	}
}

/* Return true if hetjob separator in the script */
static bool _hetjob_check(char *tok)
{
	if (xstrncmp(tok + 1, "SLURM",  5) &&
	    xstrncmp(tok + 1, "SBATCH", 6))
		return false;
	if (!xstrstr(tok + 6, "packjob") &&
	    !xstrstr(tok + 6, "hetjob"))
		return false;
	return true;
}

/* Pack individual burst buffer records into a buffer */
extern int bb_pack_bufs(uid_t uid, bb_state_t *state_ptr, buf_t *buffer,
			uint16_t protocol_version)
{
	int i, rec_count = 0;
	struct bb_alloc *bb_alloc;
	int eof, offset;

	xassert(state_ptr);
	offset = get_buf_offset(buffer);
	pack32(rec_count,  buffer);
	if (!state_ptr->bb_ahash)
		return rec_count;

	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_alloc = state_ptr->bb_ahash[i];
		while (bb_alloc) {
			if ((uid == 0) || (uid == bb_alloc->user_id)) {
				_pack_alloc(bb_alloc, buffer, protocol_version);
				rec_count++;
			}
			bb_alloc = bb_alloc->next;
		}
	}
	if (rec_count != 0) {
		eof = get_buf_offset(buffer);
		set_buf_offset(buffer, offset);
		pack32(rec_count, buffer);
		set_buf_offset(buffer, eof);
	}

	return rec_count;
}

/* Pack state and configuration parameters into a buffer */
extern void bb_pack_state(bb_state_t *state_ptr, buf_t *buffer,
			  uint16_t protocol_version)
{
	bb_config_t *config_ptr = &state_ptr->bb_config;
	int i;


	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(config_ptr->allow_users_str, buffer);
		packstr(config_ptr->create_buffer,   buffer);
		packstr(config_ptr->default_pool,    buffer);
		packstr(config_ptr->deny_users_str,  buffer);
		packstr(config_ptr->destroy_buffer,  buffer);
		pack32(config_ptr->flags,            buffer);
		packstr(config_ptr->get_sys_state,   buffer);
		packstr(config_ptr->get_sys_status,   buffer);
		pack64(config_ptr->granularity,      buffer);
		pack32(config_ptr->pool_cnt,         buffer);
		for (i = 0; i < config_ptr->pool_cnt; i++) {
			packstr(config_ptr->pool_ptr[i].name, buffer);
			pack64(config_ptr->pool_ptr[i].total_space, buffer);
			pack64(config_ptr->pool_ptr[i].granularity, buffer);
			pack64(config_ptr->pool_ptr[i].unfree_space, buffer);
			pack64(config_ptr->pool_ptr[i].used_space, buffer);
		}
		pack32(config_ptr->other_timeout,    buffer);
		packstr(config_ptr->start_stage_in,  buffer);
		packstr(config_ptr->start_stage_out, buffer);
		packstr(config_ptr->stop_stage_in,   buffer);
		packstr(config_ptr->stop_stage_out,  buffer);
		pack32(config_ptr->stage_in_timeout, buffer);
		pack32(config_ptr->stage_out_timeout,buffer);
		pack64(state_ptr->total_space,       buffer);
		pack64(state_ptr->unfree_space,      buffer);
		pack64(state_ptr->used_space,        buffer);
		pack32(config_ptr->validate_timeout, buffer);
	}
}

/* Pack individual burst buffer usage records into a buffer (used for limits) */
extern int bb_pack_usage(uid_t uid, bb_state_t *state_ptr, buf_t *buffer,
			 uint16_t protocol_version)
{
	int i, rec_count = 0;
	bb_user_t *bb_usage;
	int eof, offset;

	xassert(state_ptr);
	offset = get_buf_offset(buffer);
	pack32(rec_count,  buffer);
	if (!state_ptr->bb_uhash)
		return rec_count;

	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_usage = state_ptr->bb_uhash[i];
		while (bb_usage) {
			if (((uid == 0) || (uid == bb_usage->user_id)) &&
			    (bb_usage->size != 0)) {
				pack64(bb_usage->size,          buffer);
				pack32(bb_usage->user_id,       buffer);
				rec_count++;
			}
			bb_usage = bb_usage->next;
		}
	}
	if (rec_count != 0) {
		eof = get_buf_offset(buffer);
		set_buf_offset(buffer, offset);
		pack32(rec_count, buffer);
		set_buf_offset(buffer, eof);
	}

	return rec_count;
}

/* Translate a burst buffer size specification in string form to numeric form,
 * recognizing various (case insensitive) sufficies:
 * K/KiB, M/MiB, G/GiB, T/TiB, P/PiB for powers of 1024
 * KB, MB, GB, TB, PB for powers of 1000
 * N/Node/Nodes will consider the size in nodes
 * Default units are bytes. */
extern uint64_t bb_get_size_num(char *tok, uint64_t granularity)
{
	char *tmp = NULL, *unit;
	uint64_t bb_size_i, mult;
	uint64_t bb_size_u = 0;

	errno = 0;
	bb_size_i = (uint64_t) strtoull(tok, &tmp, 10);
	if ((errno == ERANGE) || !bb_size_i || (tok == tmp)) {
		/*
		 * Either the value in tok was too big, zero was specified, or
		 * there were no numbers in tok.
		 */
		return 0;
	}
	bb_size_u = bb_size_i;
	if (tmp && !isspace(tmp[0])) {
		unit = xstrdup(tmp);
		strtok(unit, " ");
		if (!xstrcasecmp(unit, "n") ||
		    !xstrcasecmp(unit, "node") ||
		    !xstrcasecmp(unit, "nodes")) {
			bb_size_u |= BB_SIZE_IN_NODES;
			granularity = 1;
		} else if ((mult = suffix_mult(unit)) != NO_VAL64) {
			bb_size_u *= mult;
		}
		xfree(unit);
	}

	if (granularity > 1) {
		bb_size_u = ((bb_size_u + granularity - 1) / granularity) *
			    granularity;
	}

	return bb_size_u;
}

/* Translate a burst buffer size specification in numeric form to string form,
 * appending various sufficies (KiB, MiB, GB, TB, PB, and Nodes). Default units
 * are bytes. */
extern char *bb_get_size_str(uint64_t size)
{
	static char size_str[64];

	if (size == 0) {
		snprintf(size_str, sizeof(size_str), "%"PRIu64, size);
	} else if (size & BB_SIZE_IN_NODES) {
		size &= (~BB_SIZE_IN_NODES);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"N", size);

	} else if ((size % ((uint64_t)1024 * 1024 * 1024 * 1024 * 1024)) == 0) {
		size /= ((uint64_t)1024 * 1024 * 1024 * 1024 * 1024);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"PiB", size);
	} else if ((size % ((uint64_t)1000 * 1000 * 1000 * 1000 * 1000)) == 0) {
		size /= ((uint64_t)1000 * 1000 * 1000 * 1000 * 1000);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"PB", size);

	} else if ((size % ((uint64_t)1024 * 1024 * 1024 * 1024)) == 0) {
		size /= ((uint64_t)1024 * 1024 * 1024 * 1024);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"TiB", size);
	} else if ((size % ((uint64_t)1000 * 1000 * 1000 * 1000)) == 0) {
		size /= ((uint64_t)1000 * 1000 * 1000 * 1000);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"TB", size);

	} else if ((size % ((uint64_t)1024 * 1024 * 1024)) == 0) {
		size /= ((uint64_t)1024 * 1024 * 1024);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"GiB", size);
	} else if ((size % ((uint64_t)1000 * 1000 * 1000)) == 0) {
		size /= ((uint64_t)1000 * 1000 * 1000);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"GB", size);

	} else if ((size % ((uint64_t)1024 * 1024)) == 0) {
		size /= ((uint64_t)1024 * 1024);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"MiB", size);
	} else if ((size % ((uint64_t)1000 * 1000)) == 0) {
		size /= ((uint64_t)1000 * 1000);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"MB", size);

	} else if ((size % ((uint64_t)1024)) == 0) {
		size /= ((uint64_t)1024);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"KiB", size);
	} else if ((size % ((uint64_t)1000)) == 0) {
		size /= ((uint64_t)1000);
		snprintf(size_str, sizeof(size_str), "%"PRIu64"KB", size);

	} else {
		snprintf(size_str, sizeof(size_str), "%"PRIu64, size);
	}

	return size_str;
}

/* Round up a number based upon some granularity */
extern uint64_t bb_granularity(uint64_t start_size, uint64_t granularity)
{
	if (start_size) {
		start_size = start_size + granularity - 1;
		start_size /= granularity;
		start_size *= granularity;
	}
	return start_size;
}

extern void bb_job_queue_del(void *x)
{
	xfree(x);
}

/* Sort job queue by expected start time */
extern int bb_job_queue_sort(void *x, void *y)
{
	bb_job_queue_rec_t *job_rec1 = *(bb_job_queue_rec_t **) x;
	bb_job_queue_rec_t *job_rec2 = *(bb_job_queue_rec_t **) y;
	job_record_t *job_ptr1 = job_rec1->job_ptr;
	job_record_t *job_ptr2 = job_rec2->job_ptr;

	if (job_ptr1->start_time > job_ptr2->start_time)
		return 1;
	if (job_ptr1->start_time < job_ptr2->start_time)
		return -1;
	return 0;
}

/* Sort preempt_bb_recs in order of DECREASING use_time */
extern int bb_preempt_queue_sort(void *x, void *y)
{
	struct preempt_bb_recs *bb_ptr1 = *(struct preempt_bb_recs **) x;
	struct preempt_bb_recs *bb_ptr2 = *(struct preempt_bb_recs **) y;

	if (bb_ptr1->use_time > bb_ptr2->use_time)
		return -1;
	if (bb_ptr1->use_time < bb_ptr2->use_time)
		return 1;
	return 0;
};

/* For each burst buffer record, set the use_time to the time at which its
 * use is expected to begin (i.e. each job's expected start time) */
extern void bb_set_use_time(bb_state_t *state_ptr)
{
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc = NULL;
	time_t now = time(NULL);
	int i;

	state_ptr->next_end_time = now + 60 * 60; /* Start estimate now+1hour */
	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_alloc = state_ptr->bb_ahash[i];
		while (bb_alloc) {
			if (bb_alloc->job_id &&
			    ((bb_alloc->state == BB_STATE_STAGING_IN) ||
			     (bb_alloc->state == BB_STATE_STAGED_IN))) {
				job_ptr = find_job_record(bb_alloc->job_id);
				if (!job_ptr && !bb_alloc->orphaned) {
					bb_alloc->orphaned = true;
					error("%s: JobId=%u not found for allocated burst buffer",
					      __func__, bb_alloc->job_id);
					bb_alloc->use_time = now + 24 * 60 * 60;
				} else if (!job_ptr) {
					bb_alloc->use_time = now + 24 * 60 * 60;
				} else if (job_ptr->start_time) {
					bb_alloc->end_time = job_ptr->end_time;
					bb_alloc->use_time = job_ptr->start_time;
				} else {
					/* Unknown start time */
					bb_alloc->use_time = now + 60 * 60;
				}
			} else if (bb_alloc->job_id) {
				job_ptr = find_job_record(bb_alloc->job_id);
				if (job_ptr)
					bb_alloc->end_time = job_ptr->end_time;
			} else {
				bb_alloc->use_time = now;
			}
			if (bb_alloc->end_time && bb_alloc->size) {
				if (bb_alloc->end_time <= now)
					state_ptr->next_end_time = now;
				else if (state_ptr->next_end_time >
					 bb_alloc->end_time) {
					state_ptr->next_end_time =
						bb_alloc->end_time;
				}
			}
			bb_alloc = bb_alloc->next;
		}
	}
}

/* Sleep function, also handles termination signal */
extern void bb_sleep(bb_state_t *state_ptr, int add_secs)
{
	struct timespec ts = {0, 0};
	struct timeval  tv = {0, 0};

	if (gettimeofday(&tv, NULL)) {		/* Some error */
		sleep(1);
		return;
	}

	ts.tv_sec  = tv.tv_sec + add_secs;
	ts.tv_nsec = tv.tv_usec * 1000;
	slurm_mutex_lock(&state_ptr->term_mutex);
	if (!state_ptr->term_flag) {
		slurm_cond_timedwait(&state_ptr->term_cond,
				     &state_ptr->term_mutex, &ts);
	}
	slurm_mutex_unlock(&state_ptr->term_mutex);
}


/* Allocate a named burst buffer record for a specific user.
 * Return a pointer to that record.
 * Use bb_free_name_rec() to purge the returned record. */
extern bb_alloc_t *bb_alloc_name_rec(bb_state_t *state_ptr, char *name,
				     uint32_t user_id)
{
	bb_alloc_t *bb_alloc = NULL;
	time_t now = time(NULL);
	int i;

	xassert(state_ptr->bb_ahash);
	state_ptr->last_update_time = now;
	bb_alloc = xmalloc(sizeof(bb_alloc_t));
	i = user_id % BB_HASH_SIZE;
	bb_alloc->magic = BB_ALLOC_MAGIC;
	bb_alloc->next = state_ptr->bb_ahash[i];
	state_ptr->bb_ahash[i] = bb_alloc;
	bb_alloc->array_task_id = NO_VAL;
	bb_alloc->name = xstrdup(name);
	bb_alloc->state = BB_STATE_ALLOCATED;
	bb_alloc->state_time = now;
	bb_alloc->seen_time = now;
	bb_alloc->user_id = user_id;

	return bb_alloc;
}

/* Allocate a per-job burst buffer record for a specific job.
 * Return a pointer to that record.
 * Use bb_free_alloc_rec() to purge the returned record. */
extern bb_alloc_t *bb_alloc_job_rec(bb_state_t *state_ptr,
				    job_record_t *job_ptr,
				    bb_job_t *bb_job)
{
	bb_alloc_t *bb_alloc = NULL;
	int i;

	xassert(state_ptr->bb_ahash);
	xassert(job_ptr);
	state_ptr->last_update_time = time(NULL);
	bb_alloc = xmalloc(sizeof(bb_alloc_t));
	bb_alloc->account = xstrdup(bb_job->account);
	bb_alloc->array_job_id = job_ptr->array_job_id;
	bb_alloc->array_task_id = job_ptr->array_task_id;
	bb_alloc->assoc_ptr = job_ptr->assoc_ptr;
	bb_alloc->job_id = job_ptr->job_id;
	bb_alloc->magic = BB_ALLOC_MAGIC;
	i = job_ptr->user_id % BB_HASH_SIZE;
	xstrfmtcat(bb_alloc->name, "%u", job_ptr->job_id);
	bb_alloc->next = state_ptr->bb_ahash[i];
	bb_alloc->partition = xstrdup(bb_job->partition);
	bb_alloc->pool = xstrdup(bb_job->job_pool);
	bb_alloc->qos = xstrdup(bb_job->qos);
	state_ptr->bb_ahash[i] = bb_alloc;
	bb_alloc->size = bb_job->total_size;
	bb_alloc->state = BB_STATE_ALLOCATED;
	bb_alloc->state_time = time(NULL);
	bb_alloc->seen_time = time(NULL);
	bb_alloc->user_id = job_ptr->user_id;
	bb_alloc->group_id = job_ptr->group_id;

	return bb_alloc;
}

/* Allocate a burst buffer record for a job and increase the job priority
 * if so configured.
 * Use bb_free_alloc_rec() to purge the returned record. */
extern bb_alloc_t *bb_alloc_job(bb_state_t *state_ptr, job_record_t *job_ptr,
				bb_job_t *bb_job)
{
	bb_alloc_t *bb_alloc;

	bb_alloc = bb_alloc_job_rec(state_ptr, job_ptr, bb_job);

	return bb_alloc;
}

extern int bb_build_bb_script(job_record_t *job_ptr, char *script_file)
{
	char *out_buf = NULL;
	int rc;

	xstrcat(out_buf, "#!/bin/bash\n");
	xstrcat(out_buf, job_ptr->burst_buffer);
	rc = bb_write_file(script_file, out_buf);
	xfree(out_buf);

	return rc;
}

extern char *bb_common_build_het_job_script(char *script,
					    uint32_t het_job_offset,
					    bool (*is_directive) (char *tok))
{
	char *result = NULL, *tmp = NULL;
	char *tok, *save_ptr = NULL;
	bool fini = false;
	int cur_offset = 0;

	tmp = xstrdup(script);
	tok = strtok_r(tmp, "\n", &save_ptr);
	while (tok) {
		if (!result) {
			xstrfmtcat(result, "%s\n", tok);
		} else if (tok[0] != '#') {
			fini = true;
		} else if (_hetjob_check(tok)) {
			cur_offset++;
			if (cur_offset > het_job_offset)
				fini = true;
		} else if (cur_offset == het_job_offset) {
			xstrfmtcat(result, "%s\n", tok);
		}
		if (fini)
			break;
		tok = strtok_r(NULL, "\n", &save_ptr);
	}

	if (het_job_offset == 0) {
		while (tok) {
			char *sep = "";
			if (is_directive(tok)) {
				sep = "#EXCLUDED ";
				tok++;
			}
			xstrfmtcat(result, "%s%s\n", sep, tok);
			tok = strtok_r(NULL, "\n", &save_ptr);
		}
	} else if (result) {
		xstrcat(result, "exit 0\n");
	}
	xfree(tmp);

	return result;
}

/* Free memory associated with allocated bb record, caller is responsible for
 * maintaining linked list */
extern void bb_free_alloc_buf(bb_alloc_t *bb_alloc)
{
	if (bb_alloc) {
		xassert(bb_alloc->magic == BB_ALLOC_MAGIC);
		bb_alloc->magic = 0;
		xfree(bb_alloc->account);
		xfree(bb_alloc->assocs);
		xfree(bb_alloc->name);
		xfree(bb_alloc->partition);
		xfree(bb_alloc->pool);
		xfree(bb_alloc->qos);
		xfree(bb_alloc);
	}
}


/* Remove a specific bb_alloc_t from global records.
 * RET true if found, false otherwise */
extern bool bb_free_alloc_rec(bb_state_t *state_ptr, bb_alloc_t *bb_alloc)
{
	bb_alloc_t *bb_link, **bb_plink;
	int i;

	xassert(state_ptr);
	xassert(state_ptr->bb_ahash);
	xassert(bb_alloc);

	i = bb_alloc->user_id % BB_HASH_SIZE;
	bb_plink = &state_ptr->bb_ahash[i];
	bb_link = state_ptr->bb_ahash[i];
	while (bb_link) {
		if (bb_link == bb_alloc) {
			xassert(bb_link->magic == BB_ALLOC_MAGIC);
			*bb_plink = bb_alloc->next;
			bb_free_alloc_buf(bb_alloc);
			state_ptr->last_update_time = time(NULL);
			return true;
		}
		bb_plink = &bb_link->next;
		bb_link = bb_link->next;
	}
	return false;
}

/* Allocate a bb_job_t record, hashed by job_id, delete with bb_job_del() */
extern bb_job_t *bb_job_alloc(bb_state_t *state_ptr, uint32_t job_id)
{
	int inx = job_id % BB_HASH_SIZE;
	bb_job_t *bb_job = xmalloc(sizeof(bb_job_t));

	xassert(state_ptr);
	bb_job->magic = BB_JOB_MAGIC;
	bb_job->next = state_ptr->bb_jhash[inx];
	bb_job->job_id = job_id;
	/*
	 * Zero or positive integers are valid file descriptors. We need
	 * to initialize this to an invalid file descriptor so when we delete
	 * bb_job we can check if we ever opened a file.
	 */
	bb_job->memfd = -1;
	state_ptr->bb_jhash[inx] = bb_job;

	return bb_job;
}

/* Return a pointer to the existing bb_job_t record for a given job_id or
 * NULL if not found */
extern bb_job_t *bb_job_find(bb_state_t *state_ptr, uint32_t job_id)
{
	bb_job_t *bb_job;

	xassert(state_ptr);

	if (!state_ptr->bb_jhash)
		return NULL;

	bb_job = state_ptr->bb_jhash[job_id % BB_HASH_SIZE];
	while (bb_job) {
		if (bb_job->job_id == job_id) {
			xassert(bb_job->magic == BB_JOB_MAGIC);
			return bb_job;
		}
		bb_job = bb_job->next;
	}

	return bb_job;
}

/* Delete a bb_job_t record, hashed by job_id */
extern void bb_job_del(bb_state_t *state_ptr, uint32_t job_id)
{
	int inx = job_id % BB_HASH_SIZE;
	bb_job_t *bb_job, **bb_pjob;

	xassert(state_ptr);
	bb_pjob = &state_ptr->bb_jhash[inx];
	bb_job  =  state_ptr->bb_jhash[inx];
	while (bb_job) {
		if (bb_job->job_id == job_id) {
			xassert(bb_job->magic == BB_JOB_MAGIC);
			bb_job->magic = 0;
			*bb_pjob = bb_job->next;
			_bb_job_del2(bb_job);
			return;
		}
		bb_pjob = &bb_job->next;
		bb_job  =  bb_job->next;
	}
}

/* Delete a bb_job_t record. DOES NOT UNLINK FROM HASH TABLE */
static void _bb_job_del2(bb_job_t *bb_job)
{
	int i;

	if (bb_job) {
		/*
		 * We only set memfd if symbol replacement was used, so only
		 * attempt to close it if it was set.
		 */
		if (bb_job->memfd != -1)
			(void) close(bb_job->memfd);

		xfree(bb_job->account);
		for (i = 0; i < bb_job->buf_cnt; i++) {
			xfree(bb_job->buf_ptr[i].access);
			xfree(bb_job->buf_ptr[i].name);
			xfree(bb_job->buf_ptr[i].pool);
			xfree(bb_job->buf_ptr[i].type);
		}
		xfree(bb_job->buf_ptr);
		xfree(bb_job->job_pool);
		xfree(bb_job->memfd_path);
		xfree(bb_job->partition);
		xfree(bb_job->qos);
		xfree(bb_job);
	}
}

/* Log the contents of a bb_job_t record using "info()" */
extern void bb_job_log(bb_state_t *state_ptr, bb_job_t *bb_job)
{
	bb_buf_t *buf_ptr;
	char *out_buf = NULL;
	int i;

	if (bb_job) {
		xstrfmtcat(out_buf, "%s: JobId=%u UserID:%u ",
			   state_ptr->name, bb_job->job_id, bb_job->user_id);
		xstrfmtcat(out_buf, "Swap:%ux%u ", bb_job->swap_size,
			   bb_job->swap_nodes);
		xstrfmtcat(out_buf, "TotalSize:%"PRIu64"", bb_job->total_size);
		info("%s", out_buf);
		xfree(out_buf);
		for (i = 0, buf_ptr = bb_job->buf_ptr; i < bb_job->buf_cnt;
		     i++, buf_ptr++) {
			if (buf_ptr->create) {
				info("  Create  Name:%s Pool:%s Size:%"PRIu64
				     " Access:%s Type:%s State:%s",
				     buf_ptr->name, buf_ptr->pool,
				     buf_ptr->size, buf_ptr->access,
				     buf_ptr->type,
				     bb_state_string(buf_ptr->state));
			} else if (buf_ptr->destroy) {
				info("  Destroy Name:%s Hurry:%d",
				     buf_ptr->name, (int) buf_ptr->hurry);
			} else {
				info("  Use  Name:%s", buf_ptr->name);
			}
		}
	}
}

/* Make claim against resource limit for a user
 * user_id IN - Owner of burst buffer
 * bb_size IN - Size of burst buffer
 * pool IN - Pool containing the burst buffer
 * state_ptr IN - Global state to update
 * update_pool_unfree IN - If true, update the pool's unfree space */
extern void bb_limit_add(uint32_t user_id, uint64_t bb_size, char *pool,
			 bb_state_t *state_ptr, bool update_pool_unfree)
{
	burst_buffer_pool_t *pool_ptr;
	bb_user_t *bb_user;
	int i;

	/* Update the pool's used_space, plus unfree_space if needed */
	if (!pool || !xstrcmp(pool, state_ptr->bb_config.default_pool)) {
		state_ptr->used_space += bb_size;
		if (update_pool_unfree)
			state_ptr->unfree_space += bb_size;
	} else {
		pool_ptr = state_ptr->bb_config.pool_ptr;
		for (i = 0; i < state_ptr->bb_config.pool_cnt; i++, pool_ptr++){
			if (xstrcmp(pool, pool_ptr->name))
				continue;
			pool_ptr->used_space += bb_size;
			if (update_pool_unfree)
				pool_ptr->unfree_space += bb_size;
			break;
		}
		if (i >= state_ptr->bb_config.pool_cnt)
			error("%s: Unable to located pool %s", __func__, pool);
	}

	/* Update user space used */
	bb_user = bb_find_user_rec(user_id, state_ptr);
	xassert(bb_user);
	bb_user->size += bb_size;

}

/* Release claim against resource limit for a user */
extern void bb_limit_rem(uint32_t user_id, uint64_t bb_size, char *pool,
			 bb_state_t *state_ptr)
{
	burst_buffer_pool_t *pool_ptr;
	bb_user_t *bb_user;
	int i;

	if (!pool || !xstrcmp(pool, state_ptr->bb_config.default_pool)) {
		if (state_ptr->used_space >= bb_size) {
			state_ptr->used_space -= bb_size;
		} else {
			error("%s: used_space underflow", __func__);
			state_ptr->used_space = 0;
		}
		if (state_ptr->unfree_space >= bb_size) {
			state_ptr->unfree_space -= bb_size;
		} else {
			/*
			 * This will happen if we reload burst buffer state
			 * after making a claim against resources, but before
			 * the buffer actually gets created.
			 */
			debug2("%s: unfree_space underflow (%"PRIu64" < %"PRIu64")",
			        __func__, state_ptr->unfree_space, bb_size);
			state_ptr->unfree_space = 0;
		}
	} else {
		pool_ptr = state_ptr->bb_config.pool_ptr;
		for (i = 0; i < state_ptr->bb_config.pool_cnt; i++, pool_ptr++){
			if (xstrcmp(pool, pool_ptr->name))
				continue;
			if (pool_ptr->used_space >= bb_size) {
				pool_ptr->used_space -= bb_size;
			} else {
				error("%s: used_space underflow for pool %s",
				      __func__, pool);
				pool_ptr->used_space = 0;
			}
			if (pool_ptr->unfree_space >= bb_size) {
				pool_ptr->unfree_space -= bb_size;
			} else {
				/*
				 * This will happen if we reload burst buffer
				 * state after making a claim against resources,
				 * but before the buffer actually gets created.
				 */
				debug2("%s: unfree_space underflow for pool %s",
				       __func__, pool);
				pool_ptr->unfree_space = 0;
			}
			break;
		}
		if (i >= state_ptr->bb_config.pool_cnt)
			error("%s: Unable to located pool %s", __func__, pool);
	}

	bb_user = bb_find_user_rec(user_id, state_ptr);
	xassert(bb_user);
	if (bb_user->size >= bb_size)
		bb_user->size -= bb_size;
	else {
		bb_user->size = 0;
		error("%s: user limit underflow for uid %u", __func__, user_id);
	}

}

/* Log creation of a persistent burst buffer in the database
 * job_ptr IN - Point to job that created, could be NULL at startup
 * bb_alloc IN - Pointer to persistent burst buffer state info
 * state_ptr IN - Pointer to burst_buffer plugin state info
 * NOTE: assoc_mgr association and qos read lock should be set before this.
 */
extern int bb_post_persist_create(job_record_t *job_ptr, bb_alloc_t *bb_alloc,
				  bb_state_t *state_ptr)
{
	int rc = SLURM_SUCCESS;
	slurmdb_reservation_rec_t resv;
	uint64_t size_mb;

	if (!state_ptr->tres_id) {
		debug2("%s: Not tracking this TRES, "
		       "not sending to the database.", __func__);
		return SLURM_SUCCESS;
	}

	size_mb = (bb_alloc->size / (1024 * 1024));

	memset(&resv, 0, sizeof(slurmdb_reservation_rec_t));
	resv.assocs = bb_alloc->assocs;
	resv.cluster = slurm_conf.cluster_name;
	resv.name = bb_alloc->name;
	resv.id = bb_alloc->id;
	resv.time_start = bb_alloc->create_time;
	xstrfmtcat(resv.tres_str, "%d=%"PRIu64, state_ptr->tres_id, size_mb);
	rc = acct_storage_g_add_reservation(acct_db_conn, &resv);
	xfree(resv.tres_str);

	if (state_ptr->tres_pos > 0) {
		slurmdb_assoc_rec_t *assoc_ptr = bb_alloc->assoc_ptr;

		while (assoc_ptr) {
			assoc_ptr->usage->grp_used_tres[state_ptr->tres_pos] +=
				size_mb;
			debug2("%s: after adding persistent bb %s(%u), "
			       "assoc %u(%s/%s/%s) grp_used_tres(%s) "
			       "is %"PRIu64,
			       __func__, bb_alloc->name, bb_alloc->id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[state_ptr->tres_pos],
			       assoc_ptr->usage->
			       grp_used_tres[state_ptr->tres_pos]);

			/* FIXME: should grp_used_tres_run_secs be
			 * done some how? Same for QOS below.
			 */
			/* debug2("%s: after adding persistent bb %s(%u), " */
			/*        "assoc %u(%s/%s/%s) grp_used_tres_run_secs(%s) " */
			/*        "is %"PRIu64, */
			/*        __func__, bb_alloc->name, bb_alloc->id, */
			/*        assoc_ptr->id, assoc_ptr->acct, */
			/*        assoc_ptr->user, assoc_ptr->partition, */
			/*        assoc_mgr_tres_name_array[state_ptr->tres_pos], */
			/*        assoc_ptr->usage-> */
			/*        grp_used_tres_run_secs[state_ptr->tres_pos]); */
			assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		}

		if (job_ptr && job_ptr->tres_alloc_cnt)
			job_ptr->tres_alloc_cnt[state_ptr->tres_pos] -= size_mb;

		if (bb_alloc->qos_ptr) {
			bb_alloc->qos_ptr->usage->grp_used_tres[
				state_ptr->tres_pos] += size_mb;
		}
	}

	return rc;
}

/* Log deletion of a persistent burst buffer in the database */
extern int bb_post_persist_delete(bb_alloc_t *bb_alloc, bb_state_t *state_ptr)
{
	int rc = SLURM_SUCCESS;
	slurmdb_reservation_rec_t resv;
	uint64_t size_mb;

	if (!state_ptr->tres_id) {
		debug2("%s: Not tracking this TRES, "
		       "not sending to the database.", __func__);
		return SLURM_SUCCESS;
	}

	size_mb = (bb_alloc->size / (1024 * 1024));

	memset(&resv, 0, sizeof(slurmdb_reservation_rec_t));
	resv.assocs = bb_alloc->assocs;
	resv.cluster = slurm_conf.cluster_name;
	resv.name = bb_alloc->name;
	resv.id = bb_alloc->id;
	resv.time_end = time(NULL);
	resv.time_start = bb_alloc->create_time;
	xstrfmtcat(resv.tres_str, "%d=%"PRIu64, state_ptr->tres_id, size_mb);

	rc = acct_storage_g_remove_reservation(acct_db_conn, &resv);
	xfree(resv.tres_str);

	if (state_ptr->tres_pos > 0) {
		slurmdb_assoc_rec_t *assoc_ptr = bb_alloc->assoc_ptr;

		while (assoc_ptr) {
			if (assoc_ptr->usage->grp_used_tres[state_ptr->tres_pos]
			    >= size_mb) {
				assoc_ptr->usage->grp_used_tres[
					state_ptr->tres_pos] -= size_mb;
				debug2("%s: after removing persistent "
				       "bb %s(%u), assoc %u(%s/%s/%s) "
				       "grp_used_tres(%s) is %"PRIu64,
				       __func__, bb_alloc->name, bb_alloc->id,
				       assoc_ptr->id, assoc_ptr->acct,
				       assoc_ptr->user, assoc_ptr->partition,
				       assoc_mgr_tres_name_array[
					       state_ptr->tres_pos],
				       assoc_ptr->usage->
				       grp_used_tres[state_ptr->tres_pos]);
			} else {
				error("%s: underflow removing persistent "
				      "bb %s(%u), assoc %u(%s/%s/%s) "
				      "grp_used_tres(%s) had %"PRIu64
				      " but we are trying to remove %"PRIu64,
				      __func__, bb_alloc->name, bb_alloc->id,
				      assoc_ptr->id, assoc_ptr->acct,
				      assoc_ptr->user, assoc_ptr->partition,
				      assoc_mgr_tres_name_array[
					      state_ptr->tres_pos],
				      assoc_ptr->usage->
				      grp_used_tres[state_ptr->tres_pos],
				      size_mb);
				assoc_ptr->usage->grp_used_tres[
					state_ptr->tres_pos] = 0;
			}

			/* FIXME: should grp_used_tres_run_secs be
			 * done some how? Same for QOS below. */
			/* debug2("%s: after removing persistent bb %s(%u), " */
			/*        "assoc %u(%s/%s/%s) grp_used_tres_run_secs(%s) " */
			/*        "is %"PRIu64, */
			/*        __func__, bb_alloc->name, bb_alloc->id, */
			/*        assoc_ptr->id, assoc_ptr->acct, */
			/*        assoc_ptr->user, assoc_ptr->partition, */
			/*        assoc_mgr_tres_name_array[state_ptr->tres_pos], */
			/*        assoc_ptr->usage-> */
			/*        grp_used_tres_run_secs[state_ptr->tres_pos]); */
			assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		}

		if (bb_alloc->qos_ptr) {
			if (bb_alloc->qos_ptr->usage->grp_used_tres[
				    state_ptr->tres_pos] >= size_mb)
				bb_alloc->qos_ptr->usage->grp_used_tres[
					state_ptr->tres_pos] -= size_mb;
			else
				bb_alloc->qos_ptr->usage->grp_used_tres[
					state_ptr->tres_pos] = 0;
		}
	}

	return rc;
}

/* Reduced burst buffer space in advanced reservation for resources already
 * allocated to jobs. What's left is space reserved for future jobs */
static void _rm_active_job_bb(char *resv_name, char **pool_name,
			      int64_t *resv_space, int ds_len,
			      bb_state_t *bb_state)
{
	ListIterator job_iterator;
	job_record_t *job_ptr;
	bb_job_t *bb_job;
	int i;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if ((job_ptr->burst_buffer == NULL) ||
		    (job_ptr->burst_buffer[0] == '\0') ||
		    (xstrcmp(job_ptr->resv_name, resv_name) == 0))
			continue;
		bb_job = bb_job_find(bb_state,job_ptr->job_id);
		if (!bb_job || (bb_job->state <= BB_STATE_PENDING) ||
		    (bb_job->state >= BB_STATE_COMPLETE))
			continue;
		for (i = 0; i < ds_len; i++) {
			if (xstrcmp(bb_job->job_pool, pool_name[i]))
				continue;
			if (resv_space[i] >= bb_job->total_size)
				resv_space[i] -= bb_job->total_size;
			else
				resv_space[i] = 0;
			break;
		}
	}
	list_iterator_destroy(job_iterator);
}

extern int bb_test_size_limit(job_record_t *job_ptr, bb_job_t *bb_job,
			      bb_state_t *bb_state_ptr,
			      void (*preempt_func) (uint32_t job_id,
						    uint32_t user_id,
						    bool hurry) )
{
	int64_t *add_space = NULL, *avail_space = NULL, *granularity = NULL;
	int64_t *preempt_space = NULL, *resv_space = NULL, *total_space = NULL;
	uint64_t unfree_space;
	burst_buffer_info_msg_t *resv_bb = NULL;
	struct preempt_bb_recs *preempt_ptr = NULL;
	char **pool_name, *my_pool;
	int ds_len;
	burst_buffer_pool_t *pool_ptr;
	bb_buf_t *buf_ptr;
	bb_alloc_t *bb_ptr = NULL;
	int i, j, k, rc = BB_CAN_START_NOW;
	bool avail_ok, do_preempt, preempt_ok;
	time_t now = time(NULL);
	List preempt_list = NULL;
	ListIterator preempt_iter;
	bb_state_t bb_state = *bb_state_ptr;

	xassert(bb_job);

	/* Initialize data structure */
	ds_len = bb_state.bb_config.pool_cnt + 1;
	add_space = xcalloc(ds_len, sizeof(int64_t));
	avail_space = xcalloc(ds_len, sizeof(int64_t));
	granularity = xcalloc(ds_len, sizeof(int64_t));
	pool_name = xcalloc(ds_len, sizeof(char *));
	preempt_space = xcalloc(ds_len, sizeof(int64_t));
	resv_space = xcalloc(ds_len, sizeof(int64_t));
	total_space = xcalloc(ds_len, sizeof(int64_t));
	for (i = 0, pool_ptr = bb_state.bb_config.pool_ptr;
	     i < bb_state.bb_config.pool_cnt; i++, pool_ptr++) {
		unfree_space = MAX(pool_ptr->used_space,
				   pool_ptr->unfree_space);
		if (pool_ptr->total_space >= unfree_space)
			avail_space[i] = pool_ptr->total_space - unfree_space;
		granularity[i] = pool_ptr->granularity;
		pool_name[i] = pool_ptr->name;
		total_space[i] = pool_ptr->total_space;
	}
	unfree_space = MAX(bb_state.used_space, bb_state.unfree_space);
	if (bb_state.total_space - unfree_space)
		avail_space[i] = bb_state.total_space - unfree_space;
	granularity[i] = bb_state.bb_config.granularity;
	pool_name[i] = bb_state.bb_config.default_pool;
	total_space[i] = bb_state.total_space;

	/* Determine job size requirements by pool */
	if (bb_job->total_size) {
		for (j = 0; j < ds_len; j++) {
			if (!xstrcmp(bb_job->job_pool, pool_name[j])) {
				add_space[j] += bb_granularity(
							bb_job->total_size,
							granularity[j]);
				break;
			}
		}
	}
	for (i = 0, buf_ptr = bb_job->buf_ptr; i < bb_job->buf_cnt;
	     i++, buf_ptr++) {
		if (!buf_ptr->create || (buf_ptr->state >= BB_STATE_ALLOCATING))
			continue;
		for (j = 0; j < ds_len; j++) {
			if (!xstrcmp(buf_ptr->pool, pool_name[j])) {
				add_space[j] += bb_granularity(buf_ptr->size,
							       granularity[j]);
				break;
			}
		}
	}

	/* Account for reserved resources. Reduce reservation size for
	 * resources already claimed from the reservation. Assume node reboot
	 * required since we have not selected the compute nodes yet. */
	resv_bb = job_test_bb_resv(job_ptr, now, true);
	if (resv_bb) {
		burst_buffer_info_t *resv_bb_ptr;
		for (i = 0, resv_bb_ptr = resv_bb->burst_buffer_array;
		     i < resv_bb->record_count; i++, resv_bb_ptr++) {
			if (xstrcmp(resv_bb_ptr->name, bb_state.name))
				continue;
			for (j = 0, pool_ptr = resv_bb_ptr->pool_ptr;
			     j < resv_bb_ptr->pool_cnt; j++, pool_ptr++) {
				if (pool_ptr->name) {
					my_pool = pool_ptr->name;
				} else {
					my_pool =
						bb_state.bb_config.default_pool;
				}
				unfree_space = MAX(pool_ptr->used_space,
						   pool_ptr->unfree_space);
				for (k = 0; k < ds_len; k++) {
					if (xstrcmp(my_pool, pool_name[k]))
						continue;
					resv_space[k] += bb_granularity(
							unfree_space,
							granularity[k]);
					break;
				}
			}
			if (resv_bb_ptr->used_space) {
				/* Pool not specified, use default */
				my_pool = bb_state.bb_config.default_pool;
				for (k = 0; k < ds_len; k++) {
					if (xstrcmp(my_pool, pool_name[k]))
						continue;
					resv_space[k] += bb_granularity(
							resv_bb_ptr->used_space,
							granularity[k]);
					break;
				}
			}
#if 1
			/* Is any of this reserved space already taken? */
			_rm_active_job_bb(job_ptr->resv_name,
					  pool_name, resv_space, ds_len,
					  bb_state_ptr);
#endif
		}
	}

#if _DEBUG
	info("TEST_SIZE_LIMIT for %pJ", job_ptr);
	for (j = 0; j < ds_len; j++) {
		info("POOL:%s ADD:%"PRIu64" AVAIL:%"PRIu64
		     " GRANULARITY:%"PRIu64" RESV:%"PRIu64" TOTAL:%"PRIu64,
		     pool_name[j], add_space[j], avail_space[j], granularity[j],
		     resv_space[j], total_space[j]);
	}
#endif

	/* Determine if resources currently are available for the job */
	avail_ok = true;
	for (j = 0; j < ds_len; j++) {
		if (add_space[j] > total_space[j]) {
			rc = BB_EXCEEDS_LIMITS;
			goto fini;
		}
		if ((add_space[j] + resv_space[j]) > avail_space[j])
			avail_ok = false;
	}
	if (avail_ok) {
		rc = BB_CAN_START_NOW;
		goto fini;
	}
	rc = BB_NOT_ENOUGH_RESOURCES;

	if (!preempt_func) {
		/* Cannot preempt any burst buffers. */
		goto fini;
	}

	/* Identify candidate burst buffers to revoke for higher priority job */
	preempt_list = list_create(bb_job_queue_del);
	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_ptr = bb_state.bb_ahash[i];
		while (bb_ptr) {
			if ((bb_ptr->job_id != 0) &&
			    ((bb_ptr->name == NULL) ||
			     ((bb_ptr->name[0] >= '0') &&
			      (bb_ptr->name[0] <= '9'))) &&
			    (bb_ptr->use_time > now) &&
			    (bb_ptr->use_time > job_ptr->start_time)) {
				if (!bb_ptr->pool) {
					bb_ptr->name = xstrdup(
						bb_state.bb_config.default_pool);
				}
				preempt_ptr = xmalloc(sizeof(
						struct preempt_bb_recs));
				preempt_ptr->bb_ptr = bb_ptr;
				preempt_ptr->job_id = bb_ptr->job_id;
				preempt_ptr->pool = bb_ptr->name;
				preempt_ptr->size = bb_ptr->size;
				preempt_ptr->use_time = bb_ptr->use_time;
				preempt_ptr->user_id = bb_ptr->user_id;
				list_push(preempt_list, preempt_ptr);

				for (j = 0; j < ds_len; j++) {
					if (xstrcmp(bb_ptr->name, pool_name[j]))
						continue;
					preempt_ptr->size = bb_granularity(
								bb_ptr->size,
								granularity[j]);
					preempt_space[j] += preempt_ptr->size;
					break;
				}
			}
			bb_ptr = bb_ptr->next;
		}
	}

#if _DEBUG
	for (j = 0; j < ds_len; j++) {
		info("POOL:%s ADD:%"PRIu64" AVAIL:%"PRIu64
		     " GRANULARITY:%"PRIu64" PREEMPT:%"PRIu64
		     " RESV:%"PRIu64" TOTAL:%"PRIu64,
		     pool_name[j], add_space[j], avail_space[j], granularity[j],
		     preempt_space[j], resv_space[j], total_space[j]);
	}
#endif

	/* Determine if sufficient resources available after preemption */
	preempt_ok = true;
	for (j = 0; j < ds_len; j++) {
		if ((add_space[j] + resv_space[j]) >
		    (avail_space[j] + preempt_space[j])) {
			preempt_ok = false;
			break;
		}
	}
	if (!preempt_ok)
		goto fini;

	/* Now preempt/teardown the most appropriate buffers */
	list_sort(preempt_list, bb_preempt_queue_sort);
	preempt_iter = list_iterator_create(preempt_list);
	while ((preempt_ptr = list_next(preempt_iter))) {
		do_preempt = false;
		for (j = 0; j < ds_len; j++) {
			if (xstrcmp(preempt_ptr->pool, pool_name[j]))
				continue;
			if ((add_space[j] + resv_space[j]) > avail_space[j]) {
				avail_space[j] += preempt_ptr->size;
				preempt_space[j] -= preempt_ptr->size;
				do_preempt = true;
			}
			break;
		}
		if (do_preempt) {
			preempt_ptr->bb_ptr->cancelled = true;
			preempt_ptr->bb_ptr->end_time = 0;
			preempt_ptr->bb_ptr->state = BB_STATE_TEARDOWN;
			preempt_ptr->bb_ptr->state_time = time(NULL);
			preempt_func(preempt_ptr->job_id,
				     preempt_ptr->user_id, true);
			log_flag(BURST_BUF, "Preempting stage-in of JobId=%u for %pJ",
				 preempt_ptr->job_id,
				 job_ptr);
		}

	}
	list_iterator_destroy(preempt_iter);

fini:	xfree(add_space);
	xfree(avail_space);
	xfree(granularity);
	xfree(pool_name);
	xfree(preempt_space);
	xfree(resv_space);
	xfree(total_space);
	if (resv_bb)
		slurm_free_burst_buffer_info_msg(resv_bb);
	FREE_NULL_LIST(preempt_list);
	return rc;
}

extern void bb_update_system_comment(job_record_t *job_ptr, char *operation,
				     char *resp_msg, bool update_database)
{
	char *sep = NULL;

	if (job_ptr->system_comment &&
	    (strlen(job_ptr->system_comment) >= 1024)) {
		/* Avoid filling comment with repeated BB failures */
		return;
	}

	if (job_ptr->system_comment)
		xstrftimecat(sep, "\n%x %X");
	else
		xstrftimecat(sep, "%x %X");
	xstrfmtcat(job_ptr->system_comment, "%s %s: %s: %s",
		   sep, plugin_type, operation, resp_msg);
	xfree(sep);

	if (update_database) {
		slurmdb_job_cond_t job_cond;
		slurmdb_job_rec_t job_rec;
		slurm_selected_step_t selected_step;
		List ret_list;

		memset(&job_cond, 0, sizeof(slurmdb_job_cond_t));
		memset(&job_rec, 0, sizeof(slurmdb_job_rec_t));
		memset(&selected_step, 0, sizeof(slurm_selected_step_t));

		selected_step.array_task_id = NO_VAL;
		selected_step.step_id.job_id = job_ptr->job_id;
		selected_step.het_job_offset = NO_VAL;
		selected_step.step_id.step_id = NO_VAL;
		selected_step.step_id.step_het_comp = NO_VAL;
		job_cond.step_list = list_create(NULL);
		list_append(job_cond.step_list, &selected_step);

		job_cond.flags = JOBCOND_FLAG_NO_WAIT |
			JOBCOND_FLAG_NO_DEFAULT_USAGE;

		job_cond.cluster_list = list_create(NULL);
		list_append(job_cond.cluster_list, slurm_conf.cluster_name);

		job_cond.usage_start = job_ptr->details->submit_time;

		job_rec.system_comment = job_ptr->system_comment;

		ret_list = acct_storage_g_modify_job(acct_db_conn,
		                                     slurm_conf.slurm_user_id,
		                                     &job_cond, &job_rec);

		FREE_NULL_LIST(job_cond.cluster_list);
		FREE_NULL_LIST(job_cond.step_list);
		FREE_NULL_LIST(ret_list);
	}
}


/* Determine if the specified pool name is valid on this system */
extern bool bb_valid_pool_test(bb_state_t *state_ptr, char *pool_name)
{
	burst_buffer_pool_t *pool_ptr;
	int i;

	xassert(state_ptr);
	if (!pool_name)
		return true;
	if (!xstrcmp(pool_name, state_ptr->bb_config.default_pool))
		return true;
	pool_ptr = state_ptr->bb_config.pool_ptr;
	for (i = 0; i < state_ptr->bb_config.pool_cnt; i++, pool_ptr++) {
		if (!xstrcmp(pool_name, pool_ptr->name))
			return true;
	}
	info("%s: Invalid pool requested (%s)", __func__, pool_name);

	return false;
}

extern int bb_write_file(char *file_name, char *buf)
{
	int fd, nwrite;

	(void) unlink(file_name);
	fd = creat(file_name, 0600);
	if (fd < 0) {
		error("Error creating file %s, %m", file_name);
		return errno;
	}

	if (!buf) {
		error("buf is NULL");
		return SLURM_ERROR;
	}

	nwrite = strlen(buf);
	safe_write(fd, buf, nwrite);

	(void) close(fd);
	return SLURM_SUCCESS;

rwfail:
	error("Error writing file %s: %m", file_name);
	(void) close(fd);
	return SLURM_ERROR;
}

extern int bb_write_nid_file(char *file_name, char *node_list,
			     job_record_t *job_ptr)
{
#if defined(HAVE_NATIVE_CRAY)
	char *tmp, *sep, *buf = NULL;
	int i, j, rc;

	xassert(file_name);
	tmp = xstrdup(node_list);
	/* Remove any trailing "]" */
	sep = strrchr(tmp, ']');
	if (sep)
		sep[0] = '\0';
	/* Skip over "nid[" or "nid" */
	sep = strchr(tmp, '[');
	if (sep) {
		sep++;
	} else {
		sep = tmp;
		for (i = 0; !isdigit(sep[0]) && sep[0]; i++)
			sep++;
	}
	/* Copy numeric portion */
	buf = xmalloc(strlen(sep) + 1);
	for (i = 0, j = 0; sep[i]; i++) {
		/* Skip leading zeros */
		if ((sep[i] == '0') && isdigit(sep[i+1]))
			continue;
		/* Copy significant digits and separator */
		while (sep[i]) {
			if (sep[i] == ',') {
				buf[j++] = '\n';
				break;
			}
			buf[j++] = sep[i];
			if (sep[i] == '-')
				break;
			i++;
		}
		if (!sep[i])
			break;
	}
	xfree(tmp);

	if (buf[0]) {
		rc = bb_write_file(file_name, buf);
	} else {
		error("%pJ has node list without numeric component (%s)",
		      job_ptr, node_list);
		rc = EINVAL;
	}
	xfree(buf);
	return rc;
#else
	char *tok, *buf = NULL;
	int rc;

	xassert(file_name);
	if (node_list && node_list[0]) {
		hostlist_t hl = hostlist_create(node_list);
		while ((tok = hostlist_shift(hl))) {
			xstrfmtcat(buf, "%s\n", tok);
			free(tok);
		}
		hostlist_destroy(hl);
		rc = bb_write_file(file_name, buf);
		xfree(buf);
	} else {
		error("%pJ lacks a node list",
		      job_ptr);
		rc = EINVAL;
	}
	return rc;
#endif
}

extern void bb_write_state_file(char* old_file, char *reg_file, char *new_file,
				const char *plugin, buf_t *buffer,
				int buffer_size, time_t save_time,
				time_t *last_save_time)
{
	int state_fd, error_code = 0;

	state_fd = creat(new_file, 0600);
	if (state_fd < 0) {
		error("Can't save state, error creating file %s, %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount, rc;
		char *data = (char *)get_buf_data(buffer);
		buffer_size = MAX(nwrite, buffer_size);
		while (nwrite > 0) {
			amount = write(state_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}

		rc = fsync_and_close(state_fd, plugin);
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code)
		(void) unlink(new_file);
	else {
		/* file shuffle */
		*last_save_time = save_time;
		(void) unlink(old_file);
		if (link(reg_file, old_file)) {
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		}
		(void) unlink(reg_file);
		if (link(new_file, reg_file)) {
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		}
		(void) unlink(new_file);
	}
}
