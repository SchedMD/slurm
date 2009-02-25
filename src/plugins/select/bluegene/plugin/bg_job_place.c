/*****************************************************************************\
 *  bg_job_place.c - blue gene job placement (e.g. base block selection)
 *  functions.
 *
 *  $Id$ 
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Morris Jette <jette1@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_BG 

#define _DEBUG 0
#define MAX_GROUPS 128

#define SWAP(a,b,t)	\
_STMT_START {		\
	(t) = (a);	\
	(a) = (b);	\
	(b) = (t);	\
} _STMT_END


pthread_mutex_t create_dynamic_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t job_list_test_mutex = PTHREAD_MUTEX_INITIALIZER;

/* This list is for the test_job_list function because we will be
 * adding and removing blocks off the bg_job_block_list and don't want
 * to ruin that list in submit_job it should = bg_job_block_list
 * otherwise it should be a copy of that list.
 */
List job_block_test_list = NULL;

static void _rotate_geo(uint16_t *req_geometry, int rot_cnt);
static int _bg_record_sort_aval_inc(bg_record_t* rec_a, bg_record_t* rec_b); 
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
					 uint32_t max_procs,
					 int *allow, int check_image,
					 int overlap_check,
					 List overlapped_list,
					 bool test_only);
static int _check_for_booted_overlapping_blocks(
	List block_list, ListIterator bg_record_itr,
	bg_record_t *bg_record, int overlap_check, List overlapped_list,
	bool test_only);
static int _dynamically_request(List block_list, int *blocks_added,
				ba_request_t *request,
				bitstr_t* slurm_block_bitmap,
				char *user_req_nodes);
static int _find_best_block_match(List block_list, int *blocks_added,
				  struct job_record* job_ptr,
				  bitstr_t* slurm_block_bitmap,
				  uint32_t min_nodes, 
				  uint32_t max_nodes, uint32_t req_nodes,
				  bg_record_t** found_bg_record,
				  bool test_only);
static int _sync_block_lists(List full_list, List incomp_list);

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
 * Comparator used for sorting blocks smallest to largest
 * 
 * returns: -1: rec_a < rec_b   0: rec_a == rec_b   1: rec_a > rec_b
 * 
 */
static int _bg_record_sort_aval_inc(bg_record_t* rec_a, bg_record_t* rec_b)
{
	if(rec_a->job_ptr && !rec_b->job_ptr)
		return -1;
	else if(!rec_a->job_ptr && rec_b->job_ptr)
		return 1;
	else if(rec_a->job_ptr && rec_b->job_ptr) {
		if(rec_a->job_ptr->start_time > rec_b->job_ptr->start_time)
			return 1;
		else if(rec_a->job_ptr->start_time < rec_b->job_ptr->start_time)
			return -1;
	}

	return bg_record_cmpf_inc(rec_a, rec_b);
}

/* 
 * Comparator used for sorting blocks smallest to largest
 * 
 * returns: -1: rec_a > rec_b   0: rec_a == rec_b   1: rec_a < rec_b
 * 
 */
static int _bg_record_sort_aval_dec(bg_record_t* rec_a, bg_record_t* rec_b)
{
	if(rec_a->job_ptr && !rec_b->job_ptr)
		return 1;
	else if(!rec_a->job_ptr && rec_b->job_ptr)
		return -1;
	else if(rec_a->job_ptr && rec_b->job_ptr) {
		if(rec_a->job_ptr->start_time > rec_b->job_ptr->start_time)
			return -1;
		else if(rec_a->job_ptr->start_time < rec_b->job_ptr->start_time)
			return 1;
	}

	return bg_record_cmpf_inc(rec_a, rec_b);
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
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_BLRTS_IMAGE, blrtsimage);
	
	if (*blrtsimage) {
		allow = _test_image_perms(*blrtsimage, bg_blrtsimage_list, 
					  job_ptr);
		if (!allow) {
			error("User %u:%u is not allowed to use BlrtsImage %s",
			      job_ptr->user_id, job_ptr->group_id,
			      *blrtsimage);
			return SLURM_ERROR;
		       
		}
	}
#endif
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_LINUX_IMAGE, linuximage);
	if (*linuximage) {
		allow = _test_image_perms(*linuximage, bg_linuximage_list, 
					  job_ptr);
		if (!allow) {
			error("User %u:%u is not allowed to use LinuxImage %s",
			      job_ptr->user_id, job_ptr->group_id, *linuximage);
			return SLURM_ERROR;
		}
	}

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_MLOADER_IMAGE, mloaderimage);
	if (*mloaderimage) {
		allow = _test_image_perms(*mloaderimage, bg_mloaderimage_list, 
					  job_ptr);
		if(!allow) {
			error("User %u:%u is not allowed "
			      "to use MloaderImage %s",
			      job_ptr->user_id, job_ptr->group_id, 
			      *mloaderimage);
			return SLURM_ERROR;
		}
	}

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_RAMDISK_IMAGE, ramdiskimage);
	if (*ramdiskimage) {
		allow = _test_image_perms(*ramdiskimage, bg_ramdiskimage_list, 
					  job_ptr);
		if(!allow) {
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
					 uint32_t max_procs,
					 int *allow, int check_image,
					 int overlap_check,
					 List overlapped_list,
					 bool test_only)
{
	bg_record_t *bg_record = NULL;
	ListIterator itr = NULL;
	char tmp_char[256];
	
	debug("number of blocks to check: %d state %d", 
	      list_count(block_list),
	      test_only);
		
	itr = list_iterator_create(block_list);
	while ((bg_record = list_next(itr))) {		
		/* If test_only we want to fall through to tell the 
		   scheduler that it is runnable just not right now. 
		*/
		debug3("%s job_running = %d", 
		       bg_record->bg_block_id, bg_record->job_running);
		/*block is messed up some how (BLOCK_ERROR_STATE) ignore it*/
		if((bg_record->job_running == BLOCK_ERROR_STATE)
		   || (bg_record->state == RM_PARTITION_ERROR)) {
			debug("block %s is in an error state (can't use)", 
			      bg_record->bg_block_id);			
			continue;
		} else if((bg_record->job_running != NO_JOB_RUNNING) 
			  && (bg_record->job_running != job_ptr->job_id)
			  && (bluegene_layout_mode == LAYOUT_DYNAMIC 
			      || (!test_only 
				  && bluegene_layout_mode != LAYOUT_DYNAMIC))) {
			debug("block %s in use by %s job %d", 
			      bg_record->bg_block_id,
			      bg_record->user_name,
			      bg_record->job_running);
			continue;
		}

		/* Check processor count */
		debug3("asking for %u-%u looking at %d", 
		       request->procs, max_procs, bg_record->cpu_cnt);
		if ((bg_record->cpu_cnt < request->procs)
		    || ((max_procs != NO_VAL)
			&& (bg_record->cpu_cnt > max_procs))) {
			/* We use the proccessor count per block here
			   mostly to see if we can run on a smaller block. 
			 */
			convert_num_unit((float)bg_record->cpu_cnt, tmp_char, 
					 sizeof(tmp_char), UNIT_NONE);
			debug("block %s CPU count (%s) not suitable",
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
			debug("bg block %s has nodes not usable by this job",
			      bg_record->bg_block_id);
			continue;
		}

		/*
		 * Insure that any required nodes are in this BG block
		 */
		if (job_ptr->details->req_node_bitmap
		    && (!bit_super_set(job_ptr->details->req_node_bitmap,
				       bg_record->bitmap))) {
			debug("bg block %s lacks required nodes",
				bg_record->bg_block_id);
			continue;
		}
		
		
		if(_check_for_booted_overlapping_blocks(
			   block_list, itr, bg_record,
			   overlap_check, overlapped_list, test_only))
			continue;
		
		if(check_image) {
#ifdef HAVE_BGL
			if(request->blrtsimage &&
			   strcasecmp(request->blrtsimage,
				      bg_record->blrtsimage)) {
				*allow = 1;
				continue;
			} 
#endif
			if(request->linuximage &&
			   strcasecmp(request->linuximage,
				      bg_record->linuximage)) {
				*allow = 1;
				continue;
			}

			if(request->mloaderimage &&
			   strcasecmp(request->mloaderimage, 
				      bg_record->mloaderimage)) {
				*allow = 1;
				continue;
			}

			if(request->ramdiskimage &&
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
			if(request->conn_type >= SELECT_SMALL) {
				/* we only want to reboot blocks if
				   they have to be so skip booted
				   blocks if in small state
				*/
				if(check_image 
				   && (bg_record->state
				       == RM_PARTITION_READY)) {
					*allow = 1;
					continue;			
				} 
				goto good_conn_type;
			} 
#endif
			debug("bg block %s conn-type not usable asking for %s "
			      "bg_record is %s", 
			      bg_record->bg_block_id,
			      convert_conn_type(request->conn_type),
			      convert_conn_type(bg_record->conn_type));
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
				
				_rotate_geo((uint16_t *)request->geometry,
					    rot_cnt);
			}
			
			if (!match) 
				continue;	/* Not usable */
		}
		debug2("we found one! %s", bg_record->bg_block_id);
		break;
	}
	list_iterator_destroy(itr);
	
	return bg_record;
}

static int _check_for_booted_overlapping_blocks(
	List block_list, ListIterator bg_record_itr,
	bg_record_t *bg_record, int overlap_check, List overlapped_list,
	bool test_only)
{
	bg_record_t *found_record = NULL;
	ListIterator itr = NULL;
	int rc = 0;
	int overlap = 0;

	 /* this test only is for actually picking a block not testing */
	if(test_only && bluegene_layout_mode == LAYOUT_DYNAMIC)
		return rc;

	/* Make sure no other blocks are under this block 
	   are booted and running jobs
	*/
	itr = list_iterator_create(block_list);
	while ((found_record = (bg_record_t*)list_next(itr)) != NULL) {
		if ((!found_record->bg_block_id)
		    || (bg_record == found_record)) {
			debug4("Don't need to look at myself %s %s",
			       bg_record->bg_block_id,
			       found_record->bg_block_id);
			continue;
		}
		
		slurm_mutex_lock(&block_state_mutex);
		overlap = blocks_overlap(bg_record, found_record);
		slurm_mutex_unlock(&block_state_mutex);

		if(overlap) {
			overlap = 0;
			/* make the available time on this block
			 * (bg_record) the max of this found_record's job
			 * or the one already set if in overlapped_block_list
			 * since we aren't setting job_running we
			 * don't have to remove them since the
			 * block_list should always be destroyed afterwards.
			 */
			if(test_only && overlapped_list
			   && found_record->job_ptr 
			   && bg_record->job_running == NO_JOB_RUNNING) {
				debug2("found over lapping block %s "
				       "overlapped %s with job %u",
				       found_record->bg_block_id,
				       bg_record->bg_block_id,
				       found_record->job_ptr->job_id);
				ListIterator itr = list_iterator_create(
					overlapped_list);
				bg_record_t *tmp_rec = NULL;
				while((tmp_rec = list_next(itr))) {
					if(tmp_rec == bg_record)
						break;
				}
				list_iterator_destroy(itr);
				if(tmp_rec && tmp_rec->job_ptr->end_time 
				   < found_record->job_ptr->end_time)
					tmp_rec->job_ptr =
						found_record->job_ptr;
				else if(!tmp_rec) {
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
			if(rc)
				continue;
			/* This test is here to check if the block we
			 * chose is not booted or if there is a block
			 * overlapping that we could avoid freeing if
			 * we choose something else
			 */
			if(bluegene_layout_mode == LAYOUT_OVERLAP
			   && ((overlap_check == 0 && bg_record->state 
				!= RM_PARTITION_READY)
			       || (overlap_check == 1 && found_record->state 
				   != RM_PARTITION_FREE))) {

				if(!test_only) {
					rc = 1;
					break;
				}
			}

			if(found_record->job_running != NO_JOB_RUNNING) {
				if(found_record->job_running
				   == BLOCK_ERROR_STATE)
					error("can't use %s, "
					      "overlapping block %s "
					      "is in an error state.",
					      bg_record->bg_block_id,
					      found_record->bg_block_id);
				else
					debug("can't use %s, there is "
					      "a job (%d) running on "
					      "an overlapping "
					      "block %s", 
					      bg_record->bg_block_id,
					      found_record->job_running,
					      found_record->bg_block_id);
				
				if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
					/* this will remove and
					 * destroy the memory for
					 * bg_record
					*/
					list_remove(bg_record_itr);
					slurm_mutex_lock(&block_state_mutex);

					if(bg_record->original) {
						debug3("This was a copy");
						found_record =
							bg_record->original;
						remove_from_bg_list(
							bg_list, found_record);
					} else {
						debug("looking for original");
						found_record =
							find_and_remove_org_from_bg_list(
								bg_list,
								bg_record);
					}
					destroy_bg_record(bg_record);
					if(!found_record) {
						debug2("This record wasn't "
						       "found in the bg_list, "
						       "no big deal, it "
						       "probably wasn't added");
						//rc = SLURM_ERROR;
					} else {
						List temp_list =
							list_create(NULL);
						list_push(temp_list, 
							  found_record);
						num_block_to_free++;
						free_block_list(temp_list);
						list_destroy(temp_list);
					}
					slurm_mutex_unlock(&block_state_mutex);
				} 
				rc = 1;
					
				if(!test_only) 
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
				bitstr_t* slurm_block_bitmap,
				char *user_req_nodes)
{
	List list_of_lists = NULL;
	List temp_list = NULL;
	List new_blocks = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_ERROR;
	int create_try = 0;
	int start_geo[BA_SYSTEM_DIMENSIONS];
	
	memcpy(start_geo, request->geometry, sizeof(int)*BA_SYSTEM_DIMENSIONS);
	debug2("going to create %d", request->size);
	list_of_lists = list_create(NULL);
	
	if(user_req_nodes) 
		list_append(list_of_lists, job_block_test_list);
	else {
		list_append(list_of_lists, block_list);
		if(job_block_test_list == bg_job_block_list &&
		   list_count(block_list) != list_count(bg_booted_block_list)) {
			list_append(list_of_lists, bg_booted_block_list);
			if(list_count(bg_booted_block_list) 
			   != list_count(job_block_test_list)) 
				list_append(list_of_lists, job_block_test_list);
		} else if(list_count(block_list) 
			  != list_count(job_block_test_list)) {
			list_append(list_of_lists, job_block_test_list);
		}
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
		debug("trying with %d", create_try);
		if((new_blocks = create_dynamic_block(block_list,
						      request, temp_list))) {
			bg_record_t *bg_record = NULL;
			while((bg_record = list_pop(new_blocks))) {
				if(block_exist_in_list(block_list, bg_record))
					destroy_bg_record(bg_record);
				else {
					if(job_block_test_list 
					   == bg_job_block_list) {
						if(configure_block(bg_record)
						   == SLURM_ERROR) {
							destroy_bg_record(
								bg_record);
							error("_dynamically_"
							      "request: "
							      "unable to "
							      "configure "
							      "block");
							rc = SLURM_ERROR;
							break;
						}
					}
					list_append(block_list, bg_record);
					print_bg_record(bg_record);
					(*blocks_added) = 1;
				}
			}
			list_destroy(new_blocks);
			if(!*blocks_added) {
				memcpy(request->geometry, start_geo,      
				       sizeof(int)*BA_SYSTEM_DIMENSIONS); 
				rc = SLURM_ERROR;
				continue;
			}
			list_sort(block_list,
				  (ListCmpF)_bg_record_sort_aval_dec);
	
			rc = SLURM_SUCCESS;
			break;
		} else if (errno == ESLURM_INTERCONNECT_FAILURE) {
			rc = SLURM_ERROR;
			break;
		} 

		memcpy(request->geometry, start_geo,
		       sizeof(int)*BA_SYSTEM_DIMENSIONS);
	
	}
	list_iterator_destroy(itr);

	if(list_of_lists)
		list_destroy(list_of_lists);

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
				  bool test_only)
{
	bg_record_t *bg_record = NULL;
	uint16_t req_geometry[BA_SYSTEM_DIMENSIONS];
	uint16_t start[BA_SYSTEM_DIMENSIONS];
	uint16_t conn_type, rotate, target_size = 0;
	uint32_t req_procs = job_ptr->num_procs;
	ba_request_t request; 
	int i;
	int overlap_check = 0;
	int allow = 0;
	int check_image = 1;
	uint32_t max_procs = (uint32_t)NO_VAL;
	char tmp_char[256];
	int start_req = 0;
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

	if(!total_cpus)
		total_cpus = DIM_SIZE[X] * DIM_SIZE[Y] * DIM_SIZE[Z] 
			* procs_per_node;

	if(req_nodes > max_nodes) {
		error("can't run this job max bps is %u asking for %u",
		      max_nodes, req_nodes);
		return SLURM_ERROR;
	}

	if(!test_only && req_procs > num_unused_cpus) {
		debug2("asking for %u I only got %d", 
		       req_procs, num_unused_cpus);
		return SLURM_ERROR;
	}

	if(!block_list) {
		error("_find_best_block_match: There is no block_list");
		return SLURM_ERROR;
	}
	
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_START, &start);
		
	if(start[X] != (uint16_t)NO_VAL)
		start_req = 1;

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_CONN_TYPE, &conn_type);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_GEOMETRY, &req_geometry);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_ROTATE, &rotate);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_MAX_PROCS, &max_procs);

	
#ifdef HAVE_BGL
	if((rc = _check_images(job_ptr, &blrtsimage, &linuximage,
			       &mloaderimage, &ramdiskimage)) == SLURM_ERROR)
		goto end_it;
#else
	if((rc = _check_images(job_ptr, &linuximage,
			       &mloaderimage, &ramdiskimage)) == SLURM_ERROR)
		goto end_it;
#endif
	
	if(req_geometry[X] != 0 && req_geometry[X] != (uint16_t)NO_VAL) {
		target_size = 1;
		for (i=0; i<BA_SYSTEM_DIMENSIONS; i++)
			target_size *= (uint16_t)req_geometry[i];
		if(target_size != min_nodes) {
			debug2("min_nodes not set correctly %u should be %u "
			      "from %u%u%u",
			      min_nodes, target_size, 
			      req_geometry[X],
			      req_geometry[Y],
			      req_geometry[Z]);
			min_nodes = target_size;
		}
		if(!req_nodes)
			req_nodes = min_nodes;
	}
	if (target_size == 0) {	/* no geometry specified */
		if(job_ptr->details->req_nodes 
		   && !start_req) {
			bg_record_t *tmp_record = NULL;
			char *tmp_nodes= job_ptr->details->req_nodes;
			int len = strlen(tmp_nodes);
			
			i = 0;
			while(i<len 
			      && tmp_nodes[i] != '[' 
			      && (tmp_nodes[i] < '0' || tmp_nodes[i] > 'Z'
				  || (tmp_nodes[i] > '9'
				      && tmp_nodes[i] < 'A')))
				i++;
			
			if(i<len) {
				len -= i;
				tmp_record = xmalloc(sizeof(bg_record_t));
				tmp_record->bg_block_list =
					list_create(destroy_ba_node);
				
				len += strlen(bg_slurm_node_prefix)+1;
				tmp_record->nodes = xmalloc(len);
				
				snprintf(tmp_record->nodes,
					 len,
					 "%s%s", 
					 bg_slurm_node_prefix, 
					 tmp_nodes+i);
				
			
				process_nodes(tmp_record, false);
				for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) {
					req_geometry[i] = tmp_record->geo[i];
					start[i] = tmp_record->start[i];
				}
				destroy_bg_record(tmp_record);
				select_g_set_jobinfo(job_ptr->select_jobinfo,
						     SELECT_DATA_GEOMETRY, 
						     &req_geometry);
				select_g_set_jobinfo(job_ptr->select_jobinfo,
						     SELECT_DATA_START, 
						     &start);
				start_req = 1;
			}  else 
				error("BPs=%s is in a weird format", 
				      tmp_nodes); 
		} else {
			req_geometry[X] = (uint16_t)NO_VAL;
		}
		target_size = min_nodes;
	}
	
	*found_bg_record = NULL;
	allow = 0;

	memset(&request, 0, sizeof(ba_request_t));

	for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
		request.start[i] = start[i];
	
	for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
		request.geometry[i] = req_geometry[i];

	request.deny_pass = (uint16_t)NO_VAL;
	request.save_name = NULL;
	request.elongate_geos = NULL;
	request.size = target_size;
	request.procs = req_procs;
	request.conn_type = conn_type;
	request.rotate = rotate;
	request.elongate = true;
	request.start_req = start_req;
#ifdef HAVE_BGL
	request.blrtsimage = blrtsimage;
#endif
	request.linuximage = linuximage;
	request.mloaderimage = mloaderimage;
	request.ramdiskimage = ramdiskimage;
	if(job_ptr->details->req_node_bitmap) 
		request.avail_node_bitmap = 
			job_ptr->details->req_node_bitmap;
	else
		request.avail_node_bitmap = slurm_block_bitmap;

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_MAX_PROCS, &max_procs);
	/* since we only look at procs after this and not nodes we
	 *  need to set a max_procs if given
	 */
	if(max_procs == (uint32_t)NO_VAL) 
		max_procs = max_nodes * procs_per_node;
	
	while(1) {
		/* Here we are creating a list of all the blocks that
		 * have overlapped jobs so if we don't find one that
		 * works we will have can look and see the earliest
		 * the job can start.  This doesn't apply to Dynamic mode.
		 */ 
		if(test_only && bluegene_layout_mode != LAYOUT_DYNAMIC) 
			overlapped_list = list_create(NULL);
		
		bg_record = _find_matching_block(block_list, 
						 job_ptr,
						 slurm_block_bitmap,
						 &request,
						 max_procs,
						 &allow, check_image,
						 overlap_check, 
						 overlapped_list,
						 test_only);
		if(!bg_record && test_only
		   && bluegene_layout_mode != LAYOUT_DYNAMIC
		   && list_count(overlapped_list)) {
			ListIterator itr =
				list_iterator_create(overlapped_list);
			bg_record_t *tmp_rec = NULL;
			while((tmp_rec = list_next(itr))) {
				if(!bg_record || 
				   (tmp_rec->job_ptr->end_time <
				    bg_record->job_ptr->end_time))
					bg_record = tmp_rec;
			}
			list_iterator_destroy(itr);
		}
		
		if(test_only && bluegene_layout_mode != LAYOUT_DYNAMIC)
			list_destroy(overlapped_list);

		/* set the bitmap and do other allocation activities */
		if (bg_record) {
			if(!test_only) {
				if(check_block_bp_states(
					   bg_record->bg_block_id) 
				   == SLURM_ERROR) {
					error("_find_best_block_match: Marking "
					      "block %s in an error state "
					      "because of bad bps.",
					      bg_record->bg_block_id);
					bg_record->job_running =
						BLOCK_ERROR_STATE;
					bg_record->state = RM_PARTITION_ERROR;
					trigger_block_error();
					continue;
				}
			}
			format_node_name(bg_record, tmp_char, sizeof(tmp_char));
			
			debug("_find_best_block_match %s <%s>", 
			      bg_record->bg_block_id, 
			      tmp_char);
			bit_and(slurm_block_bitmap, bg_record->bitmap);
			rc = SLURM_SUCCESS;
			*found_bg_record = bg_record;
			goto end_it;
		} else {
			/* this gets altered in _find_matching_block so we
			   reset it */
			for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
				request.geometry[i] = req_geometry[i];
		}
		
		/* see if we can just reset the image and reboot the block */
		if(allow) {
			check_image = 0;
			allow = 0;
			continue;
		}
		
		check_image = 1;

		/* all these assume that the *bg_record is NULL */

		if(bluegene_layout_mode == LAYOUT_OVERLAP
		   && !test_only && overlap_check < 2) {
			overlap_check++;
			continue;
		}
		
		if(create_try || bluegene_layout_mode != LAYOUT_DYNAMIC)
			goto no_match;
		
		if((rc = _dynamically_request(block_list, blocks_added,
					      &request, 
					      slurm_block_bitmap, 
					      job_ptr->details->req_nodes))
		   == SLURM_SUCCESS) {
			create_try = 1;
			continue;
		}
			

		if(test_only) {
			List new_blocks = NULL;
			List job_list = NULL;
			debug("trying with empty machine");
			slurm_mutex_lock(&block_state_mutex);
			if(job_block_test_list == bg_job_block_list) 
				job_list = copy_bg_list(job_block_test_list);
			else
				job_list = job_block_test_list;
			slurm_mutex_unlock(&block_state_mutex);
			list_sort(job_list, (ListCmpF)_bg_record_sort_aval_inc);
			while(1) {
				/* this gets altered in
				 * create_dynamic_block so we reset it */
				for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
					request.geometry[i] = req_geometry[i];

				bg_record = list_pop(job_list);
				if(bg_record)
					debug2("taking off %d(%s) started "
					       "at %d ends at %d",
					       bg_record->job_running,
					       bg_record->bg_block_id,
					       bg_record->job_ptr->start_time,
					       bg_record->job_ptr->end_time);
				if(!(new_blocks = create_dynamic_block(
					     block_list, &request, job_list))) {
					destroy_bg_record(bg_record);
					if(errno == ESLURM_INTERCONNECT_FAILURE
					   || !list_count(job_list)) {
						error("this job will never "
						      "run on this system");
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
				if(!(*found_bg_record)) {
					error("got an empty list back");
					list_destroy(new_blocks);
					if(bg_record) {
						destroy_bg_record(bg_record);
						continue;
					} else {
						rc = SLURM_ERROR;
						break;
					}
				}
				bit_and(slurm_block_bitmap,
					(*found_bg_record)->bitmap);

				if(bg_record) {
					(*found_bg_record)->job_ptr 
						= bg_record->job_ptr; 
					destroy_bg_record(bg_record);
				}
					
				if(job_block_test_list != bg_job_block_list) {
					list_append(block_list,
						    (*found_bg_record));
					while((bg_record = 
					       list_pop(new_blocks))) {
						if(block_exist_in_list(
							   block_list,
							   bg_record))
							destroy_bg_record(
								bg_record);
						else {
							list_append(block_list,
								    bg_record);
//					print_bg_record(bg_record);
						}
					}
				} 
					
				list_destroy(new_blocks);
				break;
			}

			if(job_block_test_list == bg_job_block_list) 
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
	while((new_record = list_next(itr))) {
		while((bg_record = list_next(itr2))) {
			if(bit_equal(bg_record->bitmap, new_record->bitmap)
			   && bit_equal(bg_record->ionode_bitmap,
					new_record->ionode_bitmap))
				break;
		} 

		if(!bg_record) {
			bg_record = xmalloc(sizeof(bg_record_t));
			copy_bg_record(new_record, bg_record);
			debug4("adding %s", bg_record->bg_block_id);
			list_append(incomp_list, bg_record);
			count++;
		} 
		list_iterator_reset(itr2);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	sort_bg_record_inc_size(incomp_list);

	return count;
}

#endif // HAVE_BG

/*
 * Try to find resources for a given job request
 * IN job_ptr - pointer to job record in slurmctld
 * IN/OUT bitmap - nodes availble for assignment to job, clear those not to
 *	be used
 * IN min_nodes, max_nodes  - minimum and maximum number of nodes to allocate
 *	to this job (considers slurm block limits)
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * RET - SLURM_SUCCESS if job runnable now, error code otherwise
 */
extern int submit_job(struct job_record *job_ptr, bitstr_t *slurm_block_bitmap,
		      uint32_t min_nodes, uint32_t max_nodes,
		      uint32_t req_nodes, int mode)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BG
	bg_record_t* bg_record = NULL;
	char buf[100];
	uint16_t conn_type = (uint16_t)NO_VAL;
	List block_list = NULL;
	int blocks_added = 0;
	time_t starttime = time(NULL);
	bool test_only;

	if (mode == SELECT_MODE_TEST_ONLY || mode == SELECT_MODE_WILL_RUN)
		test_only = true;
	else if (mode == SELECT_MODE_RUN_NOW)
		test_only = false;
	else	
		return EINVAL;	/* something not yet supported */

	if(bluegene_layout_mode == LAYOUT_DYNAMIC)
		slurm_mutex_lock(&create_dynamic_mutex);

	job_block_test_list = bg_job_block_list;
	
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_CONN_TYPE, &conn_type);
	if(conn_type == SELECT_NAV) {
		uint32_t max_procs = (uint32_t)NO_VAL;
		if(min_nodes > 1) {
			conn_type = SELECT_TORUS;
			/* make sure the max procs are set to NO_VAL */
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_MAX_PROCS,
					     &max_procs);

		} else {
			select_g_get_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_MAX_PROCS,
					     &max_procs);
			if((max_procs > procs_per_node)
			   || (max_procs == NO_VAL))
				conn_type = SELECT_TORUS;
			else
				conn_type = SELECT_SMALL;
		}
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_CONN_TYPE,
				     &conn_type);
	}
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_MIXED);
	debug("bluegene:submit_job: %s nodes=%u-%u-%u", 
	      buf, min_nodes, req_nodes, max_nodes);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_BLRTS_IMAGE);
#ifdef HAVE_BGL
	debug2("BlrtsImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_LINUX_IMAGE);
#endif
#ifdef HAVE_BGL
	debug2("LinuxImage=%s", buf);
#else
	debug2("ComputNodeImage=%s", buf);
#endif

	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_MLOADER_IMAGE);
	debug2("MloaderImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_RAMDISK_IMAGE);
#ifdef HAVE_BGL
	debug2("RamDiskImage=%s", buf);
#else
	debug2("RamDiskIoLoadImage=%s", buf);
#endif	
	slurm_mutex_lock(&block_state_mutex);
	block_list = copy_bg_list(bg_list);
	slurm_mutex_unlock(&block_state_mutex);
	
	list_sort(block_list, (ListCmpF)_bg_record_sort_aval_dec);

	rc = _find_best_block_match(block_list, &blocks_added,
				    job_ptr, slurm_block_bitmap, min_nodes, 
				    max_nodes, req_nodes,  
				    &bg_record, test_only);
	
	if(rc == SLURM_SUCCESS) {
		if(bg_record) {
			/* Here we see if there is a job running since
			 * some jobs take awhile to finish we need to
			 * make sure the time of the end is in the
			 * future.  If it isn't (meaning it is in the
			 * past or current time) we add 5 seconds to
			 * it so we don't use the block immediately.
			 */
			if(bg_record->job_ptr 
			   && bg_record->job_ptr->end_time) { 
				if(bg_record->job_ptr->end_time <= starttime)
					starttime += 5;
				else
					starttime =
						bg_record->job_ptr->end_time;
			}
						
			job_ptr->start_time = starttime;
			
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_NODES, 
					     bg_record->nodes);
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_IONODES, 
					     bg_record->ionodes);
			
			if(!bg_record->bg_block_id) {
				uint16_t geo[BA_SYSTEM_DIMENSIONS];
				
				debug2("%d can start unassigned job %u at "
				       "%u on %s",
				       test_only, job_ptr->job_id, starttime,
				       bg_record->nodes);
				select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_BLOCK_ID,
					     "unassigned");

				min_nodes = bg_record->node_cnt;
				select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_NODE_CNT,
					     &min_nodes);
				memset(geo, 0, 
				       sizeof(uint16_t) * BA_SYSTEM_DIMENSIONS);
				select_g_set_jobinfo(job_ptr->select_jobinfo,
						     SELECT_DATA_GEOMETRY, 
						     &geo);
				/* This is a fake record so we need to
				 * destroy it after we get the info from
				 * it */
				destroy_bg_record(bg_record);
			} else {
				if((bg_record->ionodes)
				   && (job_ptr->part_ptr->max_share <= 1))
					error("Small block used in "
					      "non-shared partition");
				
				debug2("%d can start job %u at %u on %s(%s)",
				       test_only, job_ptr->job_id, starttime,
				       bg_record->bg_block_id,
				       bg_record->nodes);
				
				select_g_set_jobinfo(job_ptr->select_jobinfo,
						     SELECT_DATA_BLOCK_ID,
						     bg_record->bg_block_id);
				select_g_set_jobinfo(job_ptr->select_jobinfo,
						     SELECT_DATA_NODE_CNT, 
						     &bg_record->node_cnt);
				select_g_set_jobinfo(job_ptr->select_jobinfo,
						     SELECT_DATA_GEOMETRY, 
						     &bg_record->geo);

				/* tmp16 = bg_record->conn_type; */
/* 				select_g_set_jobinfo(job_ptr->select_jobinfo, */
/* 						     SELECT_DATA_CONN_TYPE,  */
/* 						     &tmp16); */
			}
		} else {
			error("we got a success, but no block back");
		}
	}

	if(bluegene_layout_mode == LAYOUT_DYNAMIC) {		
		slurm_mutex_lock(&block_state_mutex);
		if(blocks_added) 
			_sync_block_lists(block_list, bg_list);		
		slurm_mutex_unlock(&block_state_mutex);
		slurm_mutex_unlock(&create_dynamic_mutex);
	}

	list_destroy(block_list);
#endif
	return rc;
}

extern int test_job_list(List req_list)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BG
	bg_record_t* bg_record = NULL;
	bg_record_t* new_record = NULL;
	char buf[100];
//	uint16_t tmp16 = (uint16_t)NO_VAL;
	List block_list = NULL;
	int blocks_added = 0;
	time_t starttime = time(NULL);
	ListIterator itr = NULL;
	select_will_run_t *will_run = NULL;

	slurm_mutex_lock(&job_list_test_mutex);
	
	if(bluegene_layout_mode == LAYOUT_DYNAMIC)
		slurm_mutex_lock(&create_dynamic_mutex);

	job_block_test_list = copy_bg_list(bg_job_block_list);

	slurm_mutex_lock(&block_state_mutex);
	block_list = copy_bg_list(bg_list);
	slurm_mutex_unlock(&block_state_mutex);

	itr = list_iterator_create(req_list);
	while((will_run = list_next(itr))) {
		uint16_t conn_type = (uint16_t)NO_VAL;

		if(!will_run->job_ptr) {
			error("test_job_list: you need to give me a job_ptr");
			rc = SLURM_ERROR;
			break;
		}
		
		select_g_get_jobinfo(will_run->job_ptr->select_jobinfo,
				     SELECT_DATA_CONN_TYPE, &conn_type);
		if(conn_type == SELECT_NAV) {
			uint32_t max_procs = (uint32_t)NO_VAL;
			if(will_run->min_nodes > 1) {
				conn_type = SELECT_TORUS;
				/* make sure the max procs are set to NO_VAL */
				select_g_set_jobinfo(
					will_run->job_ptr->select_jobinfo,
					SELECT_DATA_MAX_PROCS,
					&max_procs);
				
			} else {
				select_g_get_jobinfo(
					will_run->job_ptr->select_jobinfo,
					SELECT_DATA_MAX_PROCS,
					&max_procs);
				if((max_procs > procs_per_node)
				   || (max_procs == NO_VAL))
					conn_type = SELECT_TORUS;
				else
					conn_type = SELECT_SMALL;
			}
			select_g_set_jobinfo(will_run->job_ptr->select_jobinfo,
					     SELECT_DATA_CONN_TYPE,
					     &conn_type);
		}
		select_g_sprint_jobinfo(will_run->job_ptr->select_jobinfo,
					buf, sizeof(buf), 
					SELECT_PRINT_MIXED);
		debug("bluegene:submit_job_list: %s nodes=%u-%u-%u", 
		      buf, will_run->min_nodes,
		      will_run->req_nodes, will_run->max_nodes);
		list_sort(block_list, (ListCmpF)_bg_record_sort_aval_dec);
		rc = _find_best_block_match(block_list, &blocks_added,
					    will_run->job_ptr,
					    will_run->avail_nodes,
					    will_run->min_nodes, 
					    will_run->max_nodes,
					    will_run->req_nodes, 
					    &bg_record, true);
		
		if(rc == SLURM_SUCCESS) {
			if(bg_record) {
				/* Here we see if there is a job running since
				 * some jobs take awhile to finish we need to
				 * make sure the time of the end is in the
				 * future.  If it isn't (meaning it is in the
				 * past or current time) we add 5 seconds to
				 * it so we don't use the block immediately.
				 */
				if(bg_record->job_ptr 
				   && bg_record->job_ptr->end_time) { 
					if(bg_record->job_ptr->end_time <= 
					   starttime)
						starttime += 5;
					else {
						starttime = bg_record->
							    job_ptr->end_time;
					}
				}
				bg_record->job_running =
					will_run->job_ptr->job_id;
				bg_record->job_ptr = will_run->job_ptr;
				debug2("test_job_list: "
				       "can run job %u on found block at %d"
				       "nodes = %s",
				       bg_record->job_ptr->job_id,
				       starttime,
				       bg_record->nodes);
				
				if(!block_exist_in_list(job_block_test_list,
							bg_record)) {
					new_record =
						xmalloc(sizeof(bg_record_t));
					copy_bg_record(bg_record, new_record);
					list_append(job_block_test_list,
						    new_record);
				}

				if(will_run->job_ptr->start_time) {
					if(will_run->job_ptr->start_time
					   < starttime) {
						debug2("test_job_list: "
						       "Time is later "
						       "than one supplied.");
						rc = SLURM_ERROR;
						break;
					}
					
					//continue;
				} else
					will_run->job_ptr->start_time 
						= starttime;

				if(will_run->job_ptr->time_limit != INFINITE
				   && will_run->job_ptr->time_limit != NO_VAL) 
					will_run->job_ptr->end_time =
						will_run->job_ptr->start_time +
						will_run->job_ptr->time_limit *
						60;
				else if(will_run->job_ptr->part_ptr->max_time
					!= INFINITE
					&& will_run->job_ptr->
					part_ptr->max_time != NO_VAL) 
					will_run->job_ptr->end_time =
						will_run->job_ptr->start_time +
						will_run->job_ptr->
						part_ptr->max_time * 60;
				else
					will_run->job_ptr->end_time = 
						will_run->job_ptr->start_time +
						31536000; // + year
						
				select_g_set_jobinfo(
					will_run->job_ptr->select_jobinfo,
					SELECT_DATA_NODES, 
					bg_record->nodes);
				select_g_set_jobinfo(
					will_run->job_ptr->select_jobinfo,
					SELECT_DATA_IONODES, 
					bg_record->ionodes);
				
/* 				if(!bg_record->bg_block_id) { */
/* 					uint16_t geo[BA_SYSTEM_DIMENSIONS]; */
					
/* 					debug2("test_job_list: " */
/* 					       "can start job at " */
/* 					       "%u on %s on unmade block", */
/* 					       starttime, */
/* 					       bg_record->nodes); */
/* 					select_g_set_jobinfo( */
/* 						will_run->job_ptr-> */
/* 						select_jobinfo, */
/* 						SELECT_DATA_BLOCK_ID, */
/* 						"unassigned"); */
/* 					if(will_run->job_ptr->num_procs */
/* 					   < bluegene_bp_node_cnt  */
/* 					   && will_run->job_ptr->num_procs */
/* 					   > 0) { */
/* 						i = procs_per_node/ */
/* 							will_run->job_ptr-> */
/* 							num_procs; */
/* 						debug2("divide by %d", i); */
/* 					} else  */
/* 						i = 1; */
/* 					will_run->min_nodes *=  */
/* 						bluegene_bp_node_cnt/i; */
/* 					select_g_set_jobinfo( */
/* 						will_run->job_ptr-> */
/* 						select_jobinfo, */
/* 						SELECT_DATA_NODE_CNT, */
/* 						&will_run->min_nodes); */
/* 					memset(geo, 0,  */
/* 					       sizeof(uint16_t)  */
/* 					       * BA_SYSTEM_DIMENSIONS); */
/* 					select_g_set_jobinfo( */
/* 						will_run->job_ptr-> */
/* 						select_jobinfo, */
/* 						SELECT_DATA_GEOMETRY,  */
/* 						&geo); */
/* 				} else { */
/* 					if((bg_record->ionodes) */
/* 					   && (will_run->job_ptr->part_ptr-> */
/* 					       max_share */
/* 					       <= 1)) */
/* 						error("Small block used in " */
/* 						      "non-shared partition"); */
					
/* 					debug2("test_job_list: " */
/* 					       "can start job at %u on %s", */
/* 					       starttime, */
/* 					       bg_record->nodes); */
					
/* 					select_g_set_jobinfo( */
/* 						will_run->job_ptr-> */
/* 						select_jobinfo, */
/* 						SELECT_DATA_BLOCK_ID, */
/* 						bg_record->bg_block_id); */
/* 					select_g_set_jobinfo( */
/* 						will_run->job_ptr-> */
/* 						select_jobinfo, */
/* 						SELECT_DATA_NODE_CNT,  */
/* 						&bg_record->node_cnt); */
/* 					select_g_set_jobinfo( */
/* 						will_run->job_ptr-> */
/* 						select_jobinfo, */
/* 						SELECT_DATA_GEOMETRY,  */
/* 						&bg_record->geo); */
					
/* 					tmp16 = bg_record->conn_type; */
/* 					select_g_set_jobinfo( */
/* 						will_run->job_ptr-> */
/* 						select_jobinfo, */
/* 						SELECT_DATA_CONN_TYPE,  */
/* 						&tmp16); */
/* 				} */
			} else {
				error("we got a success, but no block back");
				rc = SLURM_ERROR;
			}
		}
	}
	list_iterator_destroy(itr);

	if(bluegene_layout_mode == LAYOUT_DYNAMIC) 		
		slurm_mutex_unlock(&create_dynamic_mutex);
	

	list_destroy(block_list);
	list_destroy(job_block_test_list);
	
	slurm_mutex_unlock(&job_list_test_mutex);
#endif
	return rc;
}
