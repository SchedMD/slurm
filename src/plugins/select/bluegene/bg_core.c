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
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include "src/common/uid.h"
#include <fcntl.h>
#ifdef HAVE_BG_L_P
#include "bl/bridge_status.h"
#endif
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
	} else if (bg_record->free_cnt) {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("%d other are trying to destroy this block %s",
			     bg_record->free_cnt, bg_record->bg_block_id);
		return SLURM_SUCCESS;
	}

	if ((bg_record->state != BG_BLOCK_FREE)
	    && (bg_record->state != BG_BLOCK_ERROR)) {
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
		block_msg.state = BG_BLOCK_ERROR;
		block_msg.reason = "Block would not deallocate";
		slurm_mutex_unlock(&block_state_mutex);
		select_g_update_block(&block_msg);
		slurm_mutex_lock(&block_state_mutex);
		return SLURM_SUCCESS;
	}

	/* A bit of a sanity check to make sure blocks are being
	   removed out of all the lists.
	*/
	if (blocks_are_created) {
		remove_from_bg_list(bg_lists->booted, bg_record);
		if (remove_from_bg_list(bg_lists->job_running, bg_record)
		    == SLURM_SUCCESS)
			num_unused_cpus += bg_record->cpu_cnt;
	}

	if (restore)
		return SLURM_SUCCESS;

	if (blocks_are_created
	    && remove_from_bg_list(bg_lists->main, bg_record)
	    != SLURM_SUCCESS) {
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
		/* if (rc == PARTITION_NOT_FOUND) { */
		/* 	debug("_post_block_free: block %s is not found", */
		/* 	      bg_record->bg_block_id); */
		/* } else { */
			error("_post_block_free: "
			      "bridge_block_remove(%s): %s",
			      bg_record->bg_block_id,
			      bridge_err_str(rc));
		/* } */
	} else
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
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
#ifdef HAVE_BG_L_P
		/* just to make sure state is updated */
		bridge_status_update_block_list_state(track_list);
#endif
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
			if ((bg_record->state != BG_BLOCK_ERROR)
			    && (retry_cnt >= 3))
				bg_record->state = BG_BLOCK_FREE;
#endif
			if ((bg_record->state == BG_BLOCK_FREE)
			    || (bg_record->state == BG_BLOCK_ERROR))
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

	if ((bg_conf->layout_mode == LAYOUT_DYNAMIC) || destroy)
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
	if ((rec_a->mp_count > 1) && (rec_b->mp_count > 1)) {
		/* Test for conflicting passthroughs */
		reset_ba_system(false);
		check_and_set_mp_list(rec_a->ba_mp_list);
		if (check_and_set_mp_list(rec_b->ba_mp_list)
		    == SLURM_ERROR)
			return true;
	}

	if (rec_a->bitmap && rec_b->bitmap
	    && !bit_overlap(rec_a->bitmap, rec_b->bitmap))
		return false;

	if ((rec_a->node_cnt >= bg_conf->mp_node_cnt)
	    || (rec_b->node_cnt >= bg_conf->mp_node_cnt))
		return true;

	if (rec_a->ionode_bitmap && rec_b->ionode_bitmap
	    && !bit_overlap(rec_a->ionode_bitmap, rec_b->ionode_bitmap))
		return false;

	return true;
}

/* block_state_mutex must be unlocked before calling this. */
extern void bg_requeue_job(uint32_t job_id, bool wait_for_start)
{
	int rc;
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	/* Wait for the slurmd to begin the batch script, slurm_fail_job()
	   is a no-op if issued prior to the script initiation do
	   clean up just incase the fail job isn't ran. */
	if (wait_for_start)
		sleep(2);

	lock_slurmctld(job_write_lock);
	if ((rc = job_requeue(0, job_id, -1, (uint16_t)NO_VAL, false))) {
		error("Couldn't requeue job %u, failing it: %s",
		      job_id, slurm_strerror(rc));
		job_fail(job_id);
	}
	unlock_slurmctld(job_write_lock);
}

/* if SLURM_ERROR you will need to fail the job with
   slurm_fail_job(bg_record->job_running);
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
/* 				if (rc == PARTITION_NOT_FOUND) { */
/* 					debug("block %s is not found", */
/* 					      bg_record->bg_block_id); */
/* 					break; */
/* 				} else if (rc == INCOMPATIBLE_STATE) { */
/* #ifndef HAVE_BGL */
/* 					/\* If the state is error and */
/* 					   we get an incompatible */
/* 					   state back here, it means */
/* 					   we set it ourselves so */
/* 					   break out. */
/* 					*\/ */
/* 					if (bg_record->state */
/* 					    == BG_BLOCK_ERROR) */
/* 						break; */
/* #endif */
/* 					if (bg_conf->slurm_debug_flags */
/* 					    & DEBUG_FLAG_SELECT_TYPE) */
/* 						info("bridge_block_remove" */
/* 						     "(%s): %s State = %d", */
/* 						     bg_record->bg_block_id, */
/* 						     bridge_err_str(rc), */
/* 						     bg_record->state); */
/* 				} else { */
					error("bridge_block_remove"
					      "(%s): %s State = %d",
					      bg_record->bg_block_id,
					      bridge_err_str(rc),
					      bg_record->state);
				/* } */
			}
		}
#else
		/* Fake a free since we are n deallocating
		   state before this.
		*/
		if (bg_record->state == BG_BLOCK_ERROR)
			break;
		else if (count >= 3)
			bg_record->state = BG_BLOCK_FREE;
		else if (bg_record->state != BG_BLOCK_FREE)
			bg_record->state = BG_BLOCK_TERM;
#endif

		if (!wait || (bg_record->state == BG_BLOCK_FREE)
#ifdef HAVE_BGL
		    ||  (bg_record->state == BG_BLOCK_ERROR)
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
	    || (bg_record->state == BG_BLOCK_ERROR))
		remove_from_bg_list(bg_lists->booted, bg_record);
	else if (count >= MAX_FREE_RETRIES) {
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
		block_msg.state = BG_BLOCK_ERROR;
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
extern int free_block_list(uint32_t job_id, List track_list,
			   bool destroy, bool wait)
{
	bg_record_t *bg_record = NULL;
	int retries;
	ListIterator itr = NULL;
	bg_free_block_list_t *bg_free_list;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;

	if (!track_list || !list_count(track_list))
		return SLURM_SUCCESS;

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

		if (bg_record->job_ptr
		    && !IS_JOB_FINISHED(bg_record->job_ptr)) {
			info("We are freeing a block (%s) that has job %u(%u).",
			     bg_record->bg_block_id,
			     bg_record->job_ptr->job_id,
			     bg_record->job_running);
			/* This is not thread safe if called from
			   bg_job_place.c anywhere from within
			   submit_job() */
			slurm_mutex_unlock(&block_state_mutex);
			bg_requeue_job(bg_record->job_ptr->job_id, 0);
			slurm_mutex_lock(&block_state_mutex);
		}
		if (remove_from_bg_list(bg_lists->job_running, bg_record)
		    == SLURM_SUCCESS)
			num_unused_cpus += bg_record->cpu_cnt;

		bg_free_block(bg_record, 0, 1);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);

	if (wait) {
		/* Track_freeing_blocks waits until the list is done
		   and frees the memory of bg_free_list.
		*/
		_track_freeing_blocks(bg_free_list);
		return SLURM_SUCCESS;
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
			fatal("Can't create "
			      "pthread");
		/* sleep and retry */
		usleep(1000);
	}
	slurm_attr_destroy(&attr_agent);
	return SLURM_SUCCESS;
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

extern int load_state_file(List curr_block_list, char *dir_name)
{
	int state_fd, i, j=0;
	char *state_file = NULL;
	Buf buffer = NULL;
	char *data = NULL;
	int data_size = 0;
	block_info_msg_t *block_ptr = NULL;
	bg_record_t *bg_record = NULL;
	block_info_t *block_info = NULL;
	bitstr_t *node_bitmap = NULL, *ionode_bitmap = NULL;
	uint16_t geo[SYSTEM_DIMENSIONS];
	char temp[256];
	List results = NULL;
	int data_allocated, data_read = 0;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	int blocks = 0;
	uid_t my_uid;
	int ionodes = 0;
	char *name = NULL;
	struct part_record *part_ptr = NULL;
	bitstr_t *usable_mp_bitmap = NULL;
	ListIterator itr = NULL;
	uint16_t protocol_version = (uint16_t)NO_VAL;

	if (!dir_name) {
		debug2("Starting bluegene with clean slate");
		return SLURM_SUCCESS;
	}

	xassert(curr_block_list);

	state_file = xstrdup(dir_name);
	xstrcat(state_file, "/block_state");
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		error("No block state file (%s) to recover", state_file);
		xfree(state_file);
		return SLURM_SUCCESS;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	debug3("Version string in block_state header is %s", ver_str);
	if (ver_str) {
		if (!strcmp(ver_str, BLOCK_STATE_VERSION)) {
			protocol_version = SLURM_PROTOCOL_VERSION;
		} else if (!strcmp(ver_str, BLOCK_2_1_STATE_VERSION)) {
			protocol_version = SLURM_2_1_PROTOCOL_VERSION;
		}
	}

	if (protocol_version == (uint16_t)NO_VAL) {
		error("***********************************************");
		error("Can not recover block state, "
		      "data version incompatible");
		error("***********************************************");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);
	if (slurm_unpack_block_info_msg(&block_ptr, buffer, protocol_version)
	    == SLURM_ERROR) {
		error("select_p_state_restore: problem unpacking block_info");
		goto unpack_error;
	}

#if defined HAVE_BG_FILES
	for (i=0; i<block_ptr->record_count; i++) {
		block_info = &(block_ptr->block_array[i]);

		/* we only care about the states we need here
		 * everthing else should have been set up already */
		if (block_info->state == BG_BLOCK_ERROR) {
			slurm_mutex_lock(&block_state_mutex);
			if ((bg_record = find_bg_record_in_list(
				     curr_block_list,
				     block_info->bg_block_id)))
				/* put_block_in_error_state should be
				   called after the bg_lists->main has been
				   made.  We can't call it here since
				   this record isn't the record kept
				   around in bg_lists->main.
				*/
				bg_record->state = block_info->state;
			slurm_mutex_unlock(&block_state_mutex);
		}
	}

	slurm_free_block_info_msg(block_ptr);
	free_buf(buffer);
	return SLURM_SUCCESS;
#endif

	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system(true);

	/* Locks are already in place to protect part_list here */
	usable_mp_bitmap = bit_alloc(node_record_count);
	itr = list_iterator_create(part_list);
	while ((part_ptr = list_next(itr))) {
		/* we only want to use mps that are in partitions */
		if (!part_ptr->node_bitmap) {
			debug4("Partition %s doesn't have any nodes in it.",
			       part_ptr->name);
			continue;
		}
		bit_or(usable_mp_bitmap, part_ptr->node_bitmap);
	}
	list_iterator_destroy(itr);

	if (bit_ffs(usable_mp_bitmap) == -1) {
		fatal("We don't have any nodes in any partitions.  "
		      "Can't create blocks.  "
		      "Please check your slurm.conf.");
	}

	node_bitmap = bit_alloc(node_record_count);
	ionode_bitmap = bit_alloc(bg_conf->ionodes_per_mp);
	for (i=0; i<block_ptr->record_count; i++) {
		block_info = &(block_ptr->block_array[i]);

		bit_nclear(node_bitmap, 0, bit_size(node_bitmap) - 1);
		bit_nclear(ionode_bitmap, 0, bit_size(ionode_bitmap) - 1);

		j = 0;
		while (block_info->mp_inx[j] >= 0) {
			if (block_info->mp_inx[j+1]
			    >= node_record_count) {
				fatal("Job state recovered incompatible with "
				      "bluegene.conf. mp=%u state=%d",
				      node_record_count,
				      block_info->mp_inx[j+1]);
			}
			bit_nset(node_bitmap,
				 block_info->mp_inx[j],
				 block_info->mp_inx[j+1]);
			j += 2;
		}

		j = 0;
		while (block_info->ionode_inx[j] >= 0) {
			if (block_info->ionode_inx[j+1]
			    >= bg_conf->ionodes_per_mp) {
				fatal("Job state recovered incompatible with "
				      "bluegene.conf. ionodes=%u state=%d",
				      bg_conf->ionodes_per_mp,
				      block_info->ionode_inx[j+1]);
			}
			bit_nset(ionode_bitmap,
				 block_info->ionode_inx[j],
				 block_info->ionode_inx[j+1]);
			j += 2;
		}

		bg_record = xmalloc(sizeof(bg_record_t));
		bg_record->magic = BLOCK_MAGIC;
		bg_record->bg_block_id = xstrdup(block_info->bg_block_id);
		bg_record->nodes = xstrdup(block_info->nodes);
		bg_record->ionodes = xstrdup(block_info->ionodes);
		bg_record->ionode_bitmap = bit_copy(ionode_bitmap);
		/* put_block_in_error_state should be
		   called after the bg_lists->main has been
		   made.  We can't call it here since
		   this record isn't the record kept
		   around in bg_lists->main.
		*/
		bg_record->state = block_info->state;
		bg_record->job_running = NO_JOB_RUNNING;

		bg_record->mp_count = bit_set_count(node_bitmap);
		bg_record->node_cnt = block_info->node_cnt;
		if (bg_conf->mp_node_cnt > bg_record->node_cnt) {
			ionodes = bg_conf->mp_node_cnt
				/ bg_record->node_cnt;
			bg_record->cpu_cnt = bg_conf->cpus_per_mp / ionodes;
		} else {
			bg_record->cpu_cnt = bg_conf->cpus_per_mp
				* bg_record->mp_count;
		}
#ifdef HAVE_BGL
		bg_record->node_use = block_info->node_use;
#endif
		memcpy(bg_record->conn_type, block_info->conn_type,
		       sizeof(bg_record->conn_type));

		process_nodes(bg_record, true);

		bg_record->target_name = xstrdup(bg_conf->slurm_user_name);
		bg_record->user_name = xstrdup(bg_conf->slurm_user_name);

		if (uid_from_string (bg_record->user_name, &my_uid) < 0) {
			error("uid_from_strin(%s): %m",
			      bg_record->user_name);
		} else {
			bg_record->user_uid = my_uid;
		}

		bg_record->blrtsimage = xstrdup(block_info->blrtsimage);
		bg_record->linuximage =	xstrdup(block_info->linuximage);
		bg_record->mloaderimage = xstrdup(block_info->mloaderimage);
		bg_record->ramdiskimage = xstrdup(block_info->ramdiskimage);

		for(j=0; j<SYSTEM_DIMENSIONS; j++)
			geo[j] = bg_record->geo[j];

		if ((bg_conf->layout_mode == LAYOUT_OVERLAP)
		    || bg_record->full_block) {
			reset_ba_system(false);
		}

		ba_set_removable_mps2(usable_mp_bitmap, 1);
		/* we want the mps that aren't
		 * in this record to mark them as used
		 */
		if (set_all_mps_except(bg_record->nodes)
		    != SLURM_SUCCESS)
			fatal("something happened in "
			      "the load of %s.  "
			      "Did you use smap to "
			      "make the "
			      "bluegene.conf file?",
			      bg_record->bg_block_id);
#ifdef HAVE_BGQ
		results = list_create(destroy_ba_mp);
#else
		results = list_create(NULL);
#endif
		name = set_bg_block(results,
				    bg_record->start,
				    geo,
				    bg_record->conn_type);
		ba_reset_all_removed_mps2();

		if (!name) {
			error("I was unable to "
			      "make the "
			      "requested block.");
			list_destroy(results);
			destroy_bg_record(bg_record);
			continue;
		}


		snprintf(temp, sizeof(temp), "%s%s",
			 bg_conf->slurm_node_prefix,
			 name);

		xfree(name);
		if (strcmp(temp, bg_record->nodes)) {
			fatal("bad wiring in preserved state "
			      "(found %s, but allocated %s) "
			      "YOU MUST COLDSTART",
			      bg_record->nodes, temp);
		}
		if (bg_record->ba_mp_list)
			list_destroy(bg_record->ba_mp_list);
#ifdef HAVE_BGQ
		bg_record->ba_mp_list =	results;
		results = NULL;
#else
		bg_record->ba_mp_list =	list_create(destroy_ba_mp);
		copy_node_path(results, &bg_record->ba_mp_list);
		list_destroy(results);
#endif

		bridge_block_create(bg_record);
		blocks++;
		list_push(curr_block_list, bg_record);
		if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
			bg_record_t *tmp_record = xmalloc(sizeof(bg_record_t));
			copy_bg_record(bg_record, tmp_record);
			list_push(bg_lists->main, tmp_record);
		}
	}

	FREE_NULL_BITMAP(ionode_bitmap);
	FREE_NULL_BITMAP(node_bitmap);
	FREE_NULL_BITMAP(usable_mp_bitmap);

	sort_bg_record_inc_size(curr_block_list);
	slurm_mutex_unlock(&block_state_mutex);

	info("Recovered %d blocks", blocks);
	slurm_free_block_info_msg(block_ptr);
	free_buf(buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("Incomplete block data checkpoint file");
	free_buf(buffer);
	return SLURM_FAILURE;
}
