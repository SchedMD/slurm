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
 *  For details, see <http://slurm.schedmd.com/>.
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
#include "../ba_common.h"
#include "block_allocator.h"
#include "src/common/uid.h"
#include "src/common/timers.h"

/* These are here to avoid linking issues with the bridge for
 * unresolved symbols.
 */
time_t last_job_update;
time_t last_bg_update;
bg_config_t *bg_conf;
bg_lists_t *bg_lists;
pthread_mutex_t block_state_mutex = PTHREAD_MUTEX_INITIALIZER;
int bg_recover = 1;
int num_unused_cpus = 0;

extern int bridge_init(char *properties_file)
{
	return SLURM_ERROR;
}

extern int bridge_fini()
{
	return SLURM_ERROR;
}

extern int bridge_get_size(int *size)
{
	return SLURM_ERROR;
}

extern int bridge_setup_system()
{
	return SLURM_ERROR;
}

extern struct job_record *find_job_in_bg_record(bg_record_t *bg_record,
						uint32_t job_id)
{
	return NULL;
}

extern int bridge_check_nodeboards(char *mp_loc)
{
	return 0;
}

/** */
int main(int argc, char** argv)
{
	select_ba_request_t *request = xmalloc(sizeof(select_ba_request_t));
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	int debug_level = 5;
	uint16_t ba_debug_flags = 0;

	List results;
//	List results2;
//	int i,j;
	log_opts.stderr_level  = (log_level_t)debug_level;
	log_opts.logfile_level = (log_level_t)debug_level;
	log_opts.syslog_level  = (log_level_t)debug_level;

	ba_debug_flags |= DEBUG_FLAG_BG_ALGO;
	ba_debug_flags |= DEBUG_FLAG_BG_ALGO_DEEP;
	log_alter(log_opts, (log_facility_t)LOG_DAEMON, "/dev/null");

	DIM_SIZE[A]=0;
	DIM_SIZE[X]=0;
	DIM_SIZE[Y]=0;
	DIM_SIZE[Z]=0;

	slurm_conf_reinit(NULL);
	ba_init(NULL, 1);
	set_ba_debug_flags(ba_debug_flags);

	/* [010x831] */
	results = list_create(NULL);
	request->geometry[0] = 1;
	request->geometry[1] = 1;
	request->geometry[2] = 1;
	request->geometry[3] = 1;
	request->start[0] = 0;
	request->start[1] = 1;
	request->start[2] = 0;
	request->start[3] = 0;
	request->start_req = 1;
//	request->size = 16;
	request->rotate = 0;
	request->elongate = 0;
	request->conn_type[A] = SELECT_TORUS;
	request->conn_type[X] = SELECT_TORUS;
	request->conn_type[Y] = SELECT_TORUS;
	request->conn_type[Z] = SELECT_TORUS;
	new_ba_request(request);
	print_ba_request(request);
	if (!allocate_block(request, results)) {
       		debug("couldn't allocate %c%c%c",
		       alpha_num[request->geometry[0]],
		       alpha_num[request->geometry[1]],
		       alpha_num[request->geometry[2]]);
	} else
		info("got back mps %s\n", request->save_name);

	list_destroy(results);

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
/* 	if (!allocate_block(request, results)) { */
/*        		debug("couldn't allocate %c%c%c", */
/* 		       request->geometry[0], */
/* 		       request->geometry[1], */
/* 		       request->geometry[2]); */
/* 	} */
/* 	list_destroy(results); */

	/* [001x801] */
	results = list_create(NULL);
	request->geometry[0] = 1;
	request->geometry[1] = 2;
	request->geometry[2] = 4;
	request->geometry[3] = 1;
	request->start[0] = 0;
	request->start[1] = 0;
	request->start[2] = 0;
	request->start[3] = 0;
	request->start_req = 0;
//	request->size = 1;
	request->rotate = 1;
	request->elongate = 1;
	request->conn_type[A] = SELECT_TORUS;
	request->conn_type[X] = SELECT_TORUS;
	request->conn_type[Y] = SELECT_TORUS;
	request->conn_type[Z] = SELECT_TORUS;
	new_ba_request(request);
	print_ba_request(request);
	if (!allocate_block(request, results)) {
       		debug("couldn't allocate %c%c%c%c",
		       request->geometry[0],
		       request->geometry[1],
		       request->geometry[2],
		       request->geometry[3]);
	} else
		info("got back mps %s\n", request->save_name);
	list_destroy(results);

	int dim;
	int a,b,c,d;
	int starta=0;
	int startb=0;
	int startc=0;
	int startd=0;
	int enda=1;//DIM_SIZE[A];
	int endb=DIM_SIZE[X];
	int endc=DIM_SIZE[Y];
	int endd=1;//DIM_SIZE[Z];

	for(a=starta;a<enda;a++) {
		for(b=startb;b<endb;b++) {
			for(c=startc;c<endc;c++) {
				for(d=startd;d<endd;d++) {
					ba_mp_t *curr_mp =
						&ba_main_grid[a][b][c][d];
					info("Node %c%c%c%c Used = %d",
					     alpha_num[a],alpha_num[b],
					     alpha_num[c],alpha_num[d],
					     curr_mp->used);
					for(dim=0; dim<4; dim++) {
						info("\tDim %d usage is %s ",
						     dim,
						     ba_switch_usage_str(
							     curr_mp->
							     axis_switch[dim].
							     usage));
					}
				}
			}
		}
	}
	/* list_destroy(results); */

/* 	ba_fini(); */

/* 	delete_ba_request(request); */

	return 0;
}
