/*
 * job.c - convert data between job (step) related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "ppport.h"

#undef VERSION /* MakeMaker defines VERSION to some version we don't care
		* about. The true version will be defined in config.h which is
		* included from src/common/job_resources.h below.
		*/

#include "slurm-perl.h"
#include "src/common/job_resources.h"
#include "src/common/xstring.h"

static node_info_msg_t *job_node_ptr = NULL;

/* This set of functions loads/free node information so that we can map a job's
 * core bitmap to it's CPU IDs based upon the thread count on each node. */
static void _load_node_info(void)
{
	if (!job_node_ptr)
		(void) slurm_load_node((time_t) NULL, &job_node_ptr, 0);
}

static void _free_node_info(void)
{
	if (job_node_ptr) {
		slurm_free_node_info_msg(job_node_ptr);
		job_node_ptr = NULL;
	}
}

static uint32_t _threads_per_core(char *host)
{
	uint32_t i, threads = 1;

	if (!job_node_ptr || !host)
		return threads;

	for (i = 0; i < job_node_ptr->record_count; i++) {
		if (job_node_ptr->node_array[i].name &&
		    !xstrcmp(host, job_node_ptr->node_array[i].name)) {
			threads = job_node_ptr->node_array[i].threads;
			break;
		}
	}

	return threads;
}

static int _job_resrcs_to_hv(job_info_t *job_info, HV *hv)
{
	AV *av;
	HV *nr_hv;
	bitstr_t *cpu_bitmap;
	int sock_inx, sock_reps, last, cnt = 0, i, j, k;
	char tmp1[128], tmp2[128];
	char *host;
	job_resources_t *job_resrcs = job_info->job_resrcs;
	int bit_inx, bit_reps;
	int abs_node_inx, rel_node_inx;
	uint64_t *last_mem_alloc_ptr = NULL;
	uint64_t last_mem_alloc = NO_VAL64;
	char *last_hosts;
	hostlist_t hl, hl_last;
	uint32_t threads;

	if (!job_resrcs || !job_resrcs->core_bitmap
	    || ((last = slurm_bit_fls(job_resrcs->core_bitmap)) == -1))
		return 0;

	if (!(hl = slurm_hostlist_create(job_resrcs->nodes)))
		return 1;

	if (!(hl_last = slurm_hostlist_create(NULL)))
		return 1;
	av = newAV();

	bit_inx = 0;
	i = sock_inx = sock_reps = 0;
	abs_node_inx = job_info->node_inx[i];

/*	tmp1[] stores the current cpu(s) allocated	*/
	tmp2[0] = '\0';	/* stores last cpu(s) allocated */
	for (rel_node_inx=0; rel_node_inx < job_resrcs->nhosts;
	     rel_node_inx++) {

		if (sock_reps >= job_resrcs->sock_core_rep_count[sock_inx]) {
			sock_inx++;
			sock_reps = 0;
		}
		sock_reps++;

		bit_reps = job_resrcs->sockets_per_node[sock_inx] *
			job_resrcs->cores_per_socket[sock_inx];
		host = slurm_hostlist_shift(hl);
		threads = _threads_per_core(host);
		cpu_bitmap = slurm_bit_alloc(bit_reps * threads);
		for (j = 0; j < bit_reps; j++) {
			if (slurm_bit_test(job_resrcs->core_bitmap, bit_inx)){
				for (k = 0; k < threads; k++)
					slurm_bit_set(cpu_bitmap,
						      (j * threads) + k);
			}
			bit_inx++;
		}
		slurm_bit_fmt(tmp1, sizeof(tmp1), cpu_bitmap);
		FREE_NULL_BITMAP(cpu_bitmap);
/*
 *		If the allocation values for this host are not the same as the
 *		last host, print the report of the last group of hosts that had
 *		identical allocation values.
 */
		if (strcmp(tmp1, tmp2) ||
		    (last_mem_alloc_ptr != job_resrcs->memory_allocated) ||
		    (job_resrcs->memory_allocated &&
		     (last_mem_alloc !=
		      job_resrcs->memory_allocated[rel_node_inx]))) {
			if (slurm_hostlist_count(hl_last)) {
				last_hosts =
					slurm_hostlist_ranged_string_xmalloc(
						hl_last);
				nr_hv = newHV();
				hv_store_charp(nr_hv, "nodes", last_hosts);
				hv_store_charp(nr_hv, "cpu_ids", tmp2);
				hv_store_uint64_t(nr_hv, "mem",
						  last_mem_alloc_ptr ?
						  last_mem_alloc : 0);
				av_store(av, cnt++, newRV_noinc((SV*)nr_hv));
				xfree(last_hosts);
				slurm_hostlist_destroy(hl_last);
				hl_last = slurm_hostlist_create(NULL);
			}
			strcpy(tmp2, tmp1);
			last_mem_alloc_ptr = job_resrcs->memory_allocated;
			if (last_mem_alloc_ptr)
				last_mem_alloc = job_resrcs->
					memory_allocated[rel_node_inx];
			else
				last_mem_alloc = NO_VAL64;
		}
		slurm_hostlist_push_host(hl_last, host);
		free(host);

		if (bit_inx > last)
			break;

		if (abs_node_inx > job_info->node_inx[i+1]) {
			i += 2;
			abs_node_inx = job_info->node_inx[i];
		} else {
			abs_node_inx++;
		}
	}

	if (slurm_hostlist_count(hl_last)) {
		last_hosts = slurm_hostlist_ranged_string_xmalloc(hl_last);
		nr_hv = newHV();
		hv_store_charp(nr_hv, "nodes", last_hosts);
		hv_store_charp(nr_hv, "cpu_ids", tmp2);
		hv_store_uint64_t(nr_hv, "mem",
				  last_mem_alloc_ptr ?
				  last_mem_alloc : 0);
		av_store(av, cnt++, newRV_noinc((SV*)nr_hv));
		xfree(last_hosts);
	}
	slurm_hostlist_destroy(hl);
	slurm_hostlist_destroy(hl_last);
	hv_store_sv(hv, "node_resrcs", newRV_noinc((SV*)av));

	return 0;
}

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
	if (job_info->extra)
		STORE_FIELD(hv, job_info, extra, charp);
	if (job_info->features)
		STORE_FIELD(hv, job_info, features, charp);
	if (job_info->tres_per_node)
		STORE_FIELD(hv, job_info, tres_per_node, charp);
	STORE_FIELD(hv, job_info, group_id, uint32_t);
	STORE_FIELD(hv, job_info, job_id, uint32_t);
	STORE_FIELD(hv, job_info, job_state, uint32_t);
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
	STORE_FIELD(hv, job_info, nice, uint32_t);
	if(job_info->nodes)
		STORE_FIELD(hv, job_info, nodes, charp);
	if(job_info->sched_nodes)
		STORE_FIELD(hv, job_info, sched_nodes, charp);
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
	STORE_FIELD(hv, job_info, ntasks_per_tres, uint16_t);
	STORE_FIELD(hv, job_info, num_nodes, uint32_t);
	STORE_FIELD(hv, job_info, num_cpus, uint32_t);
	STORE_FIELD(hv, job_info, pn_min_memory, uint64_t);
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
	STORE_PTR_FIELD(hv, job_info, job_resrcs, "Slurm::job_resources_t");
	STORE_FIELD(hv, job_info, shared, uint16_t);
	STORE_FIELD(hv, job_info, show_flags, uint16_t);
	STORE_FIELD(hv, job_info, start_time, time_t);
	if(job_info->state_desc)
		STORE_FIELD(hv, job_info, state_desc, charp);
	STORE_FIELD(hv, job_info, state_reason, uint32_t);
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

	_job_resrcs_to_hv(job_info, hv);

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
	FETCH_FIELD(hv, job_info, extra, charp, FALSE);
	FETCH_FIELD(hv, job_info, features, charp, FALSE);
	FETCH_FIELD(hv, job_info, tres_per_node, charp, FALSE);
	FETCH_FIELD(hv, job_info, group_id, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, job_id, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, job_state, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, licenses, charp, FALSE);
	FETCH_FIELD(hv, job_info, max_cpus, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, max_nodes, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, profile, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, sockets_per_node, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, cores_per_socket, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, threads_per_core, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, name, charp, FALSE);
	FETCH_FIELD(hv, job_info, network, charp, FALSE);
	FETCH_FIELD(hv, job_info, nice, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, nodes, charp, FALSE);
	FETCH_FIELD(hv, job_info, sched_nodes, charp, FALSE);
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
	FETCH_FIELD(hv, job_info, ntasks_per_tres, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, num_nodes, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, num_cpus, uint32_t, TRUE);
	FETCH_FIELD(hv, job_info, pn_min_memory, uint64_t, TRUE);
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
	FETCH_PTR_FIELD(hv, job_info, job_resrcs, "Slurm::job_resources_t", FALSE);
	FETCH_FIELD(hv, job_info, shared, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, show_flags, uint16_t, TRUE);
	FETCH_FIELD(hv, job_info, start_time, time_t, TRUE);
	FETCH_FIELD(hv, job_info, state_desc, charp, FALSE);
	FETCH_FIELD(hv, job_info, state_reason, uint32_t, TRUE);
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

	_load_node_info();

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

	_free_node_info();

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
		Perl_warn (aTHX_ "job_array is not an array reference in HV for job_info_msg_t");
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
