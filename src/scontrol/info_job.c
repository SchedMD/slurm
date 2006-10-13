/*****************************************************************************\
 *  info_job.c - job information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
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

#include "scontrol.h"

static bool	_in_node_bit_list(int inx, int *node_list_array);

/*
 * Determine if a node index is in a node list pair array. 
 * RET -  true if specified index is in the array
 */
static bool
_in_node_bit_list(int inx, int *node_list_array)
{
	int i;
	bool rc = false;

	for (i=0; ; i+=2) {
		if (node_list_array[i] == -1)
			break;
		if ((inx >= node_list_array[i]) &&
		    (inx <= node_list_array[i+1])) {
			rc = true;
			break;
		}
	}

	return rc;
}

/* Load current job table information into *job_buffer_pptr */
extern int 
scontrol_load_jobs (job_info_msg_t ** job_buffer_pptr) 
{
	int error_code;
	static job_info_msg_t *old_job_info_ptr = NULL;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;
	job_info_msg_t * job_info_ptr = NULL;

	if (all_flag)
		show_flags |= SHOW_ALL;

	if (old_job_info_ptr) {
		if (last_show_flags != show_flags)
			old_job_info_ptr->last_update = (time_t) 0;
		error_code = slurm_load_jobs (old_job_info_ptr->last_update, 
					&job_info_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg (old_job_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			job_info_ptr = old_job_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
 				printf ("slurm_load_jobs no change in data\n");
		}
	}
	else
		error_code = slurm_load_jobs ((time_t) NULL, &job_info_ptr,
				show_flags);

	if (error_code == SLURM_SUCCESS) {
		old_job_info_ptr = job_info_ptr;
		last_show_flags  = show_flags;
		*job_buffer_pptr = job_info_ptr;
	}

	return error_code;
}

/* 
 * scontrol_pid_info - given a local process id, print the corresponding 
 *	slurm job id and its expected end time
 * IN job_pid - the local process id of interest
 */
extern void
scontrol_pid_info(pid_t job_pid)
{
	int error_code;
	uint32_t job_id;
	time_t end_time;
	long rem_time;

	error_code = slurm_pid2jobid (job_pid, &job_id);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_pid2jobid error");
		return;
	}

	error_code = slurm_get_end_time(job_id, &end_time);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_get_end_time error");
		return;
	}
	printf("Slurm job id %u ends at %s\n", job_id, ctime(&end_time));

	rem_time = slurm_get_rem_time(job_id);
	printf("slurm_get_rem_time is %ld\n", rem_time);
	return;
}

/*
 * scontrol_print_completing - print jobs in completing state and 
 *	associated nodes in COMPLETING or DOWN state
 */
extern void	
scontrol_print_completing (void)
{
	int error_code, i;
	job_info_msg_t  *job_info_msg;
	job_info_t      *job_info;
	node_info_msg_t *node_info_msg;
	uint16_t         show_flags = 0;

	error_code = scontrol_load_jobs (&job_info_msg);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_jobs error");
		return;
	}
	/* Must load all nodes including hidden for cross-index 
	 * from job's node_inx to node table to work */
	/*if (all_flag)		Always set this flag */
		show_flags |= SHOW_ALL;
	error_code = scontrol_load_nodes (&node_info_msg, show_flags);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_nodes error");
		return;
	}

	/* Scan the jobs for completing state */
	job_info = job_info_msg->job_array;
	for (i=0; i<job_info_msg->record_count; i++) {
		if (job_info[i].job_state & JOB_COMPLETING)
			scontrol_print_completing_job(&job_info[i], 
					node_info_msg);
	}
}

extern void
scontrol_print_completing_job(job_info_t *job_ptr, 
		node_info_msg_t *node_info_msg)
{
	int i;
	node_info_t *node_info;
	uint16_t node_state, base_state;
	hostlist_t all_nodes, comp_nodes, down_nodes;
	char node_buf[1024];

	all_nodes  = hostlist_create(job_ptr->nodes);
	comp_nodes = hostlist_create("");
	down_nodes = hostlist_create("");

	node_info = node_info_msg->node_array;
	for (i=0; i<node_info_msg->record_count; i++) {
		node_state = node_info[i].node_state;
		base_state = node_info[i].node_state & NODE_STATE_BASE;
		if ((node_state & NODE_STATE_COMPLETING) && 
		    (_in_node_bit_list(i, job_ptr->node_inx)))
			hostlist_push_host(comp_nodes, node_info[i].name);
		else if ((base_state == NODE_STATE_DOWN) &&
			 (hostlist_find(all_nodes, node_info[i].name) != -1))
			hostlist_push_host(down_nodes, node_info[i].name);
	}

	fprintf(stdout, "JobId=%u ", job_ptr->job_id);
	i = hostlist_ranged_string(comp_nodes, sizeof(node_buf), node_buf);
	if (i > 0)
		fprintf(stdout, "Nodes(COMPLETING)=%s ", node_buf);
	i = hostlist_ranged_string(down_nodes, sizeof(node_buf), node_buf);
	if (i > 0)
		fprintf(stdout, "Nodes(DOWN)=%s ", node_buf);
	fprintf(stdout, "\n");

	hostlist_destroy(all_nodes);
	hostlist_destroy(comp_nodes);
	hostlist_destroy(down_nodes);
}

/*
 * scontrol_print_job - print the specified job's information
 * IN job_id - job's id or NULL to print information about all jobs
 */
extern void 
scontrol_print_job (char * job_id_str) 
{
	int error_code = SLURM_SUCCESS, i, print_cnt = 0;
	uint32_t job_id = 0;
	job_info_msg_t * job_buffer_ptr = NULL;
	job_info_t *job_ptr = NULL;

	error_code = scontrol_load_jobs(&job_buffer_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_jobs error");
		return;
	}
	
	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str ((time_t *)&job_buffer_ptr->last_update, 
				     time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n", 
			time_str, job_buffer_ptr->record_count);
	}

	if (job_id_str)
		job_id = (uint32_t) strtol (job_id_str, (char **)NULL, 10);

	job_ptr = job_buffer_ptr->job_array ;
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_id_str && job_id != job_ptr[i].job_id) 
			continue;
		print_cnt++;
		slurm_print_job_info (stdout, & job_ptr[i], one_liner ) ;
		if (job_id_str)
			break;
	}

	if (print_cnt == 0) {
		if (job_id_str) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Job %u not found\n", job_id);
		} else if (quiet_flag != 1)
			printf ("No jobs in the system\n");
	}
}

/*
 * scontrol_print_step - print the specified job step's information
 * IN job_step_id_str - job step's id or NULL to print information
 *	about all job steps
 */
extern void 
scontrol_print_step (char *job_step_id_str)
{
	int error_code, i;
	uint32_t job_id = 0, step_id = 0, step_id_set = 0;
	char *next_str;
	job_step_info_response_msg_t *job_step_info_ptr;
	job_step_info_t * job_step_ptr;
	static uint32_t last_job_id = 0, last_step_id = 0;
	static job_step_info_response_msg_t *old_job_step_info_ptr = NULL;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;

	if (job_step_id_str) {
		job_id = (uint32_t) strtol (job_step_id_str, &next_str, 10);
		if (next_str[0] == '.') {
			step_id = (uint32_t) strtol (&next_str[1], NULL, 10);
			step_id_set = 1;
		}
	}

	if (all_flag)
		show_flags |= SHOW_ALL;

	if ((old_job_step_info_ptr) &&
	    (last_job_id == job_id) && (last_step_id == step_id)) {
		if (last_show_flags != show_flags)
			old_job_step_info_ptr->last_update = (time_t) 0;
		error_code = slurm_get_job_steps ( 
					old_job_step_info_ptr->last_update,
					job_id, step_id, &job_step_info_ptr,
					show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_step_info_response_msg (
					old_job_step_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			job_step_info_ptr = old_job_step_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
				printf ("slurm_get_job_steps no change in data\n");
		}
	}
	else {
		if (old_job_step_info_ptr) {
			slurm_free_job_step_info_response_msg (
					old_job_step_info_ptr);
			old_job_step_info_ptr = NULL;
		}
		error_code = slurm_get_job_steps ( (time_t) 0, job_id, step_id, 
				&job_step_info_ptr, show_flags);
	}

	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_get_job_steps error");
		return;
	}

	old_job_step_info_ptr = job_step_info_ptr;
	last_show_flags = show_flags;
	last_job_id = job_id;
	last_step_id = step_id;

	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str ((time_t *)&job_step_info_ptr->last_update, 
			             time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n", 
			time_str, job_step_info_ptr->job_step_count);
	}

	job_step_ptr = job_step_info_ptr->job_steps ;
	for (i = 0; i < job_step_info_ptr->job_step_count; i++) {
		if (step_id_set && (step_id == 0) && 
		    (job_step_ptr[i].step_id != 0)) 
			continue;
		slurm_print_job_step_info (stdout, & job_step_ptr[i], 
		                           one_liner ) ;
	}

	if (job_step_info_ptr->job_step_count == 0) {
		if (job_step_id_str) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Job step %u.%u not found\n", 
				        job_id, step_id);
		} else if (quiet_flag != 1)
			printf ("No job steps in the system\n");
	}
}
