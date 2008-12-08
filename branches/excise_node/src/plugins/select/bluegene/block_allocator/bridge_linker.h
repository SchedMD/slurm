/*****************************************************************************\
 *  bridge_linker.h
 * 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
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

#ifndef _BRIDGE_LINKER_H_
#define _BRIDGE_LINKER_H_

/* This must be included first for AIX systems */
#include "src/common/macros.h"

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/api/node_select_info.h"
#include "src/common/read_config.h"
#include "src/common/parse_spec.h"
#include "src/slurmctld/proc_req.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/bitstring.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/plugins/select/bluegene/wrap_rm_api.h"
#include <dlfcn.h>

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#ifdef HAVE_BG_FILES
extern bool have_db2;
extern int bridge_init();
extern int bridge_fini();

extern status_t bridge_get_bg(my_bluegene_t **bg);
extern status_t bridge_add_block(rm_partition_t *partition);
extern status_t bridge_get_block(pm_partition_id_t pid, 
				 rm_partition_t **partition);
extern status_t bridge_get_block_info(pm_partition_id_t pid, 
				      rm_partition_t **partition);
extern status_t bridge_modify_block(pm_partition_id_t pid, 
				    enum rm_modify_op op, const void *data);
extern status_t bridge_set_block_owner(pm_partition_id_t, const char *);
extern status_t bridge_add_block_user(pm_partition_id_t, const char *);
extern status_t bridge_remove_block_user(pm_partition_id_t, const char *);
extern status_t bridge_remove_block(pm_partition_id_t pid);
extern status_t bridge_get_blocks(rm_partition_state_flag_t flag, 
				  rm_partition_list_t **part_list);
extern status_t bridge_get_blocks_info(rm_partition_state_flag_t flag, 
				       rm_partition_list_t **part_list);
extern status_t bridge_get_job(db_job_id_t dbJobId, rm_job_t **job);
extern status_t bridge_get_jobs(rm_job_state_flag_t flag,
				rm_job_list_t **jobs);
extern status_t bridge_remove_job(db_job_id_t jid);  
extern status_t bridge_get_nodecards(rm_bp_id_t bpid, 
				     rm_nodecard_list_t **nc_list);
extern status_t bridge_new_block(rm_partition_t **partition);
extern status_t bridge_free_block(rm_partition_t *partition);
extern status_t bridge_free_job(rm_job_t *job);
extern status_t bridge_free_bg(my_bluegene_t *bg);
extern status_t bridge_free_block_list(rm_partition_list_t *part_list);
extern status_t bridge_free_job_list(rm_job_list_t *job_list);  
extern status_t bridge_free_nodecard_list(rm_nodecard_list_t *nc_list);
extern status_t bridge_get_data(rm_element_t* element,
				enum rm_specification field, void *data);
extern status_t bridge_set_data(rm_element_t* element, 
				enum rm_specification field, void *data);

/* all the jm functions */
extern status_t bridge_signal_job(db_job_id_t, rm_signal_t);
extern status_t bridge_cancel_job(db_job_id_t);

/* all the pm functions */
extern status_t bridge_create_block(pm_partition_id_t pid);

#ifndef HAVE_BGL
extern status_t bridge_reboot_block(pm_partition_id_t pid);
#endif

extern status_t bridge_destroy_block(pm_partition_id_t pid);

/* say message */

extern int bridge_set_log_params(char *api_file_name, unsigned int level);
#endif /* HAVE_BG_FILES */
#endif /* _BRIDGE_LINKER_H_ */
