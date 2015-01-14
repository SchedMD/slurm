/*
 * job.c - convert data between job (step) related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include <slurm/slurm.h>
#include "ppport.h"

#include "slurm-perl.h"

/*
 * convert job_info_t to perl HV
 */
int
job_info_to_hv(job_info_t *job_info, HV *hv)
{
	int j;
	AV *av;

	if(job_info->account)
		STORE_FIELD(hv, job_info, account, charp);
	if(job_info->alloc_node)
		STORE_FIELD(hv, job_info, alloc_node, charp);
	STORE_FIELD(hv, job_info, alloc_sid, uint32_t);
	STORE_FIELD(hv, job_info, array_job_id, uint32_t);
	STORE_FIELD(hv, job_info, array_task_id, uint32_t);
	if(job_info->array_task_str)
		STORE_FIELD(hv, job_info, array_task_str, charp);
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
	STORE_FIELD(hv, job_info, derived_ec, uint32_t);
	STORE_FIELD(hv, job_info, eligible_time, time_t);
	STORE_FIELD(hv, job_info, end_time, time_t);
	if(job_info->exc_nodes)
		STORE_FIELD(hv, job_info, exc_nodes, charp);
	av = newAV();
	for(j = 0; ; j += 2) {
		if(job_info->exc_node_inx[j] == -1)
			break;
		av_store(av, j, newSVuv(job_info->exc_node_inx[j]));
		av_store(av, j+1, newSVuv(job_info->exc_node_inx[j+1]));
	}
	hv_store_sv(hv, "exc_node_inx", newRV_noinc((SV*)av));

	STORE_FIELD(hv, job_info, exit_code, uint32_t);
	if(job_info->features)
		STORE_FIELD(hv, job_info, features, charp);
	if(job_info->gres)
		STORE_FIELD(hv, job_info, gres, charp);
	STORE_FIELD(hv, job_info, group_id, uint32_t);
	STORE_FIELD(hv, job_info, job_id, uint32_t);
	STORE_FIELD(hv, job_info, job_state, uint16_t);
	if(job_info->licenses)
		STORE_FIELD(hv, job_info, licenses, charp);
	STORE_FIELD(hv, job_info, max_cpus, uint32_t);
	STORE_FIELD(hv, job_info, max_nodes, uint32_t);
	STORE_FIELD(hv, job_info, profile, uint32_t);
	STORE_FIELD(hv, job_info, sockets_per_node, uint16_t);
	STORE_FIELD(hv, job_info, cores_per_socket, uint16_t);
	STORE_FIELD(hv, job_info, threads_per_core, uint16_t);
	if(job_info->name)
		STORE_FIELD(hv, job_info, name, charp);
	if(job_info->network)
		STORE_FIELD(hv, job_info, network, charp);
	STORE_FIELD(hv, job_info, nice, uint16_t);
	if(job_info->nodes)
		STORE_FIELD(hv, job_info, nodes, charp);
	av = newAV();
	for(j = 0; ; j += 2) {
		if(job_info->node_inx[j] == -1)
			break;
		av_store(av, j, newSVuv(job_info->node_inx[j]));
		av_store(av, j+1, newSVuv(job_info->node_inx[j+1]));
	}
	hv_store_sv(hv, "node_inx", newRV_noinc((SV*)av));
	STORE_FIELD(hv, job_info, ntasks_per_core, uint16_t);
	STORE_FIELD(hv, job_info, ntasks_per_node, uint16_t);
	STORE_FIELD(hv, job_info, ntasks_per_socket, uint16_t);
#ifdef HAVE_BG
	slurm_get_select_jobinfo(job_info->select_jobinfo,
				 SELECT_JOBDATA_NODE_CNT,
				 &job_info->num_nodes);

#endif
	STORE_FIELD(hv, job_info, num_nodes, uint32_t);
	STORE_FIELD(hv, job_info, num_cpus, uint32_t);
	STORE_FIELD(hv, job_info, pn_min_memory, uint32_t);
	STORE_FIELD(hv, job_info, pn_min_cpus, uint16_t);
	STORE_FIELD(hv, job_info, pn_min_tmp_disk, uint32_t);

	if(job_info->partition)
		STORE_FIELD(hv, job_info, partition, charp);
	STORE_FIELD(hv, job_info, pre_sus_time, time_t);
	STORE_FIELD(hv, job_info, priority, uint32_t);
	if(job_info->qos)
		STORE_FIELD(hv, job_info, qos, charp);
	if(job_info->req_nodes)
		STORE_FIELD(hv, job_info, req_nodes, charp);
	av = newAV();
	for(j = 0; ; j += 2) {
		if(job_info->req_node_inx[j] == -1)
			break;
		av_store(av, j, newSVuv(job_info->req_node_inx[j]));
		av_store(av, j+1, newSVuv(job_info->req_node_inx[j+1]));
	}
	hv_store_sv(hv, "req_node_inx", newRV_noinc((SV*)av));
	STORE_FIELD(hv, job_info, req_switch, uint32_t);
	STORE_FIELD(hv, job_info, requeue, uint16_t);
	STORE_FIELD(hv, job_info, resize_time, time_t);
	STORE_FIELD(hv, job_info, restart_cnt, uint16_t);
	if(job_info->resv_name)
		STORE_FIELD(hv, job_info, resv_name, charp);
	STORE_PTR_FIELD(hv, job_info, select_jobinfo, "Slurm::dynamic_plugin_data_t");
	STORE_PTR_FIELD(hv, job_info, job_resrcs, "Slurm::job_resources_t");
	STORE_FIELD(hv, job_info, shared, uint16_t);
	STORE_FIELD(hv, job_info, show_flags, uint16_t);
	STORE_FIELD(hv, job_info, start_time, time_t);
	if(job_info->state_desc)
		STORE_FIELD(hv, job_info, state_desc, charp);
	STORE_FIELD(hv, job_info, state_reason, uint16_t);
	if(job_info->std_in)
		STORE_FIELD(hv, job_info, std_in, charp);
	if(job_info->std_out)
		STORE_FIELD(hv, job_info, std_out, charp);
	if(job_info->std_err)
		STORE_FIELD(hv, job_info, std_err, charp);
	STORE_FIELD(hv, job_info, submit_time, time_t);
	STORE_FIELD(hv, job_info, suspend_time, time_t);
	STORE_FIELD(hv, job_info, time_limit, uint32_t);
	STORE_FIELD(hv, job_info, time_min, uint32_t);
	STORE_FIELD(hv, job_info, user_id, uint32_t);
	STORE_FIELD(hv, job_info, wait4switch, uint32_t);
	if(job_info->wckey)
		STORE_FIELD(hv, job_info, wckey, charp);
	if(job_info->work_dir)
		STORE_FIELD(hv, job_info, work_dir, charp);

	return 0;
}

/*
 * convert perl HV to job_info_t
 */
int
hv_to_job_info(HV *hv, job_info_t *job_info)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(job_info, 0, sizeof(job_info_t));

	FETCH_FIELD(hv, job_info, account, charp, FALSE);
	FETCH_FIELD(hv, job_info, alloc_node, charp, FALSE);
	FETCH_FIELD(hv, job_info, alloc_sid, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, array_job_id, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, array_task_id, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, array_task_str, charp, FALSE);
	FETCH_FIELD(hv, job_info, batch_flag, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, command, charp, FALSE);
	FETCH_FIELD(hv, job_info, comment, charp, FALSE);
	FETCH_FIELD(hv, job_info, contiguous, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, cpus_per_task, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, dependency, charp, FALSE);
	FETCH_FIELD(hv, job_info, derived_ec, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, eligible_time, time_t, TRUE);
	FETCH_FIELD(hv, job_info, end_time, time_t, TRUE);
	FETCH_FIELD(hv, job_info, exc_nodes, charp, FALSE);
	svp = hv_fetch(hv, "exc_node_inx", 12, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		job_info->exc_node_inx = xmalloc(n * sizeof(int));
		for (i = 0; i < n-1; i += 2) {
			job_info->exc_node_inx[i] = (int)SvIV(*(av_fetch(av, i, FALSE)));
			job_info->exc_node_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1, FALSE)));
		}
		job_info->exc_node_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}
	FETCH_FIELD(hv, job_info, exit_code, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, features, charp, FALSE);
	FETCH_FIELD(hv, job_info, gres, charp, FALSE);
	FETCH_FIELD(hv, job_info, group_id, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, job_id, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, job_state, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, licenses, charp, FALSE);
	FETCH_FIELD(hv, job_info, max_cpus, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, max_nodes, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, profile, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, sockets_per_node, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, cores_per_socket, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, threads_per_core, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, name, charp, FALSE);
	FETCH_FIELD(hv, job_info, network, charp, FALSE);
	FETCH_FIELD(hv, job_info, nice, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, nodes, charp, FALSE);
	svp = hv_fetch(hv, "node_inx", 8, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		job_info->node_inx = xmalloc(n * sizeof(int));
		for (i = 0; i < n-1; i += 2) {
			job_info->node_inx[i] = (int)SvIV(*(av_fetch(av, i, FALSE)));
			job_info->node_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1, FALSE)));
		}
		job_info->node_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}
	FETCH_FIELD(hv, job_info, ntasks_per_core, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, ntasks_per_node, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, ntasks_per_socket, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, num_nodes, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, num_cpus, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, pn_min_memory, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, pn_min_cpus, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, pn_min_tmp_disk, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, partition, charp, FALSE);
	FETCH_FIELD(hv, job_info, pre_sus_time, time_t, TRUE);
	FETCH_FIELD(hv, job_info, priority, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, qos, charp, FALSE);
	FETCH_FIELD(hv, job_info, req_nodes, charp, FALSE);
	svp = hv_fetch(hv, "req_node_inx", 12, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		job_info->req_node_inx = xmalloc(n * sizeof(int));
		for (i = 0; i < n-1; i += 2) {
			job_info->req_node_inx[i] = (int)SvIV(*(av_fetch(av, i, FALSE)));
			job_info->req_node_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1, FALSE)));
		}
		job_info->req_node_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}
	FETCH_FIELD(hv, job_info, req_switch, uint32_t, FALSE);
	FETCH_FIELD(hv, job_info, requeue, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, resize_time, time_t, TRUE);
	FETCH_FIELD(hv, job_info, restart_cnt, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, resv_name, charp, FALSE);
	FETCH_PTR_FIELD(hv, job_info, select_jobinfo, "Slurm::dynamic_plugin_data_t", FALSE);
	FETCH_PTR_FIELD(hv, job_info, job_resrcs, "Slurm::job_resources_t", FALSE);
	FETCH_FIELD(hv, job_info, shared, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, show_flags, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, start_time, time_t, TRUE);
	FETCH_FIELD(hv, job_info, state_desc, charp, FALSE);
	FETCH_FIELD(hv, job_info, state_reason, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, std_in, charp, FALSE);
	FETCH_FIELD(hv, job_info, std_out, charp, FALSE);
	FETCH_FIELD(hv, job_info, std_err, charp, FALSE);
	FETCH_FIELD(hv, job_info, submit_time, time_t, TRUE);
	FETCH_FIELD(hv, job_info, suspend_time, time_t, TRUE);
	FETCH_FIELD(hv, job_info, time_limit, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, time_min, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, user_id, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, wait4switch, uint32_t, FALSE);
	FETCH_FIELD(hv, job_info, wckey, charp, FALSE);
	FETCH_FIELD(hv, job_info, work_dir, charp, FALSE);
	return 0;
}

/*
 * convert job_info_msg_t to perl HV
 */
int
job_info_msg_to_hv(job_info_msg_t *job_info_msg, HV *hv)
{
	int i;
	HV *hv_info;
	AV *av;

	STORE_FIELD(hv, job_info_msg, last_update, time_t);
	/* record_count implied in job_array */
	av = newAV();
	for(i = 0; i < job_info_msg->record_count; i ++) {
		hv_info = newHV();
		if (job_info_to_hv(job_info_msg->job_array + i, hv_info) < 0) {
			SvREFCNT_dec(hv_info);
			SvREFCNT_dec(av);
			return -1;
		}
		av_store(av, i, newRV_noinc((SV*)hv_info));
	}
	hv_store_sv(hv, "job_array", newRV_noinc((SV*)av));
	return 0;
}

/* 
 * convert perl HV to job_info_msg_t
 */
int
hv_to_job_info_msg(HV *hv, job_info_msg_t *job_info_msg)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(job_info_msg, 0, sizeof(job_info_msg_t));

	FETCH_FIELD(hv, job_info_msg, last_update, time_t, TRUE);
	svp = hv_fetch(hv, "job_array", 9, FALSE);
	if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV)) {
		Perl_warn (aTHX_ "job_array is not an arrary reference in HV for job_info_msg_t");
		return -1;
	}
	av = (AV*)SvRV(*svp);
	n = av_len(av) + 1;
	job_info_msg->record_count = n;

	job_info_msg->job_array = xmalloc(n * sizeof(job_info_t));
	for(i = 0; i < n; i ++) {
		svp = av_fetch(av, i, FALSE);
		if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV)) {
			Perl_warn (aTHX_ "element %d in job_array is not valid", i);
			return -1;
		}
		if (hv_to_job_info((HV*)SvRV(*svp), &job_info_msg->job_array[i]) < 0) {
			Perl_warn(aTHX_ "failed to convert element %d in job_array", i);
			return -1;
		}
	}
	return 0;
}
