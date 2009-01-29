/*
 * resrc_alloc.c - convert data between resource allocation related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include <slurm/slurm.h>

#include "msg.h"

static void _free_environment(char** environ);

/*
 * convert perl HV to job_desc_msg_t
 * return 0 on success, -1 on failure
 */
int
hv_to_job_desc_msg(HV* hv, job_desc_msg_t* job_desc_msg)
{
	SV** svp;
	HV* environ_hv;
	AV* argv_av;
	SV* val;
	char* env_key, *env_val;
	I32 klen;
	STRLEN vlen;
	int num_keys, i;
	
	slurm_init_job_desc_msg(job_desc_msg);

	FETCH_FIELD(hv, job_desc_msg, contiguous, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, kill_on_node_fail, uint16_t, FALSE);
	/* environment, env_size */
	if((svp = hv_fetch(hv, "environment", 11, FALSE))) {
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
			environ_hv = (HV*)SvRV(*svp);
			num_keys = HvKEYS(environ_hv);
			job_desc_msg->env_size = num_keys;
			Newz(0, job_desc_msg->environment, num_keys + 1, char*);
			
			hv_iterinit(environ_hv);
			i = 0;
			while((val = hv_iternextsv(environ_hv, &env_key, &klen))) {
				env_val = SvPV(val, vlen);
				Newz(0, (*(job_desc_msg->environment + i)), klen + vlen + 2, char);
				sprintf(*(job_desc_msg->environment + i), "%s=%s", env_key, env_val);
				i ++;
			}
		}
		else {
			Perl_warn(aTHX_ "`environment' of job descriptor is not a hash reference, ignored");
		}
	}
	FETCH_FIELD(hv, job_desc_msg, features, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, reservation, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, immediate, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, job_id, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, name, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, job_min_procs, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, job_min_sockets, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, job_min_cores, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, job_min_threads, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, job_min_memory, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, job_min_tmp_disk, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, partition, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, priority, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, req_nodes, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, exc_nodes, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, shared, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, time_limit, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, num_procs, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, min_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, max_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, min_sockets, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, max_sockets, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, min_cores, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, max_cores, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, min_threads, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, max_threads, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, cpus_per_task, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, ntasks_per_node, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, ntasks_per_socket, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, ntasks_per_core, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, script, charp, FALSE);
	/* argv, argc */
	if((svp = hv_fetch(hv, "argv", 4, FALSE))) {
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
			argv_av = (AV*)SvRV(*svp);
			job_desc_msg->argc = av_len(argv_av) + 1;
			if (job_desc_msg->argc > 0) {
				Newz(0, job_desc_msg->argv, (int32_t)(job_desc_msg->argc + 1), char*);
				for(i = 0; i < job_desc_msg->argc; i ++) {
					if((svp = av_fetch(argv_av, i, FALSE)))
						*(job_desc_msg->argv + i) = (char*) SvPV_nolen(*svp);
					else {
						Perl_warn(aTHX_ "error fetching `argv' of job descriptor");
						free_job_desc_msg_memory(job_desc_msg);
						return -1;
					}
				}
			}
		} else {
			Perl_warn(aTHX_ "`argv' of job descriptor is not an array reference, ignored");
		}
	}
	FETCH_FIELD(hv, job_desc_msg, err, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, in, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, out, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, user_id, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, group_id, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, work_dir, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, alloc_node, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, alloc_sid, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, resp_host, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, alloc_resp_port, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, other_port, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, dependency, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, overcommit, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, num_tasks, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, nice, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, account, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, network, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, comment, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, task_dist, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, plane_size, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, begin_time, time_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, mail_type, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, mail_user, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, requeue, uint16_t, FALSE);
	/* geometry */
#if SYSTEM_DIMENSIONS
	if((svp = hv_fetch(hv, "geometry", 8, FALSE))) {
		AV *avp;
		if (!SvROK(*svp) || SvTYPE(SvRV(*svp)) != SVt_PVAV) {
			Perl_warn(aTHX_ "`geometry' is not an array reference in job descriptor");
			free_job_desc_msg_memory(job_desc_msg);
			return -1;
		}
		avp = (AV*)SvRV(*svp);
		for(i = 0; i < SYSTEM_DIMENSIONS; i ++) {
			if(! (svp = av_fetch(avp, i, FALSE))) {
				Perl_warn(aTHX_ "geometry of dimension %s missing in job descriptor", i);
				free_job_desc_msg_memory(job_desc_msg);
				return -1;
			}
			job_desc_msg->geometry[i] = SvUV(*svp);
		}
	}
#endif
	FETCH_FIELD(hv, job_desc_msg, conn_type, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, reboot, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, rotate, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc_msg, blrtsimage, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, linuximage, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, mloaderimage, charp, FALSE);
	FETCH_FIELD(hv, job_desc_msg, ramdiskimage, charp, FALSE);
	/* TODO: select_jobinfo */
	/* Don't know how to manage memory of select_jobinfo, since it's storage size is unknown. */
	/* Maybe we can do it if select_g_copy_jobinfo and select_g_free_jobinfo are exported. */
	return 0;
}

/*
 * free allocated environment variable memory for job_desc_msg_t 
 */
static void
_free_environment(char** environ)
{
	int i;
	if(! environ)
		return;
	for(i = 0; *(environ + i) ; i ++)
		Safefree(*(environ + i));
	Safefree(environ);
}

/*
 * free allocate memory for job_desc_msg_t
 */
void
free_job_desc_msg_memory(job_desc_msg_t *msg)
{
	_free_environment(msg->environment);
	if (msg->argv)
		Safefree (msg->argv);
}

/*
 * convert resource_allocation_resource_msg_t to perl HV
 */
int
resource_allocation_response_msg_to_hv(resource_allocation_response_msg_t* resp_msg, HV* hv)
{
	AV* avp;
	int i;
	
	STORE_FIELD(hv, resp_msg, job_id, uint32_t);
	if(resp_msg->node_list)
		STORE_FIELD(hv, resp_msg, node_list, charp);
	STORE_FIELD(hv, resp_msg, num_cpu_groups, uint16_t);
	if(resp_msg->num_cpu_groups) {
		avp = newAV();
		for(i = 0; i < resp_msg->num_cpu_groups; i ++) {
			av_store(avp, i, newSVuv(resp_msg->cpus_per_node[i]));
		}
		hv_store_sv(hv, "cpus_per_node", newRV_noinc((SV*)avp));
		
		avp = newAV();
		for(i = 0; i < resp_msg->num_cpu_groups; i ++) {
			av_store(avp, i, newSVuv(resp_msg->cpu_count_reps[i]));
		}
		hv_store_sv(hv, "cpu_count_reps", newRV_noinc((SV*)avp));
	}
	STORE_FIELD(hv, resp_msg, node_cnt, uint32_t);
	STORE_FIELD(hv, resp_msg, error_code, uint32_t);
	/* TODO: select_jobinfo */
	return 0;
}

/*
 * convert job_alloc_info_response_msg_t to perl HV
 */
int
job_alloc_info_response_msg_to_hv(job_alloc_info_response_msg_t *resp_msg, HV* hv)
{
	AV* avp;
	int i;
	
	STORE_FIELD(hv, resp_msg, job_id, uint32_t);
	if(resp_msg->node_list)
		STORE_FIELD(hv, resp_msg, node_list, charp);
	STORE_FIELD(hv, resp_msg, num_cpu_groups, uint16_t);
	if(resp_msg->num_cpu_groups) {
		avp = newAV();
		for(i = 0; i < resp_msg->num_cpu_groups; i ++) {
			av_store(avp, i, newSVuv(resp_msg->cpus_per_node[i]));
		}
		hv_store_sv(hv, "cpus_per_node", newRV_noinc((SV*)avp));
		
		avp = newAV();
		for(i = 0; i < resp_msg->num_cpu_groups; i ++) {
			av_store(avp, i, newSVuv(resp_msg->cpu_count_reps[i]));
		}
		hv_store_sv(hv, "cpu_count_reps", newRV_noinc((SV*)avp));
	}
	STORE_FIELD(hv, resp_msg, node_cnt, uint32_t);
	if(resp_msg->node_cnt) {
		avp = newAV();
		for(i = 0; i < resp_msg->node_cnt; i ++) {
			/* XXX: This is a packed inet address */
			av_store(avp, i, newSVpvn((char*)(resp_msg->node_addr + i), sizeof(slurm_addr)));
		}
		hv_store_sv(hv, "node_addr", newRV_noinc((SV*)avp));
	}
	STORE_FIELD(hv, resp_msg, error_code, uint32_t);
	/* TODO: select_jobinfo */
	return 0;
}

/*
 * convert submit_response_msg_t to perl HV
 */
int
submit_response_msg_to_hv(submit_response_msg_t *resp_msg, HV* hv)
{
	STORE_FIELD(hv, resp_msg, job_id, uint32_t);
	STORE_FIELD(hv, resp_msg, step_id, uint32_t);
	STORE_FIELD(hv, resp_msg, error_code, uint32_t);
	return 0;
}

