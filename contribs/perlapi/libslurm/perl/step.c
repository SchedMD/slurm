/*
 * step.c - convert data between step related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include <slurm/slurm.h>
#include "ppport.h"

#include "slurm-perl.h"


/*
 * convert job_step_info_t to perl HV
 */
int
job_step_info_to_hv(job_step_info_t *step_info, HV *hv)
{
	int j;
	AV *av;

	STORE_FIELD(hv, step_info, array_job_id, uint32_t);
	STORE_FIELD(hv, step_info, array_task_id, uint32_t);
	if(step_info->ckpt_dir)
		STORE_FIELD(hv, step_info, ckpt_dir, charp);
	STORE_FIELD(hv, step_info, ckpt_interval, uint16_t);
	if(step_info->gres)
		STORE_FIELD(hv, step_info, gres, charp);
	STORE_FIELD(hv, step_info, job_id, uint32_t);
	if(step_info->name)
		STORE_FIELD(hv, step_info, name, charp);
	if(step_info->network)
		STORE_FIELD(hv, step_info, network, charp);
	if(step_info->nodes)
		STORE_FIELD(hv, step_info, nodes, charp);
	av = newAV();
	for(j = 0; ; j += 2) {
		if(step_info->node_inx[j] == -1)
			break;
		av_store_int(av, j, step_info->node_inx[j]);
		av_store_int(av, j+1, step_info->node_inx[j+1]);
	}
	hv_store_sv(hv, "node_inx", newRV_noinc((SV*)av));
	STORE_FIELD(hv, step_info, num_cpus, uint32_t);
	STORE_FIELD(hv, step_info, num_tasks, uint32_t);
	if(step_info->partition)
		STORE_FIELD(hv, step_info, partition, charp);
	if(step_info->resv_ports)
		STORE_FIELD(hv, step_info, resv_ports, charp);
	STORE_FIELD(hv, step_info, run_time, time_t);
	STORE_FIELD(hv, step_info, start_time, time_t);
	STORE_FIELD(hv, step_info, step_id, uint32_t);
	STORE_FIELD(hv, step_info, time_limit, uint32_t);
	STORE_FIELD(hv, step_info, user_id, uint32_t);
	STORE_FIELD(hv, step_info, state, uint16_t);

	return 0;
}

/* 
 * convert perl HV to job_step_info_t
 */
int
hv_to_job_step_info(HV *hv, job_step_info_t *step_info)
{
	SV **svp;
	AV *av;
	int i, n;

	FETCH_FIELD(hv, step_info, array_job_id, uint32_t, TRUE);
	FETCH_FIELD(hv, step_info, array_task_id, uint32_t, TRUE);
	FETCH_FIELD(hv, step_info, ckpt_dir, charp, FALSE);
	FETCH_FIELD(hv, step_info, ckpt_interval, uint16_t, TRUE);
	FETCH_FIELD(hv, step_info, gres, charp, FALSE);
	FETCH_FIELD(hv, step_info, job_id, uint16_t, TRUE);
	FETCH_FIELD(hv, step_info, name, charp, FALSE);
	FETCH_FIELD(hv, step_info, network, charp, FALSE);
	FETCH_FIELD(hv, step_info, nodes, charp, FALSE);

	svp = hv_fetch(hv, "node_inx", 8, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		step_info->node_inx = xmalloc(n * sizeof(int));
		for (i = 0 ; i < n-1; i += 2) {
			step_info->node_inx[i] = (int)SvIV(*(av_fetch(av, i ,FALSE)));
			step_info->node_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1 ,FALSE)));
		}
		step_info->node_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}

	FETCH_FIELD(hv, step_info, num_cpus, uint32_t, TRUE);
	FETCH_FIELD(hv, step_info, num_tasks, uint32_t, TRUE);
	FETCH_FIELD(hv, step_info, partition, charp, FALSE);
	FETCH_FIELD(hv, step_info, resv_ports, charp, FALSE);
	FETCH_FIELD(hv, step_info, run_time, time_t, TRUE);
	FETCH_FIELD(hv, step_info, start_time, time_t, TRUE);
	FETCH_FIELD(hv, step_info, step_id, uint32_t, TRUE);
	FETCH_FIELD(hv, step_info, time_limit, uint32_t, TRUE);
	FETCH_FIELD(hv, step_info, user_id, uint32_t, TRUE);
	FETCH_FIELD(hv, step_info, state, uint16_t, TRUE);

	return 0;
}

/*
 * convert job_step_info_response_msg_t to perl HV
 */
int
job_step_info_response_msg_to_hv(
	job_step_info_response_msg_t *job_step_info_msg, HV *hv)
{
	int i;
	AV* av;
	HV* hv_info;

	STORE_FIELD(hv, job_step_info_msg, last_update, time_t);
	/* job_step_count implied in job_steps */
	av = newAV();
	for(i = 0; i < job_step_info_msg->job_step_count; i ++) {
		hv_info = newHV();
		if (job_step_info_to_hv(
			    job_step_info_msg->job_steps + i, hv_info) < 0) {
			SvREFCNT_dec(hv_info);
			SvREFCNT_dec(av);
			return -1;
		}
		av_store(av, i, newRV_noinc((SV*)hv_info));
	}
	hv_store_sv(hv, "job_steps", newRV_noinc((SV*)av));
	return 0;
}

/* 
 * convert perl HV to job_step_info_response_msg_t
 */
int
hv_to_job_step_info_response_msg(HV *hv,
		job_step_info_response_msg_t *step_info_msg)
{
	int i, n;
	SV **svp;
	AV *av;

	memset(step_info_msg, 0, sizeof(job_step_info_response_msg_t));

	FETCH_FIELD(hv, step_info_msg, last_update, time_t, TRUE);

	svp = hv_fetch(hv, "job_steps", 9, FALSE);
	if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV)) {
		Perl_warn (aTHX_ "job_steps is not an array reference in HV for job_step_info_response_msg_t");
		return -1;
	}

	av = (AV*)SvRV(*svp);
	n = av_len(av) + 1;
	step_info_msg->job_step_count = n;

	step_info_msg->job_steps = xmalloc(n * sizeof(job_step_info_t));
	for (i = 0; i < n; i ++) {
		svp = av_fetch(av, i, FALSE);
		if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV)) {
			Perl_warn (aTHX_ "element %d in job_steps is not valid", i);
			return -1;
		}
		if (hv_to_job_step_info((HV*)SvRV(*svp), &step_info_msg->job_steps[i]) < 0) {
			Perl_warn (aTHX_ "failed to convert element %d in job_steps", i);
			return -1;
		}
	}
	return 0;
}


/*
 * convert slurm_step_layout_t to perl HV
 */
int
slurm_step_layout_to_hv(slurm_step_layout_t *step_layout, HV *hv)
{
	AV* av, *av2;
	int i, j;

	if (step_layout->front_end)
		STORE_FIELD(hv, step_layout, front_end, charp);
	STORE_FIELD(hv, step_layout, node_cnt, uint16_t);
	if (step_layout->node_list)
		STORE_FIELD(hv, step_layout, node_list, charp);
	else {
		Perl_warn(aTHX_ "node_list missing in slurm_step_layout_t");
		return -1;
	}
	STORE_FIELD(hv, step_layout, plane_size, uint16_t);
	av = newAV();
	for (i = 0; i < step_layout->node_cnt; i ++)
		av_store_uint16_t(av, i, step_layout->tasks[i]);
	hv_store_sv(hv, "tasks", newRV_noinc((SV*)av));
	STORE_FIELD(hv, step_layout, task_cnt, uint32_t);
	STORE_FIELD(hv, step_layout, task_dist, uint16_t);
	av = newAV();
	for (i = 0; i < step_layout->node_cnt; i ++) {
		av2 = newAV();
		for (j = 0; j < step_layout->tasks[i]; j ++)
			av_store_uint32_t(av2, i, step_layout->tids[i][j]);
		av_store(av, i, newRV_noinc((SV*)av2));
	}
	hv_store_sv(hv, "tids", newRV_noinc((SV*)av));

	return 0;
}

/* convert job_step_pids_t to perl HV */
int
job_step_pids_to_hv(job_step_pids_t *pids, HV *hv)
{
	int i;
	AV *av;
	
	STORE_FIELD(hv, pids, node_name, charp);
	/* pid_cnt implied in pid array */
	av = newAV();
	for (i = 0; i < pids->pid_cnt; i ++) {
		av_store_uint32_t(av, i, pids->pid[i]);
	}
	hv_store_sv(hv, "pid", newRV_noinc((SV*)av));

	return 0;
}

/* convert job_step_pids_response_msg_t to HV */
int
job_step_pids_response_msg_to_hv(job_step_pids_response_msg_t *pids_msg, HV *hv)
{
	int i = 0;
	ListIterator itr;
	AV *av;
	HV *hv_pids;
	job_step_pids_t *pids;
	
	STORE_FIELD(hv, pids_msg, job_id, uint32_t);
	STORE_FIELD(hv, pids_msg, step_id, uint32_t);

	av = newAV();
	itr = slurm_list_iterator_create(pids_msg->pid_list);
	while((pids = (job_step_pids_t *)slurm_list_next(itr))) {
		hv_pids = newHV();
		if (job_step_pids_to_hv(pids, hv_pids) < 0) {
			Perl_warn(aTHX_ "failed to convert job_step_pids_t to hv for job_step_pids_response_msg_t");
			SvREFCNT_dec(hv_pids);
			SvREFCNT_dec(av);
			return -1;
		}
		av_store(av, i++, newRV_noinc((SV*)hv_pids));
	}
	slurm_list_iterator_destroy(itr);
	hv_store_sv(hv, "pid_list", newRV_noinc((SV*)av));

	return 0;
}

/*
 * convert job_step_stat_t to perl HV
 */
int
job_step_stat_to_hv(job_step_stat_t *stat, HV *hv)
{
	HV *hv_pids;

	STORE_PTR_FIELD(hv, stat, jobacct, "Slurm::jobacctinfo_t");
	STORE_FIELD(hv, stat, num_tasks, uint32_t);
	STORE_FIELD(hv, stat, return_code, uint32_t);
	hv_pids = newHV();
	if (job_step_pids_to_hv(stat->step_pids, hv_pids) < 0) {
		Perl_warn(aTHX_ "failed to convert job_step_pids_t to hv for job_step_stat_t");
		SvREFCNT_dec(hv_pids);
		return -1;
	}
	hv_store_sv(hv, "step_pids", newRV_noinc((SV*)hv_pids));

	return 0;
}

/*
 * convert job_step_stat_response_msg_t to perl HV
 */
int
job_step_stat_response_msg_to_hv(job_step_stat_response_msg_t *stat_msg, HV *hv)
{
	int i = 0;
	ListIterator itr;
	job_step_stat_t *stat;
	AV *av;
	HV *hv_stat;

	STORE_FIELD(hv, stat_msg, job_id, uint32_t);
	STORE_FIELD(hv, stat_msg, step_id, uint32_t);

	av = newAV();
	itr = slurm_list_iterator_create(stat_msg->stats_list);
	while((stat = (job_step_stat_t *)slurm_list_next(itr))) {
		hv_stat = newHV();
		if(job_step_stat_to_hv(stat, hv_stat) < 0) {
			Perl_warn(aTHX_ "failed to convert job_step_stat_t to hv for job_step_stat_response_msg_t");
			SvREFCNT_dec(hv_stat);
			SvREFCNT_dec(av);
			return -1;
		}
		av_store(av, i++, newRV_noinc((SV*)hv_stat));
	}
	slurm_list_iterator_destroy(itr);
	hv_store_sv(hv, "stats_list", newRV_noinc((SV*)av));

	return 0;
}

