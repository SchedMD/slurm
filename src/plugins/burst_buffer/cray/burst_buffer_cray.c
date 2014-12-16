/*****************************************************************************\
 *  burst_buffer_cray.c - Plugin for managing a Cray burst_buffer
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC.
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

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <ctype.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#if HAVE_JSON_OBJECT_H
#   include <json_object.h>
#endif

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
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "burst_buffer cray plugin";
const char plugin_type[]        = "burst_buffer/cray";
const uint32_t plugin_version   = 100;

/* Most state information is in a common structure so that we can more
 * easily use common functions from multiple burst buffer plugins */
static bb_state_t 	bb_state;
static char *		state_save_loc = NULL;

typedef struct {
	char *   name;		/* Generic burst buffer resource, e.g. "nodes" */
	uint32_t count;		/* Count of required resources */
} bb_gres_t;
typedef struct {
	uint32_t   gres_cnt;	/* number of records in gres_ptr */
	bb_gres_t *gres_ptr;
	uint32_t   swap_size;	/* swap space required per node */
	uint32_t   swap_nodes;	/* Number of nodes needed */
	uint32_t   total_size;	/* Total GB required for this job */
} bb_job_t;

static int	_alloc_job_bb(struct job_record *job_ptr, uint32_t bb_size);
static void *	_bb_agent(void *args);
static void	_del_bb_spec(bb_job_t *bb_spec);
static bb_job_t *_get_bb_spec(struct job_record *job_ptr);
static void	_log_bb_spec(bb_job_t *bb_spec);
static void	_load_state(uint32_t job_id);
static int	_parse_bb_opts(struct job_descriptor *job_desc);
static int	_parse_interactive(struct job_descriptor *job_desc);
static char *	_set_cmd_path(char *cmd);
static int	_start_stage_in(struct job_record *job_ptr);
static void	_start_stage_out(uint32_t job_id);
static void	_teardown(uint32_t job_id, bool hurry);
static void	_test_config(void);
static int	_test_size_limit(struct job_record *job_ptr,uint32_t add_space);
static void	_timeout_bb_rec(void);
static int	_write_file(char *file_name, char *buf);
static int	_write_nid_file(char *file_name, char *node_list);

/* Validate that our configuration is valid for this plugin type */
static void _test_config(void)
{
	if (!bb_state.bb_config.get_sys_state)
		fatal("%s: GetSysState is NULL", __func__);
}

/* Return the path to the specified command using the configured "get_sys_state"
 * path as a base.
 * xfree the return value */
static char *_set_cmd_path(char *cmd)
{
	char *tmp, *sep;

	tmp = xstrdup(bb_state.bb_config.get_sys_state);
	sep = strrchr(tmp, '/');
	if (sep)
		sep[1] = '\0';
	else
		tmp[0] = '\0';
	xstrcat(tmp, cmd);

	return tmp;
}

static int _alloc_job_bb(struct job_record *job_ptr, uint32_t bb_size)
{
	bb_alloc_t *bb_ptr;
	int rc;

	bb_ptr = bb_alloc_job(&bb_state, job_ptr, bb_size);

	if (bb_state.bb_config.debug_flag)
		info("%s: start stage-in job_id:%u", __func__, job_ptr->job_id);
	rc = _start_stage_in(job_ptr);
	if (rc != SLURM_SUCCESS) {
//FIXME: Deallocate BB and kill job
if (bb_ptr) error("Kill job here");
	}
	return rc;
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

static void _del_bb_spec(bb_job_t *bb_spec)
{
	int i;

	if (bb_spec) {
		for (i = 0; i < bb_spec->gres_cnt; i++)
			xfree(bb_spec->gres_ptr[i].name);
		xfree(bb_spec->gres_ptr);
		xfree(bb_spec);
	}
}

static void _log_bb_spec(bb_job_t *bb_spec)
{
	int i;

	if (bb_spec) {
		for (i = 0; i < bb_spec->gres_cnt; i++) {
			info("Gres[%d]:%s:%u", i, bb_spec->gres_ptr[i].name,
			     bb_spec->gres_ptr[i].count);
		}
		info("Swap:%u per node, %u nodes", bb_spec->swap_size,
		     bb_spec->swap_nodes);
		info("TotalSize:%u", bb_spec->total_size);
	}
}

/* Return the burst buffer size specification of a job
 * RET size data structure or NULL of none found
 * NOTE: delete return value using _del_bb_size() */
static bb_job_t *_get_bb_spec(struct job_record *job_ptr)
{
	char *end_ptr = NULL, *sep, *tok, *tmp;
	bool have_bb = false;
	int inx;
	bb_job_t *bb_spec;

	if (!job_ptr->burst_buffer)
		return NULL;

	bb_spec = xmalloc(sizeof(bb_job_t));
	tok = strstr(job_ptr->burst_buffer, "SLURM_SIZE=");
	if (tok) {	/* Format: "SLURM_SIZE=%u" */
		bb_spec->total_size = bb_get_size_num(tok + 11,
					bb_state.bb_config.granularity);
		if (bb_spec->total_size)
			have_bb = true;
	}

	tok = strstr(job_ptr->burst_buffer, "SLURM_SWAP=");
	if (tok) {	/* Format: "SLURM_SWAP=%uGB(%uNodes)" */
		tok += 11;
		bb_spec->swap_size = strtol(tok, &end_ptr, 10);
		if (bb_spec->swap_size)
			have_bb = true;
		if ((end_ptr[0] == 'G') && (end_ptr[1] == 'B') &&
		    (end_ptr[2] == '(')) {
			bb_spec->swap_nodes = strtol(end_ptr + 3, NULL, 10);
		} else {
			bb_spec->swap_nodes = 1;
		}
	}

	tok = strstr(job_ptr->burst_buffer, "SLURM_GRES=");
	if (tok) {	/* Format: "SLURM_GRES=nodes:%u" */
		tmp = xstrdup(tok + 11);
		tok = strtok_r(tmp, ",", &end_ptr);
		while (tok) {
			have_bb = true;
			inx = bb_spec->gres_cnt;
			bb_spec->gres_cnt++;
			bb_spec->gres_ptr = xrealloc(bb_spec->gres_ptr,
						     sizeof(bb_gres_t) *
						     bb_spec->gres_cnt);
			sep = strchr(tok, ':');
			if (sep) {
				sep[0] = '\0';
				bb_spec->gres_ptr[inx].count = atoi(sep + 1);
			} else {
				bb_spec->gres_ptr[inx].count = 1;
			}
			bb_spec->gres_ptr[inx].name = xstrdup(tok);
			tok = strtok_r(NULL, ",", &end_ptr);
		}
		xfree(tmp);
	}
	if (!have_bb)
		xfree(bb_spec);

	if (bb_state.bb_config.debug_flag)
		_log_bb_spec(bb_spec);
	return bb_spec;
}
static uint32_t _get_bb_size(struct job_record *job_ptr)
{
//FIXME: Temporary
	uint32_t size = 0;
	bb_job_t *bb_spec = _get_bb_spec(job_ptr);
	if (bb_spec) size=bb_spec->total_size;
	_del_bb_spec(bb_spec);
	return size;
}

/*
 * Determine the current actual burst buffer state.
 * Run the program "get_sys_state" and parse stdout for details.
 * job_id IN - specific job to get information about, or 0 for all jobs
 */
static void _load_state(uint32_t job_id)
{
	burst_buffer_gres_t *gres_ptr;
	bb_entry_t *ents;
	int num_ents;
	int i;

	bb_state.last_load_time = time(NULL);

	ents = get_bb_entry(&num_ents, &bb_state);
	if (ents == NULL) {
		error("%s: failed to be burst buffer entries, what now?",
		      __func__);
		return;
	}

	for (i = 0; i < num_ents; i++) {
		/* ID: "bytes"
		 */
		if (strcmp(ents[i].id, "bytes") == 0) {
			bb_state.bb_config.granularity
				= ents[i].gb_granularity;
			bb_state.total_space
				= ents[i].gb_quantity;
			bb_state.used_space
				= ents[i].gb_quantity - ents[i].gb_free;
			xassert(bb_state.used_space >= 0);

			/* Everything else is a burst buffer
			 * generic resource (gres)
			 */
			bb_state.bb_config.gres_cnt = 0;
			continue;
		}

		bb_state.bb_config.gres_ptr
			= xrealloc(bb_state.bb_config.gres_ptr,
				   sizeof(burst_buffer_gres_t) *
				   (bb_state.bb_config.gres_cnt + 1));
		gres_ptr = bb_state.bb_config.gres_ptr + bb_state.bb_config.gres_cnt;
		bb_state.bb_config.gres_cnt++;
		gres_ptr->avail_cnt = ents[i].quantity;
		gres_ptr->granularity = ents[i].gb_granularity;
		gres_ptr->name = xstrdup(ents[i].id);
		gres_ptr->used_cnt = ents[i].quantity - ents[i].gb_free;
	}
	free_bb_ents(ents, num_ents);
}

/* Write an string representing the NIDs of a job's nodes to an arbitrary
 * file location */
static int _write_nid_file(char *file_name, char *node_list)
{
	char *tmp, *sep, *tok, *save_ptr = NULL, *buf = NULL;
	int i, rc;

	tmp = xstrdup(node_list);
	sep = strrchr(tmp, ']');
	if (sep)
		sep[0] = '\0';
	sep = strchr(tmp, '[');
	if (sep) {
		sep++;
	} else {
		sep = tmp;
		for (i = 0; !isdigit(sep[0]) && sep[0]; i++)
			sep++;
	}
	tok = strtok_r(sep, ",", &save_ptr);
	while (tok) {
		xstrfmtcat(buf, "%s\n", tok);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	rc = _write_file(file_name, buf);
	xfree(buf);
	return rc;
}

/* Write an arbitrary string to an arbitrary file nalocationme */
static int _write_file(char *file_name, char *buf)
{
	int amount, fd, nwrite, pos;

	(void) unlink(file_name);
	fd = creat(file_name, 0600);
	if (fd < 0) {
		error("Error creating file %s, %m", file_name);
		return errno;
	}

	if (!buf) {
		error("%s: buf is NULL", __func__);
		return SLURM_ERROR;
	}

	nwrite = strlen(buf) + 1;
	pos = 0;
	while (nwrite > 0) {
		amount = write(fd, &buf[pos], nwrite);
		if ((amount < 0) && (errno != EINTR)) {
			error("Error writing file %s, %m", file_name);
			close(fd);
			return ESLURM_WRITING_TO_FILE;
		}
		nwrite -= amount;
		pos    += amount;
	}

	(void) close(fd);
	return SLURM_SUCCESS;
}

static int _start_stage_in(struct job_record *job_ptr)
{
	char token[32], *caller = "SLURM", owner[32], *capacity = NULL;
	char *setup_env_file = NULL, *client_nodes_file_nid = NULL;
	char *data_in_env_file = NULL;
	char *bbs_setup = NULL, *bbs_data_in = NULL;
	char *tok;
	int hash_inx = job_ptr->job_id % 10;
	uint32_t i;

	if (job_ptr->burst_buffer) {
		tok = strstr(job_ptr->burst_buffer, "SLURM_SIZE=");
		if (tok) {
			i = atoi(tok + 11);
			xstrfmtcat(capacity, "bytes:%uGB", i);
		} else {
			tok = strstr(job_ptr->burst_buffer, "SLURM_GRES=");
			if (tok) {
				tok = strstr(tok, "nodes=");
				if (tok) {
					i = atoi(tok + 6);
					xstrfmtcat(capacity, "nodes:%u", i);
				}
			}
		}
	}
	if (!capacity) {
		error("%s: Job %u has invalid burst buffer spec(%s)", __func__,
		     job_ptr->job_id, job_ptr->burst_buffer);
		return SLURM_ERROR;
	}

	snprintf(token, sizeof(token), "%u", job_ptr->job_id);
	snprintf(owner, sizeof(owner), "%d", job_ptr->user_id);

/*	pthread_mutex_lock(&bb_state.bb_mutex);	* Locked on entry */
	if (job_ptr->sched_nodes) {
		xstrfmtcat(client_nodes_file_nid,
			   "%s/hash.%d/job.%u/client_nids",
			   state_save_loc, hash_inx, job_ptr->job_id);
		if (_write_nid_file(client_nodes_file_nid,job_ptr->sched_nodes))
			xfree(client_nodes_file_nid);
	}
	xstrfmtcat(setup_env_file, "%s/hash.%d/job.%u/setup_env",
		   state_save_loc, hash_inx, job_ptr->job_id);
	xstrfmtcat(data_in_env_file, "%s/hash.%d/job.%u/data_in_env",
		   state_save_loc, hash_inx, job_ptr->job_id);
/*	pthread_mutex_unlock(&bb_state.bb_mutex); * Locked on entry */

	bbs_setup = _set_cmd_path("bbs_setup");
	bbs_data_in = _set_cmd_path("bbs_data_in");
#if 1
//FIXME: Call bbs_setup and bbs_data_in here
	info("BBS_SETUP: Token:%s Caller:%s Onwer:%s Capacity:%s "
	     "SetupEnv:%s NidFile:%s",
	     token, caller, owner, capacity,
	     setup_env_file, client_nodes_file_nid);
	info("BBS_DATA_IN: DataInEnv:%s", data_in_env_file);
#endif
	xfree(bbs_setup);
	xfree(bbs_data_in);

	xfree(capacity);
	xfree(client_nodes_file_nid);
	xfree(setup_env_file);
	xfree(data_in_env_file);
	return SLURM_SUCCESS;
}

static void _start_stage_out(uint32_t job_id)
{
	char *post_run_env_file = NULL, *data_out_env_file = NULL;
	char *bbs_post_run = NULL, *bbs_data_out = NULL;
	int hash_inx = job_id % 10;

	pthread_mutex_lock(&bb_state.bb_mutex);
	xstrfmtcat(post_run_env_file, "%s/hash.%d/job.%u/post_run_env",
		   state_save_loc, hash_inx, job_id);
	xstrfmtcat(data_out_env_file, "%s/hash.%d/job.%u/data_out_env",
		   state_save_loc, hash_inx, job_id);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	bbs_post_run = _set_cmd_path("bbs_post_run");
	bbs_data_out = _set_cmd_path("bbs_data_out");
#if 1
//FIXME: Call bbs_post_run and bbs_data_out here
	info("BBS_POST_RUN: PostRunEnv:%s", post_run_env_file);
	info("BBS_DATA_OUT: DataOutEnv:%s", data_out_env_file);
	_teardown(job_id, false);
#endif
	xfree(bbs_data_out);
	xfree(bbs_post_run);
	xfree(post_run_env_file);
	xfree(data_out_env_file);
}

static void _teardown(uint32_t job_id, bool hurry)
{
	char *teardown_env_file = NULL, *bbs_teardown = NULL;
	int hash_inx = job_id % 10;

	pthread_mutex_lock(&bb_state.bb_mutex);
	xstrfmtcat(teardown_env_file, "%s/hash.%d/job.%u/teardown_env",
		   state_save_loc, hash_inx, job_id);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	bbs_teardown = _set_cmd_path("bbs_teardown");
#if 1
//FIXME: Call bbs_teardown here
	info("BBS_TEARDOWN: TeardownEnv:%s Hurry:%d",
	     teardown_env_file, (int)hurry);
#endif
	xfree(bbs_teardown);
	xfree(teardown_env_file);
}

/* Test if a job can be allocated a burst buffer.
 * This may preempt currently active stage-in for higher priority jobs.
 *
 * RET 0: Job can be started now
 *     1: Job exceeds configured limits, continue testing with next job
 *     2: Job needs more resources than currently available can not start,
 *        skip all remaining jobs
 */
static int _test_size_limit(struct job_record *job_ptr, uint32_t add_space)
{
	struct preempt_bb_recs *preempt_ptr = NULL;
	List preempt_list;
	ListIterator preempt_iter;
	bb_user_t *user_ptr;
	uint32_t tmp_u, tmp_j, lim_u;
	int add_total_space_needed = 0, add_user_space_needed = 0;
	int add_total_space_avail  = 0, add_user_space_avail  = 0;
	time_t now = time(NULL);
	bb_alloc_t *bb_ptr = NULL;
	int i;

	/* Determine if burst buffer can be allocated now for the job.
	 * If not, determine how much space must be free. */
	if (((bb_state.bb_config.job_size_limit  != NO_VAL) &&
	     (add_space > bb_state.bb_config.job_size_limit)) ||
	    ((bb_state.bb_config.user_size_limit != NO_VAL) &&
	     (add_space > bb_state.bb_config.user_size_limit)))
		return 1;

	if (bb_state.bb_config.user_size_limit != NO_VAL) {
		user_ptr = bb_find_user_rec(job_ptr->user_id,bb_state.bb_uhash);
		tmp_u = user_ptr->size;
		tmp_j = add_space;
		lim_u = bb_state.bb_config.user_size_limit;

		add_user_space_needed = tmp_u + tmp_j - lim_u;
	}
	add_total_space_needed = bb_state.used_space + add_space -
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
				if (bb_ptr->user_id == job_ptr->user_id);
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
				_teardown(preempt_ptr->job_id, true);
				preempt_ptr->bb_ptr->cancelled = true;
				preempt_ptr->bb_ptr->end_time = 0;
				if (bb_state.bb_config.debug_flag) {
					info("%s: %s: Preempting stage-in of "
					     "job %u for job %u", plugin_type,
					     __func__, preempt_ptr->job_id,
					     job_ptr->job_id);
				}
				add_user_space_needed  -= preempt_ptr->size;
				add_total_space_needed -= preempt_ptr->size;
			}
			if ((add_total_space_needed > add_user_space_needed) &&
			    (preempt_ptr->user_id != job_ptr->user_id)) {
				_teardown(preempt_ptr->job_id, true);
				preempt_ptr->bb_ptr->cancelled = true;
				preempt_ptr->bb_ptr->end_time = 0;
				if (bb_state.bb_config.debug_flag) {
					info("%s: %s: Preempting stage-in of "
					     "job %u for job %u", plugin_type,
					     __func__, preempt_ptr->job_id,
					     job_ptr->job_id);
				}
				add_total_space_needed -= preempt_ptr->size;
			}
		}
		list_iterator_destroy(preempt_iter);
	}
	list_destroy(preempt_list);

	return 2;
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
				xfree(bb_ptr);
				break;
			}
			if ((bb_ptr->job_id != 0) &&
			    (bb_ptr->state >= BB_STATE_STAGED_OUT) &&
			    !find_job_record(bb_ptr->job_id)) {
				_teardown(bb_ptr->job_id, true);
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
				*bb_pptr = bb_ptr->next;
				xfree(bb_ptr);
				break;
			}
			age = difftime(now, bb_ptr->state_time);
			if ((bb_ptr->job_id != 0) &&
			    (bb_ptr->state == BB_STATE_STAGING_IN) &&
			    (bb_state.bb_config.stage_in_timeout != 0) &&
			    (!bb_ptr->cancelled) &&
			    (age >= bb_state.bb_config.stage_in_timeout)) {
				_teardown(bb_ptr->job_id, true);
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
			    (bb_ptr->state == BB_STATE_STAGING_OUT) &&
			    (bb_state.bb_config.stage_out_timeout != 0) &&
			    (!bb_ptr->cancelled) &&
			    (age >= bb_state.bb_config.stage_out_timeout)) {
				error("%s: StageOut for job %u timed out",
				      __func__, bb_ptr->job_id);
				_teardown(bb_ptr->job_id, true);
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
			}
			bb_pptr = &bb_ptr->next;
			bb_ptr = bb_ptr->next;
		}
	}
}

/* Translate a batch script or interactive burst_buffer options into in
 * appropriate burst_buffer argument */
static int _parse_bb_opts(struct job_descriptor *job_desc)
{
	char *capacity, *end_ptr = NULL, *script, *save_ptr = NULL, *tok;
	int64_t raw_cnt;
	uint32_t gb_cnt = 0, node_cnt = 0, swap_cnt = 0;
	int rc = SLURM_SUCCESS;

	if (!job_desc->script)
		return _parse_interactive(job_desc);

	script = xstrdup(job_desc->script);
	tok = strtok_r(script, "\n", &save_ptr);
	while (tok) {
		if ((tok[0] != '#') || (tok[1] != 'B') || (tok[2] != 'B')) {
			;
		} else {
			tok += 3;
			while (isspace(tok[0]))
				tok++;
			if (!strncmp(tok, "jobbb", 5) &&
			    (capacity = strstr(tok, "capacity="))) {
				raw_cnt = strtol(capacity + 9, &end_ptr, 10);
				if (raw_cnt <= 0) {
					rc = ESLURM_INVALID_BURST_BUFFER_CHANGE;
					break;
				}
				if ((end_ptr[0] == 'n') ||
				    (end_ptr[0] == 'N')) {
					node_cnt += raw_cnt;
				} else if ((end_ptr[0] == 'm') ||
					   (end_ptr[0] == 'M')) {
					raw_cnt = (raw_cnt + 1023) / 1024;
					gb_cnt += raw_cnt;
				} else if ((end_ptr[0] == 'g') ||
					   (end_ptr[0] == 'G')) {
					gb_cnt += raw_cnt;
				} else if ((end_ptr[0] == 't') ||
					   (end_ptr[0] == 'T')) {
					raw_cnt *= 1024;
					gb_cnt += raw_cnt;
				} else if ((end_ptr[0] == 'p') ||
					   (end_ptr[0] == 'P')) {
					raw_cnt *= (1024 * 1024);
					gb_cnt += raw_cnt;
				}
			} else if (!strncmp(tok, "swap", 4)) {
				tok += 4;
				if (isspace(tok[0]))
					tok++;
				swap_cnt = strtol(tok, &end_ptr, 10);
			}
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(script);

	if ((rc == SLURM_SUCCESS) && (gb_cnt || node_cnt || swap_cnt)) {
		xfree(job_desc->burst_buffer);
		if (swap_cnt) {
			uint32_t job_nodes;
			if ((job_desc->max_nodes == 0) ||
			    (job_desc->max_nodes == NO_VAL)) {
				job_nodes = 1;
				info("%s: user %u submitted job with swap "
				     "space specification, but no node count "
				     "specification",
				     __func__, job_desc->user_id);

			} else {
				job_nodes = job_desc->max_nodes;
			}
			xstrfmtcat(job_desc->burst_buffer,
				   "SLURM_SWAP=%uGB(%uNodes)",
				   swap_cnt, job_nodes);
			gb_cnt += swap_cnt * job_nodes;
		}
		if (gb_cnt) {
			if (job_desc->burst_buffer)
				xstrcat(job_desc->burst_buffer, " ");
			xstrfmtcat(job_desc->burst_buffer, "SLURM_SIZE=%u",
				   gb_cnt);
		}
		if (node_cnt) {
			if (job_desc->burst_buffer)
				xstrcat(job_desc->burst_buffer, " ");
			xstrfmtcat(job_desc->burst_buffer,
				   "SLURM_GRES=nodes:%u", node_cnt);
		}
	}

	return rc;
}

/* Parse interactive burst_buffer options into an appropriate burst_buffer
 * argument */
static int _parse_interactive(struct job_descriptor *job_desc)
{
	char *capacity, *end_ptr = NULL, *tok;
	int64_t raw_cnt;
	uint32_t gb_cnt = 0, node_cnt = 0, swap_cnt = 0;
	int rc = SLURM_SUCCESS;

	if (!job_desc->burst_buffer)
		return rc;

	tok = job_desc->burst_buffer;
	while ((capacity = strstr(tok, "capacity="))) {
		raw_cnt = strtol(capacity + 9, &end_ptr, 10);
		if (raw_cnt <= 0) {
			rc = ESLURM_INVALID_BURST_BUFFER_CHANGE;
			break;
		}
		if ((end_ptr[0] == 'n') || (end_ptr[0] == 'N')) {
			node_cnt += raw_cnt;
		} else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M')) {
			raw_cnt = (raw_cnt + 1023) / 1024;
			gb_cnt += raw_cnt;
		} else if ((end_ptr[0] == 'g') || (end_ptr[0] == 'G')) {
			gb_cnt += raw_cnt;
		} else if ((end_ptr[0] == 't') || (end_ptr[0] == 'T')) {
			raw_cnt *= 1024;
			gb_cnt += raw_cnt;
		} else if ((end_ptr[0] == 'p') || (end_ptr[0] == 'P')) {
			raw_cnt *= (1024 * 1024);
			gb_cnt += raw_cnt;
		}
		tok = capacity + 9;
	}

	if ((tok = strstr(job_desc->burst_buffer, "swap=")))
		swap_cnt = strtol(tok + 5, &end_ptr, 10);

	if ((rc == SLURM_SUCCESS) && (gb_cnt || node_cnt || swap_cnt)) {
		if (swap_cnt) {
			uint32_t job_nodes;
			if ((job_desc->max_nodes == 0) ||
			    (job_desc->max_nodes == NO_VAL)) {
				job_nodes = 1;
				info("%s: user %u submitted job with swap "
				     "space specification, but no node count "
				     "specification",
				     __func__, job_desc->user_id);

			} else {
				job_nodes = job_desc->max_nodes;
			}
			xstrfmtcat(job_desc->burst_buffer,
				   " SLURM_SWAP=%uGB(%uNodes)",
				   swap_cnt, job_nodes);
			gb_cnt += swap_cnt * job_nodes;
		}
		if (gb_cnt) {
			xstrfmtcat(job_desc->burst_buffer, " SLURM_SIZE=%u",
				   gb_cnt);
		}
		if (node_cnt) {
			xstrfmtcat(job_desc->burst_buffer,
				   "SLURM_GRES=nodes:%u", node_cnt);
		}
	}

	return rc;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	pthread_attr_t attr;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_load_config(&bb_state, "cray");
	_test_config();
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);
	bb_alloc_cache(&bb_state);
	slurm_attr_init(&attr);
	if (pthread_create(&bb_state.bb_thread, &attr, _bb_agent, NULL))
		error("Unable to start backfill thread: %m");
	if (!state_save_loc)
		state_save_loc = slurm_get_state_save_location();
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
	xfree(state_save_loc);
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
	bb_load_config(&bb_state, "cray");
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
	if (bb_state.bb_config.debug_flag)
		info("%s: %s",  __func__, plugin_type);
	packstr((char *)plugin_type, buffer);	/* Remove "const" qualifier */
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
	if (bb_state.bb_config.debug_flag)
		info("%s: record_count:%u",  __func__, rec_count);
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
	int32_t bb_size = 0;
	char *key;
	int i, rc;

	if ((rc = _parse_bb_opts(job_desc)) != SLURM_SUCCESS)
		return rc;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_user_id:%u, submit_uid:%d",
		     plugin_type, __func__, job_desc->user_id, submit_uid);
		info("%s: burst_buffer:%s", __func__, job_desc->burst_buffer);
		info("%s: script:%s", __func__, job_desc->script);
	}

	if (job_desc->burst_buffer) {
		key = strstr(job_desc->burst_buffer, "SLURM_SIZE=");
		if (key) {
			bb_size = bb_get_size_num(key + 11,
						bb_state.bb_config.granularity);
		}
	}
	if (bb_size == 0)
		return SLURM_SUCCESS;
	if (bb_size < 0)
		return ESLURM_BURST_BUFFER_LIMIT;

	pthread_mutex_lock(&bb_state.bb_mutex);
	if (((bb_state.bb_config.job_size_limit  != NO_VAL) &&
	     (bb_size > bb_state.bb_config.job_size_limit)) ||
	    ((bb_state.bb_config.user_size_limit != NO_VAL) &&
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
		info("Job from user %u requested burst buffer size of %u, "
		     "but total space is only %u",
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
extern int bb_p_job_validate2(struct job_record *job_ptr, char **err_msg)
{
	char *base_dir = NULL, *script_file = NULL, *setup_env_file = NULL;
	char *data_in_env_file = NULL, *pre_run_env_file = NULL;
	char *post_run_env_file = NULL, *data_out_env_file = NULL;
	char *teardown_env_file = NULL, *bbs_job_process = NULL;
	int hash_inx, rc = SLURM_SUCCESS;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return rc;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}

	pthread_mutex_lock(&bb_state.bb_mutex);
	hash_inx = job_ptr->job_id % 10;
	xstrfmtcat(base_dir, "%s/hash.%d/job.%u", state_save_loc, hash_inx,
		   job_ptr->job_id);
	xstrfmtcat(script_file, "%s/script", base_dir);
	xstrfmtcat(setup_env_file, "%s/setup_env", base_dir);
	xstrfmtcat(data_in_env_file, "%s/data_in_env", base_dir);
	xstrfmtcat(pre_run_env_file, "%s/pre_run_env", base_dir);
	xstrfmtcat(post_run_env_file, "%s/post_run_env", base_dir);
	xstrfmtcat(data_out_env_file, "%s/data_out_env", base_dir);
	xstrfmtcat(teardown_env_file, "%s/teardown_env", base_dir);
	pthread_mutex_unlock(&bb_state.bb_mutex);


	bbs_job_process = _set_cmd_path("bbs_job_process");
#if 0
//FIXME: Execute bbs_job_process in-line. It should be fast
	if (error) {
		xfree(*err_msg);
		*err_msg = xstrdup("WHATEVER");
		rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
	}
#endif
	xfree(bbs_job_process);

	xfree(base_dir);
	xfree(script_file);
	xfree(setup_env_file);
	xfree(data_in_env_file);
	xfree(pre_run_env_file);
	xfree(post_run_env_file);
	xfree(data_out_env_file);
	xfree(teardown_env_file);

	return rc;
}

/*
 * For a given job, return our best guess if when it might be able to start
 */
extern time_t bb_p_job_get_est_start(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;
	time_t est_start = time(NULL);
	uint32_t bb_size;
	int rc;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    ((bb_size = _get_bb_size(job_ptr)) == 0))
		return est_start;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}

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
	uint32_t bb_size;
	int rc;

	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);

	/* Identify candidates to be allocated burst buffers */
	job_candidates = list_create(bb_job_queue_del);
	job_iter = list_iterator_create(job_queue);
	while ((job_ptr = list_next(job_iter))) {
		if (!IS_JOB_PENDING(job_ptr) ||
		    (job_ptr->start_time == 0) ||
		    (job_ptr->burst_buffer == NULL) ||
		    (job_ptr->burst_buffer[0] == '\0'))
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

		(void) _alloc_job_bb(job_ptr, bb_size);
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
	uint32_t bb_size = 0;
	int rc = 1;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    ((bb_size = _get_bb_size(job_ptr)) == 0))
		return rc;
	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}
	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		debug("%s: job_id:%u bb_rec not found",
		      __func__, job_ptr->job_id);
		rc = -1;
		if ((test_only == false) &&
		    (_test_size_limit(job_ptr, bb_size) == 0)) {
			if (_alloc_job_bb(job_ptr, bb_size) != SLURM_SUCCESS) {
				rc = -1;
			}
		}
	} else {
		if (bb_ptr->state < BB_STATE_STAGED_IN)
			_load_state(job_ptr->job_id);
		if (bb_ptr->state < BB_STATE_STAGED_IN) {
			rc = 0;
		} else if (bb_ptr->state == BB_STATE_STAGED_IN) {
			rc = 1;
		} else {
			error("%s: job_id:%u bb_state:%u",
			      __func__, job_ptr->job_id, bb_ptr->state);
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
	char *pre_run_env_file = NULL, *client_nodes_file_nid = NULL;
	int hash_inx, rc = SLURM_SUCCESS;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return rc;
	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}

	if (!job_ptr->job_resrcs || !job_ptr->job_resrcs->nodes) {
		error("%s: Job %u lacks node allocation", __func__,
		      job_ptr->job_id);
		return SLURM_ERROR;
	}

	pthread_mutex_lock(&bb_state.bb_mutex);
	hash_inx = job_ptr->job_id % 10;
	xstrfmtcat(pre_run_env_file, "%s/hash.%d/job.%upre_run_env",
		   state_save_loc, hash_inx, job_ptr->job_id);
	xstrfmtcat(client_nodes_file_nid,
		   "%s/hash.%d/job.%u/client_nids",
		   state_save_loc, hash_inx, job_ptr->job_id);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	rc = _write_nid_file(client_nodes_file_nid, job_ptr->job_resrcs->nodes);
	xfree(client_nodes_file_nid);

	return rc;
}

/*
 * Trigger a job's burst buffer stage-out to begin
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_start_stage_out(struct job_record *job_ptr)
{
//FIXME: How to handle various job terminate states (e.g. requeue, failure), user script controlled?
	bb_alloc_t *bb_ptr;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return SLURM_SUCCESS;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		/* No job buffers. Assuming use of persistent buffers only */
		debug("%s: job_id:%u bb_rec not found",
		      __func__, job_ptr->job_id);
	} else {
		_start_stage_out(job_ptr->job_id);
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

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}
	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return 1;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		/* No job buffers. Assuming use of persistent buffers only */
		debug("%s: job_id:%u bb_rec not found",
		      __func__, job_ptr->job_id);
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
			error("%s: job_id:%u bb_state:%u",
			      __func__, job_ptr->job_id, bb_ptr->state);
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

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s",  __func__, plugin_type);
		info("%s: job_id:%u", __func__, job_ptr->job_id);
	}

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return SLURM_SUCCESS;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	_teardown(job_ptr->job_id, true);
	if (bb_ptr) {
		bb_ptr->cancelled = true;
		bb_ptr->end_time = 0;
		bb_ptr->state = BB_STATE_STAGED_OUT;
		bb_ptr->state_time = time(NULL);
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}
