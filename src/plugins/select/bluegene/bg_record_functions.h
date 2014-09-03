/*****************************************************************************\
 *  bg_record_functions.h - header for creating blocks in a static environment.
 *
 *  $Id: bg_record_functions.h 12954 2008-01-04 20:37:49Z da $
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#ifndef _BLUEGENE_BG_RECORD_FUNCTIONS_H_
#define _BLUEGENE_BG_RECORD_FUNCTIONS_H_

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <sys/stat.h>
#include <pwd.h>

#include "src/common/xstring.h"
#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/slurmctld/slurmctld.h"
#include "ba_common.h"

/* Log a bg_record's contents */
extern void print_bg_record(bg_record_t *record);
extern void destroy_bg_record(void *object);
extern void process_nodes(bg_record_t *bg_reord, bool startup);
extern List copy_bg_list(List in_list);
extern void copy_bg_record(bg_record_t *fir_record, bg_record_t *sec_record);
extern int bg_record_cmpf_inc(void *, void *);
extern int bg_record_sort_aval_inc(void *, void *);
extern int bg_record_sort_aval_time_inc(void *r1, void *r2);
extern void setup_subblock_structs(bg_record_t *bg_record);

/* change username of a block bg_record_t target_name needs to be
   updated before call of function.
*/
extern void requeue_and_error(bg_record_t *bg_record, char *reason);

extern int add_bg_record(List records, List *used_nodes, select_ba_request_t *blockreq,
			 bool no_check, bitoff_t io_start);
extern int handle_small_record_request(List records, select_ba_request_t *blockreq,
				       bg_record_t *bg_record, bitoff_t start);

extern int format_node_name(bg_record_t *bg_record, char *buf, int buf_size);
extern int down_nodecard(char *bp_name, bitoff_t io_start,
			 bool slurmctld_locked, char *reason);
extern int up_nodecard(char *bp_name, bitstr_t *ionode_bitmap);
extern int put_block_in_error_state(bg_record_t *bg_record, char *reason);
extern int resume_block(bg_record_t *bg_record);
extern int bg_reset_block(bg_record_t *bg_record, struct job_record *job_ptr);
extern void bg_record_hw_failure(bg_record_t *bg_record, List *ret_kill_list);
extern void bg_record_post_hw_failure(
	List *kill_list, bool slurmctld_locked);

#endif /* _BLUEGENE_BG_RECORD_FUNCTIONS_H_ */
