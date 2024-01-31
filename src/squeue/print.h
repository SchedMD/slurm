/*****************************************************************************\
 *  print.h - squeue print job definitions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
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

typedef struct squeue_job_rec {
	job_info_t *	job_ptr;
	char *		part_name;
	uint32_t	part_prio;
} squeue_job_rec_t;

long job_time_used(job_info_t * job_ptr);

extern void print_jobs_array(job_info_t *jobs, int size, list_t *format);
extern void print_steps_array(job_step_info_t *steps, int size, list_t *format);

/*****************************************************************************
 * Job Line Format Options
 *****************************************************************************/
int job_format_add_function(List list, int width, bool right_justify,
			    char *suffix,
			    int (*function) (job_info_t *, int, bool, char*));
#define job_format_add_prefix(list,wid,right,prefix) \
	job_format_add_function(list,0,0,prefix,_print_job_prefix)
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
int _print_job_burst_buffer(job_info_t * job, int width, bool right_justify,
			    char* suffix);
int _print_job_burst_buffer_state(job_info_t * job, int width,
				  bool right_justify, char* suffix);
int _print_job_cluster_name(job_info_t * job, int width, bool right,
			    char* suffix);
int _print_job_container(job_info_t *job, int width, bool right, char *suffix);
int _print_job_container_id(job_info_t *job, int width, bool right,
			    char *suffix);
int _print_job_core_spec(job_info_t * job, int width, bool right_justify,
			 char* suffix);
int _print_job_delay_boot(job_info_t * job, int width, bool right_justify,
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
int _print_job_group_id(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_group_name(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_job_state(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_last_sched_eval(job_info_t * job, int width, bool right,
			       char* suffix);
int _print_job_job_state_compact(job_info_t * job, int width,
			bool right_justify, char* suffix);
int _print_job_time_left(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_time_limit(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_time_used(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_time_submit(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_time_pending(job_info_t *job, int width, bool right_justify,
			    char *suffix);
int _print_job_time_start(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_time_end(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_deadline(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_priority(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_priority_long(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_nodes(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_schednodes(job_info_t * job, int width, bool right_justify,
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
int _print_job_num_tasks(job_info_t * job, int width, bool right_justify,
		         char* suffix);
int _print_job_over_subscribe(job_info_t * job, int width, bool right_justify,
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
int _print_job_cluster_features(job_info_t * job, int width, bool right_justify,
				char* suffix);
int _print_job_prefer(job_info_t * job, int width, bool right_justify,
		      char* suffix);
int _print_job_account(job_info_t * job, int width, bool right_justify,
		       char* suffix);
int _print_job_dependency(job_info_t * job, int width, bool right_justify,
			  char* suffix);
int _print_job_qos(job_info_t * job, int width, bool right_justify,
		   char* suffix);
int _print_job_admin_comment(job_info_t * job, int width, bool right_justify,
			     char* suffix);
int _print_job_system_comment(job_info_t * job, int width, bool right_justify,
			      char* suffix);
int _print_job_comment(job_info_t * job, int width, bool right_justify,
		       char* suffix);
int _print_job_reservation(job_info_t * job, int width, bool right_justify,
			   char* suffix);
int _print_job_command(job_info_t * job, int width, bool right_justify,
		       char* suffix);
int _print_job_work_dir(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_nice(job_info_t * job, int width, bool right_justify,
		    char* suffix);
int _print_job_accrue_time(job_info_t * job, int width, bool right_justify,
			   char* suffix);
int _print_job_alloc_nodes(job_info_t * job, int width, bool right_justify,
			   char* suffix);
int _print_job_alloc_sid(job_info_t * job, int width, bool right_justify,
			 char* suffix);
int _print_job_assoc_id(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_batch_flag(job_info_t * job, int width, bool right_justify,
			  char* suffix);
int _print_job_boards_per_node(job_info_t * job, int width, bool right_justify,
			       char* suffix);
int _print_job_cpus_per_task(job_info_t * job, int width, bool right_justify,
			     char* suffix);
int _print_job_cpus_per_task(job_info_t * job, int width, bool right_justify,
			     char* suffix);
int _print_job_derived_ec(job_info_t * job, int width, bool right_justify,
			  char* suffix);
int _print_job_eligible_time(job_info_t * job, int width, bool right_justify,
			     char* suffix);
int _print_job_exit_code(job_info_t * job, int width, bool right_justify,
			 char* suffix);
int _print_job_fed_origin(job_info_t * job, int width, bool right_justify,
			  char* suffix);
int _print_job_fed_origin_raw(job_info_t * job, int width, bool right_justify,
			      char* suffix);
int _print_job_fed_siblings_active(job_info_t * job, int width,
				   bool right_justify, char* suffix);
int _print_job_fed_siblings_active_raw(job_info_t * job, int width,
				       bool right_justify, char* suffix);
int _print_job_fed_siblings_viable(job_info_t * job, int width,
				   bool right_justify, char* suffix);
int _print_job_fed_siblings_viable_raw(job_info_t * job, int width,
				       bool right_justify, char* suffix);
int _print_job_max_cpus(job_info_t * job, int width, bool right_justify,
			char* suffix);
int _print_job_max_nodes(job_info_t * job, int width, bool right_justify,
			 char* suffix);
int _print_job_network(job_info_t * job, int width, bool right_justify,
		       char* suffix);
int _print_job_ntasks_per_core(job_info_t * job, int width, bool right_justify,
			       char* suffix);
int _print_job_ntasks_per_node(job_info_t * job, int width, bool right_justify,
			       char* suffix);
int _print_job_ntasks_per_socket(job_info_t * job, int width,
				 bool right_justify, char* suffix);
int _print_job_ntasks_per_board(job_info_t * job, int width,
				bool right_justify, char* suffix);
int _print_job_preempt_time(job_info_t * job, int width,
			    bool right_justify, char* suffix);
int _print_job_preempt_time(job_info_t * job, int width,
			    bool right_justify, char* suffix);
int _print_job_profile(job_info_t * job, int width,
		       bool right_justify, char* suffix);
int _print_job_reboot(job_info_t * job, int width,
		      bool right_justify, char* suffix);
int _print_job_req_switch(job_info_t * job, int width,
			  bool right_justify, char* suffix);
int _print_job_requeue(job_info_t * job, int width,
		       bool right_justify, char* suffix);
int _print_job_resize_time(job_info_t * job, int width,
			   bool right_justify, char* suffix);
int _print_job_restart_cnt(job_info_t * job, int width,
			   bool right_justify, char* suffix);
int _print_job_sockets_per_board(job_info_t * job, int width,
				 bool right_justify, char* suffix);
int _print_job_std_err(job_info_t * job, int width,
		       bool right_justify, char* suffix);
int _print_job_std_in(job_info_t * job, int width,
		      bool right_justify, char* suffix);
int _print_job_std_out(job_info_t * job, int width,
		       bool right_justify, char* suffix);
int _print_job_min_time(job_info_t * job, int width,
			bool right_justify, char* suffix);
int _print_job_wait4switch(job_info_t * job, int width,
			   bool right_justify, char* suffix);
int _print_job_cpus_per_tres(job_info_t * job, int width,
			     bool right_justify, char *suffix);
int _print_job_mem_per_tres(job_info_t * job, int width,
			    bool right_justify, char *suffix);
int _print_job_tres_alloc(job_info_t * job, int width,
			  bool right_justify, char *suffix);
int _print_job_tres_bind(job_info_t * job, int width,
			 bool right_justify, char *suffix);
int _print_job_tres_freq(job_info_t * job, int width,
			 bool right_justify, char *suffix);
int _print_job_tres_per_job(job_info_t * job, int width,
			    bool right_justify, char *suffix);
int _print_job_tres_per_node(job_info_t * job, int width,
			     bool right_justify, char *suffix);
int _print_job_tres_per_socket(job_info_t * job, int width,
			       bool right_justify, char *suffix);
int _print_job_tres_per_task(job_info_t * job, int width,
			     bool right_justify, char *suffix);
int _print_job_mcs_label(job_info_t * job, int width,
			 bool right_justify, char* suffix);
int _print_job_het_job_id(job_info_t * job, int width,
			  bool right_justify, char* suffix);
int _print_job_het_job_offset(job_info_t * job, int width,
			      bool right_justify, char* suffix);
int _print_job_het_job_id_set(job_info_t * job, int width,
			      bool right_justify, char* suffix);


/*****************************************************************************
 * Step Print Format Functions
 *****************************************************************************/
int step_format_add_function(List list, int width, bool right_justify,
			     char * suffix,
		int (*function) (job_step_info_t *, int, bool, char *));

#define step_format_add_prefix(list,wid,right,prefix) \
	step_format_add_function(list,0,0,prefix,_print_step_prefix)
#define step_format_add_invalid(list,wid,right,suffix) \
	step_format_add_function(list,wid,right,suffix,	\
				 (void*)_print_com_invalid)

// finish adding macros and function headers in the .h file.

/*****************************************************************************
 * Step Line Print Functions
 *****************************************************************************/
int _print_step_cluster_name(job_step_info_t * step, int width,
			     bool right_justify, char *suffix);
int _print_step_container(job_step_info_t *step, int width, bool right_justify,
			  char *suffix);
int _print_step_container_id(job_step_info_t *step, int width,
			     bool right_justify, char *suffix);
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
int _print_step_array_job_id(job_step_info_t * step, int width, bool right,
			     char* suffix);
int _print_step_array_task_id(job_step_info_t * step, int width, bool right,
			      char* suffix);
int _print_step_job_id(job_step_info_t * step, int width, bool right,
		       char* suffix);
int _print_step_network(job_step_info_t * step, int width, bool right,
			char* suffix);
int _print_step_node_inx(job_step_info_t * step, int width, bool right,
			 char* suffix);
int _print_step_num_cpus(job_step_info_t * step, int width, bool right,
			 char* suffix);
int _print_step_cpu_freq(job_step_info_t * step, int width, bool right,
			 char* suffix);
int _print_step_resv_ports(job_step_info_t * step, int width, bool right,
			   char* suffix);
int _print_step_state(job_step_info_t * step, int width, bool right,
		      char* suffix);
int _print_step_cpus_per_tres(job_step_info_t * step, int width, bool right,
			      char* suffix);
int _print_step_mem_per_tres(job_step_info_t * step, int width, bool right,
			     char* suffix);
int _print_step_tres_bind(job_step_info_t * step, int width, bool right,
			  char* suffix);
int _print_step_tres_freq(job_step_info_t * step, int width, bool right,
			  char* suffix);
int _print_step_tres_per_step(job_step_info_t * step, int width, bool right,
			      char* suffix);
int _print_step_tres_per_node(job_step_info_t * step, int width, bool right,
			      char* suffix);
int _print_step_tres_per_socket(job_step_info_t * step, int width, bool right,
				char* suffix);
int _print_step_tres_per_task(job_step_info_t * step, int width, bool right,
			      char* suffix);

/*****************************************************************************
 * Common Line Print Functions
 *****************************************************************************/
int _print_com_invalid(void * p, int width, bool right_justify, char * suffix);

#endif
