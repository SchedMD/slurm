/*****************************************************************************\
 *  wire_test.c - used to debug and test wires on any given system.  
 *
 *  $Id: block_allocator.c 17495 2009-05-14 16:49:52Z da $
 *****************************************************************************
 *  Copyright (C) 2004 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "block_allocator.h"
#include "src/common/uid.h"
#include "src/common/timers.h"

/** */
int main(int argc, char** argv)
{
	ba_request_t *request = (ba_request_t*) xmalloc(sizeof(ba_request_t)); 
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	int debug_level = 5;

	List results;
//	List results2;
//	int i,j;
	log_opts.stderr_level  = debug_level;
	log_opts.logfile_level = debug_level;
	log_opts.syslog_level  = debug_level;
	
	log_alter(log_opts, LOG_DAEMON, "/dev/null");
	
	DIM_SIZE[X]=0;
	DIM_SIZE[Y]=0;
	DIM_SIZE[Z]=0;

	slurm_conf_reinit(NULL);
	ba_init(NULL, 1);
	init_wires(NULL);
		
	/* [010x831] */
/* 	results = list_create(NULL); */
/* 	request->geometry[0] = 9; */
/* 	request->geometry[1] = 3; */
/* 	request->geometry[2] = 2; */
/* 	request->start[0] = 0; */
/* 	request->start[1] = 1; */
/* 	request->start[2] = 0; */
/* 	request->start_req = 1; */
/* //	request->size = 16; */
/* 	request->rotate = 0; */
/* 	request->elongate = 0; */
/* 	request->conn_type = SELECT_TORUS; */
/* 	new_ba_request(request); */
/* 	print_ba_request(request); */
/* 	if(!allocate_block(request, results)) { */
/*        		debug("couldn't allocate %c%c%c", */
/* 		       alpha_num[request->geometry[0]], */
/* 		       alpha_num[request->geometry[1]], */
/* 		       alpha_num[request->geometry[2]]); */
/* 	} */
/* 	list_destroy(results); */

/* 	/\* [001x801] *\/ */
/* 	results = list_create(NULL); */
/* 	request->geometry[0] = 9; */
/* 	request->geometry[1] = 1; */
/* 	request->geometry[2] = 1; */
/* 	request->start[0] = 0; */
/* 	request->start[1] = 0; */
/* 	request->start[2] = 1; */
/* 	request->start_req = 1; */
/* //	request->size = 1; */
/* 	request->rotate = 0; */
/* 	request->elongate = 0; */
/* 	request->conn_type = SELECT_TORUS; */
/* 	new_ba_request(request); */
/* 	print_ba_request(request); */
/* 	if(!allocate_block(request, results)) { */
/*        		debug("couldn't allocate %c%c%c", */
/* 		       request->geometry[0], */
/* 		       request->geometry[1], */
/* 		       request->geometry[2]); */
/* 	} */
/* 	list_destroy(results); */

	/* [001x801] */
	results = list_create(NULL);
	request->geometry[0] = 7;
	request->geometry[1] = 4;
	request->geometry[2] = 2;
	request->start[0] = 0;
	request->start[1] = 0;
	request->start[2] = 0;
	request->start_req = 0;
//	request->size = 1;
	request->rotate = 1;
	request->elongate = 1;
	request->conn_type = SELECT_TORUS;
	new_ba_request(request);
	print_ba_request(request);
	if(!allocate_block(request, results)) {
       		debug("couldn't allocate %c%c%c",
		       request->geometry[0],
		       request->geometry[1],
		       request->geometry[2]);
	}
	list_destroy(results);

	
	int dim,j;
	int x,y,z;
	int startx=0;
	int starty=0;
	int startz=0;
	int endx=DIM_SIZE[X];
	int endy=1;//DIM_SIZE[Y];
	int endz=1;//DIM_SIZE[Z];

	for(x=startx;x<endx;x++) {
		for(y=starty;y<endy;y++) {
			for(z=startz;z<endz;z++) {
				ba_node_t *curr_node = 
					&(ba_system_ptr->grid[x][y][z]);
				info("Node %c%c%c Used = %d Letter = %c",
				     alpha_num[x],alpha_num[y],alpha_num[z],
				     curr_node->used,
				     curr_node->letter);
				for(dim=0;dim<1;dim++) {
					info("Dim %d",dim);
					ba_switch_t *wire =
						&curr_node->axis_switch[dim];
					for(j=0;j<NUM_PORTS_PER_NODE;j++)
						info("\t%d -> %d -> %c%c%c %d "
						     "Used = %d",
						     j, wire->int_wire[j].
						     port_tar,
						     alpha_num[wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
							       node_tar[X]],
						     alpha_num[wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     node_tar[Y]],
						     alpha_num[wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     node_tar[Z]],
						     wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     port_tar,
						     wire->int_wire[j].used);
				}
			}
		}
	}
	/* list_destroy(results); */

/* 	ba_fini(); */

/* 	delete_ba_request(request); */
	
	return 0;
}
