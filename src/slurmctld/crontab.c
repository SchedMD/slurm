/*****************************************************************************\
 *  crontab.c
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/cron.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"

typedef struct {
	char *alloc_node;
	uid_t uid;
	gid_t gid;
	char **err_msg;
	char **failed_lines;
	List new_jobs;
	uint16_t protocol_version;
	int return_code;
} foreach_cron_job_args_t;

static int _handle_job(void *x, void *y)
{
	job_desc_msg_t *job = (job_desc_msg_t *) x;
	foreach_cron_job_args_t *args = (foreach_cron_job_args_t *) y;
	job_record_t *job_ptr = NULL;

	dump_job_desc(job);

	if (!job->crontab_entry || !valid_cron_entry(job->crontab_entry)) {
		error("crontab submission failed due to missing or invalid cron_entry_t");
		args->return_code = SLURM_ERROR;
		return -1;
	}

	/*
	 * The trick to scrontab: use the begin time to gate when the job can
	 * next run. On requeue, the job will need to recalculate this to
	 * determine the next valid interval.
	 */
	job->begin_time = calc_next_cron_start(job->crontab_entry);

	/*
	 * always use the authenticated values from crontab_update_request_msg_t
	 */
	job->user_id = args->uid;
	job->group_id = args->gid;

	xfree(job->alloc_node);
	job->alloc_node = xstrdup(args->alloc_node);

	/* enforce this flag so the job submit plugin can differentiate */
	job->bitflags |= CRON_JOB;

	/* give job_submit a chance to play with it first */
	args->return_code = validate_job_create_req(job, args->uid,
						    args->err_msg);

	if (args->return_code) {
		xstrfmtcat(*args->failed_lines, "%u-%u",
			   ((cron_entry_t *) job->crontab_entry)->line_start,
			   ((cron_entry_t *) job->crontab_entry)->line_end);
		return -1;
	}

	args->return_code = job_allocate(job, 0, false, NULL, 0, args->uid,
					 true, &job_ptr, args->err_msg,
					 args->protocol_version);

	/*
	 * job_allocate() will return non-terminal error codes.
	 * job rejection is designated by the job being set to JOB_FAILED
	 */
	if (job_ptr)
		list_push(args->new_jobs, job_ptr);
	if (job_ptr && job_ptr->job_state != JOB_FAILED)
		args->return_code = SLURM_SUCCESS;

	if (args->return_code) {
		xstrfmtcat(*args->failed_lines, "%u-%u",
			   ((cron_entry_t *) job->crontab_entry)->line_start,
			   ((cron_entry_t *) job->crontab_entry)->line_end);
		return -1;
	} else {
		xassert(job_ptr->details);
		job_ptr->details->crontab_entry = job->crontab_entry;
		job->crontab_entry = NULL;

		/*
		 * Ignore user-provided value since this is not guaranteed
		 * to be in sync with the bitstring data. Reconstruct,
		 * even though the reconstructed version will be uglier.
		 */
		xfree(job_ptr->details->crontab_entry->cronspec);
		job_ptr->details->crontab_entry->cronspec =
			cronspec_from_cron_entry(
				job_ptr->details->crontab_entry);

		info("Added %pJ from crontab entry from uid=%u, next start is %lu",
		     job_ptr, job->user_id, job->begin_time);
	}

	return 0;
}

static int _purge_job(void *x, void *ignored)
{
	job_record_t *job_ptr = (job_record_t *) x;
	purge_job_record(job_ptr->job_id);
	return 0;
}

/*
 * Clear the CRON_JOB flag for all jobs by a given user.
 */
static int _clear_requeue_cron(void *x, void *y)
{
	job_record_t *job_ptr = (job_record_t *) x;
	uid_t *uid = (uid_t *) y;

	if ((job_ptr->user_id == *uid) &&
	    (job_ptr->bit_flags & CRON_JOB)) {
		job_ptr->bit_flags &= ~CRON_JOB;
		job_ptr->job_state &= ~JOB_REQUEUE;

		if (!IS_JOB_RUNNING(job_ptr)) {
			time_t now = time(NULL);
			job_ptr->job_state = JOB_CANCELLED;
			job_ptr->start_time = now;
			job_ptr->end_time = now;
			job_ptr->exit_code = 1;
			job_completion_logger(job_ptr, false);
		}
	}

	return 0;
}

static int _set_requeue_cron(void *x, void *y)
{
	job_record_t *job_ptr = (job_record_t *) x;
	bool *set = (bool *) y;

	if (*set)
		job_ptr->bit_flags |= CRON_JOB;
	else
		job_ptr->bit_flags &= ~CRON_JOB;

	return 0;
}

static int _copy_jobids(void *x, void *y)
{
	job_record_t *job_ptr = (job_record_t *) x;
	crontab_update_response_msg_t *response =
		(crontab_update_response_msg_t *) y;

	response->jobids[response->jobids_count++] = job_ptr->job_id;

	return 0;
}

extern void crontab_submit(crontab_update_request_msg_t *request,
			   crontab_update_response_msg_t *response,
			   char *alloc_node, uint16_t protocol_version)
{
	foreach_cron_job_args_t args;
	char *dir = NULL, *file = NULL;

	memset(&args, 0, sizeof(args));

	xstrfmtcat(dir, "%s/crontab", slurm_conf.state_save_location);
	xstrfmtcat(file, "%s/crontab.%u", dir, request->uid);

	(void) mkdir(dir, 0700);
	xfree(dir);

	memset(response, 0, sizeof(*response));

	debug("%s: updating crontab for uid=%u", __func__, request->uid);

	if (!request->crontab) {
		debug("%s: removing crontab for uid=%u",
		      __func__, request->uid);

		(void) unlink(file);
		xfree(file);
	} else if (!request->jobs) {
		debug("%s: no jobs submitted alongside crontab for uid=%u",
		      __func__, request->uid);
	} else {
		/*
		 * Already authenticated upstream.
		 */
		args.alloc_node = alloc_node;
		args.uid = request->uid;
		args.gid = request->gid;
		args.err_msg = &response->err_msg;
		args.failed_lines = &response->failed_lines;
		args.new_jobs = list_create(NULL);
		args.protocol_version = protocol_version;
		args.return_code = SLURM_SUCCESS;

		list_for_each(request->jobs, _handle_job, &args);
		response->return_code = args.return_code;
	}

	/* on submission failure kill all newly created jobs */
	if (response->return_code) {
		int purged = list_for_each(args.new_jobs, _purge_job, NULL);
		debug("%s: failed crontab submission, purged %d records",
		      __func__, purged);
	} else {
		bool off = false, on = true;

		/*
		 * Flip the CRON_JOB flag off temporarily to avoid cancelling
		 * these new jobs.
		 */
		if (args.new_jobs)
			list_for_each(args.new_jobs, _set_requeue_cron, &off);

		/* on success, kill/modify old jobs */
		list_for_each(job_list, _clear_requeue_cron, &request->uid);

		/*
		 * Flip the flag on now that the old ones have been removed.
		 */
		if (args.new_jobs) {
			list_for_each(args.new_jobs, _set_requeue_cron, &on);
			response->jobids = xcalloc(list_count(args.new_jobs),
						   sizeof(uint32_t));
			list_for_each(args.new_jobs, _copy_jobids, response);
		}

		/*
		 * save the new file (if defined)
		 */
		if (request->crontab &&
		    write_data_to_file(file, request->crontab)) {
			error("%s: failed to save file", __func__);
			response->return_code = ESLURM_WRITING_TO_FILE;
		}
	}

	xfree(file);
	FREE_NULL_LIST(args.new_jobs);
}

extern void crontab_add_disabled_lines(uid_t uid, int line_start, int line_end)
{
	char *file = NULL, *lines = NULL;
	int fd;

	xstrfmtcat(file, "%s/crontab/crontab.%u",
		   slurm_conf.state_save_location, uid);
	xstrfmtcat(lines, "%d-%d,", line_start, line_end);

	if ((fd = open(file, O_WRONLY | O_CLOEXEC | O_APPEND)) < 0) {
		error("%s: failed to open file `%s`", __func__, file);
		xfree(file);
		return;
	}

	safe_write(fd, lines, strlen(lines));
	close(fd);
	xfree(file);
	xfree(lines);
	return;

rwfail:
	error("%s: failed to append failed lines %d-%d to file `%s`",
	      __func__, line_start, line_end, file);
	close(fd);
	xfree(file);
	xfree(lines);
	return;
}
