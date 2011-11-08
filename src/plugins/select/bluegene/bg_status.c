/*****************************************************************************\
 *  bg_status.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

typedef struct {
	int jobid;
} kill_job_struct_t;

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
		bg_record->job_ptr = NULL;
	} else if (bg_record->job_list && list_count(bg_record->job_list)) {
		struct job_record *job_ptr;

		lock_slurmctld(job_read_lock);
		while ((job_ptr = list_pop(bg_record->job_list))) {
			select_jobinfo_t *jobinfo;

			if (job_ptr->magic != JOB_MAGIC)
				continue;

			jobinfo = job_ptr->select_jobinfo->data;
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
		}
		unlock_slurmctld(job_read_lock);
	} else {
		debug("Block %s was in a ready state "
		      "but is being freed. No job running.",
		      bg_record->bg_block_id);
	}

	if (bridge_block_sync_users(bg_record) == REMOVE_USER_ERR) {
		error("Something happened removing users from block %s",
		      bg_record->bg_block_id);
	}

	if (remove_from_bg_list(bg_lists->job_running, bg_record)
	    == SLURM_SUCCESS)
		num_unused_cpus += bg_record->cpu_cnt;
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
		ListIterator itr = list_iterator_create(bg_record->ba_mp_list);
		ba_mp_t *ba_mp = NULL;

		if (remove_from_bg_list(bg_lists->job_running, bg_record)
		    == SLURM_SUCCESS)
			num_unused_cpus += bg_record->cpu_cnt;
		remove_from_bg_list(bg_lists->booted,
				    bg_record);

		/* We need to do this for all blocks just incase
		   sub-blocks were ever used and for some reason not
		   cleaned up correctly.
		*/
		while ((ba_mp = list_next(itr))) {
			/* Make sure the cnode_bitmap is in a virgin
			   state */
			if (ba_mp->cnode_bitmap) {
				FREE_NULL_BITMAP(ba_mp->cnode_bitmap);
				if (ba_mp->cnode_usable_bitmap)
					ba_mp->cnode_bitmap = bit_copy(
						ba_mp->cnode_usable_bitmap);
			}
		}
		list_iterator_destroy(itr);
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
				lock_slurmctld(job_read_lock);
				while ((job_ptr = list_next(job_itr))) {
					if (job_ptr->magic != JOB_MAGIC) {
						list_delete_item(job_itr);
						continue;
					}
					job_ptr->job_state |= JOB_CONFIGURING;
				}
				unlock_slurmctld(job_read_lock);
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

				slurm_mutex_unlock(&block_state_mutex);
				requeue_and_error(bg_record, reason);
				slurm_mutex_lock(&block_state_mutex);

				bg_record->boot_state = 0;
				bg_record->boot_count = 0;
				if (remove_from_bg_list(
					    bg_lists->job_running, bg_record)
				    == SLURM_SUCCESS)
					num_unused_cpus += bg_record->cpu_cnt;

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
				lock_slurmctld(job_read_lock);
				while ((job_ptr = list_next(job_itr))) {
					if (job_ptr->magic != JOB_MAGIC) {
						list_delete_item(job_itr);
						continue;
					}
					job_ptr->job_state &=
						(~JOB_CONFIGURING);
				}
				unlock_slurmctld(job_read_lock);
				list_iterator_destroy(job_itr);
				last_job_update = time(NULL);
			}
			/* boot flags are reset here */
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

extern void bg_status_process_kill_job_list(List kill_job_list)
{
	kill_job_struct_t *freeit = NULL;

	if (!kill_job_list)
		return;

	/* kill all the jobs from unexpectedly freed blocks */
	while ((freeit = list_pop(kill_job_list))) {
		debug2("Trying to requeue job %u", freeit->jobid);
		bg_requeue_job(freeit->jobid, 0);
		_destroy_kill_struct(freeit);
	}
}
