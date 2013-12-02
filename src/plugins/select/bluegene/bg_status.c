/*****************************************************************************\
 *  bg_status.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "bg_core.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/trigger_mgr.h"
#include "src/slurmctld/locks.h"

#define RETRY_BOOT_COUNT 3

static void _destroy_kill_struct(void *object);

static void _destroy_kill_struct(void *object)
{
	kill_job_struct_t *freeit = (kill_job_struct_t *)object;

	if (freeit) {
		xfree(freeit);
	}
}

static int _block_is_deallocating(bg_record_t *bg_record, List kill_job_list)
{
	int jobid = bg_record->job_running;

	if (bg_record->modifying)
		return SLURM_SUCCESS;

	if (bg_record->boot_state) {
		error("State went to free on a boot for block %s.",
		      bg_record->bg_block_id);
	} else if (bg_record->job_ptr && (jobid > NO_JOB_RUNNING)) {
		select_jobinfo_t *jobinfo =
			bg_record->job_ptr->select_jobinfo->data;
		if (kill_job_list) {
			kill_job_struct_t *freeit =
				(kill_job_struct_t *)
				xmalloc(sizeof(freeit));
			freeit->jobid = jobid;
			list_push(kill_job_list, freeit);
		}
		error("Block %s was in a ready state "
		      "for user %s but is being freed. "
		      "Job %d was lost.",
		      bg_record->bg_block_id,
		      jobinfo->user_name,
		      jobid);
	} else if (bg_record->job_list && list_count(bg_record->job_list)) {
		struct job_record *job_ptr;
		ListIterator itr = list_iterator_create(bg_record->job_list);
		while ((job_ptr = list_next(itr))) {
			select_jobinfo_t *jobinfo;

			if (job_ptr->magic != JOB_MAGIC)
				continue;

			jobinfo = job_ptr->select_jobinfo->data;
			if (!jobinfo->cleaning) {
				if (kill_job_list) {
					kill_job_struct_t *freeit =
						(kill_job_struct_t *)
						xmalloc(sizeof(freeit));
					freeit->jobid = job_ptr->job_id;
					list_push(kill_job_list, freeit);
				}
				error("Block %s was in a ready state "
				      "for user %s but is being freed. "
				      "Job %d was lost.",
				      bg_record->bg_block_id,
				      jobinfo->user_name,
				      job_ptr->job_id);
				jobinfo->cleaning = 1;
			}
		}
		list_iterator_destroy(itr);
	} else {
		debug("Block %s was in a ready state "
		      "but is being freed. No job running.",
		      bg_record->bg_block_id);
		/* Make sure block is cleaned up.  If there are
		 * running jobs on the block this happens when they
		 * are cleaned off. */
		bg_reset_block(bg_record, NULL);
	}

	remove_from_bg_list(bg_lists->booted, bg_record);

	return SLURM_SUCCESS;
}

extern int bg_status_update_block_state(bg_record_t *bg_record,
					uint16_t state,
					List kill_job_list)
{
	bool skipped_dealloc = false;
	kill_job_struct_t *freeit = NULL;
	int updated = 0;
	uint16_t real_state = bg_record->state & (~BG_BLOCK_ERROR_FLAG);

	if (real_state == state)
		return 0;

	debug("state of Block %s was %s and now is %s",
	      bg_record->bg_block_id,
	      bg_block_state_string(bg_record->state),
	      bg_block_state_string(state));

	/*
	  check to make sure block went
	  through freeing correctly
	*/
	if ((real_state != BG_BLOCK_TERM
	     && !(bg_record->state & BG_BLOCK_ERROR_FLAG))
	    && state == BG_BLOCK_FREE)
		skipped_dealloc = 1;
	else if ((real_state == BG_BLOCK_INITED)
		 && (state == BG_BLOCK_BOOTING)) {
		/* This means the user did a reboot through
		   mpirun but we missed the state
		   change */
		debug("Block %s skipped rebooting, "
		      "but it really is.",
		      bg_record->bg_block_id);
	} else if ((real_state == BG_BLOCK_TERM)
		   && (state == BG_BLOCK_BOOTING))
		/* This is a funky state IBM says
		   isn't a bug, but all their
		   documentation says this doesn't
		   happen, but IBM says oh yeah, you
		   weren't really suppose to notice
		   that. So we will just skip this
		   state and act like this didn't happen. */
		goto nochange_state;
	real_state = state;
	if (bg_record->state & BG_BLOCK_ERROR_FLAG)
		state |= BG_BLOCK_ERROR_FLAG;

	bg_record->state = state;

	if (real_state == BG_BLOCK_TERM || skipped_dealloc)
		_block_is_deallocating(bg_record, kill_job_list);
	else if (real_state == BG_BLOCK_BOOTING) {
		debug("Setting bootflag for %s", bg_record->bg_block_id);
		bg_record->boot_state = 1;
	} else if (real_state == BG_BLOCK_FREE) {
		/* Make sure block is cleaned up.  If there are
		 * running jobs on the block this happens when they
		 * are cleaned off. */
		if (bg_record->job_running == NO_JOB_RUNNING
		    && (!bg_record->job_list
			|| !list_count(bg_record->job_list)))
			bg_reset_block(bg_record, NULL);
		remove_from_bg_list(bg_lists->booted, bg_record);
		bg_record->action = BG_BLOCK_ACTION_NONE;
		/* This means the reason could of been set by the
		   action on the block, so clear it. */
		if (!(bg_record->state & BG_BLOCK_ERROR_FLAG))
			xfree(bg_record->reason);
	} else if (real_state & BG_BLOCK_ERROR_FLAG) {
		if (bg_record->boot_state)
			error("Block %s in an error state while booting.",
			      bg_record->bg_block_id);
		else
			error("Block %s in an error state.",
			      bg_record->bg_block_id);
		remove_from_bg_list(bg_lists->booted, bg_record);
		trigger_block_error();
	} else if (real_state == BG_BLOCK_INITED) {
		if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
			list_push(bg_lists->booted, bg_record);
	}
	updated = 1;
	last_bg_update = time(NULL);
nochange_state:

	/* check the boot state */
	debug3("boot state for block %s is %d",
	       bg_record->bg_block_id, bg_record->boot_state);
	if (bg_record->boot_state) {
		if (bg_record->state & BG_BLOCK_ERROR_FLAG) {
			/* If we get an error on boot that
			 * means it is a transparent L3 error
			 * and should be trying to fix
			 * itself.  If this is the case we
			 * just hang out waiting for the state
			 * to go to free where we will try to
			 * boot again below.
			 */
			return updated;
		}

		switch (real_state) {
		case BG_BLOCK_BOOTING:
			if (bg_record->job_ptr
			    && !IS_JOB_CONFIGURING(bg_record->job_ptr)) {
				debug3("Setting job %u on block %s "
				       "to configuring",
				       bg_record->job_ptr->job_id,
				       bg_record->bg_block_id);
				bg_record->job_ptr->job_state |=
					JOB_CONFIGURING;
				last_job_update = time(NULL);
			} else if (bg_record->job_list
				   && list_count(bg_record->job_list)) {
				struct job_record *job_ptr;
				ListIterator job_itr =
					list_iterator_create(
						bg_record->job_list);
				while ((job_ptr = list_next(job_itr))) {
					if (job_ptr->magic != JOB_MAGIC) {
						error("bg_status_update_"
						      "block_state: 1 "
						      "bad magic found when "
						      "looking at block %s",
						      bg_record->bg_block_id);
						list_delete_item(job_itr);
						continue;
					}
					job_ptr->job_state |= JOB_CONFIGURING;
				}
				list_iterator_destroy(job_itr);
				last_job_update = time(NULL);
			}
			break;
		case BG_BLOCK_FREE:
			if (bg_record->boot_count < RETRY_BOOT_COUNT) {
				bridge_block_boot(bg_record);

				if (bg_record->magic == BLOCK_MAGIC) {
					debug("boot count for block %s is %d",
					      bg_record->bg_block_id,
					      bg_record->boot_count);
					bg_record->boot_count++;
				}
			} else {
				char *reason = (char *)
					"status_check: Boot fails ";

				error("Couldn't boot Block %s",
				      bg_record->bg_block_id);

				/* We can't push on the kill_job_list
				   here since we have to put this
				   block in an error and that means
				   the killing has to take place
				   before the erroring of the block.
				*/
				slurm_mutex_unlock(&block_state_mutex);
				unlock_slurmctld(job_read_lock);
				requeue_and_error(bg_record, reason);
				lock_slurmctld(job_read_lock);
				slurm_mutex_lock(&block_state_mutex);

				bg_record->boot_state = 0;
				bg_record->boot_count = 0;

				remove_from_bg_list(bg_lists->booted,
						    bg_record);
			}
			break;
		case BG_BLOCK_INITED:
			debug("block %s is ready.",
			      bg_record->bg_block_id);
			if (bg_record->job_ptr
			    && IS_JOB_CONFIGURING(bg_record->job_ptr)) {
				bg_record->job_ptr->job_state &=
					(~JOB_CONFIGURING);
				last_job_update = time(NULL);
			} else if (bg_record->job_list
				   && list_count(bg_record->job_list)) {
				struct job_record *job_ptr;
				ListIterator job_itr =
					list_iterator_create(
						bg_record->job_list);
				while ((job_ptr = list_next(job_itr))) {
					if (job_ptr->magic != JOB_MAGIC) {
						error("bg_status_update_"
						      "block_state: 2 "
						      "bad magic found when "
						      "looking at block %s",
						      bg_record->bg_block_id);
						list_delete_item(job_itr);
						continue;
					}
					job_ptr->job_state &=
						(~JOB_CONFIGURING);
				}
				list_iterator_destroy(job_itr);
				last_job_update = time(NULL);
			}

			bg_record->boot_state = 0;
			bg_record->boot_count = 0;

			if (kill_job_list &&
			    bridge_block_sync_users(bg_record)
			    == SLURM_ERROR) {
				freeit = (kill_job_struct_t *)
					xmalloc(sizeof(kill_job_struct_t));
				freeit->jobid = bg_record->job_running;
				list_push(kill_job_list, freeit);
			}
			break;
		case BG_BLOCK_TERM:
			debug2("Block %s is in a deallocating state "
			       "during a boot.  Doing nothing until "
			       "free state.",
			       bg_record->bg_block_id);
			break;
		case BG_BLOCK_REBOOTING:
			debug2("Block %s is rebooting.",
			       bg_record->bg_block_id);
			break;
		default:
			debug("Hey the state of block "
			      "%s is %d(%s) doing nothing.",
			      bg_record->bg_block_id,
			      real_state,
			      bg_block_state_string(bg_record->state));
			break;
		}
	}

	return updated;
}

extern List bg_status_create_kill_job_list(void)
{
	return list_create(_destroy_kill_struct);
}

extern void bg_status_process_kill_job_list(List kill_job_list,
					    uint16_t job_state,
					    bool slurmctld_locked)
{
	kill_job_struct_t *freeit = NULL;

	if (!kill_job_list)
		return;

	/* kill all the jobs from unexpectedly freed blocks */
	while ((freeit = list_pop(kill_job_list))) {
		debug2("Trying to requeue job %u", freeit->jobid);
		bg_requeue_job(freeit->jobid, 0, slurmctld_locked, job_state,
			       false);
		_destroy_kill_struct(freeit);
	}
}

extern void bg_status_add_job_kill_list(
	struct job_record *job_ptr, List *killing_list)
{
	kill_job_struct_t *freeit;
	ListIterator kill_job_itr;

	if (!job_ptr || !job_ptr->kill_on_node_fail)
		return;

	if (!*killing_list)
		*killing_list = bg_status_create_kill_job_list();

	kill_job_itr = list_iterator_create(*killing_list);
	/* Since lots of cnodes could fail at
	   the same time effecting the same
	   job make sure we only add it once
	   since there is no reason to do the
	   same process over and over again.
	*/
	while ((freeit = (kill_job_struct_t *)list_next(kill_job_itr))) {
		if (freeit->jobid == job_ptr->job_id)
			break;
	}
	list_iterator_destroy(kill_job_itr);

	if (!freeit) {
		freeit = (kill_job_struct_t *)xmalloc(sizeof(freeit));
		freeit->jobid = job_ptr->job_id;
		list_push(*killing_list, freeit);
	}
}

extern void bg_status_remove_jobs_from_failed_block(
	bg_record_t *bg_record, int inx,
	bool midplane, List *delete_list,
	List *killing_list)
{
	struct job_record *job_ptr = NULL;

	if (bg_record->free_cnt) /* Already handled */
		return;

	if (!bg_record->modifying || !delete_list) {
		/* If the block is being modified (pending job), and
		   there is a delete_list just add it to the
		   list so it gets freed and clear up the cnodes so
		   the new job can start.
		*/
		if (bg_record->job_ptr)
			bg_status_add_job_kill_list(
				bg_record->job_ptr, killing_list);
		else if (bg_record->job_list
			 && list_count(bg_record->job_list)) {
			ListIterator job_itr =
				list_iterator_create(bg_record->job_list);
			while ((job_ptr = list_next(job_itr))) {
				if (midplane) {
					if (bit_test(job_ptr->node_bitmap, inx))
						bg_status_add_job_kill_list(
							bg_record->job_ptr,
							killing_list);
				} else {
					select_jobinfo_t *jobinfo =
						(select_jobinfo_t *)
						job_ptr->select_jobinfo->data;
					/* (Handling cnodes, so only one job.)
					   If no units_avail we are
					   using the whole thing, else
					   check the index.
					*/
					if (!jobinfo->units_avail
					    || bit_test(jobinfo->units_avail,
							inx)) {
						bg_status_add_job_kill_list(
							bg_record->job_ptr,
							killing_list);
						break;
					}
				}
			}
			list_iterator_destroy(job_itr);
		}
	} else if (delete_list) {
		if (!*delete_list)
			*delete_list = list_create(NULL);
		/* If there are no jobs running just
		   free the thing. (This rarely
		   happens when a mmcs job goes into
		   error right after it finishes.
		   Weird, I know.)  Here we are going
		   to just remove the block since else
		   wise we could try to free this
		   block over and over again, which
		   only needs to happen once.
		*/
		if (!block_ptr_exist_in_list(*delete_list, bg_record)) {
			debug("_remove_jobs_from_failed_block: going to "
			      "remove block %s, bad hardware "
			      "and no jobs running",
			      bg_record->bg_block_id);
			list_push(*delete_list, bg_record);
		}
	}
}
