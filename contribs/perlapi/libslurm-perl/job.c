/*
 * job_info.c - convert data between job (step) related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include <slurm/slurm.h>

#include "msg.h"

/*
 * convert job_info_t to perl HV
 */
int
job_info_to_hv(job_info_t* job_info, HV* hv)
{
	int j;
	AV* avp;

	if(job_info->account)
		STORE_FIELD(hv, job_info, account, charp);
	if(job_info->alloc_node)
		STORE_FIELD(hv, job_info, alloc_node, charp);
	STORE_FIELD(hv, job_info, alloc_sid, uint32_t);
	STORE_FIELD(hv, job_info, assoc_id, uint32_t);
	STORE_FIELD(hv, job_info, batch_flag, uint16_t);
	if(job_info->command)
		STORE_FIELD(hv, job_info, command, charp);
	if(job_info->comment)
		STORE_FIELD(hv, job_info, comment, charp);
	STORE_FIELD(hv, job_info, contiguous, uint16_t);
	STORE_FIELD(hv, job_info, cpus_per_task, uint16_t);
	if(job_info->dependency)
		STORE_FIELD(hv, job_info, dependency, charp);
	STORE_FIELD(hv, job_info, end_time, time_t);
	if(job_info->exc_nodes)
		STORE_FIELD(hv, job_info, exc_nodes, charp);
	avp = newAV();
	for(j = 0; ; j += 2) {
		if(job_info->exc_node_inx[j] == -1)
			break;
		av_store(avp, j, newSVuv(job_info->exc_node_inx[j]));
		av_store(avp, j+1, newSVuv(job_info->exc_node_inx[j+1]));
	}
	hv_store_sv(hv, "exc_node_inx", newRV_noinc((SV*)avp));

	STORE_FIELD(hv, job_info, exit_code, uint32_t);
	if(job_info->features)
		STORE_FIELD(hv, job_info, features, charp);
	STORE_FIELD(hv, job_info, group_id, uint32_t);
	STORE_FIELD(hv, job_info, job_id, uint32_t);
	STORE_FIELD(hv, job_info, job_min_memory, uint32_t);
	STORE_FIELD(hv, job_info, job_min_cpus, uint16_t);
	STORE_FIELD(hv, job_info, job_min_tmp_disk, uint32_t);
	STORE_FIELD(hv, job_info, job_state, uint16_t);
	if(job_info->licenses)
		STORE_FIELD(hv, job_info, licenses, charp);
	STORE_FIELD(hv, job_info, max_nodes, uint32_t);
	STORE_FIELD(hv, job_info, min_cores, uint16_t);
	STORE_FIELD(hv, job_info, min_sockets, uint16_t);
	STORE_FIELD(hv, job_info, min_threads, uint16_t);
	if(job_info->name)
		STORE_FIELD(hv, job_info, name, charp);
	if(job_info->network)
		STORE_FIELD(hv, job_info, network, charp);
	if(job_info->nodes)
		STORE_FIELD(hv, job_info, nodes, charp);
	avp = newAV();
	for(j = 0; ; j += 2) {
		if(job_info->node_inx[j] == -1)
			break;
		av_store(avp, j, newSVuv(job_info->node_inx[j]));
		av_store(avp, j+1, newSVuv(job_info->node_inx[j+1]));
	}
	hv_store_sv(hv, "node_inx", newRV_noinc((SV*)avp));
	STORE_FIELD(hv, job_info, ntasks_per_core, uint16_t);
	STORE_FIELD(hv, job_info, ntasks_per_node, uint16_t);
	STORE_FIELD(hv, job_info, ntasks_per_socket, uint16_t);
	STORE_FIELD(hv, job_info, num_nodes, uint32_t);
	STORE_FIELD(hv, job_info, num_procs, uint32_t);
	if(job_info->partition)
		STORE_FIELD(hv, job_info, partition, charp);
	STORE_FIELD(hv, job_info, pre_sus_time, time_t);
	STORE_FIELD(hv, job_info, priority, uint32_t);
	if(job_info->req_nodes)
		STORE_FIELD(hv, job_info, req_nodes, charp);
	avp = newAV();
	for(j = 0; ; j += 2) {
		if(job_info->req_node_inx[j] == -1)
			break;
		av_store(avp, j, newSVuv(job_info->req_node_inx[j]));
		av_store(avp, j+1, newSVuv(job_info->req_node_inx[j+1]));
	}
	hv_store_sv(hv, "req_node_inx", newRV_noinc((SV*)avp));
	STORE_FIELD(hv, job_info, requeue, uint16_t);
	STORE_FIELD(hv, job_info, restart_cnt, uint16_t);
	if(job_info->resv_name)
		STORE_FIELD(hv, job_info, resv_name, charp);
	/* TODO: select_jobinfo */
	/* TODO: select_job_res */
	STORE_FIELD(hv, job_info, shared, uint16_t);
	STORE_FIELD(hv, job_info, start_time, time_t);
	if(job_info->state_desc)
		STORE_FIELD(hv, job_info, state_desc, charp);
	STORE_FIELD(hv, job_info, state_reason, uint16_t);
	STORE_FIELD(hv, job_info, submit_time, time_t);
	STORE_FIELD(hv, job_info, suspend_time, time_t);
	STORE_FIELD(hv, job_info, time_limit, uint32_t);
	STORE_FIELD(hv, job_info, user_id, uint32_t);
	if(job_info->wckey)
		STORE_FIELD(hv, job_info, wckey, charp);
	if(job_info->work_dir)
		STORE_FIELD(hv, job_info, work_dir, charp);
			
	return 0;
}

/*
 * convert job_info_msg_t to perl HV
 */
int
job_info_msg_to_hv(job_info_msg_t* job_info_msg, HV* hv)
{
	int i;
	HV* hvp;
	AV* avp;

	STORE_FIELD(hv, job_info_msg, last_update, time_t);
	/* record_count implied in job_array */
	avp = newAV();
	for(i = 0; i < job_info_msg->record_count; i ++) {
		hvp = newHV();
		if (job_info_to_hv(job_info_msg->job_array + i, hvp) < 0) {
			SvREFCNT_dec(hvp);
			SvREFCNT_dec(avp);
			return -1;
		}
		av_store(avp, i, newRV_noinc((SV*)hvp));
	}
	hv_store_sv(hv, "job_array", newRV_noinc((SV*)avp));
	return 0;
}

/*
 * convert job_step_info_t to perl HV
 */
int
job_step_info_to_hv(job_step_info_t* step_info, HV* hv)
{
	int j;
	AV* avp;

	if(step_info->ckpt_dir)
		STORE_FIELD(hv, step_info, ckpt_dir, charp);
	STORE_FIELD(hv, step_info, ckpt_interval, uint16_t);
	STORE_FIELD(hv, step_info, job_id, uint32_t);
	if(step_info->name)
		STORE_FIELD(hv, step_info, name, charp);
	if(step_info->network)
		STORE_FIELD(hv, step_info, network, charp);
	if(step_info->nodes)
		STORE_FIELD(hv, step_info, nodes, charp);
	avp = newAV();
	for(j = 0; ; j += 2) {
		if(step_info->node_inx[j] == -1)
			break;
		av_store(avp, j, newSVuv(step_info->node_inx[j]));
		av_store(avp, j+1, newSVuv(step_info->node_inx[j+1]));
	}
	hv_store_sv(hv, "node_inx", newRV_noinc((SV*)avp));
	STORE_FIELD(hv, step_info, num_tasks, uint32_t);
	if(step_info->partition)
		STORE_FIELD(hv, step_info, partition, charp);
	if(step_info->resv_ports)
		STORE_FIELD(hv, step_info, resv_ports, charp);
	STORE_FIELD(hv, step_info, run_time, time_t);
	STORE_FIELD(hv, step_info, start_time, time_t);
	STORE_FIELD(hv, step_info, step_id, uint16_t);
	STORE_FIELD(hv, step_info, user_id, uint32_t);

	return 0;
}

/*
 * convert job_step_info_response_msg_t to perl HV
 */
int
job_step_info_response_msg_to_hv(job_step_info_response_msg_t* job_step_info_msg, HV* hv)
{
	int i;
	AV* avp;
	HV* hvp;
	
	STORE_FIELD(hv, job_step_info_msg, last_update, time_t);
	/* job_step_count implied in job_steps */
	avp = newAV();
	for(i = 0; i < job_step_info_msg->job_step_count; i ++) {
		hvp = newHV();
		if (job_step_info_to_hv(job_step_info_msg->job_steps + i, hvp) < 0) {
			SvREFCNT_dec(hvp);
			SvREFCNT_dec(avp);
			return -1;
		}
		av_store(avp, i, newRV_noinc((SV*)hvp));
	}
	hv_store_sv(hv, "job_steps", newRV_noinc((SV*)avp));
	return 0;
}

/*
 * convert slurm_step_layout_t to perl HV
 */
int
slurm_step_layout_to_hv(slurm_step_layout_t* step_layout, HV* hv)
{
	AV* avp, *avp2;
	int i, j;

	STORE_FIELD(hv, step_layout, node_cnt, uint16_t);
	if (step_layout->node_list)
		STORE_FIELD(hv, step_layout, node_list, charp);
	else {
		Perl_warn(aTHX_ "node_list missing in slurm_step_layout_t");
		return -1;
	}
	STORE_FIELD(hv, step_layout, plane_size, uint16_t);
	avp = newAV();
	for(i = 0; i < step_layout->node_cnt; i ++)
		av_store(avp, i, newSVuv(step_layout->tasks[i]));
	hv_store_sv(hv, "tasks", newRV_noinc((SV*)avp));
	STORE_FIELD(hv, step_layout, task_cnt, uint32_t);
	STORE_FIELD(hv, step_layout, task_dist, uint16_t);
	avp = newAV();
	for(i = 0; i < step_layout->node_cnt; i ++) {
		avp2 = newAV();
		for(j = 0; j < step_layout->tasks[i]; j ++) 
			av_store(avp2, i, newSVuv(step_layout->tids[i][j]));
		av_store(avp, i, newRV_noinc((SV*)avp2));
	}
	hv_store_sv(hv, "tids", newRV_noinc((SV*)avp));
	
	return 0;
}
