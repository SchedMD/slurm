/*****************************************************************************\
 *  burst_buffer_generic.c - Generic library for managing a burst_buffer
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/plugins/burst_buffer/common/burst_buffer_common.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "burst_buffer" for SLURM burst_buffer) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will only
 * load a burst_buffer plugin if the plugin_type string has a prefix of
 * "burst_buffer/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "burst_buffer generic plugin";
const char plugin_type[]        = "burst_buffer/generic";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/* Most state information is in a common structure so that we can more
 * easily use common functions from multiple burst buffer plugins */
static bb_state_t 	bb_state;

/* Local function defintions */
static void	_alloc_job_bb(struct job_record *job_ptr, uint64_t bb_size);
static void *	_bb_agent(void *args);
static char **	_build_stage_args(char *cmd, char *opt,
				  struct job_record *job_ptr,
				  uint64_t bb_size);
static void	_destroy_job_info(void *data);
static bb_alloc_t *_find_bb_name_rec(char *name, uint32_t user_id);
static uint64_t	_get_bb_size(struct job_record *job_ptr);
static void	_load_state(uint32_t job_id);
static int	_parse_job_info(void **dest, slurm_parser_enum_t type,
				const char *key, const char *value,
				const char *line, char **leftover);
static void	_stop_stage_in(uint32_t job_id);
static void	_stop_stage_out(uint32_t job_id);
static void	_test_config(void);
static int	_test_size_limit(struct job_record *job_ptr,uint64_t add_space);
static void	_timeout_bb_rec(void);

/* Validate that our configuration is valid for this plugin type */
static void _test_config(void)
{
	if (!bb_state.bb_config.get_sys_state)
		fatal("%s: GetSysState is NULL", __func__);
	if (!bb_state.bb_config.start_stage_in)
		fatal("%s: StartStageIn is NULL", __func__);
	if (!bb_state.bb_config.start_stage_out)
		fatal("%s: StartStageOUT is NULL", __func__);
	if (!bb_state.bb_config.stop_stage_in)
		fatal("%s: StopStageIn is NULL", __func__);
	if (!bb_state.bb_config.stop_stage_out)
		fatal("%s: StopStageOUT is NULL", __func__);
}

/* Return the burst buffer size requested by a job */
static uint64_t _get_bb_size(struct job_record *job_ptr)
{
	char *tok;
	uint64_t bb_size_u = 0;

	if (job_ptr->burst_buffer) {
		tok = strstr(job_ptr->burst_buffer, "size=");
		if (tok)
			bb_size_u = bb_get_size_num(tok + 5,
						bb_state.bb_config.granularity);
	}

	return bb_size_u;
}

static char **_build_stage_args(char *cmd, char *opt,
				struct job_record *job_ptr, uint64_t bb_size)
{
	char **script_argv = NULL;
	char *save_ptr = NULL, *script, *tok;
	int script_argc = 0, size;
	char jobid_buf[32];

	if (job_ptr->batch_flag == 0)
		return script_argv;

	script = get_job_script(job_ptr);
	if (!script) {
		error("%s: failed to get script for %s", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		return script_argv;
	}

	size = 20;
	script_argv = xmalloc(sizeof(char *) * size);
	tok = strrchr(cmd, '/');
	if (tok)
		xstrfmtcat(script_argv[0], "%s", tok + 1);
	else
		xstrfmtcat(script_argv[0], "%s", cmd);
	xstrfmtcat(script_argv[1], "%s", opt);
	xstrfmtcat(script_argv[2], "%u", job_ptr->job_id);
	xstrfmtcat(script_argv[3], "%u", job_ptr->user_id);
	xstrfmtcat(script_argv[4], "%"PRIu64"", bb_size);
	script_argc += 5;
	tok = strtok_r(script, "\n", &save_ptr);
	while (tok) {
		if (tok[0] != '#')
			break;
		if (tok[1] != '!') {
			if ((script_argc + 1) >= size) {
				size *= 2;
				script_argv = xrealloc(script_argv,
						       sizeof(char *) * size);
			}
			script_argv[script_argc++] = xstrdup(tok);
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(script);

	return script_argv;
}

static void _stop_stage_in(uint32_t job_id)
{
	char **script_argv = NULL;
	char *resp, *tok;
	int i, status = 0;

	if (!bb_state.bb_config.stop_stage_in)
		return;

	script_argv = xmalloc(sizeof(char *) * 4);
	tok = strrchr(bb_state.bb_config.stop_stage_in, '/');
	if (tok) {
		xstrfmtcat(script_argv[0], "%s", tok + 1);
	} else {
		xstrfmtcat(script_argv[0], "%s",
			   bb_state.bb_config.stop_stage_in);
	}
	xstrfmtcat(script_argv[1], "%s", "stop_stage_in");
	xstrfmtcat(script_argv[2], "%u", job_id);

	resp = bb_run_script("StopStageIn",
			     bb_state.bb_config.stop_stage_in,
			     script_argv, -1, &status);
	if (resp) {
		error("%s: StopStageIn: %s", __func__, resp);
		xfree(resp);
	}
	for (i = 0; script_argv[i]; i++)
		xfree(script_argv[i]);
	xfree(script_argv);
}

static void _stop_stage_out(uint32_t job_id)
{
	char **script_argv = NULL;
	char *resp, *tok;
	int i, status = 0;

	if (!bb_state.bb_config.stop_stage_out)
		return;

	script_argv = xmalloc(sizeof(char *) * 4);
	tok = strrchr(bb_state.bb_config.stop_stage_out, '/');
	if (tok)
		xstrfmtcat(script_argv[0], "%s", tok + 1);
	else
		xstrfmtcat(script_argv[0], "%s",
			   bb_state.bb_config.stop_stage_out);
	xstrfmtcat(script_argv[1], "%s", "stop_stage_out");
	xstrfmtcat(script_argv[2], "%u", job_id);

	resp = bb_run_script("StopStageOut", bb_state.bb_config.stop_stage_out,
			     script_argv, -1, &status);
	if (resp) {
		error("%s: StopStageOut: %s", __func__, resp);
		xfree(resp);
	}
	for (i = 0; script_argv[i]; i++)
		xfree(script_argv[i]);
	xfree(script_argv);
}

/* Find a per-job burst buffer record with a specific name.
 * If not found, return NULL. */
static bb_alloc_t * _find_bb_name_rec(char *name, uint32_t user_id)
{
	bb_alloc_t *bb_ptr = NULL;

	xassert(bb_state.bb_hash);
	bb_ptr = bb_state.bb_hash[user_id % BB_HASH_SIZE];
	while (bb_ptr) {
		if (!xstrcmp(bb_ptr->name, name))
			return bb_ptr;
		bb_ptr = bb_ptr->next;
	}
	return bb_ptr;
}

/* Handle timeout of burst buffer events:
 * 1. Purge per-job burst buffer records when the stage-out has completed and
 *    the job has been purged from Slurm
 * 2. Test for StageInTimeout events
 * 3. Test for StageOutTimeout events
 */
static void _timeout_bb_rec(void)
{
	struct job_record *job_ptr;
	bb_alloc_t **bb_pptr, *bb_ptr = NULL;
	uint32_t age;
	time_t now = time(NULL);
	int i;

	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_pptr = &bb_state.bb_hash[i];
		bb_ptr = bb_state.bb_hash[i];
		while (bb_ptr) {
			if (bb_ptr->seen_time < bb_state.last_load_time) {
				if (bb_ptr->job_id == 0) {
					info("%s: Persistent burst buffer %s "
					     "purged",
					     __func__, bb_ptr->name);
				} else if (bb_state.bb_config.debug_flag) {
					info("%s: burst buffer for job %u "
					     "purged",
					     __func__, bb_ptr->job_id);
				}
				bb_remove_user_load(bb_ptr, &bb_state);
				*bb_pptr = bb_ptr->next;
				bb_free_rec(bb_ptr);
				break;
			}
			if ((bb_ptr->job_id != 0) &&
			    (bb_ptr->state >= BB_STATE_STAGED_OUT) &&
			    !find_job_record(bb_ptr->job_id)) {
				_stop_stage_out(bb_ptr->job_id);
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
				*bb_pptr = bb_ptr->next;
				bb_free_rec(bb_ptr);
				break;
			}
			age = difftime(now, bb_ptr->state_time);
			if ((bb_ptr->job_id != 0) &&
			    bb_state.bb_config.stop_stage_in &&
			    (bb_ptr->state == BB_STATE_STAGING_IN) &&
			    (bb_state.bb_config.stage_in_timeout != 0) &&
			    (!bb_ptr->cancelled) &&
			    (age >= bb_state.bb_config.stage_in_timeout)) {
				_stop_stage_in(bb_ptr->job_id);
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
				job_ptr = find_job_record(bb_ptr->job_id);
				if (job_ptr) {
					error("%s: StageIn timed out, holding "
					      "job %u",
					      __func__, bb_ptr->job_id);
					job_ptr->priority = 0;
					job_ptr->direct_set_prio = 1;
					job_ptr->state_reason = WAIT_HELD;
					xfree(job_ptr->state_desc);
					job_ptr->state_desc = xstrdup(
						"Burst buffer stage-in timeout");
					last_job_update = now;
				} else {
					error("%s: StageIn timed out for "
					      "vestigial job %u ",
					      __func__, bb_ptr->job_id);
				}
			}
			if ((bb_ptr->job_id != 0) &&
			    bb_state.bb_config.stop_stage_out &&
			    (bb_ptr->state == BB_STATE_STAGING_OUT) &&
			    (bb_state.bb_config.stage_out_timeout != 0) &&
			    (!bb_ptr->cancelled) &&
			    (age >= bb_state.bb_config.stage_out_timeout)) {
				error("%s: StageOut for job %u timed out",
				      __func__, bb_ptr->job_id);
				_stop_stage_out(bb_ptr->job_id);
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
			}
			bb_pptr = &bb_ptr->next;
			bb_ptr = bb_ptr->next;
		}
	}
}

/* Test if a job can be allocated a burst buffer.
 * This may preempt currently active stage-in for higher priority jobs.
 *
 * RET 0: Job can be started now
 *     1: Job exceeds configured limits, continue testing with next job
 *     2: Job needs more resources than currently available can not start,
 *        skip all remaining jobs
 */
static int _test_size_limit(struct job_record *job_ptr, uint64_t add_space)
{
	burst_buffer_info_msg_t *resv_bb;
	struct preempt_bb_recs *preempt_ptr = NULL;
	List preempt_list;
	ListIterator preempt_iter;
	bb_user_t *user_ptr;
	uint64_t tmp_u, tmp_j, lim_u, resv_space = 0;
	int add_total_space_needed = 0, add_user_space_needed = 0;
	int add_total_space_avail  = 0, add_user_space_avail  = 0;
	time_t now = time(NULL), when;
	bb_alloc_t *bb_ptr = NULL;
	int i;
	char jobid_buf[32];

	/* Determine if burst buffer can be allocated now for the job.
	 * If not, determine how much space must be free. */
	if (((bb_state.bb_config.job_size_limit  != NO_VAL64) &&
	     (add_space > bb_state.bb_config.job_size_limit)) ||
	    ((bb_state.bb_config.user_size_limit != NO_VAL64) &&
	     (add_space > bb_state.bb_config.user_size_limit))) {
		debug("%s: %s requested space above limit", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		return 1;
	}

	if (job_ptr->start_time <= now)
		when = now;
	else
		when = job_ptr->start_time;
	resv_bb = job_test_bb_resv(job_ptr, when);
	if (resv_bb) {
		burst_buffer_info_t *resv_bb_ptr;
		for (i = 0, resv_bb_ptr = resv_bb->burst_buffer_array;
		     i < resv_bb->record_count; i++, resv_bb_ptr++) {
			if (resv_bb_ptr->name &&
			    strcmp(resv_bb_ptr->name, bb_state.name))
				continue;
			resv_bb_ptr->used_space =
				bb_granularity(resv_bb_ptr->used_space,
					       bb_state.bb_config.granularity);
			resv_space += resv_bb_ptr->used_space;
		}
		slurm_free_burst_buffer_info_msg(resv_bb);
	}

	if (bb_state.bb_config.user_size_limit != NO_VAL64) {
		user_ptr = bb_find_user_rec(job_ptr->user_id,
					    bb_state.bb_uhash);
		tmp_u = user_ptr->size;
		tmp_j = add_space;
		lim_u = bb_state.bb_config.user_size_limit;

		add_user_space_needed = tmp_u + tmp_j - lim_u;
	}
	add_total_space_needed = bb_state.used_space + add_space + resv_space -
				 bb_state.total_space;

	if ((add_total_space_needed <= 0) &&
	    (add_user_space_needed  <= 0))
		return 0;

	/* Identify candidate burst buffers to revoke for higher priority job */
	preempt_list = list_create(bb_job_queue_del);
	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_ptr = bb_state.bb_hash[i];
		while (bb_ptr) {
			if (bb_ptr->job_id &&
			    (bb_ptr->use_time > now) &&
			    (bb_ptr->use_time > job_ptr->start_time)) {
				preempt_ptr = xmalloc(sizeof(
						struct preempt_bb_recs));
				preempt_ptr->bb_ptr = bb_ptr;
				preempt_ptr->job_id = bb_ptr->job_id;
				preempt_ptr->size = bb_ptr->size;
				preempt_ptr->use_time = bb_ptr->use_time;
				preempt_ptr->user_id = bb_ptr->user_id;
				list_push(preempt_list, preempt_ptr);

				add_total_space_avail += bb_ptr->size;
				if (bb_ptr->user_id == job_ptr->user_id)
					add_user_space_avail += bb_ptr->size;
			}
			bb_ptr = bb_ptr->next;
		}
	}

	if ((add_total_space_avail >= add_total_space_needed) &&
	    (add_user_space_avail  >= add_user_space_needed)) {
		list_sort(preempt_list, bb_preempt_queue_sort);
		preempt_iter = list_iterator_create(preempt_list);
		while ((preempt_ptr = list_next(preempt_iter)) &&
		       (add_total_space_needed || add_user_space_needed)) {
			if (add_user_space_needed &&
			    (preempt_ptr->user_id == job_ptr->user_id)) {
				_stop_stage_in(preempt_ptr->job_id);
				preempt_ptr->bb_ptr->cancelled = true;
				preempt_ptr->bb_ptr->end_time = 0;
				if (bb_state.bb_config.debug_flag) {
					info("%s: %s: Preempting stage-in of "
					     "job %u for %s", plugin_type,
					     __func__, preempt_ptr->job_id,
					     jobid2fmt(job_ptr, jobid_buf,
						       sizeof(jobid_buf)));
				}
				add_user_space_needed  -= preempt_ptr->size;
				add_total_space_needed -= preempt_ptr->size;
			}
			if ((add_total_space_needed > add_user_space_needed) &&
			    (preempt_ptr->user_id != job_ptr->user_id)) {
				_stop_stage_in(preempt_ptr->job_id);
				preempt_ptr->bb_ptr->cancelled = true;
				preempt_ptr->bb_ptr->end_time = 0;
				if (bb_state.bb_config.debug_flag) {
					info("%s: %s: Preempting stage-in of "
					     "job %u for %s", plugin_type,
					     __func__, preempt_ptr->job_id,
					     jobid2fmt(job_ptr, jobid_buf,
						       sizeof(jobid_buf)));
				}
				add_total_space_needed -= preempt_ptr->size;
			}
		}
		list_iterator_destroy(preempt_iter);
	}
	list_destroy(preempt_list);

	return 2;
}

static int _parse_job_info(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover)
{
	s_p_hashtbl_t *job_tbl;
	char *name = NULL, *tmp = NULL, local_name[64] = "";
	uint64_t size = 0;
	uint32_t job_id = 0, user_id = 0;
	uint16_t state = 0;
	bb_alloc_t *bb_ptr;
	uint16_t new_nice;
	struct job_record *job_ptr = NULL;
	bb_job_t *bb_spec;
	static s_p_options_t _job_options[] = {
		{"JobID",S_P_STRING},
		{"Name", S_P_STRING},
		{"Size", S_P_STRING},
		{"State", S_P_STRING},
		{NULL}
	};

	*dest = NULL;
	user_id = strtol(value, NULL, 10);
	job_tbl = s_p_hashtbl_create(_job_options);
	s_p_parse_line(job_tbl, *leftover, leftover);
	if (s_p_get_string(&tmp, "JobID", job_tbl)) {
		job_id = strtol(tmp, NULL, 10);
		xfree(tmp);
	}
	if (s_p_get_string(&name, "Name", job_tbl)) {
		snprintf(local_name, sizeof(local_name), "%s", name);
		xfree(name);
	}
	if (s_p_get_string(&tmp, "Size", job_tbl)) {
		size =  bb_get_size_num(tmp, bb_state.bb_config.granularity);
		xfree(tmp);
	}
	if (s_p_get_string(&tmp, "State", job_tbl)) {
		state = bb_state_num(tmp);
		xfree(tmp);
	}
	s_p_hashtbl_destroy(job_tbl);

#if 0
	info("%s: JobID:%u Name:%s Size:%"PRIu64" State:%u UserID:%u",
	     __func__, job_id, local_name, size, state, user_id);
#endif
	if (job_id) {
		job_ptr = find_job_record(job_id);
		if (!job_ptr && (state == BB_STATE_STAGED_OUT)) {
			struct job_record job_rec;
			job_rec.job_id  = job_id;
			job_rec.user_id = user_id;
			bb_ptr = bb_find_job_rec(&job_rec, bb_state.bb_hash);
			_stop_stage_out(job_id);	/* Purge buffer */
			if (bb_ptr) {
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
			} else {
				/* Slurm knows nothing about this job,
				 * may be result of slurmctld cold start */
				error("%s: Vestigial buffer for purged job %u",
				      plugin_type, job_id);
			}
			return SLURM_SUCCESS;
		} else if (!job_ptr &&
			   ((state == BB_STATE_STAGING_IN) ||
			    (state == BB_STATE_STAGED_IN))) {
			struct job_record job_rec;
			job_rec.job_id  = job_id;
			job_rec.user_id = user_id;
			bb_ptr = bb_find_job_rec(&job_rec, bb_state.bb_hash);
			_stop_stage_in(job_id);		/* Purge buffer */
			if (bb_ptr) {
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
			} else {
				/* Slurm knows nothing about this job,
				 * may be result of slurmctld cold start */
				error("%s: Vestigial buffer for purged job %u",
				      plugin_type, job_id);
			}
			return SLURM_SUCCESS;
		} else if (!job_ptr) {
			error("%s: Vestigial buffer for job ID %u. "
			      "Clear manually",
			      plugin_type, job_id);
		}
		snprintf(local_name, sizeof(local_name), "VestigialJob%u",
			 job_id);
	}
	if (job_ptr) {
		bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
		if (bb_ptr == NULL) {
			bb_spec = xmalloc(sizeof(bb_job_t));
			bb_spec->total_size = _get_bb_size(job_ptr);
			bb_ptr = bb_alloc_job_rec(&bb_state, job_ptr, bb_spec);
			xfree(bb_spec);
			bb_ptr->state = state;
			/* bb_ptr->state_time set in bb_alloc_job_rec() */
		}
	} else {
		if ((bb_ptr = _find_bb_name_rec(local_name, user_id)) == NULL) {
			bb_ptr = bb_alloc_name_rec(&bb_state, local_name,
						   user_id);
			bb_ptr->size = size;
			bb_ptr->state = state;
			bb_add_user_load(bb_ptr, &bb_state);
			return SLURM_SUCCESS;
		}
	}
	bb_ptr->seen_time = time(NULL); /* used to purge defunct recs */

	/* UserID set to 0 on some failure modes */
	if ((bb_ptr->user_id != user_id) && (user_id != 0)) {
		error("%s: User ID mismatch (%u != %u). "
		      "BB UserID=%u JobID=%u Name=%s",
		      plugin_type, bb_ptr->user_id, user_id,
		      bb_ptr->user_id, bb_ptr->job_id, bb_ptr->name);
	}
	if ((bb_ptr->state == BB_STATE_RUNNING) &&
	    (state == BB_STATE_STAGED_IN))
		state = BB_STATE_RUNNING;	/* More precise state info */
	if (bb_ptr->state != state) {
		/* State is subject to real-time changes */
		debug("%s: State changed (%s to %s). "
		      "BB UserID=%u JobID=%u Name=%s",
		      plugin_type, bb_state_string(bb_ptr->state),
		      bb_state_string(state),
		      bb_ptr->user_id, bb_ptr->job_id, bb_ptr->name);
		bb_ptr->state = state;
		bb_ptr->state_time = time(NULL);
		if ((bb_ptr->state == BB_STATE_STAGED_IN) &&
		    bb_state.bb_config.prio_boost_alloc &&
		    job_ptr && job_ptr->details) {
			new_nice = (NICE_OFFSET -
				    bb_state.bb_config.prio_boost_alloc);
			if (new_nice < job_ptr->details->nice) {
				int64_t new_prio = job_ptr->priority;
				new_prio += job_ptr->details->nice;
				new_prio -= new_nice;
				job_ptr->priority = new_prio;
				job_ptr->details->nice = new_nice;
				info("%s: StageIn complete, reset priority "
					"to %u for job_id %u", __func__,
					job_ptr->priority, job_ptr->job_id);
			}
		} else if (bb_ptr->state == BB_STATE_STAGED_OUT) {
			if (bb_ptr->size != 0) {
				bb_remove_user_load(bb_ptr, &bb_state);
				bb_ptr->size = 0;
			}
		}
		if (bb_ptr->state == BB_STATE_STAGED_IN)
			queue_job_scheduler();
	}
	if ((bb_ptr->state != BB_STATE_STAGED_OUT) && (bb_ptr->size != size)) {
		bb_remove_user_load(bb_ptr, &bb_state);
		if (size != 0) {
			error("%s: Size mismatch (%"PRIu64" != %"PRIu64"). "
			      "BB UserID=%u JobID=%u Name=%s",
			      plugin_type, bb_ptr->size, size,
			      bb_ptr->user_id, bb_ptr->job_id, bb_ptr->name);
		}
		bb_ptr->size = MAX(bb_ptr->size, size);
		bb_add_user_load(bb_ptr, &bb_state);
	}

	return SLURM_SUCCESS;
}

/* Destroy any records created by _parse_job_info(), currently none */
static void _destroy_job_info(void *data)
{
}

/*
 * Determine the current actual burst buffer state.
 * Run the program "get_sys_state" and parse stdout for details.
 * job_id IN - specific job to get information about, or 0 for all jobs
 */
static void _load_state(uint32_t job_id)
{
	static uint64_t last_total_space = 0;
	char *save_ptr = NULL, *tok, *leftover = NULL, *resp, *tmp = NULL;
	char *script_args[4], job_id_str[32];
	s_p_hashtbl_t *state_hashtbl = NULL;
	static s_p_options_t state_options[] = {
		{"ENOENT", S_P_STRING},
		{"UserID", S_P_ARRAY, _parse_job_info, _destroy_job_info},
		{"TotalSize", S_P_STRING},
		{NULL}
	};
	int status = 0;
	DEF_TIMERS;

	if (!bb_state.bb_config.get_sys_state)
		return;

	bb_state.last_load_time = time(NULL);

	tok = strrchr(bb_state.bb_config.get_sys_state, '/');
	if (tok)
		script_args[0] = tok + 1;
	else
		script_args[0] = bb_state.bb_config.get_sys_state;
	if (job_id) {
		script_args[1] = "get_job";
		snprintf(job_id_str, sizeof(job_id_str), "%u", job_id);
		script_args[3] = NULL;
	} else {
		script_args[1] = "get_sys";
		script_args[2] = NULL;
	}
	START_TIMER;
	resp = bb_run_script("GetSysState", bb_state.bb_config.get_sys_state,
			     script_args, 2000, &status);
	if (resp == NULL)
		return;
	END_TIMER;
	if (DELTA_TIMER > 200000)	/* 0.2 secs */
		info("%s: GetSysState ran for %s", __func__, TIME_STR);
	else if (bb_state.bb_config.debug_flag)
		debug("%s: GetSysState ran for %s", __func__, TIME_STR);

	state_hashtbl = s_p_hashtbl_create(state_options);
	tok = strtok_r(resp, "\n", &save_ptr);
	while (tok) {
		s_p_parse_line(state_hashtbl, tok, &leftover);
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	if (s_p_get_string(&tmp, "TotalSize", state_hashtbl)) {
		bb_state.total_space = bb_get_size_num(tmp,
						bb_state.bb_config.granularity);
		xfree(tmp);
		if (bb_state.bb_config.debug_flag &&
		    (bb_state.total_space != last_total_space)) {
			info("%s: total_space:%"PRIu64"",  __func__,
			     bb_state.total_space);
		}
		last_total_space = bb_state.total_space;
	} else if (job_id == 0) {
		error("%s: GetSysState failed to respond with TotalSize",
		      plugin_type);
	}
	s_p_hashtbl_destroy(state_hashtbl);
	xfree(resp);
}

/* Perform periodic background activities */
static void *_bb_agent(void *args)
{
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	while (!bb_state.term_flag) {
		bb_sleep(&bb_state, AGENT_INTERVAL);
		if (bb_state.term_flag)
			break;
		lock_slurmctld(job_write_lock);
		pthread_mutex_lock(&bb_state.bb_mutex);
		_load_state(0);
		_timeout_bb_rec();
		pthread_mutex_unlock(&bb_state.bb_mutex);
		unlock_slurmctld(job_write_lock);
	}
	return NULL;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	pthread_attr_t attr;

	pthread_mutex_init(&bb_state.bb_mutex, NULL);
	pthread_cond_init(&bb_state.term_cond, NULL);
	pthread_mutex_init(&bb_state.term_mutex, NULL);

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_load_config(&bb_state, (char *)plugin_type); /* Remove "const" */
	_test_config();
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);
	bb_alloc_cache(&bb_state);
	slurm_attr_init(&attr);
	if (pthread_create(&bb_state.bb_thread, &attr, _bb_agent, NULL))
		error("Unable to start backfill thread: %m");
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded. Free all memory.
 */
extern int fini(void)
{
	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);

	pthread_mutex_lock(&bb_state.term_mutex);
	bb_state.term_flag = true;
	pthread_cond_signal(&bb_state.term_cond);
	pthread_mutex_unlock(&bb_state.term_mutex);

	if (bb_state.bb_thread) {
		pthread_join(bb_state.bb_thread, NULL);
		bb_state.bb_thread = 0;
	}
	bb_clear_config(&bb_state.bb_config, true);
	bb_clear_cache(&bb_state);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Load the current burst buffer state (e.g. how much space is available now).
 * Run at the beginning of each scheduling cycle in order to recognize external
 * changes to the burst buffer state (e.g. capacity is added, removed, fails,
 * etc.)
 *
 * init_config IN - true if called as part of slurmctld initialization
 * Returns a SLURM errno.
 */
extern int bb_p_load_state(bool init_config)
{
	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);
	_load_state(0);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Note configuration may have changed. Handle changes in BurstBufferParameters.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_reconfig(void)
{
	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);
	bb_load_config(&bb_state, (char *)plugin_type); /* Remove "const" */
	_test_config();
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Pack current burst buffer state information for network transmission to
 * user (e.g. "scontrol show burst")
 *
 * Returns a SLURM errno.
 */
extern int bb_p_state_pack(uid_t uid, Buf buffer, uint16_t protocol_version)
{
	uint32_t rec_count = 0;
	int eof, offset;

	pthread_mutex_lock(&bb_state.bb_mutex);
	packstr(bb_state.name, buffer);
	offset = get_buf_offset(buffer);
	pack32(rec_count,        buffer);
	bb_pack_state(&bb_state, buffer, protocol_version);
	if (bb_state.bb_config.private_data == 0)
		uid = 0;	/* User can see all data */
	rec_count = bb_pack_bufs(uid, bb_state.bb_hash,buffer,protocol_version);
	if (rec_count != 0) {
		eof = get_buf_offset(buffer);
		set_buf_offset(buffer, offset);
		pack32(rec_count, buffer);
		set_buf_offset(buffer, eof);
	}
	if (bb_state.bb_config.debug_flag) {
		debug("%s: %s: record_count:%u",
		      plugin_type,  __func__, rec_count);
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Preliminary validation of a job submit request with respect to burst buffer
 * options. Performed prior to establishing job ID or creating script file.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_validate(struct job_descriptor *job_desc,
			     uid_t submit_uid)
{
	int64_t bb_size = 0;
	char *key;
	int i;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_user_id:%u, submit_uid:%d",
		     plugin_type, __func__, job_desc->user_id, submit_uid);
		info("%s: burst_buffer:%s", __func__, job_desc->burst_buffer);
		info("%s: script:%s", __func__, job_desc->script);
	}

	if (job_desc->burst_buffer) {
		key = strstr(job_desc->burst_buffer, "size=");
		if (key) {
			bb_size = bb_get_size_num(key + 5,
					bb_state.bb_config.granularity);
		}
	}
	if (bb_size == 0)
		return SLURM_SUCCESS;
	if (bb_size < 0)
		return ESLURM_BURST_BUFFER_LIMIT;

	pthread_mutex_lock(&bb_state.bb_mutex);
	if (((bb_state.bb_config.job_size_limit  != NO_VAL64) &&
	     (bb_size > bb_state.bb_config.job_size_limit)) ||
	    ((bb_state.bb_config.user_size_limit != NO_VAL64) &&
	     (bb_size > bb_state.bb_config.user_size_limit))) {
		pthread_mutex_unlock(&bb_state.bb_mutex);
		return ESLURM_BURST_BUFFER_LIMIT;
	}

	if (bb_state.bb_config.allow_users) {
		for (i = 0; bb_state.bb_config.allow_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.allow_users[i])
				break;
		}
		if (bb_state.bb_config.allow_users[i] == 0) {
			pthread_mutex_unlock(&bb_state.bb_mutex);
			return ESLURM_BURST_BUFFER_PERMISSION;
		}
	}

	if (bb_state.bb_config.deny_users) {
		for (i = 0; bb_state.bb_config.deny_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.deny_users[i])
				break;
		}
		if (bb_state.bb_config.deny_users[i] != 0) {
			pthread_mutex_unlock(&bb_state.bb_mutex);
			return ESLURM_BURST_BUFFER_PERMISSION;
		}
	}

	if (bb_size > bb_state.total_space) {
		info("Job from user %u requested burst buffer size of "
		     "%"PRIu64", but total space is only %"PRIu64"",
		     job_desc->user_id, bb_size, bb_state.total_space);
	}

	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Secondary validation of a job submit request with respect to burst buffer
 * options. Performed after establishing job ID and creating script file.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_validate2(struct job_record *job_ptr, char **err_msg,
			      bool is_job_array)
{
	/* This function is unused by this plugin type */
	return SLURM_SUCCESS;
}

/*
 * For a given job, return our best guess if when it might be able to start
 */
extern time_t bb_p_job_get_est_start(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;
	time_t est_start = time(NULL);
	uint64_t bb_size;
	int rc;
	char jobid_buf[32];

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s", plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    ((bb_size = _get_bb_size(job_ptr)) == 0))
		return est_start;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		rc = _test_size_limit(job_ptr, bb_size);
		if (rc == 0) {		/* Could start now */
			;
		} else if (rc == 1) {	/* Exceeds configured limits */
			est_start += 365 * 24 * 60 * 60;
		} else {		/* No space currently available */
			est_start = MAX(est_start, bb_state.next_end_time);
		}
	} else if (bb_ptr->state < BB_STATE_STAGED_IN) {
		est_start++;
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return est_start;
}

static void _alloc_job_bb(struct job_record *job_ptr, uint64_t bb_size)
{
	char **script_argv, *resp;
	bb_alloc_t *bb_ptr;
	int i, status = 0;
	bb_job_t *bb_spec;
	char jobid_buf[32];

	bb_spec = xmalloc(sizeof(bb_job_t));
	bb_spec->total_size = bb_size;
	bb_ptr = bb_alloc_job(&bb_state, job_ptr, bb_spec);
	xfree(bb_spec);

	if (bb_state.bb_config.debug_flag) {
		info("%s: start stage-in %s", __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}
	script_argv = _build_stage_args(bb_state.bb_config.start_stage_in,
					"start_stage_in", job_ptr, bb_size);
	if (script_argv) {
		bb_ptr->state = BB_STATE_STAGING_IN;
		bb_ptr->state_time = time(NULL);
		resp = bb_run_script("StartStageIn",
				     bb_state.bb_config.start_stage_in,
				     script_argv, -1, &status);
		if (resp) {
			error("%s: StartStageIn: %s", __func__, resp);
			xfree(resp);
		}
		for (i = 0; script_argv[i]; i++)
			xfree(script_argv[i]);
		xfree(script_argv);
	} else {
		bb_ptr->state = BB_STATE_STAGED_IN;
		bb_ptr->state_time = time(NULL);
	}
}

/*
 * Validate a job submit request with respect to burst buffer options.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_try_stage_in(List job_queue)
{
	job_queue_rec_t *job_rec;
	List job_candidates;
	ListIterator job_iter;
	struct job_record *job_ptr;
	uint64_t bb_size;
	int rc;

	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);

	if (!bb_state.bb_config.start_stage_in)
		return SLURM_ERROR;

	/* Identify candidates to be allocated burst buffers */
	job_candidates = list_create(bb_job_queue_del);
	job_iter = list_iterator_create(job_queue);
	while ((job_ptr = list_next(job_iter))) {
		if (!IS_JOB_PENDING(job_ptr) ||
		    (job_ptr->start_time == 0) ||
		    (job_ptr->burst_buffer == NULL) ||
		    (job_ptr->burst_buffer[0] == '\0'))
			continue;
		if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL))
			continue;
		bb_size = _get_bb_size(job_ptr);
		if (bb_size == 0)
			continue;
		job_rec = xmalloc(sizeof(job_queue_rec_t));
		job_rec->job_ptr = job_ptr;
		job_rec->bb_size = bb_size;
		list_push(job_candidates, job_rec);
	}
	list_iterator_destroy(job_iter);

	/* Sort in order of expected start time */
	list_sort(job_candidates, bb_job_queue_sort);

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_set_use_time(&bb_state);
	job_iter = list_iterator_create(job_candidates);
	while ((job_rec = list_next(job_iter))) {
		job_ptr = job_rec->job_ptr;
		bb_size = job_rec->bb_size;

		if (bb_find_job_rec(job_ptr, bb_state.bb_hash))
			continue;

		rc = _test_size_limit(job_ptr, bb_size);
		if (rc == 1)
			continue;
		else if (rc == 2)
			break;

		_alloc_job_bb(job_ptr, bb_size);
	}
	list_iterator_destroy(job_iter);
	pthread_mutex_unlock(&bb_state.bb_mutex);
	list_destroy(job_candidates);

	return SLURM_SUCCESS;
}

/*
 * Determine if a job's burst buffer stage-in is complete
 * job_ptr IN - Job to test
 * test_only IN - If false, then attempt to allocate burst buffer if possible
 *
 * RET: 0 - stage-in is underway
 *      1 - stage-in complete
 *     -1 - stage-in not started or burst buffer in some unexpected state
 */
extern int bb_p_job_test_stage_in(struct job_record *job_ptr, bool test_only)
{
	bb_alloc_t *bb_ptr;
	uint64_t bb_size = 0;
	int rc = 1;
	char jobid_buf[32];

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s", plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    ((bb_size = _get_bb_size(job_ptr)) == 0))
		return rc;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		debug("%s: %s bb_rec not found", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		rc = -1;
		if ((test_only == false) &&
		    (_test_size_limit(job_ptr, bb_size) == 0))
			_alloc_job_bb(job_ptr, bb_size);
	} else {
		if (bb_ptr->state < BB_STATE_STAGED_IN)
			_load_state(job_ptr->job_id);
		if (bb_ptr->state < BB_STATE_STAGED_IN) {
			rc = 0;
		} else if (bb_ptr->state == BB_STATE_STAGED_IN) {
			rc = 1;
		} else {
			error("%s: %s bb_state:%u", __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
			      bb_ptr->state);
			rc = -1;
		}
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);
	return rc;
}

/* Attempt to claim burst buffer resources.
 * At this time, bb_g_job_test_stage_in() should have been run sucessfully AND
 * the compute nodes selected for the job.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_begin(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return SLURM_SUCCESS;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (bb_ptr)
		bb_ptr->state = BB_STATE_RUNNING;
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Trigger a job's burst buffer stage-out to begin
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_start_stage_out(struct job_record *job_ptr)
{
//FIXME: How to handle various job terminate states (e.g. requeue, failure), user script controlled?
//FIXME: Test for memory leaks
	bb_alloc_t *bb_ptr;
	char **script_argv, *resp;
	int i, status = 0;
	char jobid_buf[32];

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s", plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if (!bb_state.bb_config.start_stage_out)
		return SLURM_ERROR;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return SLURM_SUCCESS;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		/* No job buffers. Assuming use of persistent buffers only */
		debug("%s: %s bb_rec not found", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	} else {
		script_argv = _build_stage_args(bb_state.bb_config.start_stage_out,
						"start_stage_out", job_ptr,
						bb_ptr->size);
		if (script_argv) {
			bb_ptr->state = BB_STATE_STAGING_OUT;
			bb_ptr->state_time = time(NULL);
			resp = bb_run_script("StartStageOut",
					     bb_state.bb_config.start_stage_out,
					     script_argv, -1, &status);
			if (resp) {
				error("%s: StartStageOut: %s", __func__, resp);
				xfree(resp);
			}
			for (i = 0; script_argv[i]; i++)
				xfree(script_argv[i]);
			xfree(script_argv);
		} else {
			bb_ptr->state = BB_STATE_STAGED_OUT;
			bb_ptr->state_time = time(NULL);
		}
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Determine if a job's burst buffer stage-out is complete
 *
 * RET: 0 - stage-out is underway
 *      1 - stage-out complete
 *     -1 - fatal error
 */
extern int bb_p_job_test_stage_out(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;
	int rc = -1;
	char jobid_buf[32];

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s", plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return 1;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		/* No job buffers. Assuming use of persistent buffers only */
		debug("%s: %s bb_rec not found", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		rc =  1;
	} else {
		if (bb_ptr->state < BB_STATE_STAGED_OUT)
			_load_state(job_ptr->job_id);
		if (bb_ptr->state == BB_STATE_STAGING_OUT) {
			rc =  0;
		} else if (bb_ptr->state == BB_STATE_STAGED_OUT) {
			if (bb_ptr->size != 0) {
				bb_remove_user_load(bb_ptr, &bb_state);
				bb_ptr->size = 0;
			}
			rc =  1;
		} else {
			error("%s: %s bb_state:%u", __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
			      bb_ptr->state);
			rc = -1;
		}
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Terminate any file staging and completely release burst buffer resources
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_cancel(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;
	char **script_argv, *resp;
	int i, status = 0;
	char jobid_buf[32];

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s", plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if (!bb_state.bb_config.stop_stage_out)
		return SLURM_ERROR;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return SLURM_SUCCESS;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		_stop_stage_out(job_ptr->job_id);
	} else {
		script_argv = _build_stage_args(bb_state.bb_config.stop_stage_out,
						"stop_stage_out", job_ptr, 0);
		if (script_argv) {
			bb_ptr->state = BB_STATE_STAGED_OUT;
			bb_ptr->state_time = time(NULL);
			resp = bb_run_script("StopStageOut",
					     bb_state.bb_config.stop_stage_out,
					     script_argv, -1, &status);
			if (resp) {
				error("%s: StopStageOut: %s", __func__, resp);
				xfree(resp);
			}
			for (i = 0; script_argv[i]; i++)
				xfree(script_argv[i]);
			xfree(script_argv);
		} else {
			_stop_stage_out(job_ptr->job_id);
			bb_ptr->cancelled = true;
			bb_ptr->end_time = 0;
			bb_ptr->state = BB_STATE_STAGED_OUT;
			bb_ptr->state_time = time(NULL);
		}
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}
