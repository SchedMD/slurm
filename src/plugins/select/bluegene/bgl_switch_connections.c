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
	int i, bp_num;
	rm_location_t loc;

	rm_get_data(my_bgl, RM_BPNum, &bp_num);
	rm_get_data(my_bgl, RM_FirstBP, bp);

	for (i=0; i<bp_num; i++){
		rm_get_data(*bp, RM_BPLoc, &loc);
		//printf("%d%d%d %d%d%d\n",loc.X,loc.Y,loc.Z,curr_coord[X],curr_coord[Y],curr_coord[Z]); 
		if ((loc.X == curr_coord[X])
		&&  (loc.Y == curr_coord[Y])
		&&  (loc.Z == curr_coord[Z])) {
			return 1;
		}
		rm_get_data(my_bgl, RM_NextBP, bp);
	}

	// error("_get_bp_by_location: could not find specified bp.");
	return 0;
}

/* static int _set_switch(rm_switch_t* curr_switch, pa_connection_t *int_wire) */
/* { */
/* 	int firstconnect=1; */
/* 	rm_connection_t conn; */
/* 	int j; */
/* 	int conn_num=0; */
	
/* 	for(j=0;j<NUM_PORTS_PER_NODE;j+=2) { */
/* 		if(j==2) */
/* 			j++; */
/* 		if(int_wire[j].used) { */
/* 			switch(int_wire[j].port_tar) { */
/* 			case 1: */
/* 				conn.p1 = RM_PORT_S1; */
/* 				break; */
/* 			case 2: */
/* 				conn.p1 = RM_PORT_S2; */
/* 				break; */
/* 			case 4: */
/* 				conn.p1 = RM_PORT_S4; */
/* 				break; */
/* 			} */
/* 			switch(j) { */
/* 			case 0: */
/* 				conn.p2 = RM_PORT_S0;  */
/* 				break; */
/* 			case 3: */
/* 				conn.p2 = RM_PORT_S3;  */
/* 				break; */
/* 			case 5: */
/* 				conn.p2 = RM_PORT_S5;  */
/* 				break; */
/* 			} */
/* 			conn.part_state = RM_PARTITION_READY; */
/* 			//printf("Connecting %d - %d\n",(conn.p1-6),(conn.p2-6)); */
/* 			if(firstconnect) { */
/* 				rm_set_data(curr_switch,RM_SwitchFirstConnection, &conn); */
/* 				firstconnect=0; */
/* 			} else  */
/* 				rm_set_data(curr_switch,RM_SwitchNextConnection, &conn);    */
/* 			conn_num++; */
/* 		}		 */
/* 	} */
/* 	//printf("conn_num = %d\n",conn_num); */
/* 	rm_set_data(curr_switch, RM_SwitchConnNum, &conn_num); */
/* 	return 1; */
/* } */

static int _add_switch_conns(rm_switch_t* curr_switch, bgl_switch_t *bgl_switch)
{
	ListIterator itr;
	bgl_conn_t *bgl_conn;
	
	int firstconnect=1;
	rm_connection_t conn;
	int j;
	int conn_num=0;
	int port;
	
	itr = list_iterator_create(bgl_switch->conn_list);
	while((bgl_conn = list_next(itr)) != NULL) {
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
		//printf("Connecting %d - %d\n",bgl_conn->source,bgl_conn->target);
		//printf("Connecting %d - %d\n",(conn.p1-6),(conn.p2-6));
			
		if(firstconnect) {
			rm_set_data(curr_switch, RM_SwitchFirstConnection, &conn);
			firstconnect=0;
		} else 
			rm_set_data(curr_switch, RM_SwitchNextConnection, &conn);   
		conn_num++;
		
	}
	list_iterator_destroy(itr);
		
	//printf("conn_num = %d\n",conn_num);
	rm_set_data(curr_switch, RM_SwitchConnNum, &conn_num);
	return 1;
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
	//printf("looking at dim %d conn %d -> %d\n", dim, source, port_tar);
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
		//printf("adding to dim %d conn %d -> %d\n\n", dim, source, port_tar);
		list_append(bgl_switch->conn_list, bgl_conn);
	} else {
		//printf("I found a match returning\n\n");
		return 1;	
	}
	if(port_tar==target) {
		//printf("I found the target\n\n");
		
		return 1;
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
	
	//printf("going from %d%d%d port %d -> %d %d%d%d %d\n",node_src[X], node_src[Y], node_src[Z], source, port_tar1, node_tar[X], node_tar[Y], node_tar[Z], port_tar);
	next_switch = &pa_system_ptr->
		grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
	//printf("sending %d -> %d\n",port_tar,next_switch->int_wire[port_tar].port_tar);
	_lookat_path(bgl_bp, next_switch, port_tar, target, dim);	
	
	return 1;
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
	return 1;
}

/**
 * connect the given switch up with the given connections
 */
int configure_partition_switches(bgl_record_t * bgl_record)
{
	int i;
	ListIterator itr, switch_itr, bgl_itr;
	pa_node_t* pa_node;
	char *name2;
	rm_BP_t *curr_bp;
	rm_switch_t *coord_switch[PA_SYSTEM_DIMENSIONS];
	pa_switch_t *pa_switch;
	char *bpid, *curr_bpid;
	int found_bpid = 0;
	int switch_count;
	rm_location_t loc;
	bgl_bp_t *bgl_bp;
	bgl_switch_t *bgl_switch;
	int first_bp=1;
	int first_switch=1;
	
	bgl_bp_list = list_create(NULL);
	bgl_record->switch_count = 0;
	bgl_record->bp_count = 0;
		
	itr = list_iterator_create(bgl_record->bgl_part_list);
	while ((pa_node = (pa_node_t *) list_next(itr)) != NULL) {
		//printf("Looking for %d%d%d\n",pa_node->coord[X],pa_node->coord[Y],pa_node->coord[Z]); 
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
		bgl_record->bp_count++;
		itr = list_iterator_create(bgl_bp->switch_list);
		while((bgl_switch = list_next(itr)) != NULL) {
			bgl_record->switch_count++;
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(bgl_itr);
	rm_set_data(bgl_record->bgl_part,RM_PartitionBPNum,&bgl_record->bp_count);
	rm_set_data(bgl_record->bgl_part,RM_PartitionSwitchNum,&bgl_record->switch_count);
	/* printf("BP_count = %d\n",bgl_record->bp_count); */
/* 	printf("switch_count = %d\n",bgl_record->switch_count); */
	bgl_itr = list_iterator_create(bgl_bp_list);
	
	first_bp = 1;
	first_switch = 1;
	while((bgl_bp = list_next(bgl_itr)) != NULL) {
			
		if (!_get_bp_by_location(bgl, bgl_bp->coord, &curr_bp)) {
			return 0;
		}
		rm_get_data(curr_bp, RM_BPLoc, &loc);
		//printf("found %d%d%d %d%d%d\n",loc.X,loc.Y,loc.Z,pa_node->coord[X],pa_node->coord[Y],pa_node->coord[Z]);
		//if(bgl_bp->used) {
			if (first_bp){
				rm_set_data(bgl_record->bgl_part, RM_PartitionFirstBP, curr_bp);
				first_bp = 0;
			} else {
				rm_set_data(bgl_record->bgl_part, RM_PartitionNextBP, curr_bp);
			}
			//}
		rm_get_data(curr_bp, RM_BPID, &bpid);
		/* printf("bp name = %s\n",(char *)bpid); */
		
		rm_get_data(bgl, RM_SwitchNum, &switch_count);
		rm_get_data(bgl, RM_FirstSwitch,&coord_switch[X]);
		found_bpid = 0;
		for (i=0; i<switch_count; i++) {
			rm_get_data(coord_switch[X], RM_SwitchBPID, &curr_bpid);
			//printf("Bpid = %s, curr_bpid = %s\n",(char *)bpid, (char *)curr_bpid);
			if (!strcasecmp((char *)bpid, (char *)curr_bpid)) {
				found_bpid = 1;
				break;
			}
			
			rm_get_data(bgl,RM_NextSwitch,&coord_switch[X]);
		}
	
		if(found_bpid) {
			rm_get_data(bgl,RM_NextSwitch,&coord_switch[Y]);
			rm_get_data(bgl,RM_NextSwitch,&coord_switch[Z]);
					
			switch_itr = list_iterator_create(bgl_bp->switch_list);
			while((bgl_switch = list_next(switch_itr)) != NULL) {
				rm_get_data(coord_switch[bgl_switch->dim],RM_SwitchID,&name2);
				/* printf("dim %d name %s\n", bgl_switch->dim, name2); */
				
				_add_switch_conns(coord_switch[bgl_switch->dim], bgl_switch);
				
				if (first_switch){
					rm_set_data(bgl_record->bgl_part,
						    RM_PartitionFirstSwitch,
						    coord_switch[bgl_switch->dim]);
					first_switch = 0;
				} else {
					rm_set_data(bgl_record->bgl_part,
						    RM_PartitionNextSwitch,
						    coord_switch[bgl_switch->dim]);
				}
			}
		}
	}
	_destroy_bgl_bp_list(bgl_bp_list);	
	
/* 		rm_switch_t *curr_switch; */
/* 		int j; */
/* 	bgl_record->bp_count = 4; */
/* 		bgl_record->switch_count = 12; */
/* 	rm_set_data(bgl_record->bgl_part,RM_PartitionBPNum,&bgl_record->bp_count); */
/* 	rm_set_data(bgl_record->bgl_part,RM_PartitionSwitchNum,&bgl_record->switch_count); */
/*  	while ((pa_node = (pa_node_t *) list_next(itr)) != NULL) { */
/* 		if (!_get_bp_by_location(bgl, pa_node->coord, &curr_bp)) { */
/* 			return 0; */
/* 		} */
/* 		rm_get_data(curr_bp, RM_BPLoc, &loc); */
/* 		//printf("found %d%d%d %d%d%d\n",loc.X,loc.Y,loc.Z,pa_node->coord[X],pa_node->coord[Y],pa_node->coord[Z]); */
/* 		if (!i){ */
/* 			rm_set_data(bgl_record->bgl_part, RM_PartitionFirstBP, curr_bp); */
/* 		} else { */
/* 			rm_set_data(bgl_record->bgl_part, RM_PartitionNextBP, curr_bp); */
/* 		} */
/* 		rm_get_data(curr_bp,RM_BPID,&bpid); */
/* 		//printf("bp name = %s\n",(char *)bpid); */
		
/* 		rm_get_data(bgl, RM_SwitchNum, &switch_count); */
/* 		rm_get_data(bgl, RM_FirstSwitch,&curr_switch); */
/* 		found_bpid = 0; */
/* 		for (i=0; i<switch_count; i++) { */
/* 			rm_get_data(curr_switch, RM_SwitchBPID, &curr_bpid); */
/* 			//printf("Bpid = %s, curr_bpid = %s\n",(char *)bpid, (char *)curr_bpid); */
/* 			if (!strcasecmp((char *)bpid, (char *)curr_bpid)) { */
/* 				found_bpid = 1; */
/* 				break; */
/* 			} */
			
/* 			rm_get_data(bgl,RM_NextSwitch,&curr_switch); */
/* 		} */
/* 		if(found_bpid) { */
	
/* 			for(j=0;j<PA_SYSTEM_DIMENSIONS;j++) { */
/* 				//rm_get_data(curr_switch,RM_SwitchID,&name2); */
/* 				//printf("dim %d\n",j); */
/* 				if(j!=X) { */
/* 					pa_node->axis_switch[j].int_wire[3].used = 0; */
/* 					pa_node->axis_switch[j].int_wire[4].used = 0; */
/* 				} */
/* 				_set_switch(curr_switch, pa_node->axis_switch[j].int_wire); */
				
/* 				if (!i){ */
/* 					rm_set_data(bgl_record->bgl_part, RM_PartitionFirstSwitch, curr_switch); */
/* 				} else { */
/* 					rm_set_data(bgl_record->bgl_part, RM_PartitionNextSwitch, curr_switch); */
/* 				} */
/* 				if(j!=X) { */
/* 					pa_node->axis_switch[j].int_wire[3].used = 1; */
/* 					pa_node->axis_switch[j].int_wire[4].used = 1; */
/* 				} */
/* 				i++; */
/* 				rm_get_data(bgl,RM_NextSwitch,&curr_switch); */
/* 				//rm_free_switch(curr_switch); */
/* 			} */
/* 		} */
/* 	} */
	//printf("done with switches\n");
	return 1;	
}


#endif
