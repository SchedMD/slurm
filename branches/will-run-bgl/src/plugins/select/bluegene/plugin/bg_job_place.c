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
#include "src/slurmctld/trigger_mgr.h"
#include "bluegene.h"

#define _DEBUG 0
#define MAX_GROUPS 128

#define SWAP(a,b,t)	\
_STMT_START {		\
	(t) = (a);	\
	(a) = (b);	\
	(b) = (t);	\
} _STMT_END

pthread_mutex_t create_dynamic_mutex = PTHREAD_MUTEX_INITIALIZER;

static void _rotate_geo(uint16_t *req_geometry, int rot_cnt);
static int _get_user_groups(uint32_t user_id, uint32_t group_id, 
			     gid_t *groups, int max_groups, int *ngroups);
static int _test_image_perms(char *image_name, List image_list, 
			      struct job_record* job_ptr);
static int _check_requests(uint16_t *start, uint32_t req_procs, int start_req);
static int _add_to_request_list(uint16_t *start, 
				uint32_t req_procs, int start_req);
static int _check_images(struct job_record* job_ptr,
			 char **blrtsimage, char **linuximage,
			 char **mloaderimage, char **ramdiskimage);
static bg_record_t *_find_matching_block(List block_list, 
					 struct job_record* job_ptr, 
					 bitstr_t* slurm_block_bitmap,
					 ba_request_t *request,
					 uint32_t max_procs,
					 int allow, int check_image,
					 int created, int test_only);
static int _check_for_booted_overlapping_blocks(
	List block_list, ListIterator bg_record_itr,
	bg_record_t *bg_record, int overlap_check, int test_only);
static int _dynamically_request(List block_list, ba_request_t *request,
				bitstr_t* slurm_block_bitmap,
				char *user_req_nodes);
static int _find_best_block_match(List block_list, 
				  struct job_record* job_ptr,
				  bitstr_t* slurm_block_bitmap,
				  uint32_t min_nodes, 
				  uint32_t max_nodes, uint32_t req_nodes,
				  int spec, bg_record_t** found_bg_record,
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
 * Get a list of groups associated with a specific user_id
 * Return 0 on success, -1 on failure
 */
static int _get_user_groups(uint32_t user_id, uint32_t group_id, 
			    gid_t *groups, int max_groups, int *ngroups)
{
	struct passwd pwd, *results;
	char *buffer;
	static size_t buf_size = 0;
	int rc;

	if (!buf_size && ((buf_size = sysconf(_SC_GETPW_R_SIZE_MAX)) < 0)) {
		error("sysconf(_SC_GETPW_R_SIZE_MAX)");
		return -1;
	}
	buffer = xmalloc(buf_size);
	rc = getpwuid_r((uid_t) user_id, &pwd, buffer, buf_size, &results);
	if (rc != 0) {
		error("getpwuid_r(%u): %m", user_id);
		xfree(buffer);
		return -1;
	}
	*ngroups = max_groups;
	rc = getgrouplist(pwd.pw_name, (gid_t) group_id, groups, ngroups);
	xfree(buffer);
	if (rc < 0) {
		error("getgrouplist(%s): %m", pwd.pw_name);
		return -1;
	}
	*ngroups = rc;

	return 0;
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
		if (!strcasecmp(image->name, image_name) ||
		    !strcasecmp(image->name, "*")) {
			if (image->def) {
				allow = 1;
				break;
			}
			if (!image->groups ||
			    !list_count(image->groups)) {
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
			while ((allow == 0) &&
			       (image_group = list_next(itr2))) {
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

static int _check_requests(uint16_t *start, uint32_t req_procs, int start_req)
{
	int found = 0;
	ListIterator itr = NULL;
	ba_request_t *try_request = NULL; 
	
	slurm_mutex_lock(&request_list_mutex);
	itr = list_iterator_create(bg_request_list);

	while ((try_request = list_next(itr))) {
		if(start_req) {
			if ((try_request->start[X] != start[X])
			    || (try_request->start[Y] != start[Y])
			    || (try_request->start[Z] != start[Z])) {
				debug4("got %c%c%c looking for %c%c%c",
				       alpha_num[try_request->start[X]],
				       alpha_num[try_request->start[Y]],
				       alpha_num[try_request->start[Z]],
				       alpha_num[start[X]],
				       alpha_num[start[Y]],
				       alpha_num[start[Z]]);
				continue;
			}
			debug3("found %c%c%c looking for %c%c%c",
			       alpha_num[try_request->start[X]],
			       alpha_num[try_request->start[Y]],
			       alpha_num[try_request->start[Z]],
			       alpha_num[start[X]],
			       alpha_num[start[Y]],
			       alpha_num[start[Z]]);
		}

		if(try_request->procs == req_procs) {
			debug("already tried to create but "
			      "can't right now.");
			found = 1;
			break;
		}				
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&request_list_mutex);

	return found;
}

static int _add_to_request_list(uint16_t *start, 
				uint32_t req_procs, int start_req) 
{
	ba_request_t *try_request = NULL; 
	int i = 0;

	/* 
	   add request to list so we don't try again until 
	   something happens like a job finishing or 
	   something so we can try again 
	*/
	debug2("adding request for %d", req_procs);
	try_request = xmalloc(sizeof(ba_request_t));
	try_request->procs = req_procs;
	try_request->save_name = NULL;
	try_request->elongate_geos = NULL;
	try_request->start_req = start_req;
	for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
		try_request->start[i] = start[i];
	slurm_mutex_lock(&request_list_mutex);
	list_push(bg_request_list, try_request);
	slurm_mutex_unlock(&request_list_mutex);

	return SLURM_SUCCESS;
}

static int _check_images(struct job_record* job_ptr,
			 char **blrtsimage, char **linuximage,
			 char **mloaderimage, char **ramdiskimage)
{
	int allow = 0;

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_BLRTS_IMAGE, blrtsimage);
	
	if (*blrtsimage) {
		allow = _test_image_perms(*blrtsimage, bg_blrtsimage_list, 
					  job_ptr);
		if (!allow) {
			error("User %u:%u is not allowed to use BlrtsImage %s",
			      job_ptr->user_id, job_ptr->group_id, *blrtsimage);
			return SLURM_ERROR;
		       
		}
	}

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
					 int allow, int check_image,
					 int overlap_check, int test_only)
{
	bg_record_t *bg_record = NULL;
	ListIterator itr = NULL;
	uint32_t proc_cnt = 0;
	char tmp_char[256];
	
	debug("number of blocks to check: %d state %d", 
	      list_count(block_list),
	      test_only);

	itr = list_iterator_create(block_list);
	while ((bg_record = (bg_record_t*) list_next(itr))) {		
		/* If test_only we want to fall through to tell the 
		   scheduler that it is runnable just not right now. 
		*/
		debug3("%s job_running = %d", 
		       bg_record->bg_block_id, bg_record->job_running);
		/*block is messed up some how (BLOCK_ERROR_STATE) ignore it*/
		if(bg_record->job_running == BLOCK_ERROR_STATE) {
			debug("block %s is in an error state (can't use)", 
			      bg_record->bg_block_id);			
			continue;
		} else if((bg_record->job_running != NO_JOB_RUNNING) 
			  && !test_only) {
			debug("block %s in use by %s job %d", 
			      bg_record->bg_block_id,
			      bg_record->user_name,
			      bg_record->job_running);
			continue;
		}
		
		/* Check processor count */
		proc_cnt = bg_record->bp_count * bg_record->cpus_per_bp;
		debug3("asking for %u-%u looking at %d", 
		       request->procs, max_procs, proc_cnt);
		if ((proc_cnt < request->procs)
		    || ((max_procs != NO_VAL) && (proc_cnt > max_procs))) {
			/* We use the proccessor count per partition here
			   mostly to see if we can run on a smaller partition. 
			 */
			convert_num_unit((float)proc_cnt, tmp_char, 
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
			   overlap_check, test_only))
			continue;
		
		if(check_image) {
			if(request->blrtsimage &&
			   strcasecmp(request->blrtsimage,
				      bg_record->blrtsimage)) {
				allow = 1;
				continue;
			} else if(request->linuximage &&
			   strcasecmp(request->linuximage,
				      bg_record->linuximage)) {
				allow = 1;
				continue;
			} else if(request->mloaderimage &&
			   strcasecmp(request->mloaderimage, 
				      bg_record->mloaderimage)) {
				allow = 1;
				continue;
			} else if(request->ramdiskimage &&
			   strcasecmp(request->ramdiskimage,
				      bg_record->ramdiskimage)) {
				allow = 1;
				continue;
			}			
		}
			
		/***********************************************/
		/* check the connection type specified matches */
		/***********************************************/
		if ((request->conn_type != bg_record->conn_type)
		    && (request->conn_type != SELECT_NAV)) {
			debug("bg block %s conn-type not usable asking for %s "
			      "bg_record is %s", 
			      bg_record->bg_block_id,
			      convert_conn_type(request->conn_type),
			      convert_conn_type(bg_record->conn_type));
			continue;
		} 

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
				&&  (bg_record->geo[Y] >= request->geometry[Y])
				&&  (bg_record->geo[Z] >= request->geometry[Z])) {
					match = true;
					break;
				}
				if (!request->rotate) {
					break;
				}
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
	bg_record_t *bg_record, int overlap_check, int test_only)
{
	bg_record_t *found_record = NULL;
	ListIterator itr = NULL;
	int rc = 0;

	/* this test only is for actually picking a block not testing */
	if(test_only)
		return rc;

	/* Make sure no other partitions are under this partition 
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
		if(blocks_overlap(bg_record, found_record)) {
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
				rc = 1;
				break;
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
					List temp_list = list_create(NULL);
					list_remove(bg_record_itr);
					list_push(temp_list, bg_record);
					num_block_to_free++;
					free_block_list(temp_list);
					list_destroy(temp_list);
				} 
				rc = 1;
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

static int _dynamically_request(List block_list, ba_request_t *request,
				bitstr_t* slurm_block_bitmap,
				char *user_req_nodes)
{
	List lists_of_lists = NULL;
	List temp_list = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_ERROR;
	int create_try = 0;
	int start_geo[BA_SYSTEM_DIMENSIONS];

	memcpy(start_geo, request->geometry, sizeof(int)*BA_SYSTEM_DIMENSIONS);
	debug2("going to create %d", request->size);
	lists_of_lists = list_create(NULL);
	if(user_req_nodes) {
		list_append(lists_of_lists, bg_job_block_list);
	} else {
		list_append(lists_of_lists, block_list);
		if(list_count(block_list)
		   != list_count(bg_booted_block_list)) {
			list_append(lists_of_lists, 
				    bg_booted_block_list);
			if(list_count(bg_booted_block_list) 
			   != list_count(bg_job_block_list)) 
				list_append(lists_of_lists, 
					    bg_job_block_list);
		} else if(list_count(block_list) 
			  != list_count(bg_job_block_list)) 
			list_append(lists_of_lists, bg_job_block_list);
	}
	itr = list_iterator_create(lists_of_lists);
	while ((temp_list = (List)list_next(itr))) {
		create_try++;
		
		/* 1- try empty space
		   2- we see if we can create one in the 
		   unused bps
		   3- see if we can create one in the non 
		   job running bps
		*/
		debug("trying with %d", create_try);
		if(create_dynamic_block(block_list, request, temp_list) 
		   == SLURM_SUCCESS) {
			rc = SLURM_SUCCESS;
			break;
		}
		memcpy(request->geometry, start_geo,
		       sizeof(int)*BA_SYSTEM_DIMENSIONS);
	
	}
	list_iterator_destroy(itr);
	if(lists_of_lists)
		list_destroy(lists_of_lists);

	return rc;
}
/*
 * finds the best match for a given job request 
 * 
 * IN - int spec right now holds the place for some type of
 * specification as to the importance of certain job params, for
 * instance, geometry, type, size, etc.
 * 
 * OUT - block_id of matched block, NULL otherwise
 * returns 1 for error (no match)
 * 
 */
static int _find_best_block_match(List block_list, 
				  struct job_record* job_ptr, 
				  bitstr_t* slurm_block_bitmap,
				  uint32_t min_nodes, uint32_t max_nodes,
				  uint32_t req_nodes, int spec,
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
	char *blrtsimage = NULL;        /* BlrtsImage for this request */
	char *linuximage = NULL;        /* LinuxImage for this request */
	char *mloaderimage = NULL;      /* mloaderImage for this request */
	char *ramdiskimage = NULL;      /* RamDiskImage for this request */
	int rc = SLURM_SUCCESS;
	int create_try = 0;

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

	if(num_unused_cpus != total_cpus) {
		/* 
		   see if we have already tried to create this 
		   size but couldn't make it right now no reason 
		   to try again 
		*/
		if(_check_requests(start, req_procs, start_req)) {
			if(test_only)
				return SLURM_SUCCESS;
			else
				return SLURM_ERROR;
		}
	}
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_CONN_TYPE, &conn_type);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_GEOMETRY, &req_geometry);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_ROTATE, &rotate);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_MAX_PROCS, &max_procs);

	
	if(_check_images(job_ptr, &blrtsimage, &linuximage,
			 &mloaderimage, &ramdiskimage) == SLURM_ERROR)
		goto end_it;
	
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
				slurm_conf_lock();
				len += strlen(slurmctld_conf.node_prefix)+1;
				tmp_record->nodes = xmalloc(len);
				
				snprintf(tmp_record->nodes,
					 len,
					 "%s%s", 
					 slurmctld_conf.node_prefix, 
					 tmp_nodes+i);
				slurm_conf_unlock();
			
				process_nodes(tmp_record);
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
	
	/* this is where we should have the control flow depending on
	 * the spec arguement */
		
	*found_bg_record = NULL;
	allow = 0;

	for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
		request.start[i] = start[i];
	
	for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
		request.geometry[i] = req_geometry[i];
	
	request.save_name = NULL;
	request.elongate_geos = NULL;
	request.size = target_size;
	request.procs = req_procs;
	request.conn_type = conn_type;
	request.rotate = rotate;
	request.elongate = true;
	request.start_req = start_req;
	request.blrtsimage = blrtsimage;
	request.linuximage = linuximage;
	request.mloaderimage = mloaderimage;
	request.ramdiskimage = ramdiskimage;

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_MAX_PROCS, &max_procs);
	/* since we only look at procs after this and not nodes we
	 *  need to set a max_procs if given
	 */
	if(max_procs == (uint32_t)NO_VAL) 
		max_procs = max_nodes * procs_per_node;
	

	while(1) {
		bg_record = _find_matching_block(block_list, 
						 job_ptr,
						 slurm_block_bitmap,
						 &request,
						 max_procs,
						 allow, check_image,
						 overlap_check, test_only);
		
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
		
		if(bluegene_layout_mode != LAYOUT_DYNAMIC) {
			if(test_only) 
				_add_to_request_list(start,
						     req_procs, start_req);
			
			goto not_dynamic;
		}

		if(create_try)
			goto not_dynamic;
		
		if((rc = _dynamically_request(block_list, &request, 
					      slurm_block_bitmap, 
					      job_ptr->details->req_nodes))
		   == SLURM_SUCCESS) {
			create_try = 1;
			continue;
		}
			

		if(test_only) {
			char tmp_char[256];
			bitstr_t* tmp_bitmap = NULL;

			debug("trying with empty machine");
			if(create_dynamic_block(block_list, &request, NULL)
			   == SLURM_ERROR) {
				error("this job will never run on "
				      "this system");
				xfree(request.save_name);
				break;
			} 
			if(!request.save_name) {
				error("no name returned from "
				      "create_dynamic_block");
				break;
			} 
			
			_add_to_request_list(
				start, 
				req_procs,
				start_req);
			
			slurm_conf_lock();
			snprintf(tmp_char, sizeof(tmp_char), "%s%s", 
				 slurmctld_conf.node_prefix,
				 request.save_name);
			slurm_conf_unlock();
			
			if (node_name2bitmap(tmp_char, false,
					     &tmp_bitmap)) 
				fatal("Unable to convert nodes %s to bitmap", 
				      tmp_char);
			
			bit_and(slurm_block_bitmap, tmp_bitmap);
			FREE_NULL_BITMAP(tmp_bitmap);
			xfree(request.save_name);
			goto end_it;
		} else {
			break;
		}
	}
not_dynamic:
	debug("_find_best_block_match none found");
	rc = SLURM_ERROR;

end_it:
	xfree(blrtsimage);
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

	itr = list_iterator_create(full_list);
	itr2 = list_iterator_create(incomp_list);
	while((new_record = list_next(itr))) {
		while((bg_record = list_next(itr2))) {
			if(!strcmp(bg_record->bg_block_id,
				   new_record->bg_block_id))
				break;
		} 

		if(!bg_record) {
			bg_record = xmalloc(sizeof(bg_record_t));
			copy_bg_record(new_record, bg_record);
			list_append(incomp_list, bg_record);
		}
		list_iterator_reset(itr2);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	sort_bg_record_inc_size(incomp_list);

	return SLURM_SUCCESS;
}


/*
 * Try to find resources for a given job request
 * IN job_ptr - pointer to job record in slurmctld
 * IN/OUT bitmap - nodes availble for assignment to job, clear those not to
 *	be used
 * IN min_nodes, max_nodes  - minimum and maximum number of nodes to allocate
 *	to this job (considers slurm block limits)
 * IN test_only - if true, only test if ever could run, not necessarily now
 * RET - SLURM_SUCCESS if job runnable now, error code otherwise
 */
extern int submit_job(struct job_record *job_ptr, bitstr_t *slurm_block_bitmap,
		      uint32_t min_nodes, uint32_t max_nodes, uint32_t req_nodes, 
		      bool test_only)
{
	int spec = 1; /* this will be like, keep TYPE a priority, etc,  */
	bg_record_t* bg_record = NULL;
	char buf[100];
	int i, rc = SLURM_SUCCESS;
	uint16_t geo[BA_SYSTEM_DIMENSIONS];
	uint16_t tmp16 = (uint16_t)NO_VAL;
	
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_MIXED);
	debug("bluegene:submit_job: %s nodes=%u-%u-%u", 
	      buf, min_nodes, req_nodes, max_nodes);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_BLRTS_IMAGE);
	debug2("BlrtsImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_LINUX_IMAGE);
	debug2("LinuxImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_MLOADER_IMAGE);
	debug2("MloaderImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_RAMDISK_IMAGE);
	debug2("RamDiskImage=%s", buf);
	
	if(bluegene_layout_mode == LAYOUT_DYNAMIC)
		slurm_mutex_lock(&create_dynamic_mutex);
	DEF_TIMERS;
	START_TIMER;
	slurm_mutex_lock(&block_state_mutex);
	List block_list = copy_bg_list(bg_list);
	slurm_mutex_unlock(&block_state_mutex);
	END_TIMER2("submit");
	info("got time of %s", TIME_STR);
	
	rc = _find_best_block_match(block_list,
				    job_ptr, slurm_block_bitmap, min_nodes, 
				    max_nodes, req_nodes, spec, 
				    &bg_record, test_only);
	
	if(rc == SLURM_SUCCESS) {
		if(!bg_record) {
			debug2("can run, but block not made");
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_BLOCK_ID,
					     "unassigned");
			if(job_ptr->num_procs < bluegene_bp_node_cnt 
				&& job_ptr->num_procs > 0) {
				i = procs_per_node/job_ptr->num_procs;
				debug2("divide by %d", i);
			} else 
				i = 1;
			min_nodes *= bluegene_bp_node_cnt/i;
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_NODE_CNT,
					     &min_nodes);
			
			for(i=0; i<BA_SYSTEM_DIMENSIONS; i++)
				geo[i] = 0;
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_GEOMETRY, 
					     &geo);
			
		} else {
			if((bg_record->ionodes)
			   && (job_ptr->part_ptr->max_share <= 1))
				error("Small block used in "
				      "non-shared partition");

			/* set the block id and info about block */
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_BLOCK_ID, 
					     bg_record->bg_block_id);
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_IONODES, 
					     bg_record->ionodes);
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_NODE_CNT, 
					     &bg_record->node_cnt);
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_GEOMETRY, 
					     &bg_record->geo);
			tmp16 = bg_record->conn_type;
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_CONN_TYPE, 
					     &tmp16);
		}
		if(test_only) {
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_BLOCK_ID,
					     "unassigned");
		} 
	}

	if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
		slurm_mutex_lock(&block_state_mutex);
		_sync_block_lists(block_list, bg_list);
		slurm_mutex_unlock(&block_state_mutex);
		slurm_mutex_unlock(&create_dynamic_mutex);
	}

	list_destroy(block_list);
	return rc;
}

/*
 * Try to find resources and when they are avaliable for a given job request
 * IN job_ptr - pointer to job record in slurmctld
 * IN/OUT bitmap - nodes availble for assignment to job, clear those not to
 *	be used
 * IN min_nodes, max_nodes  - minimum and maximum number of nodes to allocate
 *	to this job (considers slurm block limits)
 * RET NULL on failure, select_will_run_t on success
 */
extern int job_will_run(struct job_record *job_ptr,
			bitstr_t *slurm_block_bitmap,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes)
{
	int spec = 1; /* this will be like, keep TYPE a priority, etc,  */
	bg_record_t* bg_record = NULL;
	char buf[100];
	int rc = SLURM_SUCCESS;
	uint16_t tmp16 = (uint16_t)NO_VAL;
	
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_MIXED);
	debug("bluegene:submit_job: %s nodes=%u-%u-%u", 
	      buf, min_nodes, req_nodes, max_nodes);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_BLRTS_IMAGE);
	debug2("BlrtsImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_LINUX_IMAGE);
	debug2("LinuxImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_MLOADER_IMAGE);
	debug2("MloaderImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_RAMDISK_IMAGE);
	debug2("RamDiskImage=%s", buf);
	
	if(bluegene_layout_mode == LAYOUT_DYNAMIC)
		slurm_mutex_lock(&create_dynamic_mutex);
	
	DEF_TIMERS;
	START_TIMER;
	slurm_mutex_lock(&block_state_mutex);
	List block_list = copy_bg_list(bg_list);
	slurm_mutex_unlock(&block_state_mutex);
	END_TIMER2("submit");
	info("got to of %s", TIME_STR);
	
	rc = _find_best_block_match(block_list,
				    job_ptr, slurm_block_bitmap, min_nodes, 
				    max_nodes, req_nodes, spec, 
				    &bg_record, 1);
	
	if(rc == SLURM_SUCCESS) {		
		if((bg_record->ionodes)
		   && (job_ptr->part_ptr->max_share <= 1))
			error("Small block used in "
			      "non-shared partition");
		
		/* set the block id and info about block */
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_IONODES, 
				     bg_record->ionodes);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_NODES, 
				     bg_record->nodes);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_NODE_CNT, 
				     &bg_record->node_cnt);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_GEOMETRY, 
				     &bg_record->geo);
		tmp16 = bg_record->conn_type;
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_CONN_TYPE, 
				     &tmp16);
		
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_BLOCK_ID,
				     "unassigned");
	}
	if(bluegene_layout_mode == LAYOUT_DYNAMIC)
		slurm_mutex_unlock(&create_dynamic_mutex);
	
	return rc;
}
