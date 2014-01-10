/*****************************************************************************\
 *  sstat.c - job accounting reports for SLURM's slurmdb/log plugin
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "sstat.h"

void _destroy_steps(void *object);
void _print_header(void);
void *_stat_thread(void *args);
int _sstat_query(slurm_step_layout_t *step_layout, uint32_t job_id,
		 uint32_t step_id);
int _process_results();
int _do_stat(uint32_t jobid, uint32_t stepid, char *nodelist,
	     uint32_t req_cpufreq);

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
	{17, "ConsumedEnergyRaw", print_fields_double,
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
	{10, "ReqCPUFreq", print_fields_str, PRINT_REQ_CPUFREQ},
	{0, NULL, NULL, 0}};

List jobs = NULL;
slurmdb_job_rec_t job;
slurmdb_step_rec_t step;
List print_fields_list = NULL;
ListIterator print_fields_itr = NULL;
int field_count = 0;

int _do_stat(uint32_t jobid, uint32_t stepid, char *nodelist,
	     uint32_t req_cpufreq)
{
	job_step_stat_response_msg_t *step_stat_response = NULL;
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	slurmdb_stats_t temp_stats;
	job_step_stat_t *step_stat = NULL;
	int ntasks = 0;
	int tot_tasks = 0;
	hostlist_t hl = NULL;

	debug("requesting info for job %u.%u", jobid, stepid);
	if ((rc = slurm_job_step_stat(jobid, stepid, nodelist,
				      &step_stat_response)) != SLURM_SUCCESS) {
		if (rc == ESLURM_INVALID_JOB_ID) {
			debug("job step %u.%u has already completed",
			      jobid, stepid);
		} else {
			error("problem getting step_layout for %u.%u: %s",
			      jobid, stepid, slurm_strerror(rc));
		}
		return rc;
	}

	memset(&job, 0, sizeof(slurmdb_job_rec_t));
	job.jobid = jobid;

	memset(&step, 0, sizeof(slurmdb_step_rec_t));

	memset(&temp_stats, 0, sizeof(slurmdb_stats_t));
	temp_stats.cpu_min = NO_VAL;
	memset(&step.stats, 0, sizeof(slurmdb_stats_t));
	step.stats.cpu_min = NO_VAL;

	step.job_ptr = &job;
	step.stepid = stepid;
	step.nodes = xmalloc(BUF_SIZE);
	step.req_cpufreq = req_cpufreq;
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
				jobacctinfo_2_stats(&temp_stats,
						    step_stat->jobacct);
				aggregate_stats(&step.stats, &temp_stats);
			}
		}
	}
	list_iterator_destroy(itr);
	slurm_job_step_pids_response_msg_free(step_stat_response);
	/* we printed it out already */
	if (params.pid_format)
		return rc;

	hostlist_sort(hl);
	hostlist_ranged_string(hl, BUF_SIZE, step.nodes);
	hostlist_destroy(hl);
	tot_tasks += ntasks;

	if (tot_tasks) {
		step.stats.cpu_ave /= (double)tot_tasks;
		step.stats.rss_ave /= (double)tot_tasks;
		step.stats.vsize_ave /= (double)tot_tasks;
		step.stats.pages_ave /= (double)tot_tasks;
		step.stats.disk_read_ave /= (double)tot_tasks;
		step.stats.disk_write_ave /= (double)tot_tasks;
		step.stats.act_cpufreq /= (double)tot_tasks;
		step.ntasks = tot_tasks;
	}

	print_fields(&step);

	return rc;
}

int main(int argc, char **argv)
{
	ListIterator itr = NULL;
	uint32_t req_cpufreq = NO_VAL;
	uint32_t stepid = NO_VAL;
	slurmdb_selected_step_t *selected_step = NULL;

#ifdef HAVE_ALPS_CRAY
	error("The sstat command is not supported on Cray systems");
	return 1;
#endif
#ifdef HAVE_BG
	error("The sstat command is not supported on IBM BlueGene systems");
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
		if (selected_step->stepid == INFINITE) {
			/* get the batch step info */
			job_info_msg_t *job_ptr = NULL;
			hostlist_t hl;

			if (slurm_load_job(
				    &job_ptr, selected_step->jobid, SHOW_ALL)) {
				error("couldn't get info for job %u",
				      selected_step->jobid);
				continue;
			}

			stepid = NO_VAL;
			hl = hostlist_create(job_ptr->job_array[0].nodes);
			nodelist = hostlist_pop(hl);
			free_nodelist = true;
			hostlist_destroy(hl);
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
					 step_ptr->job_steps[i].cpu_freq);
			}
			slurm_free_job_step_info_response_msg(step_ptr);
			continue;
		} else {
			/* get the first running step to query against. */
			job_step_info_response_msg_t *step_ptr = NULL;
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
			stepid = step_ptr->job_steps[0].step_id;
			nodelist = step_ptr->job_steps[0].nodes;
			req_cpufreq = step_ptr->job_steps[0].cpu_freq;
		}
		_do_stat(selected_step->jobid, stepid, nodelist, req_cpufreq);
		if (free_nodelist && nodelist)
			free(nodelist);
	}
	list_iterator_destroy(itr);

	xfree(params.opt_field_list);
	if (params.opt_job_list)
		list_destroy(params.opt_job_list);

	if (print_fields_itr)
		list_iterator_destroy(print_fields_itr);
	if (print_fields_list)
		list_destroy(print_fields_list);

	return 0;
}


