/*****************************************************************************\
 *  print.h - squeue print job definitions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _SQUEUE_PRINT_H_
#define _SQUEUE_PRINT_H_

#include "slurm/slurm.h"

#include "src/common/list.h"

#define FORMAT_STRING_SIZE 32

/*****************************************************************************
 * Format Structures
 *****************************************************************************/
typedef struct job_format {
	int (*function) (job_info_t *, int, bool, char*);
	uint32_t width;
	bool right_justify;
	char *suffix;
} job_format_t;

typedef struct step_format {
	int (*function) (job_step_info_t *, int, bool, char*);
	uint32_t width;
	bool right_justify;
	char *suffix;
} step_format_t;

long job_time_used(job_info_t * job_ptr);

int print_jobs_list(List jobs, List format);
int print_steps_list(List steps, List format);

int print_jobs_array(job_info_t * jobs, int size, List format);
int print_steps_array(job_step_info_t * steps, int size, List format);

int print_job_from_format(job_info_t * job, List list);
int print_step_from_format(job_step_info_t * job_step, List list);

/*****************************************************************************
 * Job Line Format Options
 *****************************************************************************/
int job_format_add_function(List list, int width, bool right_justify,
			    char *suffix,
			    int (*function) (job_info_t *, int, bool, char*));
#define job_format_add_array_job_id(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_array_job_id)
#define job_format_add_array_task_id(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_array_task_id)
#define job_format_add_batch_host(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_batch_host)
#define job_format_add_core_spec(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_core_spec)
#define job_format_add_job_id(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_job_id)
#define job_format_add_job_id2(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_job_id2)
#define job_format_add_partition(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_partition)
#define job_format_add_prefix(list,wid,right,prefix) \
	job_format_add_function(list,0,0,prefix,_print_job_prefix)
#define job_format_add_reason(list,wid,right,prefix) \
        job_format_add_function(list,wid,right,prefix,_print_job_reason)
#define job_format_add_reason_list(list,wid,right,prefix) \
	job_format_add_function(list,wid,right,prefix,_print_job_reason_list)
#define job_format_add_name(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_name)
#define job_format_add_licenses(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_licenses)
#define job_format_add_wckey(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_wckey)
#define job_format_add_user_name(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_user_name)
#define job_format_add_user_id(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_user_id)
#define job_format_add_gres(list,wid,right,suffix) \
        job_format_add_function(list,wid,right,suffix,_print_job_gres)
#define job_format_add_group_name(list,wid,right,suffix) \
        job_format_add_function(list,wid,right,suffix,_print_job_group_name)
#define job_format_add_group_id(list,wid,right,suffix) \
        job_format_add_function(list,wid,right,suffix,_print_job_group_id)
#define job_format_add_job_state(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_job_state)
#define job_format_add_job_state_compact(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,  \
	                        _print_job_job_state_compact)
#define job_format_add_time_left(list,wid,right,suffix)	\
	job_format_add_function(list,wid,right,suffix,	\
	                        _print_job_time_left)
#define job_format_add_time_limit(list,wid,right,suffix)	\
	job_format_add_function(list,wid,right,suffix,	\
	                        _print_job_time_limit)
#define job_format_add_time_used(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_time_used)
#define job_format_add_time_start(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_time_start)
#define job_format_add_time_end(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_time_end)
#define job_format_add_priority(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_priority)
#define job_format_add_priority_long(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_priority_long)
#define job_format_add_nodes(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_nodes)
#define job_format_add_node_inx(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_node_inx)
#define job_format_add_num_cpus(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_num_cpus)
#define job_format_add_num_nodes(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_num_nodes)
#define job_format_add_num_sct(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_num_sct)
#define job_format_add_shared(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_shared)
#define job_format_add_contiguous(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_contiguous)
#define job_format_add_min_cpus(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_pn_min_cpus)
#define job_format_add_sockets(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_sockets)
#define job_format_add_cores(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_cores)
#define job_format_add_threads(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_threads)
#define job_format_add_min_memory(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_pn_min_memory)
#define job_format_add_min_tmp_disk(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_pn_min_tmp_disk)
#define job_format_add_req_nodes(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_req_nodes)
#define job_format_add_exc_nodes(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_exc_nodes)
#define job_format_add_req_node_inx(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_req_node_inx)
#define job_format_add_exc_node_inx(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_exc_node_inx)
#define job_format_add_features(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_features)
#define job_format_add_account(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_account)
#define job_format_add_dependency(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_dependency)
#define job_format_add_qos(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_qos)
#define job_format_add_select_jobinfo(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_select_jobinfo)
#define job_format_add_comment(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_comment)
#define job_format_add_reservation(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_reservation)
#define job_format_add_command(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_command)
#define job_format_add_work_dir(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_work_dir)
#define job_format_add_invalid(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,(void*)_print_com_invalid)


/*****************************************************************************
 * Job Line Print Functions
 *****************************************************************************/
int _print_job_array_job_id(job_info_t * job, int width, bool right_justify,
			    char* suffix);
int _print_job_array_task_id(job_info_t * job, int width, bool right_justify,
			     char* suffix);
int _print_job_batch_host(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_core_spec(job_info_t * job, int width, bool right_justify,
			 char* suffix);
int _print_job_job_id(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_job_id2(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_prefix(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_reason(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_reason_list(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_name(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_licenses(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_wckey(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_user_id(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_user_name(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_gres(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_group_id(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_group_name(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_job_state(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_job_state_compact(job_info_t * job, int width,
			bool right_justify, char* suffix);
int _print_job_time_left(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_time_limit(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_time_used(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_time_start(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_time_end(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_priority(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_priority_long(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_nodes(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_node_inx(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_partition(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_num_cpus(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_num_nodes(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_num_sct(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_shared(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_contiguous(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_pn_min_cpus(job_info_t * job, int width, bool right_justify,
		       char* suffix);
int _print_sockets(job_info_t * job, int width, bool right_justify,
		   char* suffix);
int _print_cores(job_info_t * job, int width, bool right_justify, char* suffix);
int _print_threads(job_info_t * job, int width, bool right_justify,
		   char* suffix);
int _print_pn_min_memory(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_pn_min_tmp_disk(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_req_nodes(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_exc_nodes(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_req_node_inx(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_exc_node_inx(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_features(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_account(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_dependency(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_qos(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_select_jobinfo(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_comment(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_reservation(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_command(job_info_t * job, int width, bool right_justify,
		        char* suffix);
int _print_job_work_dir(job_info_t * job, int width, bool right_justify,
			char* suffix);

/*****************************************************************************
 * Step Print Format Functions
 *****************************************************************************/
int step_format_add_function(List list, int width, bool right_justify,
		char * suffix,
		int (*function) (job_step_info_t *, int, bool, char *));

#define step_format_add_id(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_id)
#define step_format_add_partition(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_partition)
#define step_format_add_prefix(list,wid,right,prefix) \
	step_format_add_function(list,0,0,prefix,_print_step_prefix)
#define step_format_add_user_id(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_user_id)
#define step_format_add_user_name(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_user_name)
#define step_format_add_time_limit(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_time_limit)
#define step_format_add_time_start(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_time_start)
#define step_format_add_time_used(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_time_used)
#define step_format_add_nodes(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_nodes)
#define step_format_add_name(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_name)
#define step_format_add_num_tasks(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_num_tasks)
#define step_format_add_gres(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,_print_step_gres)
#define step_format_add_invalid(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,	\
					(void*)_print_com_invalid)

/*****************************************************************************
 * Step Line Print Functions
 *****************************************************************************/
int _print_step_id(job_step_info_t * step, int width, bool right_justify,
			char *suffix);
int _print_step_partition(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_prefix(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_user_id(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_user_name(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_time_limit(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_time_start(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_time_used(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_name(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_nodes(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_num_tasks(job_step_info_t * step, int width,
			bool right_justify, char *suffix);
int _print_step_gres(job_step_info_t * step, int width,
			bool right_justify, char *suffix);

/*****************************************************************************
 * Common Line Print Functions
 *****************************************************************************/
int _print_com_invalid(void * p, int width, bool right_justify, char * suffix);

#endif
