/*****************************************************************************\
 *  sstat.c - job accounting reports for Slurm's slurmdb/log plugin
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "config.h"

#include "sstat.h"

int _do_stat(uint32_t jobid, uint32_t stepid, char *nodelist,
	     uint32_t req_cpufreq_min, uint32_t req_cpufreq_max,
	     uint32_t req_cpufreq_gov,
	     uint16_t use_protocol_ver);

/*
 * Globals
 */
sstat_parameters_t params;
print_field_t fields[] = {
	{10, "AveCPU", print_fields_str, PRINT_AVECPU},
	{10, "AveCPUFreq", print_fields_str, PRINT_ACT_CPUFREQ},
	{12, "AveDiskRead", print_fields_str, PRINT_AVEDISKREAD},
	{12, "AveDiskWrite", print_fields_str, PRINT_AVEDISKWRITE},
	{10, "AvePages", print_fields_str, PRINT_AVEPAGES},
	{10, "AveRSS", print_fields_str, PRINT_AVERSS},
	{10, "AveVMSize", print_fields_str, PRINT_AVEVSIZE},
	{14, "ConsumedEnergy", print_fields_str, PRINT_CONSUMED_ENERGY},
	{17, "ConsumedEnergyRaw", print_fields_uint64,
	 PRINT_CONSUMED_ENERGY_RAW},
	{-12, "JobID", print_fields_str, PRINT_JOBID},
	{12, "MaxDiskRead", print_fields_str, PRINT_MAXDISKREAD},
	{15, "MaxDiskReadNode", print_fields_str, PRINT_MAXDISKREADNODE},
	{15, "MaxDiskReadTask", print_fields_uint, PRINT_MAXDISKREADTASK},
	{12, "MaxDiskWrite", print_fields_str, PRINT_MAXDISKWRITE},
	{16, "MaxDiskWriteNode", print_fields_str, PRINT_MAXDISKWRITENODE},
	{16, "MaxDiskWriteTask", print_fields_uint, PRINT_MAXDISKWRITETASK},
	{8, "MaxPages", print_fields_str, PRINT_MAXPAGES},
	{12, "MaxPagesNode", print_fields_str, PRINT_MAXPAGESNODE},
	{14, "MaxPagesTask", print_fields_uint, PRINT_MAXPAGESTASK},
	{10, "MaxRSS", print_fields_str, PRINT_MAXRSS},
	{10, "MaxRSSNode", print_fields_str, PRINT_MAXRSSNODE},
	{10, "MaxRSSTask", print_fields_uint, PRINT_MAXRSSTASK},
	{10, "MaxVMSize", print_fields_str, PRINT_MAXVSIZE},
	{14, "MaxVMSizeNode", print_fields_str, PRINT_MAXVSIZENODE},
	{14, "MaxVMSizeTask", print_fields_uint, PRINT_MAXVSIZETASK},
	{10, "MinCPU", print_fields_str, PRINT_MINCPU},
	{10, "MinCPUNode", print_fields_str, PRINT_MINCPUNODE},
	{10, "MinCPUTask", print_fields_uint, PRINT_MINCPUTASK},
	{20, "Nodelist", print_fields_str, PRINT_NODELIST},
	{8, "NTasks", print_fields_uint, PRINT_NTASKS},
	{20, "Pids", print_fields_str, PRINT_PIDS},
	{10, "ReqCPUFreq", print_fields_str, PRINT_REQ_CPUFREQ_MIN}, /*vestigial*/
	{13, "ReqCPUFreqMin", print_fields_str, PRINT_REQ_CPUFREQ_MIN},
	{13, "ReqCPUFreqMax", print_fields_str, PRINT_REQ_CPUFREQ_MAX},
	{13, "ReqCPUFreqGov", print_fields_str, PRINT_REQ_CPUFREQ_GOV},
	{14, "TRESUsageInAve", print_fields_str, PRINT_TRESUIA},
	{14, "TRESUsageInMax", print_fields_str, PRINT_TRESUIM},
	{18, "TRESUsageInMaxNode", print_fields_str, PRINT_TRESUIMN},
	{18, "TRESUsageInMaxTask", print_fields_str, PRINT_TRESUIMT},
	{14, "TRESUsageInMin", print_fields_str, PRINT_TRESUIMI},
	{18, "TRESUsageInMinNode", print_fields_str, PRINT_TRESUIMIN},
	{18, "TRESUsageInMinTask", print_fields_str, PRINT_TRESUIMIT},
	{14, "TRESUsageInTot", print_fields_str, PRINT_TRESUIT},
	{15, "TRESUsageOutAve", print_fields_str, PRINT_TRESUOA},
	{15, "TRESUsageOutMax", print_fields_str, PRINT_TRESUOM},
	{19, "TRESUsageOutMaxNode", print_fields_str, PRINT_TRESUOMN},
	{19, "TRESUsageOutMaxTask", print_fields_str, PRINT_TRESUOMT},
	{15, "TRESUsageOutMin", print_fields_str, PRINT_TRESUOMI},
	{19, "TRESUsageOutMinNode", print_fields_str, PRINT_TRESUOMIN},
	{19, "TRESUsageOutMinTask", print_fields_str, PRINT_TRESUOMIT},
	{15, "TRESUsageOutTot", print_fields_str, PRINT_TRESUOT},
	{0, NULL, NULL, 0}};

List jobs = NULL;
slurmdb_job_rec_t job;
slurmdb_step_rec_t step;
List print_fields_list = NULL;
ListIterator print_fields_itr = NULL;
int field_count = 0;

int _do_stat(uint32_t jobid, uint32_t stepid, char *nodelist,
	     uint32_t req_cpufreq_min, uint32_t req_cpufreq_max,
	     uint32_t req_cpufreq_gov, uint16_t use_protocol_ver)
{
	job_step_stat_response_msg_t *step_stat_response = NULL;
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	jobacctinfo_t *total_jobacct = NULL;
	job_step_stat_t *step_stat = NULL;
	int ntasks = 0;
	int tot_tasks = 0;
	hostlist_t hl = NULL;
	char *ave_usage_tmp = NULL;

	debug("requesting info for job %u.%u", jobid, stepid);
	if ((rc = slurm_job_step_stat(jobid, stepid, nodelist, use_protocol_ver,
				      &step_stat_response)) != SLURM_SUCCESS) {
		if (rc == ESLURM_INVALID_JOB_ID) {
			debug("job step %u.%u has already completed",
			      jobid, stepid);
		} else {
			error("problem getting step_layout for %u.%u: %s",
			      jobid, stepid, slurm_strerror(rc));
		}
		slurm_job_step_pids_response_msg_free(step_stat_response);
		return rc;
	}

	memset(&job, 0, sizeof(slurmdb_job_rec_t));
	job.jobid = jobid;

	memset(&step, 0, sizeof(slurmdb_step_rec_t));

	memset(&step.stats, 0, sizeof(slurmdb_stats_t));

	step.job_ptr = &job;
	step.stepid = stepid;
	step.nodes = xmalloc(BUF_SIZE);
	step.req_cpufreq_min = req_cpufreq_min;
	step.req_cpufreq_max = req_cpufreq_max;
	step.req_cpufreq_gov = req_cpufreq_gov;
	step.stepname = NULL;
	step.state = JOB_RUNNING;
	hl = hostlist_create(NULL);
	itr = list_iterator_create(step_stat_response->stats_list);
	while ((step_stat = list_next(itr))) {
		if (!step_stat->step_pids || !step_stat->step_pids->node_name)
			continue;
		if (step_stat->step_pids->pid_cnt > 0 ) {
			int i;
			for(i=0; i<step_stat->step_pids->pid_cnt; i++) {
				if (step.pid_str)
					xstrcat(step.pid_str, ",");
				xstrfmtcat(step.pid_str, "%u",
					   step_stat->step_pids->pid[i]);
			}
		}

		if (params.pid_format) {
			step.nodes = step_stat->step_pids->node_name;
			print_fields(&step);
			xfree(step.pid_str);
		} else {
			hostlist_push_host(hl, step_stat->step_pids->node_name);
			ntasks += step_stat->num_tasks;
			if (step_stat->jobacct) {
				if (!assoc_mgr_tres_list &&
				    step_stat->jobacct->tres_list) {
					assoc_mgr_lock_t locks =
						{ .tres = WRITE_LOCK };
					assoc_mgr_lock(&locks);
					assoc_mgr_post_tres_list(
						step_stat->jobacct->tres_list);
					assoc_mgr_unlock(&locks);
					/*
					 * assoc_mgr_post_tres_list destroys the
					 * input list
					 */
					step_stat->jobacct->tres_list = NULL;
				}

				/*
				 * total_jobacct has to be created after
				 * assoc_mgr is set up.
				 */
				if (!total_jobacct)
					total_jobacct =
						jobacctinfo_create(NULL);

				jobacctinfo_aggregate(total_jobacct,
						      step_stat->jobacct);
			}
		}
	}
	list_iterator_destroy(itr);

	if (total_jobacct) {
		jobacctinfo_2_stats(&step.stats, total_jobacct);
		jobacctinfo_destroy(total_jobacct);
	}

	slurm_job_step_pids_response_msg_free(step_stat_response);
	/* we printed it out already */
	if (params.pid_format)
		goto getout;

	hostlist_sort(hl);
	hostlist_ranged_string(hl, BUF_SIZE, step.nodes);
	hostlist_destroy(hl);
	tot_tasks += ntasks;

	if (tot_tasks) {
		step.stats.act_cpufreq /= (double)tot_tasks;

		ave_usage_tmp = step.stats.tres_usage_in_ave;
		step.stats.tres_usage_in_ave = slurmdb_ave_tres_usage(
			ave_usage_tmp, tot_tasks);
		xfree(ave_usage_tmp);
		ave_usage_tmp = step.stats.tres_usage_out_ave;
		step.stats.tres_usage_out_ave = slurmdb_ave_tres_usage(
			ave_usage_tmp, tot_tasks);
		xfree(ave_usage_tmp);

		step.ntasks = tot_tasks;
	}

	print_fields(&step);

getout:

	xfree(step.stats.tres_usage_in_max);
	xfree(step.stats.tres_usage_out_max);
	xfree(step.stats.tres_usage_in_max_taskid);
	xfree(step.stats.tres_usage_out_max_taskid);
	xfree(step.stats.tres_usage_in_max_nodeid);
	xfree(step.stats.tres_usage_out_max_nodeid);
	xfree(step.stats.tres_usage_in_ave);
	xfree(step.stats.tres_usage_out_ave);

	return rc;
}

int main(int argc, char **argv)
{
	ListIterator itr = NULL;
	uint32_t req_cpufreq_min = NO_VAL;
	uint32_t req_cpufreq_max = NO_VAL;
	uint32_t req_cpufreq_gov = NO_VAL;
	uint32_t stepid = NO_VAL;
	slurmdb_selected_step_t *selected_step = NULL;

#ifdef HAVE_ALPS_CRAY
	error("The sstat command is not supported on Cray systems");
	return 1;
#endif

	slurm_conf_init(NULL);
	print_fields_list = list_create(NULL);
	print_fields_itr = list_iterator_create(print_fields_list);

	parse_command_line(argc, argv);
	if (!params.opt_job_list || !list_count(params.opt_job_list)) {
		error("You didn't give me any jobs to stat.");
		return 1;
	}

	print_fields_header(print_fields_list);
	itr = list_iterator_create(params.opt_job_list);
	while ((selected_step = list_next(itr))) {
		char *nodelist = NULL;
		bool free_nodelist = false;
		uint16_t use_protocol_ver = SLURM_PROTOCOL_VERSION;
		if (selected_step->stepid == SSTAT_BATCH_STEP) {
			/* get the batch step info */
			job_info_msg_t *job_ptr = NULL;
			hostlist_t hl;

			if (slurm_load_job(
				    &job_ptr, selected_step->jobid, SHOW_ALL)) {
				error("couldn't get info for job %u",
				      selected_step->jobid);
				continue;
			}

			use_protocol_ver = MIN(SLURM_PROTOCOL_VERSION,
				job_ptr->job_array[0].start_protocol_ver);
			stepid = SLURM_BATCH_SCRIPT;
			hl = hostlist_create(job_ptr->job_array[0].nodes);
			nodelist = hostlist_shift(hl);
			free_nodelist = true;
			hostlist_destroy(hl);
			slurm_free_job_info_msg(job_ptr);
		} else if (selected_step->stepid == SSTAT_EXTERN_STEP) {
			/* get the extern step info */
			job_info_msg_t *job_ptr = NULL;

			if (slurm_load_job(
				    &job_ptr, selected_step->jobid, SHOW_ALL)) {
				error("couldn't get info for job %u",
				      selected_step->jobid);
				continue;
			}
			use_protocol_ver = MIN(SLURM_PROTOCOL_VERSION,
				job_ptr->job_array[0].start_protocol_ver);
			stepid = SLURM_EXTERN_CONT;
			nodelist = job_ptr->job_array[0].nodes;
			slurm_free_job_info_msg(job_ptr);
		} else if (selected_step->stepid != NO_VAL) {
			stepid = selected_step->stepid;
		} else if (params.opt_all_steps) {
			job_step_info_response_msg_t *step_ptr = NULL;
			int i = 0;
			if (slurm_get_job_steps(
				    0, selected_step->jobid, NO_VAL,
				    &step_ptr, SHOW_ALL)) {
				error("couldn't get steps for job %u",
				      selected_step->jobid);
				continue;
			}

			for (i = 0; i < step_ptr->job_step_count; i++) {
				_do_stat(selected_step->jobid,
					 step_ptr->job_steps[i].step_id,
					 step_ptr->job_steps[i].nodes,
					 step_ptr->job_steps[i].cpu_freq_min,
					 step_ptr->job_steps[i].cpu_freq_max,
					 step_ptr->job_steps[i].cpu_freq_gov,
					 step_ptr->job_steps[i].
					 start_protocol_ver);
			}
			slurm_free_job_step_info_response_msg(step_ptr);
			continue;
		} else {
			/* get the first running step to query against. */
			job_step_info_response_msg_t *step_ptr = NULL;
			job_step_info_t *step_info;

			if (slurm_get_job_steps(
				    0, selected_step->jobid, NO_VAL,
				    &step_ptr, SHOW_ALL)) {
				error("couldn't get steps for job %u",
				      selected_step->jobid);
				continue;
			}
			if (!step_ptr->job_step_count) {
				error("no steps running for job %u",
				      selected_step->jobid);
				continue;
			}

			/* If the first step is the extern step lets
			 * just skip it.  They should ask for it
			 * directly.
			 */
			if ((step_ptr->job_steps[0].step_id ==
			    SLURM_EXTERN_CONT) && step_ptr->job_step_count > 1)
				step_info = ++step_ptr->job_steps;
			else
				step_info = step_ptr->job_steps;
			stepid = step_info->step_id;
			nodelist = step_info->nodes;
			req_cpufreq_min = step_info->cpu_freq_min;
			req_cpufreq_max = step_info->cpu_freq_max;
			req_cpufreq_gov = step_info->cpu_freq_gov;
			use_protocol_ver = MIN(SLURM_PROTOCOL_VERSION,
					       step_info->start_protocol_ver);
		}
		_do_stat(selected_step->jobid, stepid, nodelist,
			 req_cpufreq_min, req_cpufreq_max, req_cpufreq_gov,
			 use_protocol_ver);
		if (free_nodelist && nodelist)
			free(nodelist);
	}
	list_iterator_destroy(itr);

	xfree(params.opt_field_list);
	FREE_NULL_LIST(params.opt_job_list);
	if (print_fields_itr)
		list_iterator_destroy(print_fields_itr);
	FREE_NULL_LIST(print_fields_list);

	return 0;
}
