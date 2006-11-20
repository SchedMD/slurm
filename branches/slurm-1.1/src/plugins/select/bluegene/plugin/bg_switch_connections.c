/*****************************************************************************\
 *  bg_switch_connections.c - Blue Gene switch management functions, 
 *  establish switch connections
 *
 *  $Id$
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

#include "bluegene.h"

#ifdef HAVE_BG_FILES

List bg_bp_list;

static int _get_bp_by_location(rm_BGL_t* my_bg, 
			       int* curr_coord, 
			       rm_BP_t** bp);
//static int _set_switch(rm_switch_t* curr_switch, ba_connection_t *int_wire);
static int _add_switch_conns(rm_switch_t* curr_switch, 
			     bg_switch_t *bg_switch);
static int _lookat_path(bg_bp_t *bg_bp, 
			ba_switch_t *curr_switch, 
			int source, 
			int target, 
			int dim);
static int _destroy_bg_bp_list(List bg_bp);
/** 
 * this is just stupid.  there are some implicit rules for where
 * "NextBP" goes to, but we don't know, so we have to do this.
 */
static int _get_bp_by_location(rm_BGL_t* my_bg, int* curr_coord, rm_BP_t** bp)
{
	int i, bp_num, rc;
	rm_location_t loc;

	if ((rc = bridge_get_data(my_bg, RM_BPNum, &bp_num)) != STATUS_OK) {
		fatal("bridge_get_data: RM_BPNum: %s", bg_err_str(rc));
		return SLURM_ERROR;
	}

	for (i=0; i<bp_num; i++){
		if(i) {
			if ((rc = bridge_get_data(my_bg, RM_NextBP, bp)) 
			    != STATUS_OK) {
				fatal("bridge_get_data: RM_NextBP: %s", 
				      bg_err_str(rc));
				return SLURM_ERROR;
			}	
		} else {
			if ((rc = bridge_get_data(my_bg, RM_FirstBP, bp)) 
			    != STATUS_OK) {
				fatal("bridge_get_data: RM_FirstBP: %s", 
				      bg_err_str(rc));
				return SLURM_ERROR;
			}
		}	
		if ((rc = bridge_get_data(*bp, RM_BPLoc, &loc)) != STATUS_OK) {
			fatal("bridge_get_data: RM_BPLoc: %s", bg_err_str(rc));
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

static int _add_switch_conns(rm_switch_t* curr_switch, 
			     bg_switch_t *bg_switch)
{
	ListIterator itr;
	bg_conn_t *bg_conn;
	
	int firstconnect=1;
	rm_connection_t conn;
	int j, rc;
	int conn_num=0;
	int port = 0;
	
	itr = list_iterator_create(bg_switch->conn_list);
	while((bg_conn = list_next(itr)) != NULL) {
		if(bg_conn->source == bg_conn->target)
			continue;
		
		for(j=0;j<2;j++) {
			switch(j) {
			case 0:
				port = bg_conn->source;
				break;
			case 1:
				port = bg_conn->target;
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
			if ((rc = bridge_set_data(curr_switch, 
						  RM_SwitchFirstConnection, 
						  &conn)) 
			    != STATUS_OK) {
				list_iterator_destroy(itr);
				
				fatal("bridge_set_data"
				      "(RM_SwitchFirstConnection): %s", 
				      bg_err_str(rc));
				return SLURM_ERROR;
			}
			firstconnect=0;
		} else {
			if ((rc = bridge_set_data(curr_switch, 
						  RM_SwitchNextConnection,
						  &conn)) 
			    != STATUS_OK) {
				list_iterator_destroy(itr);
				
				fatal("bridge_set_data"
				      "(RM_SwitchNextConnection): %s",
				      bg_err_str(rc));
				return SLURM_ERROR;
			}
		} 
		
			
		conn_num++;
		debug2("adding %d -> %d",bg_conn->source, bg_conn->target);
	}
	list_iterator_destroy(itr);
	
	if ((rc = bridge_set_data(curr_switch, RM_SwitchConnNum, &conn_num)) 
	    != STATUS_OK) {
		fatal("bridge_set_data: RM_SwitchConnNum: %s", bg_err_str(rc));
		
		return SLURM_ERROR;
	} 
	
				
	return SLURM_SUCCESS;
}

static int _lookat_path(bg_bp_t *bg_bp, ba_switch_t *curr_switch, 
			int source, int target, int dim) 
{
	ListIterator bg_itr, switch_itr, conn_itr;
	bg_switch_t *bg_switch;
	bg_conn_t *bg_conn;
	int *node_tar;
	int port_tar;
	int port_tar1;
	int *node_src;
	ba_switch_t *next_switch; 
	
	switch_itr = list_iterator_create(bg_bp->switch_list);
	while((bg_switch = list_next(switch_itr)) != NULL) {
		if(bg_switch->dim == dim)
			break;
	}
	list_iterator_destroy(switch_itr);
	
	if(bg_switch == NULL) {
		bg_switch = xmalloc(sizeof(bg_switch_t));
		bg_switch->dim=dim;
		bg_switch->conn_list = list_create(NULL);
		list_append(bg_bp->switch_list, bg_switch);
	}
		
	port_tar = curr_switch->int_wire[source].port_tar;
	
	conn_itr = list_iterator_create(bg_switch->conn_list);
	while((bg_conn = list_next(conn_itr)) != NULL) {
		if(port_tar == curr_switch->ext_wire[port_tar].port_tar) {
			//list_delete(conn_itr);
			//continue;
			debug3("I found these %d %d", port_tar, 
			       curr_switch->ext_wire[port_tar].port_tar);
		}
		if(((bg_conn->source == port_tar)
		    && (bg_conn->target == source))
		   || ((bg_conn->source == source)
		       && (bg_conn->target == port_tar)))
			break;
	}
	list_iterator_destroy(conn_itr);
	
	if(bg_conn == NULL) {
		bg_conn = xmalloc(sizeof(bg_conn_t));
		bg_conn->source = source;
		bg_conn->target = port_tar;
		
		list_append(bg_switch->conn_list, bg_conn);
	} else {		
		return SLURM_SUCCESS;	
	}
	if(port_tar==target) {
		return SLURM_SUCCESS;
	}
	/* keep this around to tell us where we are coming from */
	port_tar1 = port_tar;
	/* set port target to to where the external wire is 
	   going to on the next node */
	port_tar = curr_switch->ext_wire[port_tar1].port_tar;
	/* set node target to where the external wire is going to */
	node_tar = curr_switch->ext_wire[port_tar1].node_tar;
	/* set source to the node you are on */
	node_src = curr_switch->ext_wire[0].node_tar;

	debug2("dim %d trying from %d%d%d %d -> %d%d%d %d",
	       dim,
	       node_src[X], 
	       node_src[Y], 
	       node_src[Z],
	       port_tar1,
	       node_tar[X], 
	       node_tar[Y], 
	       node_tar[Z],
	       port_tar);


	bg_itr = list_iterator_create(bg_bp_list);
	while((bg_bp = list_next(bg_itr)) != NULL) {
		if((bg_bp->coord[X] == node_tar[X]) 
		   && (bg_bp->coord[Y] == node_tar[Y]) 
		   && (bg_bp->coord[Z] == node_tar[Z]))
			break;
	}
	list_iterator_destroy(bg_itr);
	/* It appears this is a past through node */
	if(bg_bp == NULL) {
		bg_bp = xmalloc(sizeof(bg_bp_t));
		bg_bp->coord = node_tar;
		bg_bp->switch_list = list_create(NULL);
		list_append(bg_bp_list, bg_bp);
		bg_bp->used = 0;
	}
	
	next_switch = &ba_system_ptr->
		grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
	
	if(_lookat_path(bg_bp, next_switch, port_tar, target, dim) 
	   == SLURM_ERROR)
		return SLURM_ERROR;
	
	return SLURM_SUCCESS;
}

static int _destroy_bg_bp_list(List bg_bp_list)
{
	bg_switch_t *bg_switch;
	bg_conn_t *bg_conn;
	bg_bp_t *bg_bp;
	
	if(bg_bp_list) {
		while((bg_bp = list_pop(bg_bp_list)) != NULL) {
			while((bg_switch = list_pop(bg_bp->switch_list)) 
			      != NULL) {
				while((bg_conn = list_pop(
					       bg_switch->conn_list)) 
				      != NULL) {
					if(bg_conn)
						xfree(bg_conn);
				}
				list_destroy(bg_switch->conn_list);
				if(bg_switch)
					xfree(bg_switch);
			}
			list_destroy(bg_bp->switch_list);
			if(bg_bp)
				xfree(bg_bp);
		}
		list_destroy(bg_bp_list);
	}
	return SLURM_SUCCESS;
}

extern int configure_small_block(bg_record_t *bg_record)
{
	bool small = true;
	ListIterator itr;
	ba_node_t* ba_node = NULL;
	int rc = SLURM_SUCCESS;
	rm_BP_t *curr_bp = NULL;
	rm_bp_id_t bp_id = NULL;
	int num_ncards = 0;
	rm_nodecard_t *ncard;
	rm_nodecard_list_t *ncard_list = NULL;
	rm_quarter_t quarter;
	int num, i;

	if(bg_record->bp_count != 1) {
		error("Requesting small block with %d bps, needs to be 1.",
		      bg_record->bp_count);
		return SLURM_ERROR;
	}
	
	/* set that we are doing a small block */
	
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionSmall, 
				  &small)) != STATUS_OK) {
		
		fatal("bridge_set_data(RM_PartitionPsetsPerBP)", 
		      bg_err_str(rc));
	}

	num_ncards = bg_record->node_cnt/bluegene_nodecard_node_cnt;

	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionNodeCardNum,
				  &num_ncards))
	    != STATUS_OK) {
		
		fatal("bridge_set_data: RM_PartitionBPNum: %s", 
		      bg_err_str(rc));
	}
	
			
	itr = list_iterator_create(bg_record->bg_block_list);
	ba_node = list_next(itr);
	list_iterator_destroy(itr);

	if (_get_bp_by_location(bg, ba_node->coord, &curr_bp) 
	    == SLURM_ERROR) {
		fatal("_get_bp_by_location()");
	}
	
	/* Set the one BP */
	
	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionBPNum,
				  &bg_record->bp_count)) 
	    != STATUS_OK) {
		
		fatal("bridge_set_data: RM_PartitionBPNum: %s", 
		      bg_err_str(rc));
		return SLURM_ERROR;
	}	
	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionFirstBP, 
				  curr_bp)) 
	    != STATUS_OK) {
		
		fatal("bridge_set_data("
		      "BRIDGE_PartitionFirstBP): %s", 
		      bg_err_str(rc));
		return SLURM_ERROR;
	}
	
	
	/* find the bp_id of the bp to get the nodecards */
	if ((rc = bridge_get_data(curr_bp, RM_BPID, &bp_id))
	    != STATUS_OK) {
		error("bridge_get_data(): %d", rc);
		return SLURM_ERROR;
	}

	
	if(!bp_id) {
		error("No BP ID was returned from database");
		return SLURM_ERROR;
	}

	if ((rc = bridge_get_nodecards(bp_id, &ncard_list))
	    != STATUS_OK) {
		error("bridge_get_nodecards(%s): %d",
		      bp_id, rc);
		free(bp_id);
		return SLURM_ERROR;
	}
	free(bp_id);
		
			
	if((rc = bridge_get_data(ncard_list, RM_NodeCardListSize, &num))
	   != STATUS_OK) {
		error("bridge_get_data(RM_NodeCardListSize): %s",
		      bg_err_str(rc));
		return SLURM_ERROR;
	}
	num_ncards = 0;
	for(i=0; i<num; i++) {
		if (i) {
			if ((rc = bridge_get_data(ncard_list, 
						  RM_NodeCardListNext, 
						  &ncard)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_NodeCardListNext): %s",
				      rc);
				rc = SLURM_ERROR;
				goto cleanup;
			}
		} else {
			if ((rc = bridge_get_data(ncard_list, 
						  RM_NodeCardListFirst, 
						  &ncard)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_NodeCardListFirst): %s",
				      rc);
				rc = SLURM_ERROR;
				goto cleanup;
			}
		}
		
		if ((rc = bridge_get_data(ncard, 
					  RM_NodeCardQuarter, 
					  &quarter)) != STATUS_OK) {
			error("bridge_get_data(RM_NodeCardQuarter): %d",rc);
			rc = SLURM_ERROR;
			goto cleanup;
		}
		if(bg_record->quarter != quarter)
			continue;
		if(bg_record->nodecard != (uint16_t) NO_VAL) {
			if(bg_record->nodecard != (i%4))
				continue;
		}

		
		if (num_ncards) {
			if ((rc = bridge_set_data(bg_record->bg_block,
						  RM_PartitionNextNodeCard, 
						  ncard)) 
			    != STATUS_OK) {
				
				fatal("bridge_set_data("
				      "RM_PartitionNextNodeCard): %s", 
				      bg_err_str(rc));
			}
		} else {
			if ((rc = bridge_set_data(bg_record->bg_block,
						  RM_PartitionFirstNodeCard, 
						  ncard)) 
			    != STATUS_OK) {
				
				fatal("bridge_set_data("
				      "RM_PartitionFirstNodeCard): %s", 
				      bg_err_str(rc));
			}
		}
		
		num_ncards++;
		if(num_ncards == 4)
			break;
	}
cleanup:
	if ((rc = bridge_free_nodecard_list(ncard_list)) != STATUS_OK) {
		error("bridge_free_nodecard_list(): %s", bg_err_str(rc));
		return SLURM_ERROR;
	}
	
	debug2("making the small block");
	return rc;
}

/**
 * connect the given switch up with the given connections
 */
extern int configure_block_switches(bg_record_t * bg_record)
{
	int i, rc = SLURM_SUCCESS;
	ListIterator itr, switch_itr, bg_itr;
	ba_node_t* ba_node = NULL;
	rm_BP_t *curr_bp = NULL;
	rm_switch_t *coord_switch[BA_SYSTEM_DIMENSIONS];
	rm_switch_t *curr_switch = NULL;
	ba_switch_t *ba_switch = NULL;
	char *bpid = NULL, *curr_bpid = NULL;
	int found_bpid = 0;
	int switch_count;
	bg_bp_t *bg_bp = NULL;
	bg_switch_t *bg_switch = NULL;
	int first_bp=1;
	int first_switch=1;
	
	if(!bg_record->bg_block_list) {
		error("There was no block_list given, can't create block");
		return SLURM_ERROR;
	}

	bg_bp_list = list_create(NULL);
	bg_record->switch_count = 0;
	bg_record->bp_count = 0;
		
	itr = list_iterator_create(bg_record->bg_block_list);
	while ((ba_node = (ba_node_t *) list_next(itr)) != NULL) {
		if(!ba_node->used) {
			debug3("%d%d%d is a passthrough, "
			       "not including in request",
			       ba_node->coord[X], 
			       ba_node->coord[Y], 
			       ba_node->coord[Z]);
			continue;
		}
		debug2("using node %d%d%d",
		       ba_node->coord[X], 
		       ba_node->coord[Y], 
		       ba_node->coord[Z]);
		bg_itr = list_iterator_create(bg_bp_list);
		while((bg_bp = list_next(bg_itr)) != NULL) {
			if((bg_bp->coord[X] == ba_node->coord[X])
			   && (bg_bp->coord[Y] == ba_node->coord[Y])
			   && (bg_bp->coord[Z] == ba_node->coord[Z]))
				break;
		}
		list_iterator_destroy(bg_itr);
		
		if(bg_bp == NULL) {
			bg_bp = xmalloc(sizeof(bg_bp_t));
			bg_bp->coord = ba_node->coord;
			bg_bp->switch_list = list_create(NULL);
			list_append(bg_bp_list, bg_bp);
		}
		bg_record->bp_count++;
		bg_bp->used = 1;
		for(i=0;i<BA_SYSTEM_DIMENSIONS;i++) {			
			ba_switch = &ba_node->axis_switch[i];
			if(ba_switch->int_wire[0].used) {
				_lookat_path(bg_bp, ba_switch, 0, 1, i);
			}
		}
	}
	list_iterator_destroy(itr);
	
	bg_itr = list_iterator_create(bg_bp_list);
	while((bg_bp = list_next(bg_itr)) != NULL) {
		debug3("node %d%d%d %d",
		       bg_bp->coord[X], 
		       bg_bp->coord[Y], 
		       bg_bp->coord[Z], list_count(bg_bp->switch_list));
		itr = list_iterator_create(bg_bp->switch_list);
		while((bg_switch = list_next(itr)) != NULL) {
			bg_record->switch_count++;
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(bg_itr);

	
	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionBPNum,
				  &bg_record->bp_count)) 
	    != STATUS_OK) {
		fatal("bridge_set_data: RM_PartitionBPNum: %s", 
		      bg_err_str(rc));
		rc = SLURM_ERROR;
		
		goto cleanup;
	}
	debug3("BP count %d",bg_record->bp_count);
	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionSwitchNum,
				  &bg_record->switch_count)) 
	    != STATUS_OK) {
		fatal("bridge_set_data: RM_PartitionSwitchNum: %s", 
		      bg_err_str(rc));
		rc = SLURM_ERROR;
		
		goto cleanup;
	}
	
	debug3("switch count %d",bg_record->switch_count);
		
	first_bp = 1;
	first_switch = 1;
	
	if ((rc = bridge_get_data(bg, RM_SwitchNum, &switch_count)) 
	    != STATUS_OK) {
		fatal("bridge_get_data: RM_SwitchNum: %s", bg_err_str(rc));
		rc = SLURM_ERROR;
		goto cleanup;
	}
	
	bg_itr = list_iterator_create(bg_bp_list);
	while((bg_bp = list_next(bg_itr)) != NULL) {			
		if (_get_bp_by_location(bg, bg_bp->coord, &curr_bp) 
		    == SLURM_ERROR) {
			list_iterator_destroy(bg_itr);
			rc = SLURM_ERROR;
			goto cleanup;
		}
		
		if(bg_bp->used) {
			
			if (first_bp){
				if ((rc = bridge_set_data(bg_record->bg_block,
							  RM_PartitionFirstBP, 
							  curr_bp)) 
				    != STATUS_OK) {
					list_iterator_destroy(bg_itr);
					
					fatal("bridge_set_data("
					      "RM_PartitionFirstBP): %s", 
					      bg_err_str(rc));
				}
				first_bp = 0;
			} else {
				if ((rc = bridge_set_data(bg_record->bg_block,
							  RM_PartitionNextBP, 
							  curr_bp)) 
				    != STATUS_OK) {
					list_iterator_destroy(bg_itr);
					
					fatal("bridge_set_data"
					      "(RM_PartitionNextBP): %s", 
					      bg_err_str(rc));
				}
			}
			
		}

		if ((rc = bridge_get_data(curr_bp,  RM_BPID, &bpid)) 
		    != STATUS_OK) {
			list_iterator_destroy(bg_itr);
			fatal("bridge_get_data: RM_BPID: %s", bg_err_str(rc));
		}		

		if(!bpid) {
			error("No BP ID was returned from database");
			continue;
		}

		found_bpid = 0;
		for (i=0; i<switch_count; i++) {
			if(i) {
				if ((rc = bridge_get_data(bg, RM_NextSwitch, 
							  &curr_switch)) 
				    != STATUS_OK) {
					list_iterator_destroy(bg_itr);
					fatal("bridge_get_data"
					      "(RM_NextSwitch): %s",
					      bg_err_str(rc));
				}
			} else {
				if ((rc = bridge_get_data(bg, RM_FirstSwitch, 
							  &curr_switch)) 
				    != STATUS_OK) {
					list_iterator_destroy(bg_itr);
					fatal("bridge_get_data"
					      "(RM_FirstSwitch): %s",
					      bg_err_str(rc));
				}
			}
			if ((rc = bridge_get_data(curr_switch, RM_SwitchBPID, 
						  &curr_bpid)) != STATUS_OK) {
				list_iterator_destroy(bg_itr);
				fatal("bridge_get_data: RM_SwitchBPID: %s", 
				      bg_err_str(rc));
			}

			if(!curr_bpid) {
				error("No BP ID was returned from database");
				continue;
			}

			if (!strcasecmp((char *)bpid, (char *)curr_bpid)) {
				coord_switch[found_bpid] = curr_switch;
				found_bpid++;
				if(found_bpid==BA_SYSTEM_DIMENSIONS) {
					free(curr_bpid);
					break;
				}
			}
			free(curr_bpid);
		}

		free(bpid);

		if(found_bpid==BA_SYSTEM_DIMENSIONS) {
						
			debug2("adding bp %d%d%d",
			       bg_bp->coord[X],
			       bg_bp->coord[Y],
			       bg_bp->coord[Z]);
			switch_itr = list_iterator_create(bg_bp->switch_list);
			while((bg_switch = list_next(switch_itr)) != NULL) {
				
				debug2("adding switch dim %d",
				       bg_switch->dim);
				     
				if (_add_switch_conns(coord_switch
						      [bg_switch->dim],
						      bg_switch) 
				    == SLURM_ERROR) {
					list_iterator_destroy(switch_itr);
					list_iterator_destroy(bg_itr);
					rc = SLURM_ERROR;
					goto cleanup;
				}
				
				
				if (first_switch){
					if ((rc = bridge_set_data(
						     bg_record->bg_block,
						     RM_PartitionFirstSwitch,
						     coord_switch
						     [bg_switch->dim])) 
					    != STATUS_OK) {
						list_iterator_destroy(
							switch_itr);
						list_iterator_destroy(bg_itr);
						fatal("bridge_set_data("
						      "RM_PartitionFirst"
						      "Switch): %s", 
						      bg_err_str(rc));
					}
					
					first_switch = 0;
				} else {
					if ((rc = bridge_set_data(
						     bg_record->bg_block,
						     RM_PartitionNextSwitch,
						     coord_switch
						     [bg_switch->dim])) 
					    != STATUS_OK) {
						list_iterator_destroy(
							switch_itr);
						list_iterator_destroy(bg_itr);
						fatal("bridge_set_data("
						      "RM_PartitionNext"
						      "Switch): %s", 
						      bg_err_str(rc));
					}
				}
			}
			list_iterator_destroy(switch_itr);
		}
	}
	rc = SLURM_SUCCESS;
cleanup:
	if (_destroy_bg_bp_list(bg_bp_list) == SLURM_ERROR)
		return SLURM_ERROR;	
	
	return rc;	
}

#endif
