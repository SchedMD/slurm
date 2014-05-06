/*****************************************************************************\
 *  bg_job_run.c - blue gene job execution (e.g. initiation and termination)
 *  functions.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, Danny Auble <da@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#  if WITH_PTHREADS
#    include <pthread.h>
#  endif
#endif

#include <signal.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/proc_req.h"
#include "bg_core.h"

bool deleting_old_blocks_flag = 0;

enum update_op {START_OP, TERM_OP};

typedef struct {
	char *bg_block_id;
	char *blrtsimage;       /* BlrtsImage for this block */
	uint16_t conn_type[HIGHEST_DIMENSIONS]; /* needed to boot small
						   blocks into HTC
						   mode or not */
	struct job_record *job_ptr;	/* pointer to job running on
					 * block or NULL if no job */
	char *linuximage;       /* LinuxImage for this block */
	char *mloaderimage;     /* mloaderImage for this block */
	enum update_op op;	/* start | terminate | sync */
	char *ramdiskimage;     /* RamDiskImage for this block */
	uint16_t reboot;	/* reboot block before starting job */
} bg_action_t;

static void	_destroy_bg_action(void *x);
static void *	_block_agent(void *args);
static void	_block_op(bg_action_t *bg_action_ptr);
static void	_start_agent(bg_action_t *bg_action_ptr);
static void	_sync_agent(bg_action_t *bg_action_ptr, bg_record_t *bg_record);

/* block_state_mutex should be locked before
 * calling this function.  This should only be called in _start_agent.
 * RET 1 if exists 0 if not, and job is requeued.
 */
static int _make_sure_block_still_exists(bg_action_t *bg_action_ptr,
					 bg_record_t *bg_record)
{
	/* check to make sure this block still exists since
	 * something could had happened and the block is no
	 * longer in existance */
	if ((bg_record->magic != BLOCK_MAGIC)
	    || !block_ptr_exist_in_list(bg_lists->main, bg_record)) {
		slurm_mutex_unlock(&block_state_mutex);
		if (bg_action_ptr->job_ptr) {
			debug("The block %s disappeared while starting "
			      "job %u requeueing if possible.",
			      bg_action_ptr->bg_block_id,
			      bg_action_ptr->job_ptr->job_id);
			bg_requeue_job(bg_action_ptr->job_ptr->job_id, 1, 0,
				       JOB_BOOT_FAIL, false);
		}
		return 0;
	}
	return 1;
}


/* Delete a bg_action_t record */
static void _destroy_bg_action(void *x)
{
	bg_action_t *bg_action_ptr = (bg_action_t *) x;

	if (bg_action_ptr) {
		xfree(bg_action_ptr->blrtsimage);
		xfree(bg_action_ptr->linuximage);
		xfree(bg_action_ptr->mloaderimage);
		xfree(bg_action_ptr->ramdiskimage);
		xfree(bg_action_ptr->bg_block_id);
		xfree(bg_action_ptr);
	}
}

/* Update block user and reboot as needed block_state_mutex needs to
 * be locked before coming in. */
static void _sync_agent(bg_action_t *bg_action_ptr, bg_record_t *bg_record)
{
	struct job_record *job_ptr = bg_action_ptr->job_ptr;

	debug3("Queue sync of job %u in BG block %s ending at %ld",
	       job_ptr->job_id, bg_action_ptr->bg_block_id,
	       job_ptr->end_time);

	last_bg_update = time(NULL);

	ba_sync_job_to_block(bg_record, job_ptr);

	set_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_BLOCK_PTR,
			   bg_record);

	num_unused_cpus -= job_ptr->total_cpus;

	if (!block_ptr_exist_in_list(bg_lists->job_running, bg_record))
		list_push(bg_lists->job_running, bg_record);

	if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
		list_push(bg_lists->booted, bg_record);

	if (bg_record->state == BG_BLOCK_INITED) {
		int sync_user_rc;
		job_ptr->job_state &= (~JOB_CONFIGURING);
		last_job_update = time(NULL);
		/* Just in case reset the boot flags */
		bg_record->boot_state = 0;
		bg_record->boot_count = 0;
		sync_user_rc = bridge_block_sync_users(bg_record);

		if (sync_user_rc == SLURM_ERROR) {
			slurm_mutex_unlock(&block_state_mutex);
			(void) slurm_fail_job(job_ptr->job_id, JOB_BOOT_FAIL);
			slurm_mutex_lock(&block_state_mutex);
		}
		_destroy_bg_action(bg_action_ptr);
	} else {
		if (bg_record->state != BG_BLOCK_BOOTING) {
			error("Block %s isn't ready and isn't "
			      "being configured! Starting job again.",
			      bg_action_ptr->bg_block_id);
		} else {
			debug("Block %s is booting, job ok",
			      bg_action_ptr->bg_block_id);
		}
		/* the function _block_op calls will destroy the
		   bg_action_ptr */
		_block_op(bg_action_ptr);
	}
}

/* Perform job initiation work */
static void _start_agent(bg_action_t *bg_action_ptr)
{
	int rc, set_user_rc = SLURM_SUCCESS;
	bg_record_t *bg_record = NULL;
	bg_record_t *found_record = NULL;
	ListIterator itr;
	List delete_list = NULL;
	int requeue_job = 0;
	uint32_t req_job_id = bg_action_ptr->job_ptr->job_id;
	bool block_inited = 0;
	bool delete_it = 0;

	slurm_mutex_lock(&block_state_mutex);
	bg_record = find_bg_record_in_list(bg_lists->main,
					   bg_action_ptr->bg_block_id);

	if (!bg_record) {
		bg_record->modifying = 0;
		slurm_mutex_unlock(&block_state_mutex);
		error("block %s not found in bg_lists->main",
		      bg_action_ptr->bg_block_id);
		bg_requeue_job(req_job_id, 1, 0, JOB_BOOT_FAIL, false);
		return;
	}

	if ((bg_record->job_running <= NO_JOB_RUNNING)
	    && !find_job_in_bg_record(bg_record, req_job_id)) {
		bg_record->modifying = 0;
		// bg_reset_block(bg_record); should already happened
		slurm_mutex_unlock(&block_state_mutex);
		debug("job %u finished during the queueing job "
		      "(everything is ok)",
		      req_job_id);
		return;
	}

	if ((bg_record->state == BG_BLOCK_TERM) || bg_record->free_cnt) {
		/* It doesn't appear state of a small block
		   (conn_type) is held on a BGP system so
		   if we to reset it so, just set the reboot flag and
		   handle it later in that code. */
		bg_action_ptr->reboot = 1;
	}

	delete_list = list_create(NULL);
	itr = list_iterator_create(bg_lists->main);
	while ((found_record = list_next(itr))) {
		if (bg_record == found_record)
			continue;

		if (!blocks_overlap(bg_record, found_record)) {
			debug2("block %s isn't part of %s",
			       found_record->bg_block_id,
			       bg_record->bg_block_id);
			continue;
		}

		if (found_record->job_ptr
		    || (found_record->job_list
			&& list_count(found_record->job_list))) {
			struct job_record *job_ptr = found_record->job_ptr;
			if (!found_record->job_ptr)
				job_ptr = find_job_in_bg_record(
					found_record, NO_VAL);
			error("Trying to start job %u on block %s, "
			      "but there is a job %u running on an overlapping "
			      "block %s it will not end until %ld.  "
			      "This should never happen.",
			      req_job_id,
			      bg_record->bg_block_id,
			      job_ptr->job_id,
			      found_record->bg_block_id,
			      job_ptr->end_time);
			requeue_job = 1;
			break;
		}

		debug2("need to make sure %s is free, it's part of %s",
		       found_record->bg_block_id,
		       bg_record->bg_block_id);
		list_push(delete_list, found_record);
	}
	list_iterator_destroy(itr);

	if (requeue_job) {
		list_destroy(delete_list);

		bg_reset_block(bg_record, bg_action_ptr->job_ptr);

		bg_record->modifying = 0;
		slurm_mutex_unlock(&block_state_mutex);
		bg_requeue_job(req_job_id, 0, 0, JOB_BOOT_FAIL, false);
		return;
	}

	slurm_mutex_unlock(&block_state_mutex);

	if (bg_conf->layout_mode == LAYOUT_DYNAMIC)
		delete_it = 1;
	free_block_list(req_job_id, delete_list, delete_it, 1);
	list_destroy(delete_list);

	while (1) {
		slurm_mutex_lock(&block_state_mutex);
		/* Failure will unlock block_state_mutex so no need to
		   unlock before return.  No need to reset modifying
		   here if the block doesn't exist.
		*/
		if (!_make_sure_block_still_exists(bg_action_ptr, bg_record)) {
			error("Problem with deallocating blocks to run job %u "
			      "on block %s", req_job_id,
			      bg_action_ptr->bg_block_id);
			return;
		}
		/* If another thread is freeing this block we need to
		   wait until it is done or we will get into a state
		   where this job will be killed.
		*/
		if (!bg_record->free_cnt)
			break;
		debug("Waiting for block %s to free for job %u.  "
		      "%d thread(s) trying to free it",
		      bg_record->bg_block_id, req_job_id,
		      bg_record->free_cnt);
		slurm_mutex_unlock(&block_state_mutex);
		sleep(1);
	}
	/* This was set in the start_job function to close the above
	   window where a job could be mistakenly requeued if another
	   thread is trying to free this block as we are trying to run
	   on it, which is fine since we will reboot it later.
	*/
	bg_record->modifying = 0;

	if ((bg_record->job_running <= NO_JOB_RUNNING)
	    && !find_job_in_bg_record(bg_record, req_job_id)) {
		// bg_reset_block(bg_record); should already happened
		slurm_mutex_unlock(&block_state_mutex);
		debug("job %u already finished before boot",
		      req_job_id);
		return;
	}

	if (bg_record->job_list
	    && (bg_action_ptr->job_ptr->total_cpus != bg_record->cpu_cnt)
	    && (list_count(bg_record->job_list) != 1)) {
		/* We don't allow modification of a block or reboot of
		   a block if we are running multiple jobs on the
		   block.
		*/
		debug2("no reboot");
		goto no_reboot;
	}

	rc = 0;
#ifdef HAVE_BGL
	if (bg_action_ptr->blrtsimage
	   && strcasecmp(bg_action_ptr->blrtsimage, bg_record->blrtsimage)) {
		debug3("changing BlrtsImage from %s to %s",
		       bg_record->blrtsimage, bg_action_ptr->blrtsimage);
		xfree(bg_record->blrtsimage);
		bg_record->blrtsimage = xstrdup(bg_action_ptr->blrtsimage);
		rc = 1;
	}
#elif defined HAVE_BGP
	if ((bg_action_ptr->conn_type[0] >= SELECT_SMALL)
	   && (bg_action_ptr->conn_type[0] != bg_record->conn_type[0])) {
		if (bg_conf->slurm_debug_level >= LOG_LEVEL_DEBUG3) {
			char *req_conn_type =
				conn_type_string_full(bg_action_ptr->conn_type);
			char *conn_type =
				conn_type_string_full(bg_record->conn_type);
			debug3("changing small block mode from %s to %s",
			       conn_type, req_conn_type);
			xfree(req_conn_type);
			xfree(conn_type);
		}
		rc = 1;
# ifndef HAVE_BG_FILES
		/* since we don't check state on an emulated system we
		 * have to change it here
		 */
		bg_record->conn_type[0] = bg_action_ptr->conn_type[0];
# endif
	}
#endif

#ifdef HAVE_BG_L_P
	if (bg_action_ptr->linuximage
	   && strcasecmp(bg_action_ptr->linuximage, bg_record->linuximage)) {
# ifdef HAVE_BGL
		debug3("changing LinuxImage from %s to %s",
		       bg_record->linuximage, bg_action_ptr->linuximage);
# else
		debug3("changing CnloadImage from %s to %s",
		       bg_record->linuximage, bg_action_ptr->linuximage);
# endif
		xfree(bg_record->linuximage);
		bg_record->linuximage = xstrdup(bg_action_ptr->linuximage);
		rc = 1;
	}
	if (bg_action_ptr->ramdiskimage
	   && strcasecmp(bg_action_ptr->ramdiskimage,
			 bg_record->ramdiskimage)) {
# ifdef HAVE_BGL
		debug3("changing RamDiskImage from %s to %s",
		       bg_record->ramdiskimage, bg_action_ptr->ramdiskimage);
# else
		debug3("changing IoloadImage from %s to %s",
		       bg_record->ramdiskimage, bg_action_ptr->ramdiskimage);
# endif
		xfree(bg_record->ramdiskimage);
		bg_record->ramdiskimage = xstrdup(bg_action_ptr->ramdiskimage);
		rc = 1;
	}
#endif
	if (bg_action_ptr->mloaderimage
	   && strcasecmp(bg_action_ptr->mloaderimage,
			 bg_record->mloaderimage)) {
		debug3("changing MloaderImage from %s to %s",
		       bg_record->mloaderimage, bg_action_ptr->mloaderimage);
		xfree(bg_record->mloaderimage);
		bg_record->mloaderimage = xstrdup(bg_action_ptr->mloaderimage);
		rc = 1;
	}

	if (rc || bg_action_ptr->reboot) {
		bg_record->modifying = 1;

		/* Increment free_cnt to make sure we don't loose this
		 * block since bg_free_block will unlock block_state_mutex.
		 */
		bg_record->free_cnt++;
		bg_free_block(bg_record, 1, 1);
		bg_record->free_cnt--;

#if defined HAVE_BG_FILES && defined HAVE_BG_L_P
#ifdef HAVE_BGL
		if ((rc = bridge_block_modify(bg_record->bg_block_id,
					      RM_MODIFY_BlrtsImg,
					      bg_record->blrtsimage))
		    != SLURM_SUCCESS)
			error("bridge_block_modify(RM_MODIFY_BlrtsImg): %s",
			      bg_err_str(rc));

		if ((rc = bridge_block_modify(bg_record->bg_block_id,
					      RM_MODIFY_LinuxImg,
					      bg_record->linuximage))
		    != SLURM_SUCCESS)
			error("bridge_block_modify(RM_MODIFY_LinuxImg): %s",
			      bg_err_str(rc));

		if ((rc = bridge_block_modify(bg_record->bg_block_id,
					      RM_MODIFY_RamdiskImg,
					      bg_record->ramdiskimage))
		    != SLURM_SUCCESS)
			error("bridge_block_modify(RM_MODIFY_RamdiskImg): %s",
			      bg_err_str(rc));

#elif defined HAVE_BGP
		if ((rc = bridge_block_modify(bg_record->bg_block_id,
					      RM_MODIFY_CnloadImg,
					      bg_record->linuximage))
		    != SLURM_SUCCESS)
			error("bridge_block_modify(RM_MODIFY_CnloadImg): %s",
			      bg_err_str(rc));

		if ((rc = bridge_block_modify(bg_record->bg_block_id,
					      RM_MODIFY_IoloadImg,
					      bg_record->ramdiskimage))
		    != SLURM_SUCCESS)
			error("bridge_block_modify(RM_MODIFY_IoloadImg): %s",
			      bg_err_str(rc));

		if (bg_action_ptr->conn_type[0] > SELECT_SMALL) {
			char *conn_type = NULL;
			switch(bg_action_ptr->conn_type[0]) {
			case SELECT_HTC_S:
				conn_type = "s";
				break;
			case SELECT_HTC_D:
				conn_type = "d";
				break;
			case SELECT_HTC_V:
				conn_type = "v";
				break;
			case SELECT_HTC_L:
				conn_type = "l";
				break;
			default:
				break;
			}
			/* the option has to be set before the pool can be
			   set */
			if ((rc = bridge_block_modify(
				     bg_record->bg_block_id,
				     RM_MODIFY_Options,
				     conn_type)) != SLURM_SUCCESS)
				error("bridge_set_data(RM_MODIFY_Options): %s",
				      bg_err_str(rc));
		}
#endif
		if ((rc = bridge_block_modify(bg_record->bg_block_id,
					      RM_MODIFY_MloaderImg,
					      bg_record->mloaderimage))
		    != SLURM_SUCCESS)
			error("bridge_block_modify(RM_MODIFY_MloaderImg): %s",
			      bg_err_str(rc));

#endif
		bg_record->modifying = 0;
	}

no_reboot:
	if (bg_record->state == BG_BLOCK_FREE) {
		if ((rc = bridge_block_boot(bg_record)) != SLURM_SUCCESS) {
			char reason[200];

			bg_record->boot_state = 0;
			bg_record->boot_count = 0;

			if (rc == BG_ERROR_INVALID_STATE)
				snprintf(reason, sizeof(reason),
					 "Block %s is in an incompatible "
					 "state.  This usually means "
					 "hardware is allocated "
					 "by another block (maybe outside "
					 "of SLURM).",
					 bg_record->bg_block_id);
			else
				snprintf(reason, sizeof(reason),
					 "Couldn't boot block %s: %s",
					 bg_record->bg_block_id,
					 bg_err_str(rc));
			slurm_mutex_unlock(&block_state_mutex);
			requeue_and_error(bg_record, reason);
			return;
		}
	} else if (bg_record->state == BG_BLOCK_BOOTING) {
#ifdef HAVE_BG_FILES
		bg_record->boot_state = 1;
#else
		if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
			list_push(bg_lists->booted, bg_record);
		bg_record->state = BG_BLOCK_INITED;
		last_bg_update = time(NULL);
#endif
	}


	if ((bg_record->job_running <= NO_JOB_RUNNING)
	    && !find_job_in_bg_record(bg_record, req_job_id)) {
		slurm_mutex_unlock(&block_state_mutex);
		debug("job %u finished during the start of the boot "
		      "(everything is ok)",
		      req_job_id);
		return;
	}

	/* Don't reset boot_count, it will be reset when state
	   changes, and needs to outlast a job allocation.
	*/
	/* bg_record->boot_count = 0; */
	if (bg_record->state == BG_BLOCK_INITED) {
		debug("block %s is already ready.", bg_record->bg_block_id);
		/* Just in case reset the boot flags */
		bg_record->boot_state = 0;
		bg_record->boot_count = 0;
		set_user_rc = bridge_block_sync_users(bg_record);
		block_inited = 1;
	}
	slurm_mutex_unlock(&block_state_mutex);

	/* This lock needs to happen after the block_state_mutex to
	   avoid deadlock.
	*/
	if (block_inited && bg_action_ptr->job_ptr) {
		slurmctld_lock_t job_write_lock = {
			NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
		lock_slurmctld(job_write_lock);
		bg_action_ptr->job_ptr->job_state &= (~JOB_CONFIGURING);
		last_job_update = time(NULL);
		unlock_slurmctld(job_write_lock);
	}

	if (set_user_rc == SLURM_ERROR) {
		sleep(2);
		/* wait for the slurmd to begin
		   the batch script, slurm_fail_job()
		   is a no-op if issued prior
		   to the script initiation do clean up just
		   incase the fail job isn't ran */
		(void) slurm_fail_job(req_job_id, JOB_BOOT_FAIL);
	}
}

static void *_block_agent(void *args)
{
	bg_action_t *bg_action_ptr = (bg_action_t *)args;

	if (bg_action_ptr->op == START_OP)
		_start_agent(bg_action_ptr);
	else if (bg_action_ptr->op == TERM_OP)
		bridge_block_post_job(bg_action_ptr->bg_block_id,
				      bg_action_ptr->job_ptr);
	_destroy_bg_action(bg_action_ptr);

	return NULL;
}

/* Perform an operation upon a BG block (block) for starting or
 * terminating a job */
static void _block_op(bg_action_t *bg_action_ptr)
{
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;

	/* spawn an agent */
	slurm_attr_init(&attr_agent);
	if (pthread_attr_setdetachstate(&attr_agent, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");

	retries = 0;
	while (pthread_create(&thread_agent, &attr_agent,
			      _block_agent, bg_action_ptr)) {
		error("pthread_create error %m");
		if (++retries > MAX_PTHREAD_RETRIES)
			fatal("Can't create pthread");
		usleep(1000);	/* sleep and retry */
	}
	slurm_attr_destroy(&attr_agent);
}

/*
 * Perform any work required to terminate a jobs on a block.
 * bg_block_id IN - block name
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: The job is killed before the function returns. This can take
 * many seconds. Do not call from slurmctld  or any other entity that
 * can not wait.
 */
int term_jobs_on_block(char *bg_block_id)
{
	int rc = SLURM_SUCCESS;
	bg_action_t *bg_action_ptr;

	bg_action_ptr = xmalloc(sizeof(bg_action_t));
	bg_action_ptr->op = TERM_OP;
	bg_action_ptr->bg_block_id = xstrdup(bg_block_id);
	_block_op(bg_action_ptr);

	return rc;
}

/*
 * Perform any setup required to initiate a job
 * job_ptr IN - pointer to the job being initiated
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: This happens in parallel with srun and slurmd spawning
 * the job. A prolog script is expected to defer initiation of
 * the job script until the BG block is available for use.
 */
extern int start_job(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	bg_record_t *bg_record = NULL;
	bg_action_t *bg_action_ptr = NULL;
	select_jobinfo_t *jobinfo = job_ptr->select_jobinfo->data;

	slurm_mutex_lock(&block_state_mutex);
	bg_record = jobinfo->bg_record;

	if (!bg_record || !block_ptr_exist_in_list(bg_lists->main, bg_record)) {
		slurm_mutex_unlock(&block_state_mutex);
		error("bg_record %s doesn't exist, requested for job (%d)",
		      jobinfo->bg_block_id, job_ptr->job_id);
		return SLURM_ERROR;
	}

	if ((jobinfo->conn_type[0] != SELECT_NAV)
	    && (jobinfo->conn_type[0] < SELECT_SMALL)) {
		int dim;
		for (dim=0; dim<SYSTEM_DIMENSIONS; dim++)
			jobinfo->conn_type[dim] = bg_record->conn_type[dim];
	}

	/* If it isn't 0 then it was setup previous (sub-block)
	*/
	if (jobinfo->geometry[SYSTEM_DIMENSIONS] == 0)
		memcpy(jobinfo->geometry, bg_record->geo,
		       sizeof(bg_record->geo));

	if (bg_record->job_list) {
		/* Mark the ba_mp cnodes as used now. */
		ba_mp_t *ba_mp = list_peek(bg_record->ba_mp_list);
		xassert(ba_mp);
		xassert(ba_mp->cnode_bitmap);
		bit_or(ba_mp->cnode_bitmap, jobinfo->units_avail);
		if (!find_job_in_bg_record(bg_record, job_ptr->job_id))
			list_append(bg_record->job_list, job_ptr);
	} else {
		bg_record->job_running = job_ptr->job_id;
		bg_record->job_ptr = job_ptr;
	}

	job_ptr->job_state |= JOB_CONFIGURING;

	bg_action_ptr = xmalloc(sizeof(bg_action_t));
	bg_action_ptr->op = START_OP;
	bg_action_ptr->job_ptr = job_ptr;

	/* FIXME: The below get_select_jobinfo calls could be avoided
	 * by just using the jobinfo as we do above.
	 */
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_BLOCK_ID,
			   &(bg_action_ptr->bg_block_id));
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_REBOOT,
			   &(bg_action_ptr->reboot));
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_CONN_TYPE,
			   &(bg_action_ptr->conn_type));
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_MLOADER_IMAGE,
			   &(bg_action_ptr->mloaderimage));
#ifdef HAVE_BG_L_P
# ifdef HAVE_BGL
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_BLRTS_IMAGE,
			   &(bg_action_ptr->blrtsimage));
	if (!bg_action_ptr->blrtsimage) {
		bg_action_ptr->blrtsimage =
			xstrdup(bg_conf->default_blrtsimage);
		set_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_BLRTS_IMAGE,
				   bg_action_ptr->blrtsimage);
	}
# elif defined HAVE_BGP
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_CONN_TYPE,
			   &(bg_action_ptr->conn_type));
# endif
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_LINUX_IMAGE,
			   &(bg_action_ptr->linuximage));
	if (!bg_action_ptr->linuximage) {
		bg_action_ptr->linuximage =
			xstrdup(bg_conf->default_linuximage);
		set_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_LINUX_IMAGE,
				   bg_action_ptr->linuximage);
	}

	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_RAMDISK_IMAGE,
			   &(bg_action_ptr->ramdiskimage));
	if (!bg_action_ptr->ramdiskimage) {
		bg_action_ptr->ramdiskimage =
			xstrdup(bg_conf->default_ramdiskimage);
		set_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_RAMDISK_IMAGE,
				   bg_action_ptr->ramdiskimage);
	}

#endif
	if (!bg_action_ptr->mloaderimage) {
		bg_action_ptr->mloaderimage =
			xstrdup(bg_conf->default_mloaderimage);
		set_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_MLOADER_IMAGE,
				   bg_action_ptr->mloaderimage);
	}

	num_unused_cpus -= job_ptr->total_cpus;

	if (!block_ptr_exist_in_list(bg_lists->job_running, bg_record))
		list_push(bg_lists->job_running, bg_record);

	if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
		list_push(bg_lists->booted, bg_record);
	/* Just incase something happens to free this block before we
	   start the job we will make it so this job doesn't get blown
	   away.
	*/
	bg_record->modifying = 1;
	last_bg_update = time(NULL);

	slurm_mutex_unlock(&block_state_mutex);

	info("Queue start of job %u in BG block %s",
	     job_ptr->job_id,
	     bg_action_ptr->bg_block_id);
	_block_op(bg_action_ptr);
	return rc;
}


/*
 * Perform any work required to terminate a job
 * job_ptr IN - pointer to the job being terminated
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: This happens in parallel with srun and slurmd terminating
 * the job. Insure that this function, mpirun and the epilog can
 * all deal with termination race conditions.
 */
int term_job(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	bg_action_t *bg_action_ptr = NULL;

	bg_action_ptr = xmalloc(sizeof(bg_action_t));
	bg_action_ptr->op = TERM_OP;
	bg_action_ptr->job_ptr = job_ptr;
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_BLOCK_ID,
			   &(bg_action_ptr->bg_block_id));
	info("Queue termination of job %u in BG block %s",
	     job_ptr->job_id, bg_action_ptr->bg_block_id);
	_block_op(bg_action_ptr);

	return rc;
}

/*
 * Synchronize BG block state to that of currently active jobs.
 * This can recover from slurmctld crashes when block usership
 * changes were queued
 */
extern int sync_jobs(List job_list)
{
	ListIterator itr;
	struct job_record  *job_ptr = NULL;
	List block_list = NULL, kill_list = NULL;
	static bool run_already = false;
	bg_record_t *bg_record = NULL;

	/* Execute only on initial startup. We don't support bgblock
	 * creation on demand today, so there is no need to re-sync data. */
	if (run_already)
		return SLURM_SUCCESS;
	run_already = true;

	if (!job_list) {
		error("sync_jobs: no job_list");
		return SLURM_ERROR;
	}
	slurm_mutex_lock(&block_state_mutex);
	/* Insure that all running jobs own the specified block */
	itr = list_iterator_create(job_list);
	while ((job_ptr = list_next(itr))) {
		bg_action_t *bg_action_ptr = NULL;
		if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_COMPLETING(job_ptr))
			continue;

		bg_action_ptr = xmalloc(sizeof(bg_action_t));
		if (IS_JOB_COMPLETING(job_ptr))
			bg_action_ptr->op = TERM_OP;
		else
			bg_action_ptr->op = START_OP;
		bg_action_ptr->job_ptr = job_ptr;

		get_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_BLOCK_ID,
				   &(bg_action_ptr->bg_block_id));
#ifdef HAVE_BG_L_P
# ifdef HAVE_BGL
		get_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_BLRTS_IMAGE,
				   &(bg_action_ptr->blrtsimage));
# else
		get_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_CONN_TYPE,
				   &(bg_action_ptr->conn_type));
# endif
		get_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_LINUX_IMAGE,
				   &(bg_action_ptr->linuximage));
		get_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_RAMDISK_IMAGE,
				   &(bg_action_ptr->ramdiskimage));
#endif
		get_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_MLOADER_IMAGE,
				   &(bg_action_ptr->mloaderimage));

		if (bg_action_ptr->bg_block_id == NULL) {
			error("Running job %u has bgblock==NULL",
			      job_ptr->job_id);
		} else if (job_ptr->nodes == NULL) {
			error("Running job %u has nodes==NULL",
			      job_ptr->job_id);
		} else if (!(bg_record = find_bg_record_in_list(
				     bg_lists->main,
				     bg_action_ptr->bg_block_id))) {
			error("Kill job %u belongs to defunct "
			      "bgblock %s",
			      job_ptr->job_id,
			      bg_action_ptr->bg_block_id);
		}

		if (!bg_record) {
			/* Can't fail it just now, we have locks in
			   place. */
			bg_status_add_job_kill_list(job_ptr, &kill_list);
			_destroy_bg_action(bg_action_ptr);
			continue;
		}
		/* _sync_agent will destroy the bg_action_ptr */
		_sync_agent(bg_action_ptr, bg_record);
	}
	list_iterator_destroy(itr);

	block_list = list_create(destroy_bg_record);
	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = list_next(itr))) {
		bg_record_t *rm_record;
		if (bg_record->job_ptr
		    || (bg_record->job_list
			&& list_count(bg_record->job_list)))
			continue;
		rm_record = xmalloc(sizeof(bg_record_t));
		rm_record->magic = BLOCK_MAGIC;
		rm_record->bg_block_id = xstrdup(bg_record->bg_block_id);
		rm_record->mp_str = xstrdup(bg_record->mp_str);
		list_append(block_list, rm_record);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);

	if (kill_list) {
		/* slurmctld is already locked up, so handle this right after
		 * the unlock of block_state_mutex.
		 */
		bg_status_process_kill_job_list(kill_list, JOB_BOOT_FAIL, 1);
		list_destroy(kill_list);
	}

	/* Insure that all other blocks are free of users */
	if (block_list) {
		itr = list_iterator_create(block_list);
		while ((bg_record = list_next(itr))) {
			info("Queue clearing of users of BG block %s",
			     bg_record->bg_block_id);
			term_jobs_on_block(bg_record->bg_block_id);
		}
		list_iterator_destroy(itr);
		list_destroy(block_list);
	} else {
		/* this should never happen,
		 * vestigial logic */
		error("sync_jobs: no block_list");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}
