/*****************************************************************************\
 *  bgl_job_run.c - blue gene job execution (e.g. initiation and termination) 
 *  functions. 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

#include <slurm/slurm_errno.h>

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "bgl_job_run.h"
#include "bluegene.h"

#ifdef HAVE_BGL_FILES
typedef struct bgl_update {
	bool start;	/* true=start, false=terminate */
	uid_t uid;	/* new owner */	
	pm_partition_id_t bgl_part_id;
} bgl_update_t;

List bgl_update_list = NULL;

/* Delete a bgl_update_t record */
static void _bgl_list_del(void *x)
{
	bgl_update_t *bgl_update_ptr = (bgl_update_t *) x;

	if (bgl_update_ptr) {
		xfree(bgl_update_ptr->bgl_part_id);
		xfree(bgl_update_ptr);
	}
}

/* Perform an operation upon a BGL block for starting or terminating a job */
static void _block_op(void)
{
}
#endif

/*
 * Perform any setup required to initiate a job
 * job_ptr IN - pointer to the job being initiated
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: This happens in parallel with srun and slurmd spawning
 * the job. A prolog script is expected to defer initiation of
 * the job script until the BGL block is available for use.
 */
extern int start_job(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BGL_FILES
	pm_partition_id_t bgl_part_id;
	bgl_update_t *bgl_update_ptr;

	select_g_get_jobinfo(job_ptr->select_jobinfo, 
		SELECT_DATA_PART_ID, &bgl_part_id);
	info("Starting job %u in BGL partition %s", 
		job_ptr->job_id, bgl_part_id);

	if ((bgl_update_list == NULL)
	&&  ((bgl_update_list = list_create(_bgl_list_del)) == NULL)) {
		fatal("malloc failure in start_job/list_create");
		return SLURM_ERROR;
	}

	bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
	bgl_update_ptr->start = true;
	bgl_update_ptr->uid = job_ptr->user_id;
	bgl_update_ptr->bgl_part_id = bgl_part_id;

	if (list_push(bgl_update_list, bgl_update_ptr) == NULL) {
		fatal("malloc failure in start_job/list_push");
		return SLURM_ERROR;
	}
#endif
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
extern int term_job(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BGL_FILES
	pm_partition_id_t bgl_part_id;
	bgl_update_t *bgl_update_ptr;

	/* Identify the BGL block */
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_PART_ID, &bgl_part_id);
	info("Terminating job %u in BGL partition %s",
		job_ptr->job_id, bgl_part_id);

	bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
	bgl_update_ptr->start = false;
	bgl_update_ptr->uid = job_ptr->user_id;
	bgl_update_ptr->bgl_part_id = bgl_part_id;

	if (list_push(bgl_update_list, bgl_update_ptr) == NULL) {
		fatal("malloc failure in start_job/list_push");
		return SLURM_ERROR;
	}

	/* Find and kill any jobs in this block */

	/* Wait for termination of all jobs */

	/* Change the block's owner */
#endif
	return rc;
}

