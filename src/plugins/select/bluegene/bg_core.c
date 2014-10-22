/*****************************************************************************\
 *  bg_core.c - blue gene node configuration processing module.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov> et. al.
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

#include "bg_read_config.h"
#include "bg_core.h"
#include "bg_defined_block.h"
#include "src/slurmctld/locks.h"
#include <fcntl.h>

#define MAX_FREE_RETRIES           200 /* max number of
					* FREE_SLEEP_INTERVALS to wait
					* before putting a
					* deallocating block into
					* error state.
					*/
#define FREE_SLEEP_INTERVAL        3 /* When freeing a block wait this
				      * long before looking at state
				      * again.
				      */
#define HUGE_BUF_SIZE (1024*16)

#define _DEBUG 0

typedef struct {
	List track_list;
	uint32_t job_id;
	bool destroy;
} bg_free_block_list_t;

static int _post_block_free(bg_record_t *bg_record, bool restore);
static void *_track_freeing_blocks(void *args);

/* block_state_mutex should be locked before calling this */
static int _post_block_free(bg_record_t *bg_record, bool restore)
{
	int rc = SLURM_SUCCESS;

	if (bg_record->magic != BLOCK_MAGIC) {
		error("block already destroyed %p", bg_record);
		xassert(0);
		return SLURM_ERROR;
	}

	bg_record->free_cnt--;
	if (bg_record->free_cnt == -1) {
		info("we got a negative 1 here for %s",
		     bg_record->bg_block_id);
		xassert(0);
		return SLURM_SUCCESS;
	} else if (bg_record->modifying) {
		info("others are modifing this block %s, don't clear it up",
		     bg_record->bg_block_id);
		return SLURM_SUCCESS;
	} else if (bg_record->free_cnt) {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("%d others are trying to destroy this block %s",
			     bg_record->free_cnt, bg_record->bg_block_id);
		return SLURM_SUCCESS;
	}

	/* Even if the block is already in error state we need to do this to
	   avoid any overlapping blocks that may have been created due
	   to bad hardware.
	*/
	if ((bg_record->state & (~BG_BLOCK_ERROR_FLAG)) != BG_BLOCK_FREE) {
		/* Something isn't right, go mark this one in an error
		   state. */
		update_block_msg_t block_msg;
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("_post_block_free: block %s is not in state "
			     "free (%s), putting it in error state.",
			     bg_record->bg_block_id,
			     bg_block_state_string(bg_record->state));
		slurm_init_update_block_msg(&block_msg);
		block_msg.bg_block_id = bg_record->bg_block_id;
		block_msg.state = BG_BLOCK_ERROR_FLAG;
		block_msg.reason = "Block would not deallocate";
		slurm_mutex_unlock(&block_state_mutex);
		select_g_update_block(&block_msg);
		slurm_mutex_lock(&block_state_mutex);
		if (block_ptr_exist_in_list(bg_lists->main, bg_record))
			bg_record->destroy = 0;
		return SLURM_SUCCESS;
	}

	/* If we are here we are done with the destroy so just reset it. */
	bg_record->destroy = 0;

	/* A bit of a sanity check to make sure blocks are being
	   removed out of all the lists.
	*/
	remove_from_bg_list(bg_lists->booted, bg_record);
	if (remove_from_bg_list(bg_lists->job_running, bg_record)
	    == SLURM_SUCCESS) {
		debug2("_post_block_free: we are freeing block %s and "
		       "it was in the job_running list.  This can happen if a "
		       "block is removed while waiting for mmcs to finish "
		       "removing the job from the block.",
		       bg_record->bg_block_id);
		num_unused_cpus += bg_record->cpu_cnt;
	}

	/* If we don't have any mp_counts force block removal */
	if (restore && bg_record->mp_count)
		return SLURM_SUCCESS;

	if (remove_from_bg_list(bg_lists->main, bg_record) != SLURM_SUCCESS) {
		/* This should only happen if called from
		 * bg_job_place.c where the block was never added to
		 * the list. */
		debug("_post_block_free: It appears this block %s isn't "
		      "in the main list anymore.",
		      bg_record->bg_block_id);
	}

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("_post_block_free: removing %s from database",
		     bg_record->bg_block_id);

	rc = bridge_block_remove(bg_record);
	if (rc != SLURM_SUCCESS) {
		if (rc == BG_ERROR_BLOCK_NOT_FOUND) {
			debug("_post_block_free: block %s is not found",
			      bg_record->bg_block_id);
		} else {
			error("_post_block_free: "
			      "bridge_block_remove(%s): %s",
			      bg_record->bg_block_id,
			      bg_err_str(rc));
		}
	} else if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("_post_block_free: done %s(%p)",
		     bg_record->bg_block_id, bg_record);

	destroy_bg_record(bg_record);
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("_post_block_free: destroyed");

	return SLURM_SUCCESS;
}

static void *_track_freeing_blocks(void *args)
{
	bg_free_block_list_t *bg_free_list = (bg_free_block_list_t *)args;
	List track_list = bg_free_list->track_list;
	bool destroy = bg_free_list->destroy;
	uint32_t job_id = bg_free_list->job_id;
	int retry_cnt = 0;
	int free_cnt = 0, track_cnt = list_count(track_list);
	ListIterator itr = list_iterator_create(track_list);
	bg_record_t *bg_record;
	bool restore = true;

	debug("_track_freeing_blocks: Going to free %d for job %u",
	      track_cnt, job_id);
	while (retry_cnt < MAX_FREE_RETRIES) {
		free_cnt = 0;
		slurm_mutex_lock(&block_state_mutex);

		/* just to make sure state is updated */
		bridge_status_update_block_list_state(track_list);

		list_iterator_reset(itr);
		/* just incase this changes from the update function */
		track_cnt = list_count(track_list);
		while ((bg_record = list_next(itr))) {
			if (bg_record->magic != BLOCK_MAGIC) {
				/* update_block_list_state should
				   remove this already from the list
				   so we shouldn't ever have this.
				*/
				error("_track_freeing_blocks: block was "
				      "already destroyed %p", bg_record);
				xassert(0);
				free_cnt++;
				continue;
			}
#ifndef HAVE_BG_FILES
			/* Fake a free since we are n deallocating
			   state before this.
			*/
			if (!(bg_record->state & BG_BLOCK_ERROR_FLAG)
			    && (retry_cnt >= 3))
				bg_record->state = BG_BLOCK_FREE;
#endif
			if ((bg_record->state == BG_BLOCK_FREE)
			    || (bg_record->state & BG_BLOCK_ERROR_FLAG))
				free_cnt++;
			else if (bg_record->state != BG_BLOCK_TERM)
				bg_free_block(bg_record, 0, 1);
		}
		slurm_mutex_unlock(&block_state_mutex);
		if (free_cnt == track_cnt)
			break;
		debug("_track_freeing_blocks: freed %d of %d for job %u",
		      free_cnt, track_cnt, job_id);
		sleep(FREE_SLEEP_INTERVAL);
		retry_cnt++;
	}
	debug("_track_freeing_blocks: Freed them all for job %u", job_id);

	if (destroy)
		restore = false;

	/* If there is a block in error state we need to keep all
	 * these blocks around. */
	slurm_mutex_lock(&block_state_mutex);
	list_iterator_reset(itr);
	while ((bg_record = list_next(itr))) {
		/* block no longer exists */
		if (bg_record->magic != BLOCK_MAGIC)
			continue;
		if (bg_record->state != BG_BLOCK_FREE) {
			restore = true;
			break;
		}
	}

	list_iterator_reset(itr);
	while ((bg_record = list_next(itr)))
		_post_block_free(bg_record, restore);
	slurm_mutex_unlock(&block_state_mutex);
	last_bg_update = time(NULL);
	list_iterator_destroy(itr);
	list_destroy(track_list);
	xfree(bg_free_list);
	return NULL;
}

/*
 * block_state_mutex should be locked before calling this function
 */
extern bool blocks_overlap(bg_record_t *rec_a, bg_record_t *rec_b)
{
	/* deal with large blocks here */
	if ((rec_a->mp_count > 1) && (rec_b->mp_count > 1)) {
		/* check for overlap. */
		if (rec_a->mp_bitmap && rec_b->mp_bitmap
		    && bit_overlap(rec_a->mp_bitmap, rec_b->mp_bitmap))
			return true;
		/* Test for conflicting passthroughs */
		reset_ba_system(false);
		check_and_set_mp_list(rec_a->ba_mp_list);
		if (check_and_set_mp_list(rec_b->ba_mp_list) == SLURM_ERROR)
			return true;
		return false;
	}

	/* now deal with at least one of these being a small block */
	if (rec_a->mp_bitmap && rec_b->mp_bitmap
	    && !bit_overlap(rec_a->mp_bitmap, rec_b->mp_bitmap))
		return false;

	if ((rec_a->cnode_cnt >= bg_conf->mp_cnode_cnt)
	    || (rec_b->cnode_cnt >= bg_conf->mp_cnode_cnt))
		return true;

	if (rec_a->ionode_bitmap && rec_b->ionode_bitmap
	    && !bit_overlap(rec_a->ionode_bitmap, rec_b->ionode_bitmap))
		return false;

	return true;
}

extern bool block_mp_passthrough(bg_record_t *bg_record, int mp_bit)
{
	bool has_pass = 0;
	ba_mp_t *ba_mp = NULL;
	ListIterator itr;

	/* no passthrough */
	if (bg_record->mp_count == list_count(bg_record->ba_mp_list))
		return 0;

	itr = list_iterator_create(bg_record->ba_mp_list);
	while ((ba_mp = list_next(itr))) {
		if (ba_mp->index == mp_bit && ba_mp->used == BA_MP_USED_FALSE) {
			has_pass = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	return has_pass;
}

/* block_state_mutex must be unlocked before calling this. */
extern void bg_requeue_job(uint32_t job_id, bool wait_for_start,
			   bool slurmctld_locked, uint16_t job_state,
			   bool preempted)
{
	int rc;
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	/* Wait for the slurmd to begin the batch script, slurm_fail_job()
	   is a no-op if issued prior to the script initiation do
	   clean up just incase the fail job isn't ran. */
	if (wait_for_start)
		sleep(2);

	if (!slurmctld_locked)
		lock_slurmctld(job_write_lock);
	rc = job_requeue(0, job_id, -1, (uint16_t)NO_VAL, preempted, 0);
	if (rc == ESLURM_JOB_PENDING) {
		error("%s: Could not requeue pending job %u", __func__, job_id);
	} else if (rc != SLURM_SUCCESS) {
		error("%s: Could not requeue job %u, failing it: %s",
		      __func__, job_id, slurm_strerror(rc));
		job_fail(job_id, job_state);
	}
	if (!slurmctld_locked)
		unlock_slurmctld(job_write_lock);
}

/* if SLURM_ERROR you will need to fail the job with
   slurm_fail_job(bg_record->job_running, job_state);
*/

/**
 * sort the partitions by increasing size
 */
extern void sort_bg_record_inc_size(List records){
	if (records == NULL)
		return;
	list_sort(records, (ListCmpF) bg_record_cmpf_inc);
	last_bg_update = time(NULL);
}

extern int bg_free_block(bg_record_t *bg_record, bool wait, bool locked)
{
	int rc = SLURM_SUCCESS;
	int count = 0;

	if (!bg_record) {
		error("bg_free_block: there was no bg_record");
		return SLURM_ERROR;
	}

	if (!locked)
		slurm_mutex_lock(&block_state_mutex);

	while (count < MAX_FREE_RETRIES) {
		/* block was removed */
		if (bg_record->magic != BLOCK_MAGIC) {
			error("block was removed while freeing it here");
			xassert(0);
			if (!locked)
				slurm_mutex_unlock(&block_state_mutex);
			return SLURM_SUCCESS;
		}
		/* Reset these here so we don't try to reboot it
		   when the state goes to free.
		*/
		bg_record->boot_state = 0;
		bg_record->boot_count = 0;
		/* Here we don't need to check if the block is still
		 * in exsistance since this function can't be called on
		 * the same block twice.  It may
		 * had already been removed at this point also.
		 */
#ifdef HAVE_BG_FILES
		if (bg_record->state != BG_BLOCK_FREE
		    && bg_record->state != BG_BLOCK_TERM) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
				info("bridge_destroy %s",
				     bg_record->bg_block_id);
			rc = bridge_block_free(bg_record);
			if (rc != SLURM_SUCCESS) {
				if (rc == BG_ERROR_BLOCK_NOT_FOUND) {
					debug("block %s is not found",
					      bg_record->bg_block_id);
					bg_record->state = BG_BLOCK_FREE;
					break;
				} else if (rc == BG_ERROR_FREE) {
					if (bg_conf->slurm_debug_flags
					    & DEBUG_FLAG_SELECT_TYPE)
						info("bridge_block_free"
						     "(%s): %s State = %s",
						     bg_record->bg_block_id,
						     bg_err_str(rc),
						     bg_block_state_string(
							     bg_record->state));
				} else if (rc == BG_ERROR_INVALID_STATE) {
#ifndef HAVE_BGL
					/* If the state is error and
					   we get an incompatible
					   state back here, it means
					   we set it ourselves so
					   break out.
					*/
					if (bg_record->state
					    & BG_BLOCK_ERROR_FLAG)
						break;
#endif
					if (bg_conf->slurm_debug_flags
					    & DEBUG_FLAG_SELECT_TYPE)
						info("bridge_block_free"
						     "(%s): %s State = %s",
						     bg_record->bg_block_id,
						     bg_err_str(rc),
						     bg_block_state_string(
							     bg_record->state));
#ifdef HAVE_BGQ
					if (bg_record->state != BG_BLOCK_FREE
					    && bg_record->state
					    != BG_BLOCK_TERM)
					bg_record->state = BG_BLOCK_TERM;
#endif
				} else {
					error("bridge_block_free"
					      "(%s): %s State = %s",
					      bg_record->bg_block_id,
					      bg_err_str(rc),
					      bg_block_state_string(
						      bg_record->state));
				}
			}
		}
#else
		/* Fake a free since we are n deallocating
		   state before this.
		*/
		if (bg_record->state & BG_BLOCK_ERROR_FLAG) {
			/* This will set the state to ERROR(Free)
			 * just incase the state was ERROR(SOMETHING ELSE) */
			bg_record->state = BG_BLOCK_ERROR_FLAG;
			break;
		} else if (!wait || (count >= 3))
			bg_record->state = BG_BLOCK_FREE;
		else if (bg_record->state != BG_BLOCK_FREE)
			bg_record->state = BG_BLOCK_TERM;
#endif

		if (!wait || (bg_record->state == BG_BLOCK_FREE)
#ifndef HAVE_BGL
		    ||  (bg_record->state & BG_BLOCK_ERROR_FLAG)
#endif
			) {
			break;
		}
		/* If we were locked outside of this we need to unlock
		   to not cause deadlock on this mutex until we are
		   done.
		*/
		slurm_mutex_unlock(&block_state_mutex);
		sleep(FREE_SLEEP_INTERVAL);
		count++;
		slurm_mutex_lock(&block_state_mutex);
	}

	rc = SLURM_SUCCESS;
	if ((bg_record->state == BG_BLOCK_FREE)
	    || (bg_record->state & BG_BLOCK_ERROR_FLAG)) {

		if (bg_record->err_ratio
		    && (bg_record->state == BG_BLOCK_FREE)) {
			/* Sometime the realtime server can report
			   software error on cnodes even though the
			   block is free.  If this is the case we need
			   to manually clear them.
			*/
			ba_mp_t *found_ba_mp;
			ListIterator itr =
				list_iterator_create(bg_record->ba_mp_list);
			debug("Block %s is free, but has %u cnodes in error.  "
			      "This can happen if a large block goes into "
			      "error and then is freed and the state of "
			      "the block changes before the "
			      "database informs all the cnodes are back to "
			      "normal.  This is no big deal.",
			      bg_record->bg_block_id, bg_record->cnode_err_cnt);
			while ((found_ba_mp = list_next(itr))) {
				if (!found_ba_mp->used)
					continue;

				if (!found_ba_mp->cnode_err_bitmap)
					found_ba_mp->cnode_err_bitmap =
						bit_alloc(
							bg_conf->mp_cnode_cnt);

				bit_nclear(found_ba_mp->cnode_err_bitmap, 0,
					   bit_size(found_ba_mp->
						    cnode_err_bitmap)-1);
			}
			list_iterator_destroy(itr);
			bg_record->cnode_err_cnt = 0;
			bg_record->err_ratio = 0;
		}

		remove_from_bg_list(bg_lists->booted, bg_record);
	} else if (count >= MAX_FREE_RETRIES) {
		/* Something isn't right, go mark this one in an error
		   state. */
		update_block_msg_t block_msg;
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("bg_free_block: block %s is not in state "
			     "free (%s), putting it in error state.",
			     bg_record->bg_block_id,
			     bg_block_state_string(bg_record->state));
		slurm_init_update_block_msg(&block_msg);
		block_msg.bg_block_id = bg_record->bg_block_id;
		block_msg.state = BG_BLOCK_ERROR_FLAG;
		block_msg.reason = "Block would not deallocate";
		slurm_mutex_unlock(&block_state_mutex);
		select_g_update_block(&block_msg);
		slurm_mutex_lock(&block_state_mutex);
		rc = SLURM_ERROR;
	}
	if (!locked)
		slurm_mutex_unlock(&block_state_mutex);

	return rc;
}

/* block_state_mutex should be unlocked before calling this */
extern void free_block_list(uint32_t job_id, List track_list,
			    bool destroy, bool wait)
{
	bg_record_t *bg_record = NULL;
	int retries;
	ListIterator itr = NULL;
	bg_free_block_list_t *bg_free_list;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	List kill_job_list = NULL;
	kill_job_struct_t *freeit;

	if (!track_list || !list_count(track_list))
		return;

	bg_free_list = xmalloc(sizeof(bg_free_block_list_t));
	bg_free_list->track_list = list_create(NULL);
	bg_free_list->destroy = destroy;
	bg_free_list->job_id = job_id;

	slurm_mutex_lock(&block_state_mutex);
	list_transfer(bg_free_list->track_list, track_list);
	itr = list_iterator_create(bg_free_list->track_list);
	while ((bg_record = list_next(itr))) {
		if (bg_record->magic != BLOCK_MAGIC) {
			error("block was already destroyed %p", bg_record);
			continue;
		}
		bg_record->free_cnt++;

		/* just so we don't over write a different thread that
		   wants this block destroyed */
		if (destroy && !bg_record->destroy)
			bg_record->destroy = destroy;

		if (destroy && (bg_record->state & BG_BLOCK_ERROR_FLAG))
			resume_block(bg_record);

		/* This means we are wanting this block free so we can
		   run this job on it, so it is ok to have the job
		   remain here.  Only checking for jobs should go
		   below this.
		*/
		if (bg_record->modifying) {
			debug("free_block_list: Just FYI, we are "
			      "freeing a block (%s) that "
			      "has at least one pending job.",
			      bg_record->bg_block_id);
			continue;
		}

		if (bg_record->job_ptr
		    && !IS_JOB_FINISHED(bg_record->job_ptr)) {
			info("We are freeing a block (%s) that "
			     "has job %u(%u).",
			     bg_record->bg_block_id,
			     bg_record->job_ptr->job_id,
			     bg_record->job_running);
			if (!kill_job_list)
				kill_job_list =
					bg_status_create_kill_job_list();
			freeit = xmalloc(sizeof(kill_job_struct_t));
			freeit->jobid = bg_record->job_ptr->job_id;
			list_push(kill_job_list, freeit);
		} else if (bg_record->job_list
			   && list_count(bg_record->job_list)) {
			struct job_record *job_ptr;
			ListIterator itr;

			if (!kill_job_list)
				kill_job_list =
					bg_status_create_kill_job_list();
			info("We are freeing a block (%s) that has at "
			     "least 1 job.",
			     bg_record->bg_block_id);
			itr = list_iterator_create(bg_record->job_list);
			while ((job_ptr = list_next(itr))) {
				if ((job_ptr->magic != JOB_MAGIC)
				    || IS_JOB_FINISHED(job_ptr))
					continue;
				freeit = xmalloc(sizeof(kill_job_struct_t));
				freeit->jobid = job_ptr->job_id;
				list_push(kill_job_list, freeit);
			}
			list_iterator_destroy(itr);
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);

	if (kill_job_list) {
		bg_status_process_kill_job_list(kill_job_list, JOB_FAILED, 0);
		list_destroy(kill_job_list);
		kill_job_list = NULL;
	}

	if (wait) {
		/* Track_freeing_blocks waits until the list is done
		   and frees the memory of bg_free_list.
		*/
		_track_freeing_blocks(bg_free_list);
		return;
	}

	/* _track_freeing_blocks handles cleanup */
	slurm_attr_init(&attr_agent);
	if (pthread_attr_setdetachstate(&attr_agent, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
	retries = 0;
	while (pthread_create(&thread_agent, &attr_agent,
			      _track_freeing_blocks,
			      bg_free_list)) {
		error("pthread_create error %m");
		if (++retries > MAX_PTHREAD_RETRIES)
			fatal("Can't create pthread");
		/* sleep and retry */
		usleep(1000);
	}
	slurm_attr_destroy(&attr_agent);
	return;
}

/* Determine if specific slurm node is already in DOWN or DRAIN state */
extern int node_already_down(char *node_name)
{
	struct node_record *node_ptr = find_node_record(node_name);

	if (node_ptr) {
		if (IS_NODE_DRAIN(node_ptr))
			return 2;
		else if (IS_NODE_DOWN(node_ptr))
			return 1;
		else
			return 0;
	}

	return 0;
}

/*
 * Convert a BG API error code to a string
 * IN inx - error code from any of the BG Bridge APIs
 * RET - string describing the error condition
 */
extern const char *bg_err_str(int inx)
{
	static char tmp_char[10];

	switch (inx) {
	case SLURM_SUCCESS:
		return "Slurm Success";
	case SLURM_ERROR:
		return "Slurm Error";
	case BG_ERROR_INVALID_STATE:
		return "Invalid State";
	case BG_ERROR_BLOCK_NOT_FOUND:
		return "Block not found";
	case BG_ERROR_BOOT_ERROR:
		return "Block boot error";
	case BG_ERROR_JOB_NOT_FOUND:
		return "Job not found";
	case BG_ERROR_MP_NOT_FOUND:
		return "Midplane not found";
	case BG_ERROR_SWITCH_NOT_FOUND:
		return "Switch not found";
	case BG_ERROR_BLOCK_ALREADY_DEFINED:
		return "Block already defined";
	case BG_ERROR_JOB_ALREADY_DEFINED:
		return "Job already defined";
	case BG_ERROR_CONNECTION_ERROR:
		return "Connection error";
	case BG_ERROR_INTERNAL_ERROR:
		return "Internal error";
	case BG_ERROR_INVALID_INPUT:
		return "Invalid input";
	case BG_ERROR_INCONSISTENT_DATA:
		return "Inconsistent data";
	case BG_ERROR_NO_IOBLOCK_CONNECTED:
		return "No IO Block Connected";
	case BG_ERROR_FREE:
		return "BlockFreeError (Most likely the block has pending action, should clear up shortly, check bridgeapi.log for further info)";
	}
	/* I know this isn't the best way to handle this, but it only
	   happens very rarely and usually in debugging, so it
	   hopefully isn't really all that bad.
	*/
	snprintf(tmp_char, sizeof(tmp_char), "unknown %u?", inx);
	return tmp_char;
}

