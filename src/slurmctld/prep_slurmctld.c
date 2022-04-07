/*****************************************************************************\
 *  prep_slurmctld.c - slurmctld-specific aspects of the PrEpPlugin interface
 *		       (for PrologSlurmctld / EpilogSlurmctld scripts)
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

#include <signal.h>

#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

extern void prep_prolog_slurmctld_callback(int rc, uint32_t job_id)
{
	slurmctld_lock_t job_write_lock =
		{ .job = WRITE_LOCK, .node = WRITE_LOCK, .fed = READ_LOCK };
	job_record_t *job_ptr;

	lock_slurmctld(job_write_lock);
	if (!(job_ptr = find_job_record(job_id))) {
		error("%s: missing JobId=%u", __func__, job_id);
		unlock_slurmctld(job_write_lock);
		return;
	}
	if (WEXITSTATUS(rc)) {
		error("prolog_slurmctld JobId=%u prolog exit status %u:%u",
		      job_id, WEXITSTATUS(rc), WTERMSIG(rc));
		job_ptr->prep_prolog_failed = true;
	}

	/* prevent underflow */
	if (job_ptr->prep_prolog_cnt)
		job_ptr->prep_prolog_cnt--;

	if (job_ptr->prep_prolog_cnt) {
		debug2("%s: still %u async prologs left to complete",
		       __func__, job_ptr->prep_prolog_cnt);
		unlock_slurmctld(job_write_lock);
		return;
	}

	/* all async prologs have completed, continue on now */
	if (job_ptr->prep_prolog_failed) {
		uint32_t jid = job_id;

		job_ptr->prep_prolog_failed = false;

		/* requeue het leader if het job */
		if (job_ptr->het_job_id)
			jid = job_ptr->het_job_id;

		if ((rc = job_requeue(0, jid, NULL, false, 0)) &&
		    (rc != ESLURM_JOB_PENDING)) {
			info("unable to requeue JobId=%u: %s", jid,
			     slurm_strerror(rc));

			srun_user_message(job_ptr,
					  "PrologSlurmctld failed, job killed");

			if (job_ptr->het_job_id) {
				job_record_t *het_leader = job_ptr;

				if (!het_leader->het_job_list) {
					het_leader = find_job_record(
						job_ptr->het_job_id);
				}

				/*
				 * Don't do anything if there isn't a het_leader
				 * (which there should be).
				 */
				if (het_leader) {
					(void) het_job_signal(het_leader,
							      SIGKILL,
							      0, 0, false);
				} else {
					error("No het_leader found for %pJ",
					      job_ptr);
				}
			} else {
				job_signal(job_ptr, SIGKILL, 0, 0, false);
			}
		}
	} else
		debug2("prolog_slurmctld JobId=%u prolog completed", job_id);

	prolog_running_decr(job_ptr);

	unlock_slurmctld(job_write_lock);
}

extern void prep_epilog_slurmctld_callback(int rc, uint32_t job_id)
{
	slurmctld_lock_t job_write_lock = {
		.job = WRITE_LOCK, .node = WRITE_LOCK};
	job_record_t *job_ptr;

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(job_id);
	if (!(job_ptr = find_job_record(job_id))) {
		error("%s: missing JobId=%u", __func__, job_id);
		unlock_slurmctld(job_write_lock);
		return;
	}

	/* prevent underflow */
	if (job_ptr->prep_epilog_cnt)
		job_ptr->prep_epilog_cnt--;

	if (job_ptr->prep_epilog_cnt) {
		debug2("%s: still %u async epilogs left to complete",
		       __func__, job_ptr->prep_epilog_cnt);
		unlock_slurmctld(job_write_lock);
		return;
	}

	/* all async prologs have completed, continue on now */
	job_ptr->epilog_running = false;

	/*
	 * Clear the JOB_COMPLETING flag only if the node count is 0
	 * meaning the slurmd epilogs have already completed.
	 */
	if ((job_ptr->node_cnt == 0) && IS_JOB_COMPLETING(job_ptr)) {
		cleanup_completing(job_ptr);
		batch_requeue_fini(job_ptr);
	}

	unlock_slurmctld(job_write_lock);
}
