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

/* These are here to avoid linking issues with the bridge for
 * unresolved symbols.
 */
time_t last_job_update;

extern void bg_requeue_job(uint32_t job_id, bool wait_for_start)
{
	return;
}

extern int update_block_user(bg_record_t *bg_block_id, int set)
{
	return SLURM_SUCCESS;
}

extern int set_block_user(bg_record_t *bg_record)
{
	return SLURM_SUCCESS;
}

extern void requeue_and_error(bg_record_t *bg_record, char *reason)
{
	return;
}

extern void trigger_block_error(void)
{

}

extern void destroy_bg_record(bg_record_t *bg_record)
{

}

/** */
int main(int argc, char** argv)
{
	ba_request_t *request = (ba_request_t*) xmalloc(sizeof(ba_request_t));
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	int debug_level = 5;

	List results;
//	List results2;
//	int i,j;
	log_opts.stderr_level  = (log_level_t)debug_level;
	log_opts.logfile_level = (log_level_t)debug_level;
	log_opts.syslog_level  = (log_level_t)debug_level;
	set_ba_debug_flags(DEBUG_FLAG_BG_ALGO);
	log_alter(log_opts, (log_facility_t)LOG_DAEMON, "/dev/null");

	DIM_SIZE[A]=0;
	DIM_SIZE[X]=0;
	DIM_SIZE[Y]=0;
	DIM_SIZE[Z]=0;

	slurm_conf_reinit(NULL);
	ba_init(NULL, 1);

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
/* 	if (!allocate_block(request, results)) { */
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
	request->geometry[1] = 3;
	request->geometry[2] = 1;
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
	}
	list_destroy(results);


	int dim;
	int a,b,c,d;
	int starta=0;
	int startb=0;
	int startc=0;
	int startd=0;
	int enda=DIM_SIZE[A];
	int endb=1;//DIM_SIZE[X];
	int endc=1;//DIM_SIZE[Y];
	int endd=1;//DIM_SIZE[Z];

	for(a=starta;a<enda;a++) {
		for(b=startb;b<endb;b++) {
			for(c=startc;c<endc;c++) {
				for(d=startd;d<endd;d++) {
					ba_mp_t *curr_mp =
						&(ba_system_ptr->grid
						  [a][b][c][d]);
					info("Node %c%c%c%c Used = %d "
					     "Letter = %c",
					     alpha_num[a],alpha_num[b],
					     alpha_num[c],alpha_num[d],
					     curr_mp->used,
					     curr_mp->letter);
					for(dim=0;dim<1;dim++) {
						info("\tDim %d usage is %d ",
						     dim,
						     curr_mp->axis_switch[dim].
						     usage);
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
