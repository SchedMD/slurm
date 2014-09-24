/*****************************************************************************\
 *  info_job.c - job information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "scontrol.h"
#include "src/common/bitstring.h"
#include "src/common/stepd_api.h"
#include "src/plugins/select/bluegene/bg_enums.h"

#define POLL_SLEEP	3	/* retry interval in seconds  */

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
scontrol_load_job(job_info_msg_t ** job_buffer_pptr, uint32_t job_id)
{
	int error_code;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;
	job_info_msg_t * job_info_ptr = NULL;

	if (all_flag)
		show_flags |= SHOW_ALL;

	if (detail_flag) {
		show_flags |= SHOW_DETAIL;
		if (detail_flag > 1)
			show_flags |= SHOW_DETAIL2;
	}

	if (old_job_info_ptr) {
		if (last_show_flags != show_flags)
			old_job_info_ptr->last_update = (time_t) 0;
		if (job_id) {
			error_code = slurm_load_job(&job_info_ptr, job_id,
						    show_flags);
		} else {
			error_code = slurm_load_jobs(
				old_job_info_ptr->last_update,
				&job_info_ptr, show_flags);
		}
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg (old_job_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			job_info_ptr = old_job_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
 				printf ("slurm_load_jobs no change in data\n");
		}
	} else if (job_id) {
		error_code = slurm_load_job(&job_info_ptr, job_id, show_flags);
	} else {
		error_code = slurm_load_jobs((time_t) NULL, &job_info_ptr,
					     show_flags);
	}

	if (error_code == SLURM_SUCCESS) {
		old_job_info_ptr = job_info_ptr;
		if (job_id)
			old_job_info_ptr->last_update = (time_t) 0;
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
	printf("Slurm job id %u ends at %s\n", job_id, slurm_ctime(&end_time));

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

	error_code = scontrol_load_job (&job_info_msg, 0);
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
	hostlist_t all_nodes, comp_nodes, down_nodes;
	char *node_buf;

	all_nodes  = hostlist_create(job_ptr->nodes);
	comp_nodes = hostlist_create(NULL);
	down_nodes = hostlist_create(NULL);

	for (i=0; i<node_info_msg->record_count; i++) {
		node_info = &(node_info_msg->node_array[i]);
		if (IS_NODE_COMPLETING(node_info) &&
		    (_in_node_bit_list(i, job_ptr->node_inx)))
			hostlist_push_host(comp_nodes, node_info->name);
		else if (IS_NODE_DOWN(node_info) &&
			 (hostlist_find(all_nodes, node_info->name) != -1))
			hostlist_push_host(down_nodes, node_info->name);
	}

	fprintf(stdout, "JobId=%u ", job_ptr->job_id);

	node_buf = hostlist_ranged_string_xmalloc(comp_nodes);
	if (node_buf && node_buf[0])
		fprintf(stdout, "Nodes(COMPLETING)=%s ", node_buf);
	xfree(node_buf);

	node_buf = hostlist_ranged_string_xmalloc(down_nodes);
	if (node_buf && node_buf[0])
		fprintf(stdout, "Nodes(DOWN)=%s ", node_buf);
	xfree(node_buf);
	fprintf(stdout, "\n");

	hostlist_destroy(all_nodes);
	hostlist_destroy(comp_nodes);
	hostlist_destroy(down_nodes);
}

extern uint16_t
scontrol_get_job_state(uint32_t job_id)
{
	job_info_msg_t * job_buffer_ptr = NULL;
	int error_code = SLURM_SUCCESS, i;
	job_info_t *job_ptr = NULL;

	error_code = scontrol_load_job(&job_buffer_ptr, job_id);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag == -1)
			slurm_perror ("slurm_load_job error");
		return (uint16_t) NO_VAL;
	}
	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str((time_t *)&job_buffer_ptr->last_update,
				    time_str, sizeof(time_str));
		printf("last_update_time=%s, records=%d\n",
		       time_str, job_buffer_ptr->record_count);
	}

	job_ptr = job_buffer_ptr->job_array ;
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_ptr->job_id == job_id)
			return job_ptr->job_state;
	}
	if (quiet_flag == -1)
		printf("Could not find job %u", job_id);
	return (uint16_t) NO_VAL;
}

static bool _task_id_in_job(job_info_t *job_ptr, uint32_t array_id)
{
	bitstr_t *array_bitmap;
	uint32_t array_len;

	if ((array_id == NO_VAL) ||
	    (array_id == job_ptr->array_task_id))
		return true;

	array_bitmap = (bitstr_t *) job_ptr->array_bitmap;
	if (array_bitmap == NULL)
		return false;
	array_len = bit_size(array_bitmap);
	if (array_id >= array_len)
		return false;
	if (bit_test(array_bitmap, array_id))
		return true;
	return false;
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
	uint32_t array_id = NO_VAL;
	job_info_msg_t * job_buffer_ptr = NULL;
	job_info_t *job_ptr = NULL;
	char *end_ptr = NULL;

	if (job_id_str) {
		job_id = (uint32_t) strtol (job_id_str, &end_ptr, 10);
		if (end_ptr[0] == '_')
			array_id = strtol( end_ptr + 1, &end_ptr, 10 );
	}

	error_code = scontrol_load_job(&job_buffer_ptr, job_id);
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

	for (i = 0, job_ptr = job_buffer_ptr->job_array;
	     i < job_buffer_ptr->record_count; i++, job_ptr++) {
		char *save_array_str = NULL;
		uint32_t save_task_id = 0;
		if (!_task_id_in_job(job_ptr, array_id))
			continue;
		if ((array_id != NO_VAL) && job_ptr->array_task_str) {
			save_array_str = job_ptr->array_task_str;
			job_ptr->array_task_str = NULL;
			save_task_id = job_ptr->array_task_id;
			job_ptr->array_task_id = array_id;
		}
		slurm_print_job_info(stdout, job_ptr, one_liner);
		if (save_array_str) {
			job_ptr->array_task_str = save_array_str;
			job_ptr->array_task_id = save_task_id;
		}
		print_cnt++;
	}

	if (print_cnt == 0) {
		if (job_id_str) {
			exit_code = 1;
			if (quiet_flag != 1) {
				if (array_id == (uint16_t) NO_VAL) {
					printf("Job %u not found\n", job_id);
				} else {
					printf("Job %u_%u not found\n",
					       job_id, array_id);
				}
			}
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
	int error_code, i, print_cnt = 0;
	uint32_t job_id = NO_VAL, step_id = NO_VAL;
	uint32_t array_id = NO_VAL;
	char *next_str;
	job_step_info_response_msg_t *job_step_info_ptr;
	job_step_info_t * job_step_ptr;
	static uint32_t last_job_id = 0, last_array_id, last_step_id = 0;
	static job_step_info_response_msg_t *old_job_step_info_ptr = NULL;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;

	if (job_step_id_str) {
		job_id = (uint32_t) strtol (job_step_id_str, &next_str, 10);
		if (next_str[0] == '_')
			array_id = (uint16_t) strtol(next_str+1, &next_str, 10);
		if (next_str[0] == '.')
			step_id = (uint32_t) strtol (next_str+1, NULL, 10);
	}

	if (all_flag)
		show_flags |= SHOW_ALL;

	if ((old_job_step_info_ptr) && (last_job_id == job_id) &&
	    (last_array_id == array_id) && (last_step_id == step_id)) {
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
	} else {
		if (old_job_step_info_ptr) {
			slurm_free_job_step_info_response_msg (
				old_job_step_info_ptr);
			old_job_step_info_ptr = NULL;
		}
		error_code = slurm_get_job_steps ( (time_t) 0, job_id, step_id,
						   &job_step_info_ptr,
						   show_flags);
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

	for (i = 0, job_step_ptr = job_step_info_ptr->job_steps;
	     i < job_step_info_ptr->job_step_count; i++, job_step_ptr++) {
		if ((array_id != NO_VAL) &&
		    (array_id != job_step_ptr->array_task_id))
			continue;
		slurm_print_job_step_info(stdout, job_step_ptr, one_liner);
		print_cnt++;
	}

	if (print_cnt == 0) {
		if (job_step_id_str) {
			exit_code = 1;
			if (quiet_flag != 1) {
				if (array_id == (uint16_t) NO_VAL) {
					printf ("Job step %u.%u not found\n",
						job_id, step_id);
				} else {
					printf ("Job step %u_%u.%u not found\n",
						job_id, array_id, step_id);
				}
			}
		} else if (quiet_flag != 1)
			printf ("No job steps in the system\n");
	}
}

/* Return 1 on success, 0 on failure to find a jobid in the string */
static int _parse_jobid(const char *jobid_str, uint32_t *out_jobid)
{
	char *ptr, *job;
	long jobid;

	job = xstrdup(jobid_str);
	ptr = index(job, '.');
	if (ptr != NULL) {
		*ptr = '\0';
	}

	jobid = strtol(job, &ptr, 10);
	if (!xstring_is_whitespace(ptr)) {
		fprintf(stderr, "\"%s\" does not look like a jobid\n", job);
		xfree(job);
		return 0;
	}

	*out_jobid = (uint32_t) jobid;
	xfree(job);
	return 1;
}

/* Return 1 on success, 0 on failure to find a stepid in the string */
static int _parse_stepid(const char *jobid_str, uint32_t *out_stepid)
{
	char *ptr, *job, *step;
	long stepid;

	job = xstrdup(jobid_str);
	ptr = index(job, '.');
	if (ptr == NULL) {
		/* did not find a period, so no step ID in this string */
		xfree(job);
		return 0;
	} else {
		step = ptr + 1;
	}

	stepid = strtol(step, &ptr, 10);
	if (!xstring_is_whitespace(ptr)) {
		fprintf(stderr, "\"%s\" does not look like a stepid\n", step);
		xfree(job);
		return 0;
	}

	*out_stepid = (uint32_t) stepid;
	xfree(job);
	return 1;
}


static bool
_in_task_array(pid_t pid, slurmstepd_task_info_t *task_array,
	       uint32_t task_array_count)
{
	int i;

	for (i = 0; i < task_array_count; i++) {
		if (pid == task_array[i].pid)
			return true;
	}

	return false;
}


static void
_list_pids_one_step(const char *node_name, uint32_t jobid, uint32_t stepid)
{
	int fd;
	slurmstepd_task_info_t *task_info;
	uint32_t *pids;
	uint32_t count = 0;
	uint32_t tcount = 0;
	int i;
	uint16_t protocol_version;

	fd = stepd_connect(NULL, node_name, jobid, stepid, &protocol_version);
	if (fd == -1) {
		exit_code = 1;
		if (errno == ENOENT) {
			fprintf(stderr,
				"Job step %u.%u does not exist on this node.\n",
				jobid, stepid);
			exit_code = 1;
		} else {
			perror("Unable to connect to slurmstepd");
		}
		return;
	}

	stepd_task_info(fd, protocol_version, &task_info, &tcount);
	for (i = 0; i < (int)tcount; i++) {
		if (!task_info[i].exited) {
			if (stepid == NO_VAL)
				printf("%-8d %-8u %-6s %-7d %-8d\n",
				       task_info[i].pid,
				       jobid,
				       "batch",
				       task_info[i].id,
				       task_info[i].gtid);
			else
				printf("%-8d %-8u %-6u %-7d %-8d\n",
				       task_info[i].pid,
				       jobid,
				       stepid,
				       task_info[i].id,
				       task_info[i].gtid);
		}
	}

	stepd_list_pids(fd, protocol_version, &pids, &count);
	for (i = 0; i < count; i++) {
		if (!_in_task_array((pid_t)pids[i], task_info, tcount)) {
			if (stepid == NO_VAL)
				printf("%-8d %-8u %-6s %-7s %-8s\n",
				       pids[i], jobid, "batch", "-", "-");
			else
				printf("%-8d %-8u %-6u %-7s %-8s\n",
				       pids[i], jobid, stepid, "-", "-");
		}
	}

	if (count > 0)
		xfree(pids);
	if (tcount > 0)
		xfree(task_info);
	close(fd);
}

static void
_list_pids_all_steps(const char *node_name, uint32_t jobid)
{
	List steps;
	ListIterator itr;
	step_loc_t *stepd;
	int count = 0;

	steps = stepd_available(NULL, node_name);
	if (!steps || list_count(steps) == 0) {
		fprintf(stderr, "Job %u does not exist on this node.\n", jobid);
		if (steps)
			list_destroy(steps);
		exit_code = 1;
		return;
	}

	itr = list_iterator_create(steps);
	while((stepd = list_next(itr))) {
		if (jobid == stepd->jobid) {
			_list_pids_one_step(stepd->nodename, stepd->jobid,
					    stepd->stepid);
			count++;
		}
	}
	list_iterator_destroy(itr);
	list_destroy(steps);

	if (count == 0) {
		fprintf(stderr, "Job %u does not exist on this node.\n",
			jobid);
		exit_code = 1;
	}
}

static void
_list_pids_all_jobs(const char *node_name)
{
	List steps;
	ListIterator itr;
	step_loc_t *stepd;

	steps = stepd_available(NULL, node_name);
	if (!steps || list_count(steps) == 0) {
		fprintf(stderr, "No job steps exist on this node.\n");
		if (steps)
			list_destroy(steps);
		exit_code = 1;
		return;
	}

	itr = list_iterator_create(steps);
	while((stepd = list_next(itr))) {
		_list_pids_one_step(stepd->nodename, stepd->jobid,
				    stepd->stepid);
	}
	list_iterator_destroy(itr);
	list_destroy(steps);
}

/*
 * scontrol_list_pids - given a slurmd job ID or job ID + step ID,
 *	print the process IDs of the processes each job step (or
 *	just the specified step ID).
 * IN jobid_str - string representing a jobid: jobid[.stepid]
 * IN node_name - May be NULL, in which case it will attempt to
 *	determine the NodeName of the local host on its own.
 *	This is mostly of use when multiple-slurmd support is in use,
 *	because if NULL is used when there are multiple slurmd on the
 *	node, one of them will be selected more-or-less at random.
 */
extern void
scontrol_list_pids(const char *jobid_str, const char *node_name)
{
	uint32_t jobid = 0, stepid = 0;

	/* Job ID is optional */
	if (jobid_str != NULL
	    && jobid_str[0] != '*'
	    && !_parse_jobid(jobid_str, &jobid)) {
		exit_code = 1;
		return;
	}

	/* Step ID is optional */
	printf("%-8s %-8s %-6s %-7s %-8s\n",
	       "PID", "JOBID", "STEPID", "LOCALID", "GLOBALID");
	if (jobid_str == NULL || jobid_str[0] == '*') {
		_list_pids_all_jobs(node_name);
	} else if (_parse_stepid(jobid_str, &stepid)) {
		_list_pids_one_step(node_name, jobid, stepid);
	} else {
		_list_pids_all_steps(node_name, jobid);
	}
}

/*
 * scontrol_print_hosts - given a node list expression, return
 *	a list of nodes, one per line
 */
extern void
scontrol_print_hosts (char * node_list)
{
	hostlist_t hl;
	char *host;

	if (!node_list) {
		error("host list is empty");
		return;
	}
	hl = hostlist_create_dims(node_list, 0);
	if (!hl) {
		fprintf(stderr, "Invalid hostlist: %s\n", node_list);
		return;
	}
	while ((host = hostlist_shift_dims(hl, 0))) {
		printf("%s\n", host);
		free(host);
	}
	hostlist_destroy(hl);
}

/* Replace '\n' with ',', remove duplicate comma */
static void
_reformat_hostlist(char *hostlist)
{
	int i, o;
	for (i=0; (hostlist[i] != '\0'); i++) {
		if (hostlist[i] == '\n')
			hostlist[i] = ',';
	}

	o = 0;
	for (i=0; (hostlist[i] != '\0'); i++) {
		while ((hostlist[i] == ',') && (hostlist[i+1] == ','))
			i++;
		hostlist[o++] = hostlist[i];
	}
	hostlist[o] = '\0';
}

/*
 * scontrol_encode_hostlist - given a list of hostnames or the pathname
 *	of a file containing hostnames, translate them into a hostlist
 *	expression
 */
extern int
scontrol_encode_hostlist(char *hostlist, bool sorted)
{
	char *io_buf = NULL, *tmp_list, *ranged_string;
	int buf_size = 1024 * 1024;
	hostlist_t hl;

	if (!hostlist) {
		fprintf(stderr, "Hostlist is NULL\n");
		return SLURM_ERROR;
	}

	if (hostlist[0] == '/') {
		ssize_t buf_read;
		int fd = open(hostlist, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Can not open %s\n", hostlist);
			return SLURM_ERROR;
		}
		io_buf = xmalloc(buf_size);
		buf_read = read(fd, io_buf, buf_size);
		close(fd);
		if (buf_read >= buf_size) {
			/* If over 1MB, the file is almost certainly invalid */
			fprintf(stderr, "File %s is too large\n", hostlist);
			return SLURM_ERROR;
		}
		io_buf[buf_read] = '\0';
		_reformat_hostlist(io_buf);
		tmp_list = io_buf;
	} else
		tmp_list = hostlist;

	hl = hostlist_create(tmp_list);
	if (hl == NULL) {
		fprintf(stderr, "Invalid hostlist: %s\n", tmp_list);
		return SLURM_ERROR;
	}
	if (sorted)
		hostlist_sort(hl);
	ranged_string = hostlist_ranged_string_xmalloc(hl);
	printf("%s\n", ranged_string);
	hostlist_destroy(hl);
	xfree(ranged_string);
	xfree(io_buf);
	return SLURM_SUCCESS;
}

/*
 * Test if any BG blocks are in deallocating state since they are
 * probably related to this job we will want to sleep longer
 * RET	1:  deallocate in progress
 *	0:  no deallocate in progress
 *     -1: error occurred
 */
static int _blocks_dealloc(void)
{
	static block_info_msg_t *bg_info_ptr = NULL, *new_bg_ptr = NULL;
	int rc = 0, error_code = 0, i;
	uint16_t show_flags = 0;

	if (all_flag)
		show_flags |= SHOW_ALL;
	if (bg_info_ptr) {
		error_code = slurm_load_block_info(bg_info_ptr->last_update,
						   &new_bg_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_block_info_msg(bg_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bg_ptr = bg_info_ptr;
		}
	} else {
		error_code = slurm_load_block_info((time_t) NULL,
						   &new_bg_ptr, show_flags);
	}

	if (error_code) {
		error("slurm_load_partitions: %s",
		      slurm_strerror(slurm_get_errno()));
		return -1;
	}
	for (i=0; i<new_bg_ptr->record_count; i++) {
		if (new_bg_ptr->block_array[i].state == BG_BLOCK_TERM) {
			rc = 1;
			break;
		}
	}
	bg_info_ptr = new_bg_ptr;
	return rc;
}

static int _wait_bluegene_block_ready(resource_allocation_response_msg_t *alloc)
{
	int is_ready = SLURM_ERROR, i, rc = 0;
	char *block_id = NULL;
	int cur_delay = 0;
	int max_delay = BG_FREE_PREVIOUS_BLOCK + BG_MIN_BLOCK_BOOT +
		(BG_INCR_BLOCK_BOOT * alloc->node_cnt);

	select_g_select_jobinfo_get(alloc->select_jobinfo,
				    SELECT_JOBDATA_BLOCK_ID,
				    &block_id);

	for (i=0; (cur_delay < max_delay); i++) {
		if (i) {
			if (i == 1) {
				info("Waiting for block %s to become ready for "
				     "job", block_id);
			} else
				debug("still waiting");
			sleep(POLL_SLEEP);
			rc = _blocks_dealloc();
			if ((rc == 0) || (rc == -1))
				cur_delay += POLL_SLEEP;
		}

		rc = slurm_job_node_ready(alloc->job_id);

		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0)	/* job killed */
			break;
		if (rc & READY_NODE_STATE) {		/* job and node ready */
			is_ready = SLURM_SUCCESS;
			break;
		}
	}

	if (is_ready == SLURM_SUCCESS)
     		info("Block %s is ready for job %u", block_id, alloc->job_id);
	else if ((rc & READY_JOB_STATE) == 0)
		info("Job %u no longer running", alloc->job_id);
	else
		info("Problem running job %u", alloc->job_id);
	xfree(block_id);

	return is_ready;
}

static int _wait_nodes_ready(uint32_t job_id)
{
	int is_ready = SLURM_ERROR, i, rc = 0;
	int cur_delay = 0;
	int suspend_time, resume_time, max_delay;

	suspend_time = slurm_get_suspend_timeout();
	resume_time  = slurm_get_resume_timeout();
	if ((suspend_time == 0) || (resume_time == 0))
		return SLURM_SUCCESS;	/* Power save mode disabled */
	max_delay = suspend_time + resume_time;
	max_delay *= 5;		/* Allow for ResumeRate support */

	for (i=0; (cur_delay < max_delay); i++) {
		if (i) {
			if (i == 1)
				info("Waiting for nodes to boot");
			sleep(POLL_SLEEP);
			cur_delay += POLL_SLEEP;
		}

		rc = slurm_job_node_ready(job_id);

		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0)	/* job killed */
			break;
		if (rc & READY_NODE_STATE) {		/* job and node ready */
			is_ready = SLURM_SUCCESS;
			break;
		}
	}
	if (is_ready == SLURM_SUCCESS)
     		info("Nodes are ready for job %u", job_id);
	else if ((rc & READY_JOB_STATE) == 0)
		info("Job %u no longer running", job_id);
	else
		info("Problem running job %u", job_id);

	return is_ready;
}

/*
 * Wait until a job is ready to execute or enters some failed state
 * RET 1: job ready to run
 *     0: job can't run (cancelled, failure state, timeout, etc.)
 */
extern int scontrol_job_ready(char *job_id_str)
{
	int rc;
	uint32_t job_id;

	job_id = atoi(job_id_str);
	if (job_id <= 0) {
		fprintf(stderr, "Invalid job_id %s", job_id_str);
		return SLURM_ERROR;
	}

	if (cluster_flags & CLUSTER_FLAG_BG) {
		resource_allocation_response_msg_t *alloc;
		rc = slurm_allocation_lookup_lite(job_id, &alloc);
		if (rc == SLURM_SUCCESS) {
			rc = _wait_bluegene_block_ready(alloc);
			slurm_free_resource_allocation_response_msg(alloc);
		} else {
			error("slurm_allocation_lookup_lite: %m");
			rc = SLURM_ERROR;
		}
	} else
		rc = _wait_nodes_ready(job_id);

	return rc;
}
