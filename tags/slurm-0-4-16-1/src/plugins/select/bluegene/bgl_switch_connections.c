/*****************************************************************************\
 *  bgl_switch_connections.c - Blue Gene switch management functions, 
 *  establish switch connections
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Danny Auble <da@llnl.gov>
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

#include "bluegene.h"

#ifdef HAVE_BGL_FILES

List bgl_bp_list;

static int _get_bp_by_location(rm_BGL_t* my_bgl, int* curr_coord, rm_BP_t** bp);
//static int _set_switch(rm_switch_t* curr_switch, pa_connection_t *int_wire);
static int _add_switch_conns(rm_switch_t* curr_switch, bgl_switch_t *bgl_switch);
static int _lookat_path(bgl_bp_t *bgl_bp, pa_switch_t *curr_switch, int source, int target, int dim);
static int _destroy_bgl_bp_list(List bgl_bp);
/** 
 * this is just stupid.  there are some implicit rules for where
 * "NextBP" goes to, but we don't know, so we have to do this.
 */
static int _get_bp_by_location(rm_BGL_t* my_bgl, int* curr_coord, rm_BP_t** bp)
{
	int i, bp_num, rc;
	rm_location_t loc;

	if ((rc = rm_get_data(my_bgl, RM_BPNum, &bp_num)) != STATUS_OK) {
		fatal("rm_get_data: RM_BPNum: %s", bgl_err_str(rc));
		return SLURM_ERROR;
	}

	
	
	for (i=0; i<bp_num; i++){
		if(i) {
			if ((rc = rm_get_data(my_bgl, RM_NextBP, bp)) 
			    != STATUS_OK) {
				fatal("rm_get_data: RM_NextBP: %s", 
				      bgl_err_str(rc));
				return SLURM_ERROR;
			}	
		} else {
			if ((rc = rm_get_data(my_bgl, RM_FirstBP, bp)) 
			    != STATUS_OK) {
				fatal("rm_get_data: RM_FirstBP: %s", 
				      bgl_err_str(rc));
				return SLURM_ERROR;
			}
		}	
		if ((rc = rm_get_data(*bp, RM_BPLoc, &loc)) != STATUS_OK) {
			fatal("rm_get_data: RM_BPLoc: %s", bgl_err_str(rc));
			return SLURM_ERROR;
		}

		if ((loc.X == curr_coord[X])
		&&  (loc.Y == curr_coord[Y])
		&&  (loc.Z == curr_coord[Z])) {
			return SLURM_SUCCESS;
		}
	}

	// error("_get_bp_by_location: could not find specified bp.");
	return SLURM_ERROR;
}

static int _add_switch_conns(rm_switch_t* curr_switch, bgl_switch_t *bgl_switch)
{
	ListIterator itr;
	bgl_conn_t *bgl_conn;
	
	int firstconnect=1;
	rm_connection_t conn;
	int j, rc;
	int conn_num=0;
	int port = 0;
	
	itr = list_iterator_create(bgl_switch->conn_list);
	while((bgl_conn = list_next(itr)) != NULL) {
		if(bgl_conn->source == bgl_conn->target)
			continue;
		for(j=0;j<2;j++) {
			switch(j) {
			case 0:
				port = bgl_conn->source;
				break;
			case 1:
				port = bgl_conn->target;
				break;
			}
			switch(port) {
			case 0:
				conn.p2 = RM_PORT_S0; 
				break;
			case 1:
				conn.p1 = RM_PORT_S1;
				break;
			case 2:
				conn.p1 = RM_PORT_S2;
				break;
			case 3:
				conn.p2 = RM_PORT_S3; 
				break;
			case 4:
				conn.p1 = RM_PORT_S4;
				break;
			case 5:
				conn.p2 = RM_PORT_S5; 
				break;
			}
		}
		conn.part_state = RM_PARTITION_READY;
		
		if(firstconnect) {
			if ((rc = rm_set_data(curr_switch, RM_SwitchFirstConnection, &conn)) != STATUS_OK) {
				fatal("rm_set_data: RM_SwitchFirstConnection: %s", bgl_err_str(rc));
				return SLURM_ERROR;
			}
			firstconnect=0;
		} else {
			if ((rc = rm_set_data(curr_switch, RM_SwitchNextConnection, &conn)) != STATUS_OK) {
				fatal("rm_set_data: RM_SwitchNextConnection: %s", bgl_err_str(rc));
				return SLURM_ERROR;
			}
		} 
		conn_num++;
	}
	list_iterator_destroy(itr);
	if ((rc = rm_set_data(curr_switch, RM_SwitchConnNum, &conn_num)) != STATUS_OK) {
		fatal("rm_set_data: RM_SwitchConnNum: %s", bgl_err_str(rc));
		return SLURM_ERROR;
	} 	
	return SLURM_SUCCESS;
}

static int _lookat_path(bgl_bp_t *bgl_bp, pa_switch_t *curr_switch, int source, int target, int dim) 
{
	ListIterator bgl_itr, switch_itr, conn_itr;
	bgl_switch_t *bgl_switch;
	bgl_conn_t *bgl_conn;
	int *node_tar;
	int port_tar;
	int port_tar1;
	int *node_src;
	pa_switch_t *next_switch; 
	
	switch_itr = list_iterator_create(bgl_bp->switch_list);
	while((bgl_switch = list_next(switch_itr)) != NULL) {
		if(bgl_switch->dim == dim)
			break;
	}
	list_iterator_destroy(switch_itr);
	
	if(bgl_switch == NULL) {
		bgl_switch = xmalloc(sizeof(bgl_switch_t));
		bgl_switch->dim=dim;
		bgl_switch->conn_list = list_create(NULL);
		list_append(bgl_bp->switch_list, bgl_switch);
	}
		
	port_tar = curr_switch->int_wire[source].port_tar;
	
	conn_itr = list_iterator_create(bgl_switch->conn_list);

	while((bgl_conn = list_next(conn_itr)) != NULL) {
		if(((bgl_conn->source == port_tar)
		    && (bgl_conn->target == source))
		   || ((bgl_conn->source == source)
		   && (bgl_conn->target == port_tar)))
			break;
	}
	list_iterator_destroy(conn_itr);
	
	if(bgl_conn == NULL) {
		bgl_conn = xmalloc(sizeof(bgl_conn_t));
		bgl_conn->source = source;
		bgl_conn->target = port_tar;
		
		list_append(bgl_switch->conn_list, bgl_conn);
	} else {		
		return SLURM_SUCCESS;	
	}
	if(port_tar==target) {
		return SLURM_SUCCESS;
	}
	port_tar1 = port_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;
	node_tar = curr_switch->ext_wire[port_tar].node_tar;
	node_src = curr_switch->ext_wire[0].node_tar;

	bgl_itr = list_iterator_create(bgl_bp_list);
	while((bgl_bp = list_next(bgl_itr)) != NULL) {
		if((bgl_bp->coord[X] == node_tar[X]) 
		   && (bgl_bp->coord[Y] == node_tar[Y]) 
		   && (bgl_bp->coord[Z] == node_tar[Z]))
			break;
	}
	list_iterator_destroy(bgl_itr);
	if(bgl_bp == NULL) {
		bgl_bp = xmalloc(sizeof(bgl_bp_t));
		bgl_bp->coord = node_tar;
		bgl_bp->switch_list = list_create(NULL);
		list_append(bgl_bp_list, bgl_bp);
		bgl_bp->used = 0;
	}
	
	next_switch = &pa_system_ptr->
		grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
	
	if(_lookat_path(bgl_bp, next_switch, port_tar, target, dim) == SLURM_ERROR)
		return SLURM_ERROR;
	
	return SLURM_SUCCESS;
}

static int _destroy_bgl_bp_list(List bgl_bp_list)
{
	bgl_switch_t *bgl_switch;
	bgl_conn_t *bgl_conn;
	bgl_bp_t *bgl_bp;
	
	if(bgl_bp_list) {
		while((bgl_bp = list_pop(bgl_bp_list)) != NULL) {
			while((bgl_switch = list_pop(bgl_bp->switch_list)) != NULL) {
				while((bgl_conn = list_pop(bgl_switch->conn_list)) != NULL) {
					if(bgl_conn)
						xfree(bgl_conn);
				}
				list_destroy(bgl_switch->conn_list);
				if(bgl_switch)
					xfree(bgl_switch);
			}
			list_destroy(bgl_bp->switch_list);
			if(bgl_bp)
				xfree(bgl_bp);
		}
		list_destroy(bgl_bp_list);
	}
	return SLURM_SUCCESS;
}

/**
 * connect the given switch up with the given connections
 */
extern int configure_partition_switches(bgl_record_t * bgl_record)
{
	int i, rc;
	ListIterator itr, switch_itr, bgl_itr;
	pa_node_t* pa_node;
	char *name2;
	rm_BP_t *curr_bp;
	rm_switch_t *coord_switch[PA_SYSTEM_DIMENSIONS];
	rm_switch_t *curr_switch;
	pa_switch_t *pa_switch;
	char *bpid, *curr_bpid;
	int found_bpid = 0;
	int switch_count;
	bgl_bp_t *bgl_bp;
	bgl_switch_t *bgl_switch;
	int first_bp=1;
	int first_switch=1;
	
	bgl_bp_list = list_create(NULL);
	bgl_record->switch_count = 0;
	bgl_record->bp_count = 0;
		
	itr = list_iterator_create(bgl_record->bgl_part_list);
	while ((pa_node = (pa_node_t *) list_next(itr)) != NULL) {
		
		bgl_itr = list_iterator_create(bgl_bp_list);
		while((bgl_bp = list_next(bgl_itr)) != NULL) {
			if((bgl_bp->coord[X] == pa_node->coord[X])
			   && (bgl_bp->coord[Y] == pa_node->coord[Y])
			   && (bgl_bp->coord[Z] == pa_node->coord[Z]))
				break;
		}
		list_iterator_destroy(bgl_itr);

		if(bgl_bp == NULL) {
			bgl_bp = xmalloc(sizeof(bgl_bp_t));
			bgl_bp->coord = pa_node->coord;
			bgl_bp->switch_list = list_create(NULL);
			list_append(bgl_bp_list, bgl_bp);
		}
		bgl_record->bp_count++;
		bgl_bp->used = 1;
		for(i=0;i<PA_SYSTEM_DIMENSIONS;i++) {
			
			pa_switch = &pa_node->axis_switch[i];
			if(pa_switch->int_wire[0].used) {
				_lookat_path(bgl_bp, pa_switch, 0, 1, i);
			}
			
			if(pa_switch->int_wire[1].used) {
				_lookat_path(bgl_bp, pa_switch, 1, 0, i);
			}
		}
	}
	list_iterator_destroy(itr);
	
	bgl_itr = list_iterator_create(bgl_bp_list);
	while((bgl_bp = list_next(bgl_itr)) != NULL) {
		itr = list_iterator_create(bgl_bp->switch_list);
		while((bgl_switch = list_next(itr)) != NULL) {
			bgl_record->switch_count++;
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(bgl_itr);

	if ((rc = rm_set_data(bgl_record->bgl_part,
			      RM_PartitionBPNum,
			      &bgl_record->bp_count)) 
	    != STATUS_OK) {
		fatal("rm_set_data: RM_PartitionBPNum: %s", bgl_err_str(rc));
		return SLURM_ERROR;
	}
	
	if ((rc = rm_set_data(bgl_record->bgl_part,
			      RM_PartitionSwitchNum,
			      &bgl_record->switch_count)) 
	    != STATUS_OK) {
		fatal("rm_set_data: RM_PartitionSwitchNum: %s", bgl_err_str(rc));
		return SLURM_ERROR;
	}
		
	first_bp = 1;
	first_switch = 1;
	
	if ((rc = rm_get_data(bgl, RM_SwitchNum, &switch_count)) != STATUS_OK) {
		fatal("rm_get_data: RM_SwitchNum: %s", bgl_err_str(rc));
		return SLURM_ERROR;
	}
	
	bgl_itr = list_iterator_create(bgl_bp_list);
	while((bgl_bp = list_next(bgl_itr)) != NULL) {
			
		if (_get_bp_by_location(bgl, bgl_bp->coord, &curr_bp) == SLURM_ERROR) {
			return SLURM_ERROR;
		}
		
		if(bgl_bp->used) {
			if (first_bp){
				if ((rc = rm_set_data(bgl_record->bgl_part,
						      RM_PartitionFirstBP, 
						      curr_bp)) 
				    != STATUS_OK) {
					fatal("rm_set_data: RM_PartitionFirstBP: %s", bgl_err_str(rc));
					return SLURM_ERROR;
				}
				first_bp = 0;
			} else {
				if ((rc = rm_set_data(bgl_record->bgl_part,
						      RM_PartitionNextBP, 
						      curr_bp)) 
				    != STATUS_OK) {
					fatal("rm_set_data: RM_PartitionNextBP: %s", bgl_err_str(rc));
					return SLURM_ERROR;
				}
			}
		}

		if ((rc = rm_get_data(curr_bp,  RM_BPID, &bpid)) != STATUS_OK) {
			fatal("rm_get_data: RM_BPID: %s", bgl_err_str(rc));
			return SLURM_ERROR;
		}		
		
		found_bpid = 0;
		for (i=0; i<switch_count; i++) {
			if(i) {
				if ((rc = rm_get_data(bgl, RM_NextSwitch, &curr_switch)) != STATUS_OK) {
					fatal("rm_get_data: RM_NextSwitch: %s", bgl_err_str(rc));
					return SLURM_ERROR;
				}
			} else {
				if ((rc = rm_get_data(bgl, RM_FirstSwitch, &curr_switch)) != STATUS_OK) {
					fatal("rm_get_data: RM_FirstSwitch: %s", bgl_err_str(rc));
					return SLURM_ERROR;
				}
			}
			if ((rc = rm_get_data(curr_switch, RM_SwitchBPID, &curr_bpid)) != STATUS_OK) {
				fatal("rm_get_data: RM_SwitchBPID: %s", bgl_err_str(rc));
				return SLURM_ERROR;
			}
			
			if (!strcasecmp((char *)bpid, (char *)curr_bpid)) {
				coord_switch[found_bpid] = curr_switch;
				found_bpid++;
				if(found_bpid==PA_SYSTEM_DIMENSIONS)
					break;
			}			
		}
	
		if(found_bpid==PA_SYSTEM_DIMENSIONS) {
						
			switch_itr = list_iterator_create(bgl_bp->switch_list);
			while((bgl_switch = list_next(switch_itr)) != NULL) {
				
				if ((rc = rm_get_data(coord_switch[bgl_switch->dim],
						      RM_SwitchID,&name2)) 
				    != STATUS_OK) {
					fatal("rm_get_data: RM_SwitchID: %s", bgl_err_str(rc));
					return SLURM_ERROR;
				}
								
				if (_add_switch_conns(coord_switch[bgl_switch->dim],
						      bgl_switch) == SLURM_ERROR)
					return SLURM_ERROR;
				
				if (first_switch){
					if ((rc = rm_set_data(bgl_record->bgl_part,
							      RM_PartitionFirstSwitch,
							      coord_switch[bgl_switch->dim])) 
					    != STATUS_OK) {
						fatal("rm_set_data: RM_PartitionFirstSwitch: %s", bgl_err_str(rc));
						return SLURM_ERROR;
					}
					
					first_switch = 0;
				} else {
					if ((rc = rm_set_data(bgl_record->bgl_part,
							      RM_PartitionNextSwitch,
							      coord_switch[bgl_switch->dim])) 
					    != STATUS_OK) {
						fatal("rm_set_data: RM_PartitionNextSwitch: %s", bgl_err_str(rc));
						return SLURM_ERROR;
					}
				}
			}
		}
	}
	if (_destroy_bgl_bp_list(bgl_bp_list) == SLURM_ERROR)
		return SLURM_ERROR;	
	
	return SLURM_SUCCESS;	
}


#endif
