/*****************************************************************************\
 *  sstat.c - job accounting reports for SLURM's jobacct/log plugin
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
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

#include "sstat.h"

void _destroy_steps(void *object);
void _print_header(void);
void *_stat_thread(void *args);
int _sstat_query(slurm_step_layout_t *step_layout, uint32_t job_id, 
		 uint32_t step_id);
int _process_results();
int _do_stat(uint32_t jobid, uint32_t stepid);

/*
 * Globals
 */
sstat_parameters_t params;
print_field_t fields[] = {
	{10, "AveCPU", print_fields_str, PRINT_AVECPU}, 
	{10, "AvePages", print_fields_str, PRINT_AVEPAGES}, 
	{10, "AveRSS", print_fields_str, PRINT_AVERSS}, 
	{10, "AveVMSize", print_fields_str, PRINT_AVEVSIZE}, 
	{10, "JobID", print_fields_str, PRINT_JOBID}, 
	{8, "MaxPages", print_fields_str, PRINT_MAXPAGES}, 
	{12, "MaxPagesNode", print_fields_str, PRINT_MAXPAGESNODE}, 
	{14, "MaxPagesTask", print_fields_int, PRINT_MAXPAGESTASK}, 
	{10, "MaxRSS", print_fields_str, PRINT_MAXRSS},
	{10, "MaxRSSNode", print_fields_str, PRINT_MAXRSSNODE},
	{10, "MaxRSSTask", print_fields_int, PRINT_MAXRSSTASK},
	{10, "MaxVMSize", print_fields_str, PRINT_MAXVSIZE}, 
	{14, "MaxVMSizeNode", print_fields_str, PRINT_MAXVSIZENODE}, 
	{14, "MaxVMSizeTask", print_fields_int, PRINT_MAXVSIZETASK}, 
	{10, "MinCPU", print_fields_str, PRINT_MINCPU}, 
	{10, "MinCPUNode", print_fields_str, PRINT_MINCPUNODE}, 
	{10, "MinCPUTask", print_fields_int, PRINT_MINCPUTASK}, 
	{8, "NTasks", print_fields_int, PRINT_NTASKS},
	{10, "SystemCPU", print_fields_str, PRINT_SYSTEMCPU}, 
	{10, "TotalCPU", print_fields_str, PRINT_TOTALCPU}, 
	{0, NULL, NULL, 0}};

List jobs = NULL;
jobacct_job_rec_t job;
jobacct_step_rec_t step;
List print_fields_list = NULL;
ListIterator print_fields_itr = NULL;
int field_count = 0;

int _sstat_query(slurm_step_layout_t *step_layout, uint32_t job_id,
		 uint32_t step_id)
{
	slurm_msg_t msg;
	stat_jobacct_msg_t r;
	stat_jobacct_msg_t *jobacct_msg = NULL;
	ListIterator itr;
	List ret_list = NULL;
	sacct_t temp_sacct;
	ret_data_info_t *ret_data_info = NULL;
	int rc = SLURM_SUCCESS;
	int ntasks = 0;
	int tot_tasks = 0;
	debug("getting the stat of job %d on %d nodes", 
	      job_id, step_layout->node_cnt);

	memset(&job, 0, sizeof(jobacct_job_rec_t));
	job.jobid = job_id;

	memset(&step, 0, sizeof(jobacct_step_rec_t));
	
	memset(&temp_sacct, 0, sizeof(sacct_t));
	temp_sacct.min_cpu = (float)NO_VAL;
	memset(&step.sacct, 0, sizeof(sacct_t));
	step.sacct.min_cpu = (float)NO_VAL;

	step.job_ptr = &job;
	step.stepid = step_id;
	step.nodes = step_layout->node_list;
	step.stepname = NULL;
	step.state = JOB_RUNNING;
	slurm_msg_t_init(&msg);
	/* Common message contents */
	r.job_id      = job_id;
	r.step_id     = step_id;
	r.jobacct     = jobacct_gather_g_create(NULL);
	msg.msg_type        = MESSAGE_STAT_JOBACCT;
	msg.data            = &r;
	
	ret_list = slurm_send_recv_msgs(step_layout->node_list, &msg, 0, false);
	if (!ret_list) {
		error("got an error no list returned");
		goto cleanup;
	}
	
	itr = list_iterator_create(ret_list);		
	while((ret_data_info = list_next(itr))) {
		switch (ret_data_info->type) {
		case MESSAGE_STAT_JOBACCT:
			jobacct_msg = (stat_jobacct_msg_t *)
				ret_data_info->data;
			if(jobacct_msg) {
				debug2("got it back for job %d", 
				       jobacct_msg->job_id);
				jobacct_gather_g_2_sacct(
					&temp_sacct, 
					jobacct_msg->jobacct);
				ntasks += jobacct_msg->num_tasks;
				aggregate_sacct(&step.sacct, &temp_sacct);
			}
			break;
		case RESPONSE_SLURM_RC:
			rc = slurm_get_return_code(ret_data_info->type, 
						   ret_data_info->data);
			error("there was an error with the request rc = %s", 
			      slurm_strerror(rc));
			break;
		default:
			rc = slurm_get_return_code(ret_data_info->type, 
						   ret_data_info->data);
			error("unknown return given %d rc = %s", 
			      ret_data_info->type, slurm_strerror(rc));
			break;
		}
	}
	list_iterator_destroy(itr);
	list_destroy(ret_list);

	tot_tasks += ntasks;		
cleanup:
	
	if(tot_tasks) {
		step.sacct.ave_cpu /= tot_tasks;
		step.sacct.ave_cpu /= 100;
		step.sacct.min_cpu /= 100;
		step.sacct.ave_rss /= tot_tasks;
		step.sacct.ave_vsize /= tot_tasks;
		step.sacct.ave_pages /= tot_tasks;
		step.ntasks = tot_tasks;
	}
	jobacct_gather_g_destroy(r.jobacct);	
	return SLURM_SUCCESS;
}

int _do_stat(uint32_t jobid, uint32_t stepid)
{
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	job_step_id_msg_t req;
	slurm_step_layout_t *step_layout = NULL;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	debug("requesting info for job %u.%u", jobid, stepid);
	req.job_id = jobid;
	req.step_id = stepid;
	req_msg.msg_type = REQUEST_STEP_LAYOUT;
	req_msg.data     = &req;
	
	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0) {
		return SLURM_ERROR;
	}
		
	switch (resp_msg.msg_type) {
	case RESPONSE_STEP_LAYOUT:
		step_layout = (slurm_step_layout_t *)resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);	
		printf("problem getting job: %s\n", slurm_strerror(rc));
		slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}
		
	if(!step_layout) {
		error("didn't get the job record rc = %s", slurm_strerror(rc));
		return rc;
	}

	_sstat_query(step_layout, jobid, stepid);
	
	print_fields(&step);
	
	slurm_step_layout_destroy(step_layout);	
	
	return rc;
}

int main(int argc, char **argv)
{
	ListIterator itr = NULL;
	uint32_t stepid = 0;
	jobacct_selected_step_t *selected_step = NULL;
	
	print_fields_list = list_create(NULL);
	print_fields_itr = list_iterator_create(print_fields_list);

	parse_command_line(argc, argv);
	if(!params.opt_job_list || !list_count(params.opt_job_list)) {
		error("You didn't give me any jobs to stat.");
		return 1;
	}

	print_fields_header(print_fields_list);
	itr = list_iterator_create(params.opt_job_list);
	while((selected_step = list_next(itr))) {
		if(selected_step->stepid != NO_VAL)
			stepid = selected_step->stepid;
		else if(params.opt_all_steps) {
			job_step_info_response_msg_t *step_ptr = NULL;
			int i = 0;
			if(slurm_get_job_steps(
				   0, selected_step->jobid, 0, 
				   &step_ptr, SHOW_ALL)) {
				error("couldn't get steps for job %u",
				      selected_step->jobid);
				continue;
			}
			for (i = 0; i < step_ptr->job_step_count; i++) {
				_do_stat(selected_step->jobid, 
					 step_ptr->job_steps[i].step_id);
			}
			slurm_free_job_step_info_response_msg(step_ptr);
			continue;
		} else 
			stepid = 0;
		_do_stat(selected_step->jobid, stepid);
	}
	list_iterator_destroy(itr);

	xfree(params.opt_field_list);
	if(params.opt_job_list)	
		list_destroy(params.opt_job_list);

	if(print_fields_itr)
		list_iterator_destroy(print_fields_itr);
	if(print_fields_list)
		list_destroy(print_fields_list);

	return 0;
}


