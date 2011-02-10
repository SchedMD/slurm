/*****************************************************************************\
 *  bg_job_place.c - blue gene job placement (e.g. base block selection)
 *  functions.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Morris Jette <jette1@llnl.gov>
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

#include <grp.h>
#include <pwd.h>

#include "src/common/node_select.h"
#include "src/common/uid.h"
#include "src/slurmctld/trigger_mgr.h"
#include "bluegene.h"
#include "dynamic_block.h"

#define _DEBUG 0
#define MAX_GROUPS 128

#define SWAP(a,b,t)				\
	_STMT_START {				\
		(t) = (a);			\
		(a) = (b);			\
		(b) = (t);			\
	} _STMT_END


pthread_mutex_t create_dynamic_mutex = PTHREAD_MUTEX_INITIALIZER;

static void _rotate_geo(uint16_t *req_geometry, int rot_cnt);
static int _get_user_groups(uint32_t user_id, uint32_t group_id,
			    gid_t *groups, int max_groups, int *ngroups);
static int _test_image_perms(char *image_name, List image_list,
			     struct job_record* job_ptr);
#ifdef HAVE_BGL
static int _check_images(struct job_record* job_ptr,
			 char **blrtsimage, char **linuximage,
			 char **mloaderimage, char **ramdiskimage);
#else
static int _check_images(struct job_record* job_ptr,
			 char **linuximage,
			 char **mloaderimage, char **ramdiskimage);
#endif
static bg_record_t *_find_matching_block(List block_list,
					 struct job_record* job_ptr,
					 bitstr_t* slurm_block_bitmap,
					 ba_request_t *request,
					 uint32_t max_cpus,
					 int *allow, int check_image,
					 int overlap_check,
					 List overlapped_list,
					 uint16_t query_mode);
static int _check_for_booted_overlapping_blocks(
	List block_list, ListIterator bg_record_itr,
	bg_record_t *bg_record, int overlap_check, List overlapped_list,
	uint16_t query_mode);
static int _dynamically_request(List block_list, int *blocks_added,
				ba_request_t *request,
				char *user_req_nodes,
				uint16_t query_mode);
static int _find_best_block_match(List block_list, int *blocks_added,
				  struct job_record* job_ptr,
				  bitstr_t* slurm_block_bitmap,
				  uint32_t min_nodes,
				  uint32_t max_nodes, uint32_t req_nodes,
				  bg_record_t** found_bg_record,
				  uint16_t query_mode, int avail_cpus);
static int _sync_block_lists(List full_list, List incomp_list);
static void _build_select_struct(struct job_record *job_ptr,
				 bitstr_t *bitmap, uint32_t node_cnt);
static List _get_preemptables(uint16_t query_mode, bg_record_t *bg_record,
			      List preempt_jobs);

/* Rotate a 3-D geometry array through its six permutations */
static void _rotate_geo(uint16_t *req_geometry, int rot_cnt)
{
	uint16_t tmp;

	switch (rot_cnt) {
	case 0:		/* ABC -> ACB */
	case 2:		/* CAB -> CBA */
	case 4:		/* BCA -> BAC */
		SWAP(req_geometry[Y], req_geometry[Z], tmp);
		break;
	case 1:		/* ACB -> CAB */
	case 3:		/* CBA -> BCA */
	case 5:		/* BAC -> ABC */
		SWAP(req_geometry[X], req_geometry[Y], tmp);
		break;
	}
}

/*
 * Get a list of groups associated with a specific user_id
 * Return 0 on success, -1 on failure
 */
static int _get_user_groups(uint32_t user_id, uint32_t group_id,
			    gid_t *groups, int max_groups, int *ngroups)
{
	int rc;
	char *user_name;

	user_name = uid_to_string((uid_t) user_id);
	*ngroups = max_groups;
	rc = getgrouplist(user_name, (gid_t) group_id, groups, ngroups);
	if (rc < 0) {
		error("getgrouplist(%s): %m", user_name);
		rc = -1;
	} else {
		*ngroups = rc;
		rc = 0;
	}
	xfree(user_name);
	return rc;
}

/*
 * Determine if the job has permission to use the identified image
 */
static int _test_image_perms(char *image_name, List image_list,
			     struct job_record* job_ptr)
{
	int allow = 0, i, rc;
	ListIterator itr;
	ListIterator itr2;
	image_t *image = NULL;
	image_group_t *image_group = NULL;

	/* Cache group information for most recently checked user */
	static gid_t groups[MAX_GROUPS];
	static int ngroups = -1;
	static int32_t cache_user = -1;

	itr = list_iterator_create(image_list);
	while ((image = list_next(itr))) {
		if (!strcasecmp(image->name, image_name)
		    || !strcasecmp(image->name, "*")) {
			if (image->def) {
				allow = 1;
				break;
			}
			if (!image->groups || !list_count(image->groups)) {
				allow = 1;
				break;
			}
			if (job_ptr->user_id != cache_user) {
				rc = _get_user_groups(job_ptr->user_id,
						      job_ptr->group_id,
						      groups,
						      MAX_GROUPS, &ngroups);
				if (rc)		/* Failed to get groups */
					break;
				cache_user = job_ptr->user_id;
			}
			itr2 = list_iterator_create(image->groups);
			while (!allow && (image_group = list_next(itr2))) {
				for (i=0; i<ngroups; i++) {
					if (image_group->gid
					    == groups[i]) {
						allow = 1;
						break;
					}
				}
			}
			list_iterator_destroy(itr2);
			if (allow)
				break;
		}
	}
	list_iterator_destroy(itr);

	return allow;
}

#ifdef HAVE_BGL
static int _check_images(struct job_record* job_ptr,
			 char **blrtsimage, char **linuximage,
			 char **mloaderimage, char **ramdiskimage)
#else
	static int _check_images(struct job_record* job_ptr,
				 char **linuximage,
				 char **mloaderimage, char **ramdiskimage)
#endif
{
	int allow = 0;

#ifdef HAVE_BGL
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_BLRTS_IMAGE, blrtsimage);

	if (*blrtsimage) {
		allow = _test_image_perms(*blrtsimage, bg_conf->blrts_list,
					  job_ptr);
		if (!allow) {
			error("User %u:%u is not allowed to use BlrtsImage %s",
			      job_ptr->user_id, job_ptr->group_id,
			      *blrtsimage);
			return SLURM_ERROR;

		}
	}
#endif
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_LINUX_IMAGE, linuximage);
	if (*linuximage) {
		allow = _test_image_perms(*linuximage, bg_conf->linux_list,
					  job_ptr);
		if (!allow) {
			error("User %u:%u is not allowed to use LinuxImage %s",
			      job_ptr->user_id, job_ptr->group_id, *linuximage);
			return SLURM_ERROR;
		}
	}

	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_MLOADER_IMAGE, mloaderimage);
	if (*mloaderimage) {
		allow = _test_image_perms(*mloaderimage,
					  bg_conf->mloader_list,
					  job_ptr);
		if (!allow) {
			error("User %u:%u is not allowed "
			      "to use MloaderImage %s",
			      job_ptr->user_id, job_ptr->group_id,
			      *mloaderimage);
			return SLURM_ERROR;
		}
	}

	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_RAMDISK_IMAGE, ramdiskimage);
	if (*ramdiskimage) {
		allow = _test_image_perms(*ramdiskimage,
					  bg_conf->ramdisk_list,
					  job_ptr);
		if (!allow) {
			error("User %u:%u is not allowed "
			      "to use RamDiskImage %s",
			      job_ptr->user_id, job_ptr->group_id,
			      *ramdiskimage);
			return SLURM_ERROR;
		}
	}

	return SLURM_SUCCESS;
}

static bg_record_t *_find_matching_block(List block_list,
					 struct job_record* job_ptr,
					 bitstr_t* slurm_block_bitmap,
					 ba_request_t *request,
					 uint32_t max_cpus,
					 int *allow, int check_image,
					 int overlap_check,
					 List overlapped_list,
					 uint16_t query_mode)
{
	bg_record_t *bg_record = NULL;
	ListIterator itr = NULL;
	char tmp_char[256];

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("number of blocks to check: %d state %d "
		     "asking for %u-%u cpus",
		     list_count(block_list),
		     query_mode, request->procs, max_cpus);

	itr = list_iterator_create(block_list);
	while ((bg_record = list_next(itr))) {
		/* If test_only we want to fall through to tell the
		   scheduler that it is runnable just not right now.
		*/

		/* The job running could be reset so set it back up
		   here if there is a job_ptr
		*/
		if (bg_record->job_ptr)
			bg_record->job_running = bg_record->job_ptr->job_id;

		/* block is messed up some how (BLOCK_ERROR_STATE)
		 * ignore it or if state == RM_PARTITION_ERROR */
		if ((bg_record->job_running == BLOCK_ERROR_STATE)
		    || (bg_record->state == RM_PARTITION_ERROR)) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("block %s is in an error "
				     "state (can't use)",
				     bg_record->bg_block_id);
			continue;
		} else if ((bg_conf->layout_mode == LAYOUT_DYNAMIC)
			   || ((!SELECT_IS_CHECK_FULL_SET(query_mode)
				|| SELECT_IS_MODE_RUN_NOW(query_mode))
			       && (bg_conf->layout_mode != LAYOUT_DYNAMIC))) {
			if (bg_record->free_cnt) {
				/* No reason to look at a block that
				   is being freed unless we are
				   running static and looking at the
				   full set.
				*/
				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_PICK)
					info("block %s being free for other "
					     "job(s), skipping",
					     bg_record->bg_block_id);
				continue;
			} else if ((bg_record->job_running != NO_JOB_RUNNING)
				   && (bg_record->job_running
				       != job_ptr->job_id)) {
				/* Look here if you are trying to run now or
				   if you aren't looking at the full set.  We
				   don't continue on running blocks for the
				   full set because we are seeing if the job
				   can ever run so look here.
				*/
				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_PICK)
					info("block %s in use by %s job %d",
					     bg_record->bg_block_id,
					     bg_record->user_name,
					     bg_record->job_running);
				continue;
			}
		}

		/* Check processor count */
		if ((bg_record->cpu_cnt < request->procs)
		    || ((max_cpus != NO_VAL)
			&& (bg_record->cpu_cnt > max_cpus))) {
			/* We use the proccessor count per block here
			   mostly to see if we can run on a smaller block.
			*/
			convert_num_unit((float)bg_record->cpu_cnt, tmp_char,
					 sizeof(tmp_char), UNIT_NONE);
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("block %s CPU count (%s) not suitable",
				     bg_record->bg_block_id,
				     tmp_char);
			continue;
		}

		/*
		 * Next we check that this block's bitmap is within
		 * the set of nodes which the job can use.
		 * Nodes not available for the job could be down,
		 * drained, allocated to some other job, or in some
		 * SLURM block not available to this job.
		 */
		if (!bit_super_set(bg_record->bitmap, slurm_block_bitmap)) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("bg block %s has nodes not "
				     "usable by this job",
				     bg_record->bg_block_id);
			continue;
		}

		/*
		 * Insure that any required nodes are in this BG block
		 */
		if (job_ptr->details->req_node_bitmap
		    && (!bit_super_set(job_ptr->details->req_node_bitmap,
				       bg_record->bitmap))) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("bg block %s lacks required nodes",
				     bg_record->bg_block_id);
			continue;
		}


		if (_check_for_booted_overlapping_blocks(
			    block_list, itr, bg_record,
			    overlap_check, overlapped_list, query_mode))
			continue;

		if (check_image) {
#ifdef HAVE_BGL
			if (request->blrtsimage &&
			    strcasecmp(request->blrtsimage,
				       bg_record->blrtsimage)) {
				*allow = 1;
				continue;
			}
#endif
			if (request->linuximage &&
			    strcasecmp(request->linuximage,
				       bg_record->linuximage)) {
				*allow = 1;
				continue;
			}

			if (request->mloaderimage &&
			    strcasecmp(request->mloaderimage,
				       bg_record->mloaderimage)) {
				*allow = 1;
				continue;
			}

			if (request->ramdiskimage &&
			    strcasecmp(request->ramdiskimage,
				       bg_record->ramdiskimage)) {
				*allow = 1;
				continue;
			}
		}

		/***********************************************/
		/* check the connection type specified matches */
		/***********************************************/
		if ((request->conn_type != bg_record->conn_type)
		    && (request->conn_type != SELECT_NAV)) {
#ifndef HAVE_BGL
			if (request->conn_type >= SELECT_SMALL) {
				/* we only want to reboot blocks if
				   they have to be so skip booted
				   blocks if in small state
				*/
				if (check_image
				    && (bg_record->state
					== RM_PARTITION_READY)) {
					*allow = 1;
					continue;
				}
				goto good_conn_type;
			} else if (bg_record->conn_type >= SELECT_SMALL) {
				/* since we already checked to see if
				   the cpus were good this means we are
				   looking for a block in a range that
				   includes small and regular blocks.
				   So we can just continue on.
				*/
				goto good_conn_type;
			}
#endif
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("bg block %s conn-type not usable "
				     "asking for %s bg_record is %s",
				     bg_record->bg_block_id,
				     conn_type_string(request->conn_type),
				     conn_type_string(bg_record->conn_type));
			continue;
		}
#ifndef HAVE_BGL
	good_conn_type:
#endif
		/*****************************************/
		/* match up geometry as "best" possible  */
		/*****************************************/
		if (request->geometry[X] == (uint16_t)NO_VAL)
			;	/* Geometry not specified */
		else {	/* match requested geometry */
			bool match = false;
			int rot_cnt = 0;	/* attempt six rotations  */

			for (rot_cnt=0; rot_cnt<6; rot_cnt++) {
				if ((bg_record->geo[X] >= request->geometry[X])
				    && (bg_record->geo[Y]
					>= request->geometry[Y])
				    && (bg_record->geo[Z]
					>= request->geometry[Z])) {
					match = true;
					break;
				}
				if (!request->rotate)
					break;

				_rotate_geo(request->geometry, rot_cnt);
			}

			if (!match)
				continue;	/* Not usable */
		}
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("we found one! %s", bg_record->bg_block_id);
		break;
	}
	list_iterator_destroy(itr);

	return bg_record;
}

static int _check_for_booted_overlapping_blocks(
	List block_list, ListIterator bg_record_itr,
	bg_record_t *bg_record, int overlap_check, List overlapped_list,
	uint16_t query_mode)
{
	bg_record_t *found_record = NULL;
	ListIterator itr = NULL;
	int rc = 0;
	int overlap = 0;
	bool is_test = SELECT_IS_TEST(query_mode);

	/* this test only is for actually picking a block not testing */
	if (is_test && bg_conf->layout_mode == LAYOUT_DYNAMIC)
		return rc;

	/* Make sure no other blocks are under this block
	   are booted and running jobs
	*/
	itr = list_iterator_create(block_list);
	while ((found_record = (bg_record_t*)list_next(itr)) != NULL) {
		if ((!found_record->bg_block_id)
		    || (bg_record == found_record)) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("Don't need to look at myself %s %s",
				     bg_record->bg_block_id,
				     found_record->bg_block_id);
			continue;
		}

		slurm_mutex_lock(&block_state_mutex);
		overlap = blocks_overlap(bg_record, found_record);
		slurm_mutex_unlock(&block_state_mutex);

		if (overlap) {
			overlap = 0;
			/* make the available time on this block
			 * (bg_record) the max of this found_record's job
			 * or the one already set if in overlapped_block_list
			 * since we aren't setting job_running we
			 * don't have to remove them since the
			 * block_list should always be destroyed afterwards.
			 */
			if (is_test && overlapped_list
			    && found_record->job_ptr
			    && bg_record->job_running == NO_JOB_RUNNING) {
				ListIterator itr = list_iterator_create(
					overlapped_list);
				bg_record_t *tmp_rec = NULL;

				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_PICK)
					info("found overlapping block %s "
					     "overlapped %s with job %u",
					     found_record->bg_block_id,
					     bg_record->bg_block_id,
					     found_record->job_ptr->job_id);

				while ((tmp_rec = list_next(itr))) {
					if (tmp_rec == bg_record)
						break;
				}
				list_iterator_destroy(itr);
				if (tmp_rec && tmp_rec->job_ptr->end_time
				    < found_record->job_ptr->end_time)
					tmp_rec->job_ptr =
						found_record->job_ptr;
				else if (!tmp_rec) {
					bg_record->job_ptr =
						found_record->job_ptr;
					list_append(overlapped_list,
						    bg_record);
				}
			}
			/* We already know this block doesn't work
			 * right now so we will if there is another
			 * overlapping block that ends later
			 */
			if (rc)
				continue;
			/* This test is here to check if the block we
			 * chose is not booted or if there is a block
			 * overlapping that we could avoid freeing if
			 * we choose something else
			 */
			if (bg_conf->layout_mode == LAYOUT_OVERLAP
			    && ((overlap_check == 0 && bg_record->state
				 != RM_PARTITION_READY)
				|| (overlap_check == 1 && found_record->state
				    != RM_PARTITION_FREE))) {

				if (!is_test) {
					rc = 1;
					break;
				}
			}

			if ((found_record->job_running != NO_JOB_RUNNING)
			    || (found_record->state == RM_PARTITION_ERROR)) {
				if ((found_record->job_running
				     == BLOCK_ERROR_STATE)
				    || (found_record->state
					== RM_PARTITION_ERROR))
					error("can't use %s, "
					      "overlapping block %s "
					      "is in an error state.",
					      bg_record->bg_block_id,
					      found_record->bg_block_id);
				else
					if (bg_conf->slurm_debug_flags
					    & DEBUG_FLAG_BG_PICK)
						info("can't use %s, there is "
						     "a job (%d) running on "
						     "an overlapping "
						     "block %s",
						     bg_record->bg_block_id,
						     found_record->job_running,
						     found_record->bg_block_id);

				if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
					List tmp_list = list_create(NULL);
					/* this will remove and
					 * destroy the memory for
					 * bg_record
					 */
					list_remove(bg_record_itr);
					slurm_mutex_lock(&block_state_mutex);

					if (bg_record->original) {
						if (bg_conf->slurm_debug_flags
						    & DEBUG_FLAG_BG_PICK)
							info("This was a "
							     "copy %s",
							     bg_record->
							     bg_block_id);
						found_record =
							bg_record->original;
					} else {
						if (bg_conf->slurm_debug_flags
						    & DEBUG_FLAG_BG_PICK)
							info("looking for "
							     "original");
						found_record =
							find_org_in_bg_list(
								bg_lists->main,
								bg_record);
					}

					if (bg_conf->slurm_debug_flags
					    & DEBUG_FLAG_BG_PICK)
						info("Removing unusable block "
						     "%s from the system.",
						     bg_record->bg_block_id);

					if (!found_record) {
						if (bg_conf->slurm_debug_flags
						    & DEBUG_FLAG_BG_PICK)
							info("This record %s "
							     "wasn't found in "
							     "the "
							     "bg_lists->main, "
							     "no big deal, it "
							     "probably wasn't "
							     "added",
							     bg_record->
							     bg_block_id);
						found_record = bg_record;
					} else
						destroy_bg_record(bg_record);

					list_push(tmp_list, found_record);
					slurm_mutex_unlock(&block_state_mutex);

					/* We need to make sure if a
					   job is running here to not
					   call the regular method since
					   we are inside the job write
					   lock already.
					*/
					if (found_record->job_ptr
					    && !IS_JOB_FINISHED(
						    found_record->job_ptr)) {
						info("Somehow block %s "
						     "is being freed, but "
						     "appears to already have "
						     "a job %u(%u) running "
						     "on it.",
						     found_record->bg_block_id,
						     found_record->
						     job_ptr->job_id,
						     found_record->job_running);
						if (job_requeue(0,
								found_record->
								job_ptr->job_id,
								-1,
								(uint16_t)
								NO_VAL)) {
							error("Couldn't "
							      "requeue job %u, "
							      "failing it: %s",
							      found_record->
							      job_ptr->job_id,
							      slurm_strerror(
								      rc));
							job_fail(found_record->
								 job_ptr->
								 job_id);
						}
					}
					free_block_list(NO_VAL, tmp_list, 0, 0);
					list_destroy(tmp_list);
				}
				rc = 1;

				if (!is_test)
					break;
			}
		}
	}
	list_iterator_destroy(itr);

	return rc;
}

/*
 *
 * Return SLURM_SUCCESS on successful create, SLURM_ERROR for no create
 */

static int _dynamically_request(List block_list, int *blocks_added,
				ba_request_t *request,
				char *user_req_nodes,
				uint16_t query_mode)
{
	List list_of_lists = NULL;
	List temp_list = NULL;
	List new_blocks = NULL;
	List job_list = NULL, booted_list = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_ERROR;
	int create_try = 0;
	uint16_t start_geo[SYSTEM_DIMENSIONS];

	memcpy(start_geo, request->geometry,
	       sizeof(uint16_t)*SYSTEM_DIMENSIONS);
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("going to create %d", request->size);
	list_of_lists = list_create(NULL);

	/* If preempt is set and we are checking full system it means
	   we altered the block list so only look at it.
	*/
	if (SELECT_IS_PREEMPT_SET(query_mode)
	    && SELECT_IS_CHECK_FULL_SET(query_mode)) {
		list_append(list_of_lists, block_list);
	} else if (user_req_nodes) {
		slurm_mutex_lock(&block_state_mutex);
		job_list = copy_bg_list(bg_lists->job_running);
		list_append(list_of_lists, job_list);
		slurm_mutex_unlock(&block_state_mutex);
	} else {
		slurm_mutex_lock(&block_state_mutex);
		list_append(list_of_lists, block_list);
		if (list_count(block_list) != list_count(bg_lists->booted)) {
			booted_list = copy_bg_list(bg_lists->booted);
			list_append(list_of_lists, booted_list);
			if (list_count(bg_lists->booted)
			    != list_count(bg_lists->job_running)) {
				job_list = copy_bg_list(bg_lists->job_running);
				list_append(list_of_lists, job_list);
			}
		} else if (list_count(block_list)
			   != list_count(bg_lists->job_running)) {
			job_list = copy_bg_list(bg_lists->job_running);
			list_append(list_of_lists, job_list);
		}
		slurm_mutex_unlock(&block_state_mutex);
	}
	itr = list_iterator_create(list_of_lists);
	while ((temp_list = (List)list_next(itr))) {
		create_try++;

		/* 1- try empty space
		   2- we see if we can create one in the
		   unused bps
		   3- see if we can create one in the non
		   job running bps
		*/
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("trying with %d", create_try);
		if ((new_blocks = create_dynamic_block(block_list,
						       request, temp_list,
						       true))) {
			bg_record_t *bg_record = NULL;

			while ((bg_record = list_pop(new_blocks))) {
				if (block_exist_in_list(block_list, bg_record))
					destroy_bg_record(bg_record);
				else if (SELECT_IS_TEST(query_mode)
					 || SELECT_IS_PREEMPT_ON_FULL_TEST(
						 query_mode)) {
					/* Here we don't really want
					   to create the block if we
					   are testing.

					   The second test here is to
					   make sure If we are able to
					   run here but we just
					   preempted we should wait a
					   bit to make sure the
					   preempted blocks have time
					   to clear out.
					*/
					list_append(block_list, bg_record);
					(*blocks_added) = 1;
				} else {
					if (configure_block(bg_record)
					    == SLURM_ERROR) {
						destroy_bg_record(bg_record);
						error("_dynamically_request: "
						      "unable to configure "
						      "block");
						rc = SLURM_ERROR;
						break;
					}
					list_append(block_list, bg_record);
					print_bg_record(bg_record);
					(*blocks_added) = 1;
				}
			}

			list_destroy(new_blocks);
			if (!*blocks_added) {
				memcpy(request->geometry, start_geo,
				       sizeof(uint16_t)*SYSTEM_DIMENSIONS);
				rc = SLURM_ERROR;
				continue;
			}
			list_sort(block_list,
				  (ListCmpF)bg_record_sort_aval_inc);

			rc = SLURM_SUCCESS;
			break;
		} else if (errno == ESLURM_INTERCONNECT_FAILURE) {
			rc = SLURM_ERROR;
			break;
		}

		memcpy(request->geometry, start_geo,
		       sizeof(uint16_t)*SYSTEM_DIMENSIONS);

	}
	list_iterator_destroy(itr);

	if (list_of_lists)
		list_destroy(list_of_lists);
	if (job_list)
		list_destroy(job_list);
	if (booted_list)
		list_destroy(booted_list);

	return rc;
}
/*
 * finds the best match for a given job request
 *
 *
 * OUT - block_id of matched block, NULL otherwise
 * returns 1 for error (no match)
 *
 */
static int _find_best_block_match(List block_list,
				  int *blocks_added,
				  struct job_record* job_ptr,
				  bitstr_t* slurm_block_bitmap,
				  uint32_t min_nodes, uint32_t max_nodes,
				  uint32_t req_nodes,
				  bg_record_t** found_bg_record,
				  uint16_t query_mode, int avail_cpus)
{
	bg_record_t *bg_record = NULL;
	uint16_t req_geometry[SYSTEM_DIMENSIONS];
	uint16_t conn_type, rotate, target_size = 0;
	uint32_t req_procs = job_ptr->details->min_cpus;
	ba_request_t request;
	int i;
	int overlap_check = 0;
	int allow = 0;
	int check_image = 1;
	uint32_t max_cpus = job_ptr->details->max_cpus;
	char tmp_char[256];
	static int total_cpus = 0;
#ifdef HAVE_BGL
	char *blrtsimage = NULL;        /* BlrtsImage for this request */
#endif
	char *linuximage = NULL;        /* LinuxImage for this request */
	char *mloaderimage = NULL;      /* mloaderImage for this request */
	char *ramdiskimage = NULL;      /* RamDiskImage for this request */
	int rc = SLURM_SUCCESS;
	int create_try = 0;
	List overlapped_list = NULL;
	bool is_test = SELECT_IS_TEST(query_mode);

	if (!total_cpus)
		total_cpus = DIM_SIZE[X] * DIM_SIZE[Y] * DIM_SIZE[Z]
			* bg_conf->cpus_per_bp;

	if (req_nodes > max_nodes) {
		error("can't run this job max bps is %u asking for %u",
		      max_nodes, req_nodes);
		return SLURM_ERROR;
	}

	if (!is_test && (req_procs > avail_cpus)) {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("asking for %u I only have %d",
			     req_procs, avail_cpus);
		return SLURM_ERROR;
	}

	if (!block_list) {
		error("_find_best_block_match: There is no block_list");
		return SLURM_ERROR;
	}

	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_CONN_TYPE, &conn_type);
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_GEOMETRY, &req_geometry);
	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_ROTATE, &rotate);

#ifdef HAVE_BGL
	if ((rc = _check_images(job_ptr, &blrtsimage, &linuximage,
				&mloaderimage, &ramdiskimage)) == SLURM_ERROR)
		goto end_it;
#else
	if ((rc = _check_images(job_ptr, &linuximage,
				&mloaderimage, &ramdiskimage)) == SLURM_ERROR)
		goto end_it;
#endif

	if (req_geometry[X] != 0 && req_geometry[X] != (uint16_t)NO_VAL) {
		target_size = 1;
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			target_size *= req_geometry[i];
		if (target_size != min_nodes) {
			debug2("min_nodes not set correctly %u "
			       "should be %u from %u%u%u",
			       min_nodes, target_size,
			       req_geometry[X],
			       req_geometry[Y],
			       req_geometry[Z]);
			min_nodes = target_size;
		}
		if (!req_nodes)
			req_nodes = min_nodes;
	} else {
		req_geometry[X] = (uint16_t)NO_VAL;
		target_size = min_nodes;
	}

	*found_bg_record = NULL;
	allow = 0;

	memset(&request, 0, sizeof(ba_request_t));

	for(i=0; i<SYSTEM_DIMENSIONS; i++)
		request.geometry[i] = req_geometry[i];

	request.deny_pass = (uint16_t)NO_VAL;
	request.save_name = NULL;
	request.elongate_geos = NULL;
	request.size = target_size;
	request.procs = req_procs;
	request.conn_type = conn_type;
	request.rotate = rotate;
	request.elongate = rotate;

#ifdef HAVE_BGL
	request.blrtsimage = blrtsimage;
#endif
	request.linuximage = linuximage;
	request.mloaderimage = mloaderimage;
	request.ramdiskimage = ramdiskimage;
	if (job_ptr->details->req_node_bitmap)
		request.avail_node_bitmap = job_ptr->details->req_node_bitmap;
	else
		request.avail_node_bitmap = slurm_block_bitmap;

	/* since we only look at procs after this and not nodes we
	 *  need to set a max_cpus if given
	 */
	if (max_cpus == (uint32_t)NO_VAL)
		max_cpus = max_nodes * bg_conf->cpus_per_bp;

	while (1) {
		/* Here we are creating a list of all the blocks that
		 * have overlapped jobs so if we don't find one that
		 * works we will have can look and see the earliest
		 * the job can start.  This doesn't apply to Dynamic mode.
		 */
		if (is_test && SELECT_IS_CHECK_FULL_SET(query_mode)
		    && bg_conf->layout_mode != LAYOUT_DYNAMIC)
			overlapped_list = list_create(NULL);

		bg_record = _find_matching_block(block_list,
						 job_ptr,
						 slurm_block_bitmap,
						 &request,
						 max_cpus,
						 &allow, check_image,
						 overlap_check,
						 overlapped_list,
						 query_mode);
		if (!bg_record && overlapped_list
		    && list_count(overlapped_list)) {
			ListIterator itr =
				list_iterator_create(overlapped_list);
			bg_record_t *tmp_rec = NULL;
			while ((tmp_rec = list_next(itr))) {
				if (!bg_record ||
				    (tmp_rec->job_ptr->end_time <
				     bg_record->job_ptr->end_time))
					bg_record = tmp_rec;
			}
			list_iterator_destroy(itr);
		}

		if (overlapped_list)
			list_destroy(overlapped_list);

		/* set the bitmap and do other allocation activities */
		if (bg_record) {
			if (!is_test) {
				if (check_block_bp_states(
					    bg_record->bg_block_id, 1)
				    != SLURM_SUCCESS) {
					/* check_block_bp_states will
					   set this block in the main
					   list to an error state, but
					   we aren't looking
					   at the main list, so we
					   need to set this copy of
					   the block in an
					   error state as well.
					*/
					bg_record->job_running =
						BLOCK_ERROR_STATE;
					bg_record->state = RM_PARTITION_ERROR;
					error("_find_best_block_match: Picked "
					      "block (%s) had some issues with "
					      "hardware, trying a different "
					      "one.",
					      bg_record->bg_block_id);
					continue;
				}
			}
			format_node_name(bg_record, tmp_char, sizeof(tmp_char));

			debug("_find_best_block_match %s <%s>",
			      bg_record->bg_block_id, tmp_char);
			bit_and(slurm_block_bitmap, bg_record->bitmap);
			rc = SLURM_SUCCESS;
			*found_bg_record = bg_record;
			goto end_it;
		} else {
			/* this gets altered in _find_matching_block so we
			   reset it */
			for(i=0; i<SYSTEM_DIMENSIONS; i++)
				request.geometry[i] = req_geometry[i];
		}

		/* see if we can just reset the image and reboot the block */
		if (allow) {
			check_image = 0;
			allow = 0;
			continue;
		}

		check_image = 1;

		/* all these assume that the *bg_record is NULL */

		if (bg_conf->layout_mode == LAYOUT_OVERLAP
		    && !is_test && overlap_check < 2) {
			overlap_check++;
			continue;
		}

		if (create_try || bg_conf->layout_mode != LAYOUT_DYNAMIC)
			goto no_match;

		if ((rc = _dynamically_request(block_list, blocks_added,
					       &request,
					       job_ptr->details->req_nodes,
					       query_mode))
		    == SLURM_SUCCESS) {
			create_try = 1;
			continue;
		}

		/* Only look at the full system if we aren't going to
		   preempt jobs later and look.
		*/
		if (is_test && SELECT_IS_CHECK_FULL_SET(query_mode)) {
			List new_blocks = NULL;
			List job_list = list_create(NULL);
			ListIterator itr = NULL;
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("trying with empty machine");

			/* Here we need to make sure the blocks in the
			   job list are those in the block list so go
			   through and grab them and add them to a
			   separate list.
			*/
			itr = list_iterator_create(block_list);
			while ((bg_record = list_next(itr))) {
				if (bg_record->job_running != NO_JOB_RUNNING)
					list_append(job_list, bg_record);
				/* Since the error blocks are at the
				   end we only really need to look at
				   the first one to make sure it will
				   work, so don't add more than one to
				   the job list.
				   We do need to check for at least
				   one error block because that lets
				   us know not to hold up the entire
				   machine for a job that won't run
				   until the error is removed which
				   could be a very long time.
				*/
				if (bg_record->job_running == BLOCK_ERROR_STATE)
					break;
			}
			list_iterator_destroy(itr);

			/* Block list is already in the correct order,
			   earliest avaliable first,
			   so the job list will also be. No need to
			   sort. */
			while (1) {
				bool track_down_nodes = true;
				/* this gets altered in
				 * create_dynamic_block so we reset it */
				for(i=0; i<SYSTEM_DIMENSIONS; i++)
					request.geometry[i] = req_geometry[i];

				if ((bg_record = list_pop(job_list))) {
					if (bg_record->job_ptr) {
						if (bg_conf->slurm_debug_flags
						    & DEBUG_FLAG_BG_PICK)
							info("taking off "
							     "%d(%s) started "
							     "at %ld ends "
							     "at %ld",
							     bg_record->
							     job_running,
							     bg_record->
							     bg_block_id,
							     bg_record->
							     job_ptr->
							     start_time,
							     bg_record->
							     job_ptr->
							     end_time);
						/* Mark the block as
						   not running a job,
						   this should
						   correspond to the
						   pointer in the
						   block_list.  We
						   only look at the
						   job_running var so
						   don't remove the
						   job_ptr.
						*/
						bg_record->job_running =
							NO_JOB_RUNNING;
					} else if ((bg_record->job_running
						    == BLOCK_ERROR_STATE)
						   && (bg_conf->
						       slurm_debug_flags
						       & DEBUG_FLAG_BG_PICK))
						info("taking off (%s) "
						     "which is in an "
						     "error state",
						     bg_record->bg_block_id);
				} else
					/* This means we didn't have
					   any jobs to take off
					   anymore so we are making
					   sure we can look at every
					   node on the system.
					*/
					track_down_nodes = false;
				if (!(new_blocks = create_dynamic_block(
					      block_list, &request, job_list,
					      track_down_nodes))) {
					if (errno == ESLURM_INTERCONNECT_FAILURE
					    || !list_count(job_list)) {
						char *nodes;
						if (slurmctld_conf.
						    slurmctld_debug < 5)
							break;
						nodes = bitmap2node_name(
							slurm_block_bitmap);
						debug("job %u not "
						      "runable on %s",
						      job_ptr->job_id,
						      nodes);
						xfree(nodes);
						break;
					}
					continue;
				}
				rc = SLURM_SUCCESS;
				/* outside of the job_test_list this
				 * gets destroyed later, so don't worry
				 * about it now
				 */
				(*found_bg_record) = list_pop(new_blocks);
				if (!(*found_bg_record)) {
					list_destroy(new_blocks);
					if (!bg_record) {
						/* This should never happen */
						error("got an empty list back");
						rc = SLURM_ERROR;
						break;
					}

					if (bg_conf->slurm_debug_flags
					    & DEBUG_FLAG_BG_PICK)
						info("Appears we are trying "
						     "to place this job on "
						     "the block we just "
						     "removed.");
					/* This means we placed the job on
					   the block we just popped off.
					*/
					bit_and(slurm_block_bitmap,
						bg_record->bitmap);
					*found_bg_record = bg_record;
					break;
				}
				bit_and(slurm_block_bitmap,
					(*found_bg_record)->bitmap);

				if (bg_record) {
					(*found_bg_record)->job_running =
						bg_record->job_running;
					(*found_bg_record)->job_ptr
						= bg_record->job_ptr;
				}
				list_destroy(new_blocks);
				break;
			}

			list_destroy(job_list);

			goto end_it;
		} else {
			break;
		}
	}

no_match:
	debug("_find_best_block_match none found");
	rc = SLURM_ERROR;

end_it:
#ifdef HAVE_BGL
	xfree(blrtsimage);
#endif
	xfree(linuximage);
	xfree(mloaderimage);
	xfree(ramdiskimage);

	return rc;
}


static int _sync_block_lists(List full_list, List incomp_list)
{
	ListIterator itr;
	ListIterator itr2;
	bg_record_t *bg_record = NULL;
	bg_record_t *new_record = NULL;
	int count = 0;

	itr = list_iterator_create(full_list);
	itr2 = list_iterator_create(incomp_list);
	while ((new_record = list_next(itr))) {
		/* Make sure we aren't adding any block that doesn't
		   have a block_id.
		*/
		if (!new_record->bg_block_id)
			continue;
		while ((bg_record = list_next(itr2))) {
			/* There is a possiblity the job here is
			   preempting jobs that are configuring that
			   just started on a block overlapping the
			   block we want to use, so we needed to
			   recreate the deallocating block.  Checking
			   the free_cnt will make sure we add the
			   correct block to the mix.
			*/
			if (bg_record->free_cnt == new_record->free_cnt
			    && bit_equal(bg_record->bitmap, new_record->bitmap)
			    && bit_equal(bg_record->ionode_bitmap,
					 new_record->ionode_bitmap)) {
				/* now make sure the conn_type is the same for
				   regular sized blocks */
				if ((bg_record->node_cnt
				     >= bg_conf->bp_node_cnt)
				    && bg_record->conn_type
				    != new_record->conn_type)
					continue;

				break;
			}
		}

		if (!bg_record) {
			list_remove(itr);
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("sync: adding %s %p",
				     new_record->bg_block_id,
				     new_record);
			list_append(incomp_list, new_record);
			last_bg_update = time(NULL);
			count++;
		}
		list_iterator_reset(itr2);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	sort_bg_record_inc_size(incomp_list);

	return count;
}

/* static void _build_select_struct(struct job_record *job_ptr, */
/*                                  bitstr_t *bitmap, uint32_t node_cnt2) */
/* { */
/* 	int i, j, k; */
/* 	int first_bit, last_bit; */
/* 	uint32_t node_cpus, total_cpus = 0, node_cnt; */
/* 	job_resources_t *job_resrcs_ptr; */

/* 	if (job_ptr->select_job) { */
/* 		error("select_p_job_test: already have select_job"); */
/* 		free_job_resources(&job_ptr->select_job); */
/* 	} */

/* 	node_cnt = bit_set_count(bitmap); */
/* 	job_ptr->select_job = job_resrcs_ptr = create_job_resources(); */
/* 	job_resrcs_ptr->cpu_array_reps = xmalloc(sizeof(uint32_t)*node_cnt);*/
/* 	job_resrcs_ptr->cpu_array_value = xmalloc(sizeof(uint16_t)*node_cnt);*/
/* 	job_resrcs_ptr->cpus = xmalloc(sizeof(uint16_t) * node_cnt); */
/* 	job_resrcs_ptr->cpus_used = xmalloc(sizeof(uint16_t) * node_cnt); */
/* 	job_resrcs_ptr->nhosts = node_cnt; */
/* 	job_resrcs_ptr->node_bitmap = bit_copy(bitmap); */
/* 	if (job_resrcs_ptr->node_bitmap == NULL) */
/* 		fatal("bit_copy malloc failure"); */
/* 	job_resrcs_ptr->ncpus = job_ptr->num_cpus; */
/* 	if (build_job_resources(job_resrcs_ptr, (void
 * 	*)node_record_table_ptr, 1)) */
/* 		error("select_p_job_test: build_job_resources: %m"); */

/* 	if (job_ptr->num_cpus <= bg_conf->cpus_per_bp) */
/* 		node_cpus = job_ptr->num_cpus; */
/* 	else */
/* 		node_cpus = bg_conf->cpus_per_bp; */

/* 	first_bit = bit_ffs(bitmap); */
/* 	last_bit  = bit_fls(bitmap); */
/* 	for (i=first_bit, j=0, k=-1; i<=last_bit; i++) { */
/* 		if (!bit_test(bitmap, i)) */
/* 			continue; */

/* 		job_resrcs_ptr->cpus[j] = node_cpus; */
/* 		if ((k == -1) || */
/* 		    (job_resrcs_ptr->cpu_array_value[k] != node_cpus)) { */
/* 			job_resrcs_ptr->cpu_array_cnt++; */
/* 			job_resrcs_ptr->cpu_array_reps[++k] = 1; */
/* 			job_resrcs_ptr->cpu_array_value[k] = node_cpus; */
/* 		} else */
/* 			job_resrcs_ptr->cpu_array_reps[k]++; */
/* 		total_cpus += node_cpus; */
/* #if 0 */
/* 		/\* This function could be used to control allocation of */
/* 		 * specific c-nodes for multiple job steps per job allocation.*/
/* 		 * Such functionality is not currently support on BlueGene */
/* 		 * systems. */
/* 		 * Also see #ifdef HAVE_BG logic in common/job_resources.c *\/*/
/* 		if (set_job_resources_node(job_resrcs_ptr, j)) */
/* 			error("select_p_job_test:
 * 			set_job_resources_node: %m"); */
/* #endif */
/* 		j++; */
/* 	} */
/* 	if (job_resrcs_ptr->ncpus != total_cpus) { */
/* 		error("select_p_job_test: ncpus mismatch %u != %u", */
/* 		      job_resrcs_ptr->ncpus, total_cpus); */
/* 	} */
/* } */

static void _build_select_struct(struct job_record *job_ptr,
				 bitstr_t *bitmap, uint32_t node_cnt)
{
	int i;
	uint32_t total_cpus = 0;
	job_resources_t *job_resrcs_ptr;

	xassert(job_ptr);

	if (job_ptr->job_resrcs) {
		error("select_p_job_test: already have select_job");
		free_job_resources(&job_ptr->job_resrcs);
	}

	job_ptr->job_resrcs = job_resrcs_ptr = create_job_resources();
	job_resrcs_ptr->cpu_array_reps = xmalloc(sizeof(uint32_t));
	job_resrcs_ptr->cpu_array_value = xmalloc(sizeof(uint16_t));
	job_resrcs_ptr->cpus = xmalloc(sizeof(uint16_t) * node_cnt);
	job_resrcs_ptr->cpus_used = xmalloc(sizeof(uint16_t) * node_cnt);
/* 	job_resrcs_ptr->nhosts = node_cnt; */
	job_resrcs_ptr->nhosts = bit_set_count(bitmap);
	job_resrcs_ptr->ncpus = job_ptr->details->min_cpus;
	job_resrcs_ptr->node_bitmap = bit_copy(bitmap);
	job_resrcs_ptr->nodes = bitmap2node_name(bitmap);
	if (job_resrcs_ptr->node_bitmap == NULL)
		fatal("bit_copy malloc failure");

	job_resrcs_ptr->cpu_array_cnt = 1;
	if (job_ptr->details->min_cpus < bg_conf->cpus_per_bp)
		job_resrcs_ptr->cpu_array_value[0] = job_ptr->details->min_cpus;
	else
		job_resrcs_ptr->cpu_array_value[0] = bg_conf->cpus_per_bp;
	job_resrcs_ptr->cpu_array_reps[0] = node_cnt;
	total_cpus = bg_conf->cpu_ratio * node_cnt;

	for (i=0; i<node_cnt; i++)
		job_resrcs_ptr->cpus[i] = bg_conf->cpu_ratio;

	if (job_resrcs_ptr->ncpus != total_cpus) {
		error("select_p_job_test: ncpus mismatch %u != %u",
		      job_resrcs_ptr->ncpus, total_cpus);
	}
}

static List _get_preemptables(uint16_t query_mode, bg_record_t *bg_record,
			      List preempt_jobs)
{
	List preempt = NULL;
	ListIterator itr;
	ListIterator job_itr;
	bg_record_t *found_record;
	struct job_record *job_ptr;

	xassert(bg_record);
	xassert(preempt_jobs);

	preempt = list_create(NULL);
	slurm_mutex_lock(&block_state_mutex);
	job_itr = list_iterator_create(preempt_jobs);
	itr = list_iterator_create(bg_lists->main);
	while ((found_record = list_next(itr))) {
		if (!found_record->job_ptr
		    || (!found_record->bg_block_id)
		    || (bg_record == found_record)
		    || !blocks_overlap(bg_record, found_record))
			continue;

		while ((job_ptr = list_next(job_itr))) {
			if (job_ptr == found_record->job_ptr)
				break;
		}
		if (job_ptr) {
			list_push(preempt, job_ptr);
/* 			info("going to preempt %u running on %s", */
/* 			     job_ptr->job_id, found_record->bg_block_id); */
		} else if (SELECT_IS_MODE_RUN_NOW(query_mode)) {
			error("Job %u running on block %s "
			      "wasn't in the preempt list, but needs to be "
			      "preempted for queried job to run on block %s",
			      found_record->job_ptr->job_id,
			      found_record->bg_block_id,
			      bg_record->bg_block_id);
			list_destroy(preempt);
			preempt = NULL;
			break;
		}
		list_iterator_reset(job_itr);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(job_itr);
	slurm_mutex_unlock(&block_state_mutex);

	return preempt;
}

/*
 * Try to find resources for a given job request
 * IN job_ptr - pointer to job record in slurmctld
 * IN/OUT bitmap - nodes available for assignment to job, clear those not to
 *	be used
 * IN min_nodes, max_nodes  - minimum and maximum number of nodes to allocate
 *	to this job (considers slurm block limits)
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * RET - SLURM_SUCCESS if job runnable now, error code otherwise
 */
extern int submit_job(struct job_record *job_ptr, bitstr_t *slurm_block_bitmap,
		      uint32_t min_nodes, uint32_t max_nodes,
		      uint32_t req_nodes, uint16_t mode,
		      List preemptee_candidates,
		      List *preemptee_job_list)
{
	int rc = SLURM_SUCCESS;
	bg_record_t* bg_record = NULL;
	char buf[256];
	uint16_t conn_type = (uint16_t)NO_VAL;
	List block_list = NULL;
	int blocks_added = 0;
	time_t starttime = time(NULL);
	uint16_t local_mode = mode;
	int avail_cpus = num_unused_cpus;

	if (preemptee_candidates && preemptee_job_list
	    && list_count(preemptee_candidates))
		local_mode |= SELECT_MODE_PREEMPT_FLAG;
	else
		local_mode |= SELECT_MODE_CHECK_FULL;

	if (bg_conf->layout_mode == LAYOUT_DYNAMIC)
		slurm_mutex_lock(&create_dynamic_mutex);

	slurm_mutex_lock(&block_state_mutex);
	block_list = copy_bg_list(bg_lists->main);
	slurm_mutex_unlock(&block_state_mutex);

	get_select_jobinfo(job_ptr->select_jobinfo->data,
			   SELECT_JOBDATA_CONN_TYPE, &conn_type);
	if (conn_type == SELECT_NAV) {
		if (bg_conf->bp_node_cnt == bg_conf->nodecard_node_cnt)
			conn_type = SELECT_SMALL;
		else if (min_nodes > 1)
			conn_type = SELECT_TORUS;
		else if (job_ptr->details->min_cpus < bg_conf->cpus_per_bp)
			conn_type = SELECT_SMALL;

		set_select_jobinfo(job_ptr->select_jobinfo->data,
				   SELECT_JOBDATA_CONN_TYPE,
				   &conn_type);
	}

	if (slurm_block_bitmap && !bit_set_count(slurm_block_bitmap)) {
		error("no nodes given to place job %u.", job_ptr->job_id);

		if (bg_conf->layout_mode == LAYOUT_DYNAMIC)
			slurm_mutex_unlock(&create_dynamic_mutex);

		return SLURM_ERROR;
	}

	sprint_select_jobinfo(job_ptr->select_jobinfo->data,
			      buf, sizeof(buf),
			      SELECT_PRINT_MIXED);
	debug("bluegene:submit_job: %u mode=%d %s nodes=%u-%u-%u",
	      job_ptr->job_id, local_mode, buf,
	      min_nodes, req_nodes, max_nodes);
	sprint_select_jobinfo(job_ptr->select_jobinfo->data,
			      buf, sizeof(buf),
			      SELECT_PRINT_BLRTS_IMAGE);
#ifdef HAVE_BGL
	debug3("BlrtsImage=%s", buf);
	sprint_select_jobinfo(job_ptr->select_jobinfo->data,
			      buf, sizeof(buf),
			      SELECT_PRINT_LINUX_IMAGE);
#endif
#ifdef HAVE_BGL
	debug3("LinuxImage=%s", buf);
#else
	debug3("ComputNodeImage=%s", buf);
#endif

	sprint_select_jobinfo(job_ptr->select_jobinfo->data,
			      buf, sizeof(buf),
			      SELECT_PRINT_MLOADER_IMAGE);
	debug3("MloaderImage=%s", buf);
	sprint_select_jobinfo(job_ptr->select_jobinfo->data,
			      buf, sizeof(buf),
			      SELECT_PRINT_RAMDISK_IMAGE);
#ifdef HAVE_BGL
	debug3("RamDiskImage=%s", buf);
#else
	debug3("RamDiskIoLoadImage=%s", buf);
#endif

	/* First look at the empty space, and then remove the
	   preemptable jobs and try again. */
	list_sort(block_list, (ListCmpF)bg_record_sort_aval_inc);

	rc = _find_best_block_match(block_list, &blocks_added,
				    job_ptr, slurm_block_bitmap, min_nodes,
				    max_nodes, req_nodes,
				    &bg_record, local_mode, avail_cpus);

	if ((rc != SLURM_SUCCESS) && SELECT_IS_PREEMPT_SET(local_mode)) {
		ListIterator itr;
		ListIterator job_itr;
		bg_record_t *found_record;
		struct job_record *preempt_job_ptr;

		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("doing preemption");
		local_mode |= SELECT_MODE_CHECK_FULL;

		job_itr = list_iterator_create(preemptee_candidates);
		itr = list_iterator_create(block_list);
		while ((preempt_job_ptr = list_next(job_itr))) {
			while ((found_record = list_next(itr))) {
				if (found_record->job_ptr == preempt_job_ptr) {
					/* info("removing job %u running on %s", */
					/*      preempt_job_ptr->job_id, */
					/*      found_record->bg_block_id); */
					found_record->job_ptr = NULL;
					found_record->job_running =
						NO_JOB_RUNNING;
					avail_cpus += found_record->cpu_cnt;
					break;
				}
			}
			if (!found_record) {
				list_iterator_reset(itr);
				error("Job %u wasn't found running anywhere, "
				      "can't preempt",
				      preempt_job_ptr->job_id);
				continue;
			} else if (job_ptr->details->min_cpus > avail_cpus)
				continue;

			list_sort(block_list,
				  (ListCmpF)bg_record_sort_aval_inc);
			if ((rc = _find_best_block_match(
				     block_list, &blocks_added,
				     job_ptr, slurm_block_bitmap,
				     min_nodes, max_nodes, req_nodes,
				     &bg_record, local_mode, avail_cpus))
			    == SLURM_SUCCESS)
				break;

			list_iterator_reset(itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(job_itr);
	}

	if (rc == SLURM_SUCCESS) {
		if (bg_record) {
			/* Here we see if there is a job running since
			 * some jobs take awhile to finish we need to
			 * make sure the time of the end is in the
			 * future.  If it isn't (meaning it is in the
			 * past or current time) we add 5 seconds to
			 * it so we don't use the block immediately.
			 */
			if (bg_record->job_ptr
			    && bg_record->job_ptr->end_time) {
				if (bg_record->job_ptr->end_time <= starttime)
					starttime += 5;
				else
					starttime =
						bg_record->job_ptr->end_time;
			} else if (bg_record->job_running == BLOCK_ERROR_STATE)
				starttime = INFINITE;

			/* make sure the job is eligible to run */
			if (job_ptr->details->begin_time > starttime)
				starttime = job_ptr->details->begin_time;

			job_ptr->start_time = starttime;

			set_select_jobinfo(job_ptr->select_jobinfo->data,
					   SELECT_JOBDATA_NODES,
					   bg_record->nodes);
			set_select_jobinfo(job_ptr->select_jobinfo->data,
					   SELECT_JOBDATA_IONODES,
					   bg_record->ionodes);
			if (!bg_record->bg_block_id) {
				debug("%d can start unassigned job %u "
				      "at %ld on %s",
				      local_mode, job_ptr->job_id,
				      starttime, bg_record->nodes);

				set_select_jobinfo(
					job_ptr->select_jobinfo->data,
					SELECT_JOBDATA_BLOCK_ID,
					"unassigned");
				set_select_jobinfo(
					job_ptr->select_jobinfo->data,
					SELECT_JOBDATA_NODE_CNT,
					&bg_record->node_cnt);
			} else {
				if ((bg_record->ionodes)
				    && (job_ptr->part_ptr->max_share <= 1))
					error("Small block used in "
					      "non-shared partition");

				debug("%d(%d) can start job %u "
				      "at %ld on %s(%s) %d",
				      local_mode, mode, job_ptr->job_id,
				      starttime, bg_record->bg_block_id,
				      bg_record->nodes,
				      SELECT_IS_MODE_RUN_NOW(local_mode));

				if (SELECT_IS_MODE_RUN_NOW(local_mode)) {
					set_select_jobinfo(
						job_ptr->select_jobinfo->data,
						SELECT_JOBDATA_BLOCK_ID,
						bg_record->bg_block_id);
					if (job_ptr) {
						bg_record->job_running =
							job_ptr->job_id;
						bg_record->job_ptr = job_ptr;

						job_ptr->job_state |=
							JOB_CONFIGURING;
						last_bg_update = time(NULL);
					}
				} else {
					set_select_jobinfo(
						job_ptr->select_jobinfo->data,
						SELECT_JOBDATA_BLOCK_ID,
						"unassigned");
					/* Just to make sure we don't
					   end up using this on
					   another job, or we have to
					   wait until preemption is
					   done.
					*/
					bg_record->job_ptr = NULL;
					bg_record->job_running = NO_JOB_RUNNING;
				}
				set_select_jobinfo(
					job_ptr->select_jobinfo->data,
					SELECT_JOBDATA_NODE_CNT,
					&bg_record->node_cnt);
			}
			if (SELECT_IS_MODE_RUN_NOW(local_mode))
				_build_select_struct(job_ptr,
						     slurm_block_bitmap,
						     bg_record->node_cnt);
			/* set up the preempted job list */
			if (SELECT_IS_PREEMPT_SET(local_mode)) {
				if (*preemptee_job_list)
					list_destroy(*preemptee_job_list);
				*preemptee_job_list = _get_preemptables(
					local_mode, bg_record,
					preemptee_candidates);
			}
			if (!bg_record->bg_block_id) {
				/* This is a fake record so we need to
				 * destroy it after we get the info from
				 * it.  If it was just testing then
				 * we added this record to the
				 * block_list.  If this is the case
				 * it will be handled if se sync the
				 * lists.  But we don't want to do
				 * that so we will set blocks_added to
				 * 0 so it doesn't happen. */
				if (!blocks_added) {
					destroy_bg_record(bg_record);
					bg_record = NULL;
				}
				blocks_added = 0;
			}
			last_job_update = time(NULL);
		} else {
			error("we got a success, but no block back");
		}
	}

	if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
		slurm_mutex_lock(&block_state_mutex);
		if (blocks_added)
			_sync_block_lists(block_list, bg_lists->main);
		slurm_mutex_unlock(&block_state_mutex);
		slurm_mutex_unlock(&create_dynamic_mutex);
	}
	list_destroy(block_list);
	return rc;
}
