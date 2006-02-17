/*****************************************************************************\
 *  bg_job_place.c - blue gene job placement (e.g. base block selection)
 *  functions.
 *
 *  $Id$ 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/common/node_select.h"
#include "bluegene.h"

#define BUFSIZE 4096
#define BITSIZE 128

#define _DEBUG 0

#define SWAP(a,b,t)	\
_STMT_START {		\
	(t) = (a);	\
	(a) = (b);	\
	(b) = (t);	\
} _STMT_END

static int  _find_best_block_match(struct job_record* job_ptr,
				bitstr_t* slurm_block_bitmap,
				int min_nodes, int max_nodes,
				int spec, bg_record_t** found_bg_record);
static void _rotate_geo(uint16_t *req_geometry, int rot_cnt);

/* Rotate a 3-D geometry array through its six permutations */
static void _rotate_geo(uint16_t *req_geometry, int rot_cnt)
{
	uint16_t tmp;

	switch (rot_cnt) {
		case 0:		/* ABC -> ACB */
			SWAP(req_geometry[1], req_geometry[2], tmp);
			break;
		case 1:		/* ACB -> CAB */
			SWAP(req_geometry[0], req_geometry[1], tmp);
			break;
		case 2:		/* CAB -> CBA */
			SWAP(req_geometry[1], req_geometry[2], tmp);
			break;
		case 3:		/* CBA -> BCA */
			SWAP(req_geometry[0], req_geometry[1], tmp);
			break;
		case 4:		/* BCA -> BAC */
			SWAP(req_geometry[1], req_geometry[2], tmp);
			break;
		case 5:		/* BAC -> ABC */
			SWAP(req_geometry[0], req_geometry[1], tmp);
			break;
	}
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
static int _find_best_block_match(struct job_record* job_ptr, 
		bitstr_t* slurm_block_bitmap, int min_nodes, int max_nodes,
		int spec, bg_record_t** found_bg_record)
{
	ListIterator itr;
	bg_record_t* record = NULL;
	int i;
	uint16_t req_geometry[BA_SYSTEM_DIMENSIONS];
	uint16_t conn_type, rotate, target_size = 1, checked;
	uint32_t req_procs = job_ptr->num_procs;
	int rot_cnt = 0;
	uint32_t proc_cnt;
	int job_running = 0;
       
	if(!bg_list) {
		error("_find_best_block_match: There is no bg_list");
		return SLURM_ERROR;
	}
	
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_CHECKED, &checked);
	
	/* have to check checked to see which time the node 
	   scheduler is looking to see if it is runnable.  If checked >=2 
	   we want to fall through to tell the scheduler that it is runnable
	   just not right now. 
	*/
	slurm_mutex_lock(&block_state_mutex);
	if((full_system_block->job_running != -1) && (checked < 2)) {
		checked++;
		slurm_mutex_unlock(&block_state_mutex);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_CHECKED, &checked);
	
		debug("_find_best_block_match none found "
		      "full system running on block %s. %d",
		      full_system_block->bg_block_id, 
		      full_system_block->job_running);

		return SLURM_ERROR;
	}
	slurm_mutex_unlock(&block_state_mutex);
			
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_CONN_TYPE, &conn_type);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_GEOMETRY, req_geometry);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_ROTATE, &rotate);
	for (i=0; i<BA_SYSTEM_DIMENSIONS; i++)
		target_size *= req_geometry[i];
	if (target_size == 0)	/* no geometry specified */
		target_size = min_nodes;
	/* this is where we should have the control flow depending on
	 * the spec arguement */

	*found_bg_record = NULL;
	
	debug("number of blocks to check: %d", list_count(bg_list));
     	itr = list_iterator_create(bg_list);
	while ((record = (bg_record_t*) list_next(itr))) {
		/* Check processor count */
		/* have to check checked to see which time the node 
		   scheduler is looking to see if it is runnable.  
		   If checked >=2 we want to fall through to tell the 
		   scheduler that it is runnable just not right now. 
		*/
		slurm_mutex_lock(&block_state_mutex);
		debug3("job_running = %d", record->job_running);
		if((record->job_running != -1) && (checked < 2)) {
			job_running++;
			debug("block %s in use by %s", 
			      record->bg_block_id,
			      record->user_name);
			slurm_mutex_unlock(&block_state_mutex);
			continue;
		}
		slurm_mutex_unlock(&block_state_mutex);
	
		if(record->full_block && job_running) {
			debug("Can't run on full system block "
			      "another block has a job running.");
			continue;
		}
			
		if (req_procs > record->cnodes_per_bp) {
			/* We use the c-node count here. Job could start
			 * twice this count if VIRTUAL_NODE_MODE, but this
			 * is now controlled by mpirun, not SLURM 
			 * We now use the number set by the admins in the
			 * slurm.conf file.  This should never happen.
			 */
			proc_cnt = record->bp_count * record->cnodes_per_bp;
			if (req_procs > proc_cnt) {
				debug("block %s CPU count too low",
					record->bg_block_id);
				continue;
			}
		}
		
		/*
		 * check that the number of nodes is suitable
		 */
 		if ((record->bp_count < min_nodes)
		    ||  (max_nodes != 0 && record->bp_count > max_nodes)
		    ||  (record->bp_count < target_size)) {
			debug("block %s node count not suitable",
				record->bg_block_id);
			continue;
		}
		
		/*
		 * Next we check that this block's bitmap is within 
		 * the set of nodes which the job can use. 
		 * Nodes not available for the job could be down,
		 * drained, allocated to some other job, or in some 
		 * SLURM block not available to this job.
		 */
		if (!bit_super_set(record->bitmap, slurm_block_bitmap)) {
			debug("bg block %s has nodes not usable by this "
				"job", record->bg_block_id);
			continue;
		}

		/*
		 * Insure that any required nodes are in this BG block
		 */
		if (job_ptr->details->req_node_bitmap
		    && (!bit_super_set(job_ptr->details->req_node_bitmap,
				       record->bitmap))) {
			debug("bg block %s lacks required nodes",
				record->bg_block_id);
			continue;
		}

		/***********************************************/
		/* check the connection type specified matches */
		/***********************************************/
		if ((conn_type != record->conn_type)
		&&  (conn_type != SELECT_NAV)) {
			debug("bg block %s conn-type not usable", 
				record->bg_block_id);
			continue;
		} 

		/*****************************************/
		/* match up geometry as "best" possible  */
		/*****************************************/
		if (req_geometry[0] == 0)
			;	/* Geometry not specified */
		else {	/* match requested geometry */
			bool match = false;
			rot_cnt = 0;	/* attempt six rotations  */

			for (rot_cnt=0; rot_cnt<6; rot_cnt++) {
				
				if ((record->geo[X] >= req_geometry[X])
				&&  (record->geo[Y] >= req_geometry[Y])
				&&  (record->geo[Z] >= req_geometry[Z])) {
					match = true;
					break;
				}
				if (!rotate)
					break;
				_rotate_geo(req_geometry, rot_cnt);
			}

			if (!match) 
				continue;	/* Not usable */
		}
		
		*found_bg_record = record;
		break;
	}
	list_iterator_destroy(itr);
	checked++;
	select_g_set_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_CHECKED, &checked);
				
	/* set the bitmap and do other allocation activities */
	if (*found_bg_record) {
		debug("_find_best_block_match %s <%s>", 
			(*found_bg_record)->bg_block_id, 
			(*found_bg_record)->nodes);
		bit_and(slurm_block_bitmap, (*found_bg_record)->bitmap);
		return SLURM_SUCCESS;
	}
	
	debug("_find_best_block_match none found");
	return SLURM_ERROR;
}

/*
 * Try to find resources for a given job request
 * IN job_ptr - pointer to job record in slurmctld
 * IN/OUT bitmap - nodes availble for assignment to job, clear those not to
 *	be used
 * IN min_nodes, max_nodes  - minimum and maximum number of nodes to allocate
 *	to this job (considers slurm block limits)
 * RET - SLURM_SUCCESS if job runnable now, error code otherwise
 */
extern int submit_job(struct job_record *job_ptr, bitstr_t *slurm_block_bitmap,
		      int min_nodes, int max_nodes)
{
	int spec = 1; /* this will be like, keep TYPE a priority, etc,  */
	bg_record_t* record = NULL;
	char buf[100];
		
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
		SELECT_PRINT_MIXED);
	debug("bluegene:submit_job: %s nodes=%d-%d", 
	      buf, 
	      min_nodes, 
	      max_nodes);
	
	if ((_find_best_block_match(job_ptr, slurm_block_bitmap, min_nodes, 
				max_nodes, spec, &record)) == SLURM_ERROR) {
		return SLURM_ERROR;
	} else {
		/* set the block id and quarter (if any) */
		select_g_set_jobinfo(job_ptr->select_jobinfo,
			SELECT_DATA_BLOCK_ID, record->bg_block_id);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
			SELECT_DATA_QUARTER, &record->quarter);
	}

	return SLURM_SUCCESS;
}
