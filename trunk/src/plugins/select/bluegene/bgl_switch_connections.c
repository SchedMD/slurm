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

static int _get_bp_by_location(rm_BGL_t* my_bgl, int* cur_coord, rm_BP_t** bp);
static int _set_switch(rm_switch_t* cur_switch, pa_connection_t *int_wire);

/** 
 * this is just stupid.  there are some implicit rules for where
 * "NextBP" goes to, but we don't know, so we have to do this.
 */
static int _get_bp_by_location(rm_BGL_t* my_bgl, int* cur_coord, rm_BP_t** bp)
{
	int i, bp_num;
	rm_location_t loc;

	rm_get_data(my_bgl, RM_BPNum, &bp_num);
	rm_get_data(my_bgl, RM_FirstBP, bp);

	for (i=0; i<bp_num; i++){
		rm_get_data(*bp, RM_BPLoc, &loc);
		//printf("%d%d%d %d%d%d\n",loc.X,loc.Y,loc.Z,cur_coord[X],cur_coord[Y],cur_coord[Z]); 
		if ((loc.X == cur_coord[X])
		&&  (loc.Y == cur_coord[Y])
		&&  (loc.Z == cur_coord[Z])) {
			return 1;
		}
		rm_get_data(my_bgl, RM_NextBP, bp);
	}

	// error("_get_bp_by_location: could not find specified bp.");
	return 0;
}

static int _set_switch(rm_switch_t* cur_switch, pa_connection_t *int_wire)
{
	int firstconnect=1;
	rm_connection_t conn;
	int j;
	int conn_num=0;
	
	for(j=0;j<NUM_PORTS_PER_NODE;j+=2) {
		if(j==2)
			j++;
		if(int_wire[j].used) {
			switch(int_wire[j].port_tar) {
			case 1:
				conn.p1 = RM_PORT_S1;
				break;
			case 2:
				conn.p1 = RM_PORT_S2;
				break;
			case 4:
				conn.p1 = RM_PORT_S4;
				break;
			}
			switch(j) {
			case 0:
				conn.p2 = RM_PORT_S0; 
				break;
			case 3:
				conn.p2 = RM_PORT_S3; 
				break;
			case 5:
				conn.p2 = RM_PORT_S5; 
				break;
			}
			conn.part_state = RM_PARTITION_READY;
			//printf("Connecting %d - %d\n",j,int_wire[j].port_tar);
			if(firstconnect) {
				rm_set_data(cur_switch,RM_SwitchFirstConnection, &conn);
				firstconnect=0;
			} else 
				rm_set_data(cur_switch,RM_SwitchNextConnection, &conn);   
			conn_num++;
		}		
	}
	//printf("conn_num = %d\n",conn_num);
	rm_set_data(cur_switch, RM_SwitchConnNum, &conn_num);
	return 1;
}

/**
 * connect the given switch up with the given connections
 */
int configure_partition_switches(bgl_conf_record_t * bgl_conf_record)
{
	int  i, j;
	ListIterator itr;
	pa_node_t* pa_node;
	rm_BP_t *cur_bp;
	rm_switch_t *cur_switch;
	//char *name2;
	char *bpid, *cur_bpid;
	int switchnum = bgl_conf_record->size*3;
	int found_bpid = 0;
	int switch_count;
	rm_location_t loc;
	rm_set_data(bgl_conf_record->bgl_part,RM_PartitionSwitchNum,&switchnum); 
	rm_set_data(bgl_conf_record->bgl_part,RM_PartitionBPNum,&bgl_conf_record->size); 
		
	itr = list_iterator_create(bgl_conf_record->bgl_part_list);
	i=0;
	while ((pa_node = (pa_node_t *) list_next(itr)) != NULL) {
		//printf("Looking for %d%d%d\n",pa_node->coord[X],pa_node->coord[Y],pa_node->coord[Z]); 
		
		if (!_get_bp_by_location(bgl, pa_node->coord, &cur_bp)) {
			return 0;
		}
		rm_get_data(cur_bp, RM_BPLoc, &loc);
		//printf("found %d%d%d %d%d%d\n",loc.X,loc.Y,loc.Z,pa_node->coord[X],pa_node->coord[Y],pa_node->coord[Z]); 
		if (!i){
			rm_set_data(bgl_conf_record->bgl_part, RM_PartitionFirstBP, cur_bp);
		} else {
			rm_set_data(bgl_conf_record->bgl_part, RM_PartitionNextBP, cur_bp);
		}
		rm_get_data(cur_bp,RM_BPID,&bpid);
		//printf("bp name = %s\n",(char *)bpid);
		
		rm_get_data(bgl, RM_SwitchNum, &switch_count);
		rm_get_data(bgl, RM_FirstSwitch,&cur_switch);
		found_bpid = 0;
		for (i=0; i<switch_count; i++) {
			rm_get_data(cur_switch, RM_SwitchBPID, &cur_bpid);
			//printf("Bpid = %s, cur_bpid = %s\n",(char *)bpid, (char *)cur_bpid);
			if (!strcasecmp((char *)bpid, (char *)cur_bpid)) {
				found_bpid = 1;
				break;
			}
			
			rm_get_data(bgl,RM_NextSwitch,&cur_switch);
		}
		if(found_bpid) {
	
			for(j=0;j<PA_SYSTEM_DIMENSIONS;j++) {
				//rm_get_data(cur_switch,RM_SwitchID,&name2);
				//printf("dim %d\n",j);
				if(j!=X) {
					pa_node->axis_switch[j].int_wire[3].used = 0;
					pa_node->axis_switch[j].int_wire[4].used = 0;
				}
				_set_switch(cur_switch, pa_node->axis_switch[j].int_wire);		
				
				if (!i){
					rm_set_data(bgl_conf_record->bgl_part, RM_PartitionFirstSwitch, cur_switch);
				} else {
					rm_set_data(bgl_conf_record->bgl_part, RM_PartitionNextSwitch, cur_switch);
				}
				if(j!=X) {
					pa_node->axis_switch[j].int_wire[3].used = 1;
					pa_node->axis_switch[j].int_wire[4].used = 1;
				}
				i++;
				rm_get_data(bgl,RM_NextSwitch,&cur_switch);
				//rm_free_switch(cur_switch);
			}
		}
	}
	
	//printf("done with switches\n");
	return 1;	
}


#endif
