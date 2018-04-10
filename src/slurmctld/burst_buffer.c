/*****************************************************************************\
 *  burst_buffer.c - driver for burst buffer infrastructure and plugin
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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/reservation.h"

typedef struct slurm_bb_ops {
	uint64_t	(*get_system_size)	(void);
	int		(*load_state)	(bool init_config);
	char *		(*get_status)	(uint32_t argc, char **argv);
	int		(*state_pack)	(uid_t uid, Buf buffer,
					 uint16_t protocol_version);
	int		(*reconfig)	(void);
	int		(*job_validate)	(struct job_descriptor *job_desc,
					 uid_t submit_uid);
	int		(*job_validate2)(struct job_record *job_ptr,
					 char **err_msg);
	void		(*job_set_tres_cnt) (struct job_record *job_ptr,
					     uint64_t *tres_cnt, bool locked);
	time_t		(*job_get_est_start) (struct job_record *job_ptr);
	int		(*job_try_stage_in) (List job_queue);
	int		(*job_test_stage_in) (struct job_record *job_ptr,
					      bool test_only);
	int		(*job_begin) (struct job_record *job_ptr);
	int		(*job_revoke_alloc) (struct job_record *job_ptr);
	int		(*job_start_stage_out) (struct job_record *job_ptr);
	int		(*job_test_post_run) (struct job_record *job_ptr);
	int		(*job_test_stage_out) (struct job_record *job_ptr);
	int		(*job_cancel) (struct job_record *job_ptr);
	char *		(*xlate_bb_2_tres_str) (char *burst_buffer);
} slurm_bb_ops_t;

/*
 * Must be synchronized with slurm_bb_ops_t above.
 */
static const char *syms[] = {
	"bb_p_get_system_size",
	"bb_p_load_state",
	"bb_p_get_status",
	"bb_p_state_pack",
	"bb_p_reconfig",
	"bb_p_job_validate",
	"bb_p_job_validate2",
	"bb_p_job_set_tres_cnt",
	"bb_p_job_get_est_start",
	"bb_p_job_try_stage_in",
	"bb_p_job_test_stage_in",
	"bb_p_job_begin",
	"bb_p_job_revoke_alloc",
	"bb_p_job_start_stage_out",
	"bb_p_job_test_post_run",
	"bb_p_job_test_stage_out",
	"bb_p_job_cancel",
	"bb_p_xlate_bb_2_tres_str"
};

static int g_context_cnt = -1;
static slurm_bb_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static char *bb_plugin_list = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * Initialize the burst buffer infrastructure.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *names;
	char *plugin_type = "burst_buffer";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	bb_plugin_list = slurm_get_bb_type();
	g_context_cnt = 0;
	if ((bb_plugin_list == NULL) || (bb_plugin_list[0] == '\0'))
		goto fini;

	names = bb_plugin_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrealloc(ops, (sizeof(slurm_bb_ops_t) * (g_context_cnt + 1)));
		xrealloc(g_context,
			 (sizeof(plugin_context_t *) * (g_context_cnt + 1)));
		if (xstrncmp(type, "burst_buffer/", 13) == 0)
			type += 13; /* backward compatibility */
		type = xstrdup_printf("burst_buffer/%s", type);
		g_context[g_context_cnt] = plugin_context_create(
			plugin_type, type, (void **)&ops[g_context_cnt],
			syms, sizeof(syms));
		if (!g_context[g_context_cnt]) {
			error("cannot create %s context for %s",
			      plugin_type, type);
			rc = SLURM_ERROR;
			xfree(type);
			break;
		}

		xfree(type);
		g_context_cnt++;
		names = NULL; /* for next iteration */
	}
	init_run = true;

fini:
	slurm_mutex_unlock(&g_context_lock);

	if (rc != SLURM_SUCCESS)
		bb_g_fini();

	return rc;
}

/*
 * Terminate the burst buffer infrastructure. Free memory.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_fini(void)
{
	int i, j, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt < 0)
		goto fini;

	init_run = false;
	for (i = 0; i < g_context_cnt; i++) {
		if (g_context[i]) {
			j = plugin_context_destroy(g_context[i]);
			if (j != SLURM_SUCCESS)
				rc = j;
		}
	}
	xfree(ops);
	xfree(g_context);
	xfree(bb_plugin_list);
	g_context_cnt = -1;

fini:	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Load the current burst buffer state (e.g. how much space is available now).
 * Run at the beginning of each scheduling cycle in order to recognize external
 * changes to the burst buffer state (e.g. capacity is added, removed, fails,
 * etc.).
 *
 * init_config IN - true if called as part of slurmctld initialization
 * Returns a Slurm errno.
 */
extern int bb_g_load_state(bool init_config)
{
	DEF_TIMERS;
	int i, rc, rc2;

	START_TIMER;
	rc = bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		rc2 = (*(ops[i].load_state))(init_config);
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Return string containing current burst buffer status
 * argc IN - count of status command arguments
 * argv IN - status command arguments
 * RET status string, release memory using xfree()
 */
extern char *bb_g_get_status(uint32_t argc, char **argv)
{
	DEF_TIMERS;
	int i;
	char *status = NULL, *tmp;

	START_TIMER;
	(void) bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		tmp = (*(ops[i].get_status))(argc, argv);
		if (status) {
			xstrcat(status, tmp);
			xfree(tmp);
		} else {
			status = tmp;
		}
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return status;
}

/*
 * Pack current burst buffer state information for network transmission to
 * user (e.g. "scontrol show burst")
 *
 * Returns a Slurm errno.
 */
extern int bb_g_state_pack(uid_t uid, Buf buffer, uint16_t protocol_version)
{
	DEF_TIMERS;
	int i, rc, rc2;
	uint32_t rec_count = 0;
	int eof, last_offset, offset;

	START_TIMER;
	offset = get_buf_offset(buffer);
	pack32(rec_count, buffer);
	rc = bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		last_offset = get_buf_offset(buffer);
		rc2 = (*(ops[i].state_pack))(uid, buffer, protocol_version);
		if (last_offset != get_buf_offset(buffer))
			rec_count++;
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	if (rec_count != 0) {
		eof = get_buf_offset(buffer);
		set_buf_offset(buffer, offset);
		pack32(rec_count, buffer);
		set_buf_offset(buffer, eof);
	}
	END_TIMER2(__func__);

	return rc;
}

/*
 * Note configuration may have changed. Handle changes in BurstBufferParameters.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_reconfig(void)
{
	DEF_TIMERS;
	int i, rc, rc2;

	START_TIMER;
	rc = bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		rc2 = (*(ops[i].reconfig))();
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Give the total burst buffer size in MB of a given plugin name (e.g. "cray");.
 * If "name" is NULL, return the total space of all burst buffer plugins.
 */
extern uint64_t bb_g_get_system_size(char *name)
{
	uint64_t size = 0;
	int i, offset = 0;

	(void) bb_g_init();

	if (xstrncmp(name, "burst_buffer/", 13))
		offset = 13;

	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {

		if (g_context[i] && !xstrcmp(g_context[i]->type+offset, name)) {
			size = (*(ops[i].get_system_size))();
			break;
		}
	}
	slurm_mutex_unlock(&g_context_lock);

	return size;
}

/*
 * Preliminary validation of a job submit request with respect to burst buffer
 * options. Performed after setting default account + qos, but prior to
 * establishing job ID or creating script file.
 *
 * job_desc IN - Job submission request
 * submit_uid IN - ID of the user submitting the job.
 * Returns a Slurm errno.
 */
extern int bb_g_job_validate(struct job_descriptor *job_desc,
			     uid_t submit_uid)
{
	DEF_TIMERS;
	int i, rc, rc2;

	START_TIMER;
	rc = bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_validate))(job_desc, submit_uid);
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Secondary validation of a job submit request with respect to burst buffer
 * options. Performed after establishing job ID and creating script file.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_validate2(struct job_record *job_ptr, char **err_msg)
{
	DEF_TIMERS;
	int i, rc, rc2;

	START_TIMER;
	rc = bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_validate2))(job_ptr, err_msg);
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}


/* Return true if pack job separator in the script */
static bool _pack_check(char *tok)
{
	if (strncmp(tok + 1, "SLURM",  5) &&
	    strncmp(tok + 1, "SBATCH", 6))
		return false;
	if (!strstr(tok+6, "packjob"))
		return false;
	return true;
}

/*
 * Convert a pack job batch script into a script containing only the portions
 * relevant to a specific pack job component.
 *
 * script IN - Whole job batch script
 * pack_job_offset IN - Zero origin pack job component ID
 * RET script for that job component, call xfree() to release memory
 */
extern char *bb_g_build_pack_script(char *script, uint32_t pack_job_offset)
{
	char *result = NULL, *tmp = NULL;
	char *tok, *save_ptr = NULL;
	bool fini = false;
	int cur_offset = 0;
	DEF_TIMERS;

	START_TIMER;
	tmp = xstrdup(script);
	tok = strtok_r(tmp, "\n", &save_ptr);
	while (tok) {
		if (!result) {
			xstrfmtcat(result, "%s\n", tok);
		} else if (tok[0] != '#') {
			fini = true;
		} else if (_pack_check(tok)) {
			cur_offset++;
			if (cur_offset > pack_job_offset)
				fini = true;
		} else if (cur_offset == pack_job_offset) {
			xstrfmtcat(result, "%s\n", tok);
		}
		if (fini)
			break;
		tok = strtok_r(NULL, "\n", &save_ptr);
	}

	if (pack_job_offset == 0) {
		while (tok) {
			char *sep = "";
			if ((tok[0] == '#') &&
			    (((tok[1] == 'B') && (tok[2] == 'B')) ||
			     ((tok[1] == 'D') && (tok[2] == 'W')))) {
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
	END_TIMER2(__func__);

	return result;
}

/*
 * Fill in the tres_cnt (in MB) based off the job record
 * NOTE: Based upon job-specific burst buffers, excludes persistent buffers
 * IN job_ptr - job record
 * IN/OUT tres_cnt - fill in this already allocated array with tres_cnts
 * IN locked - if the assoc_mgr tres read locked is locked or not
 */
extern void bb_g_job_set_tres_cnt(struct job_record *job_ptr,
				  uint64_t *tres_cnt,
				  bool locked)
{
	DEF_TIMERS;
	int i;

	START_TIMER;
	(void) bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		(*(ops[i].job_set_tres_cnt))(job_ptr, tres_cnt, locked);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);
}

/* sort jobs by expected start time */
static int _sort_job_queue(void *x, void *y)
{
	struct job_record *job_ptr1 = *(struct job_record **) x;
	struct job_record *job_ptr2 = *(struct job_record **) y;
	time_t t1, t2;

	t1 = job_ptr1->start_time;
	t2 = job_ptr2->start_time;
	if (t1 > t2)
		return 1;
	if (t1 < t2)
		return -1;
	return 0;
}

/*
 * For a given job, return our best guess if when it might be able to start
 */
extern time_t bb_g_job_get_est_start(struct job_record *job_ptr)
{
	DEF_TIMERS;
	int i;
	time_t start_time = time(NULL), new_time;

	START_TIMER;
	if (bb_g_init() != SLURM_SUCCESS)
		return start_time + 24 * 60 * 60;
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		new_time = (*(ops[i].job_get_est_start))(job_ptr);
		start_time = MAX(start_time, new_time);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return start_time;
}
/*
 * Allocate burst buffers to jobs expected to start soonest
 * Job records must be read locked
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_try_stage_in(void)
{
	DEF_TIMERS;
	int i, rc = 1, rc2;
	ListIterator job_iterator;
	struct job_record *job_ptr;
	time_t now = time(NULL);
	List job_queue;

	START_TIMER;
	job_queue = list_create(NULL);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_PENDING(job_ptr))
			continue;
		if ((job_ptr->burst_buffer == NULL) ||
		    (job_ptr->burst_buffer[0] == '\0'))
			continue;
		if ((job_ptr->start_time == 0) ||
		    (job_ptr->start_time > now + 10 * 60 * 60))	/* ten hours */
			continue;
		list_push(job_queue, job_ptr);
	}
	list_iterator_destroy(job_iterator);
	list_sort(job_queue, _sort_job_queue);

	rc = bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_try_stage_in))(job_queue);
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	FREE_NULL_LIST(job_queue);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Determine if a job's burst buffer stage-in is complete
 * job_ptr IN - Job to test
 * test_only IN - If false, then attempt to load burst buffer if possible
 *
 * RET: 0 - stage-in is underway
 *      1 - stage-in complete
 *     -1 - stage-in not started or burst buffer in some unexpected state
 */
extern int bb_g_job_test_stage_in(struct job_record *job_ptr, bool test_only)
{
	DEF_TIMERS;
	int i, rc = 1, rc2;

	START_TIMER;
	if (bb_g_init() != SLURM_SUCCESS)
		rc = -1;
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_test_stage_in))(job_ptr, test_only);
		rc = MIN(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/* Attempt to claim burst buffer resources.
 * At this time, bb_g_job_test_stage_in() should have been run sucessfully AND
 * the compute nodes selected for the job.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_begin(struct job_record *job_ptr)
{
	DEF_TIMERS;
	int i, rc = SLURM_SUCCESS, rc2;

	START_TIMER;
	if (bb_g_init() != SLURM_SUCCESS)
		rc = SLURM_ERROR;
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_begin))(job_ptr);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/* Revoke allocation, but do not release resources.
 * Executed after bb_g_job_begin() if there was an allocation failure.
 * Does not release previously allocated resources.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_revoke_alloc(struct job_record *job_ptr)
{
	DEF_TIMERS;
	int i, rc = SLURM_SUCCESS, rc2;

	START_TIMER;
	if (bb_g_init() != SLURM_SUCCESS)
		rc = SLURM_ERROR;
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_revoke_alloc))(job_ptr);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Trigger a job's burst buffer stage-out to begin
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_start_stage_out(struct job_record *job_ptr)
{
	DEF_TIMERS;
	int i, rc, rc2;

	START_TIMER;
	rc = bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_start_stage_out))(job_ptr);
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Determine if a job's burst buffer post_run operation is complete
 *
 * RET: 0 - post_run is underway
 *      1 - post_run complete
 *     -1 - fatal error
 */
extern int bb_g_job_test_post_run(struct job_record *job_ptr)
{
	DEF_TIMERS;
	int i, rc = 1, rc2;

	START_TIMER;
	if (bb_g_init() != SLURM_SUCCESS)
		rc = -1;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return rc;	/* No burst buffers to stage out */

	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_test_post_run))(job_ptr);
		rc = MIN(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Determine if a job's burst buffer stage-out is complete
 *
 * RET: 0 - stage-out is underway
 *      1 - stage-out complete
 *     -1 - fatal error
 */
extern int bb_g_job_test_stage_out(struct job_record *job_ptr)
{
	DEF_TIMERS;
	int i, rc = 1, rc2;

	START_TIMER;
	if (bb_g_init() != SLURM_SUCCESS)
		rc = -1;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return rc;	/* No burst buffers to stage out */

	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_test_stage_out))(job_ptr);
		rc = MIN(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	if ((rc != 0) && (job_ptr->mail_type & MAIL_JOB_STAGE_OUT)) {
		mail_job_info(job_ptr, MAIL_JOB_STAGE_OUT);
		job_ptr->mail_type &= (~MAIL_JOB_STAGE_OUT);
	}

	return rc;
}

/*
 * Terminate any file staging and completely release burst buffer resources
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_cancel(struct job_record *job_ptr)
{
	DEF_TIMERS;
	int i, rc, rc2;

	START_TIMER;
	rc = bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		rc2 = (*(ops[i].job_cancel))(job_ptr);
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Translate a burst buffer string to it's equivalent TRES string
 * (e.g. "cray:2G,generic:4M" -> "1004=2048,1005=4")
 * Caller must xfree the return value
 */
extern char *bb_g_xlate_bb_2_tres_str(char *burst_buffer)
{
	DEF_TIMERS;
	int i;
	char *tmp = NULL, *tmp2;

	START_TIMER;
	(void) bb_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		tmp2 = (*(ops[i].xlate_bb_2_tres_str))(burst_buffer);
		if (!tmp) {
			tmp = tmp2;
		} else {
			xstrcat(tmp, ",");
			xstrcat(tmp, tmp2);
			xfree(tmp2);
		}
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return tmp;
}
