/*****************************************************************************\
 *  bgl_job_place.c - blue gene job placement (e.g. base partition selection)
 *  functions. 
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
#define DEFAULT_BLUEGENE_SERIAL "BGL"

#define _DEBUG 0


static int _rotate_geo(int *geometry);
static bgl_record_t *_find_best_partition_match(struct job_record* job_ptr,
				bitstr_t* slurm_part_bitmap,
				int min_nodes, int max_nodes,
				int spec);

/* Rotate a 3-D geometry array through its six permutations */
static int _rotate_geo(int *geometry)
{
	static int rotate_count = 0;
	int temp;

	if (rotate_count==(PA_SYSTEM_DIMENSIONS-1)) {
		//printf("Special!\n");
		temp=geometry[X];
		geometry[X]=geometry[Z];
		geometry[Z]=temp;
		rotate_count++;
		return 1;
		
	} else if(rotate_count<(PA_SYSTEM_DIMENSIONS*2)) {
		temp=geometry[X];
		geometry[X]=geometry[Y];
		geometry[Y]=geometry[Z];
		geometry[Z]=temp;
		rotate_count++;
		return 1;
	} else {
		rotate_count = 0;
		return 0;
	}
}
/*
 * finds the best match for a given job request 
 * 
 * IN - int spec right now holds the place for some type of
 * specification as to the importance of certain job params, for
 * instance, geometry, type, size, etc.
 * 
 * OUT - part_id of matched partition, NULL otherwise
 * returns 1 for error (no match)
 * 
 */
static bgl_record_t *_find_best_partition_match(struct job_record* job_ptr, 
		bitstr_t* slurm_part_bitmap, int min_nodes, int max_nodes,
		int spec)
{
	ListIterator itr;
	bgl_record_t *record;
	int i;
	int req_geometry[SYSTEM_DIMENSIONS];
	int conn_type, node_use, rotate, target_size = 1;

	sort_bgl_record_inc_size(bgl_list);

	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_CONN_TYPE, &conn_type);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_GEOMETRY, req_geometry);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_NODE_USE, &node_use);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_ROTATE, &rotate);
	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		target_size *= req_geometry[i];
	if (target_size == 0)	/* no geometry specified */
		target_size = min_nodes;

	/* this is where we should have the control flow depending on
	 * the spec arguement */
	itr = list_iterator_create(bgl_list);
	
	debug("number of partitions to check: %d", list_count(bgl_list));
	while ((record = (bgl_record_t*) list_next(itr)) != NULL) {
		/*
		 * check that the number of nodes is suitable
		 */
 		if ((record->state == RM_PARTITION_FREE)  
		      && ((conn_type == record->conn_type)
			|| (conn_type != SELECT_NAV))
		    && ((node_use == record->node_use) 
			|| (node_use != SELECT_NAV))
		    && (record->bp_count > min_nodes)
		    && (record->bp_count < max_nodes)
		    && (record->bp_count < target_size) 
			) {
		
			/*****************************************/
			/* match up geometry as "best" possible  */
			/*****************************************/
			if (req_geometry[0]) {				
				/* match requested geometry */
				
				while(1) {
					if ((record->coord[X] == req_geometry[X])
					    && (record->coord[Y] == req_geometry[Y])
					    && (record->coord[Z] == req_geometry[Z]))
						break;
					else if(!_rotate_geo(req_geometry))
						break;
				}
				
			} else if (record->bp_count == target_size)
				break;
			
		}
		
		/* set the bitmap and do other allocation activities */
		if (record) {
			
			return record;
		}
	}
	
	debug("_find_best_partition_match none found");
	return NULL;
}

/*
 * Try to find resources for a given job request
 * IN job_ptr - pointer to job record in slurmctld
 * IN/OUT bitmap - nodes availble for assignment to job, clear those not to
 *	be used
 * IN min_nodes, max_nodes  - minimum and maximum number of nodes to allocate
 *	to this job (considers slurm partition limits)
 * RET - SLURM_SUCCESS if job runnable now, error code otherwise
 */
extern int submit_job(struct job_record *job_ptr, bitstr_t *slurm_part_bitmap,
		      int min_nodes, int max_nodes)
{
	int spec = 1; /* this will be like, keep TYPE a priority, etc,  */
	bgl_record_t* record;
	char buf[100];

	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
		SELECT_PRINT_MIXED);
	debug("bluegene:submit_job: %s nodes=%d-%d", buf, min_nodes, max_nodes);
	
	if((record = _find_best_partition_match(job_ptr, 
						slurm_part_bitmap, 
						min_nodes, 
						max_nodes, 
						spec)) == NULL) {
		return SLURM_ERROR;
	} else {
		/* now we place the part_id into the env of the script to run */
		
#ifdef HAVE_BGL_FILES
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_PART_ID, record->bgl_part_id);
#else
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_PART_ID, "UNDEFINED");
#endif
		
	}
	
	return SLURM_SUCCESS;
}
