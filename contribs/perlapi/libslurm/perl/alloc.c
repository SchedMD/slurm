/*
 * alloc.c - convert data between resource allocation related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include <slurm/slurm.h>
#define NEED_sv_2pv_flags_GLOBAL
#include "ppport.h"

#include "slurm-perl.h"

static void _free_environment(char** environ);

/*
 * convert perl HV to job_desc_msg_t
 * return 0 on success, -1 on failure
 */
int
hv_to_job_desc_msg(HV *hv, job_desc_msg_t *job_desc)
{
	SV **svp;
	HV *environ_hv;
	AV *argv_av;
	SV *val;
	char *env_key, *env_val;
	I32 klen;
	STRLEN vlen;
	int num_keys, i;

	slurm_init_job_desc_msg(job_desc);

	FETCH_FIELD(hv, job_desc, account, charp, FALSE);
	FETCH_FIELD(hv, job_desc, acctg_freq, charp, FALSE);
	FETCH_FIELD(hv, job_desc, alloc_node, charp, FALSE);
	FETCH_FIELD(hv, job_desc, alloc_resp_port, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, alloc_sid, uint32_t, FALSE);
	/* argv, argc */
	if((svp = hv_fetch(hv, "argv", 4, FALSE))) {
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
			argv_av = (AV*)SvRV(*svp);
			job_desc->argc = av_len(argv_av) + 1;
			if (job_desc->argc > 0) {
				Newz(0, job_desc->argv, (int32_t)(job_desc->argc + 1), char*);
				for(i = 0; i < job_desc->argc; i ++) {
					if((svp = av_fetch(argv_av, i, FALSE)))
						*(job_desc->argv + i) = (char*) SvPV_nolen(*svp);
					else {
						Perl_warn(aTHX_ "error fetching `argv' of job descriptor");
						free_job_desc_msg_memory(job_desc);
						return -1;
					}
				}
			}
		} else {
			Perl_warn(aTHX_ "`argv' of job descriptor is not an array reference, ignored");
		}
	}
	FETCH_FIELD(hv, job_desc, array_inx, charp, FALSE);
	FETCH_FIELD(hv, job_desc, begin_time, time_t, FALSE);
	FETCH_FIELD(hv, job_desc, ckpt_interval, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, ckpt_dir, charp, FALSE);
	FETCH_FIELD(hv, job_desc, comment, charp, FALSE);
	FETCH_FIELD(hv, job_desc, contiguous, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, cpu_bind, charp, FALSE);
	FETCH_FIELD(hv, job_desc, cpu_bind_type, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, dependency, charp, FALSE);
	FETCH_FIELD(hv, job_desc, end_time, time_t, FALSE);

	/* environment, env_size */
	if ((svp = hv_fetch(hv, "environment", 11, FALSE))) {
		if (SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
			environ_hv = (HV*)SvRV(*svp);
			num_keys = HvKEYS(environ_hv);
			job_desc->env_size = num_keys;
			Newz(0, job_desc->environment, num_keys + 1, char*);

			hv_iterinit(environ_hv);
			i = 0;
			while ((val = hv_iternextsv(environ_hv, &env_key, &klen))) {
				env_val = SvPV(val, vlen);
				Newz(0, (*(job_desc->environment + i)), klen + vlen + 2, char);
				sprintf(*(job_desc->environment + i), "%s=%s", env_key, env_val);
				i ++;
			}
		} else {
			Perl_warn(aTHX_ "`environment' of job descriptor is not a hash reference, ignored");
		}
	}

	FETCH_FIELD(hv, job_desc, exc_nodes, charp, FALSE);
	FETCH_FIELD(hv, job_desc, features, charp, FALSE);
	FETCH_FIELD(hv, job_desc, tres_per_job, charp, FALSE);
	FETCH_FIELD(hv, job_desc, tres_per_node, charp, FALSE);
	FETCH_FIELD(hv, job_desc, tres_per_socket, charp, FALSE);
	FETCH_FIELD(hv, job_desc, tres_per_task, charp, FALSE);
	FETCH_FIELD(hv, job_desc, group_id, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, immediate, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, job_id, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, kill_on_node_fail, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, licenses, charp, FALSE);
	FETCH_FIELD(hv, job_desc, mail_type, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, mail_user, charp, FALSE);
	FETCH_FIELD(hv, job_desc, mem_bind, charp, FALSE);
	FETCH_FIELD(hv, job_desc, mem_bind_type, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, name, charp, FALSE);
	FETCH_FIELD(hv, job_desc, network, charp, FALSE);
	FETCH_FIELD(hv, job_desc, nice, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, num_tasks, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, open_mode, uint8_t, FALSE);
	FETCH_FIELD(hv, job_desc, other_port, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, overcommit, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, partition, charp, FALSE);
	FETCH_FIELD(hv, job_desc, plane_size, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, priority, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, profile, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, qos, charp, FALSE);
	FETCH_FIELD(hv, job_desc, resp_host, charp, FALSE);
	FETCH_FIELD(hv, job_desc, req_nodes, charp, FALSE);
	FETCH_FIELD(hv, job_desc, requeue, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, reservation, charp, FALSE);
	FETCH_FIELD(hv, job_desc, script, charp, FALSE);
	FETCH_FIELD(hv, job_desc, shared, uint16_t, FALSE);
	/* spank_job_env, spank_job_env_size */
	if((svp = hv_fetch(hv, "spank_job_env", 13, FALSE))) {
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
			environ_hv = (HV*)SvRV(*svp);
			num_keys = HvKEYS(environ_hv);
			job_desc->spank_job_env_size = num_keys;
			Newz(0, job_desc->spank_job_env, num_keys + 1, char*);

			hv_iterinit(environ_hv);
			i = 0;
			while((val = hv_iternextsv(environ_hv, &env_key, &klen))) {
				env_val = SvPV(val, vlen);
				Newz(0, (*(job_desc->spank_job_env + i)), klen + vlen + 2, char);
				sprintf(*(job_desc->spank_job_env + i), "%s=%s", env_key, env_val);
				i ++;
			}
		}
		else {
			Perl_warn(aTHX_ "`spank_job_env' of job descriptor is not a hash reference, ignored");
		}
	}

	FETCH_FIELD(hv, job_desc, task_dist, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, time_limit, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, time_min, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, user_id, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, wait_all_nodes, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, warn_signal, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, warn_time, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, work_dir, charp, FALSE);
	/* job constraints: */
	FETCH_FIELD(hv, job_desc, cpu_freq_min, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, cpu_freq_max, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, cpu_freq_gov, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, cpus_per_task, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, min_cpus, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, max_cpus, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, min_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, max_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, sockets_per_node, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, cores_per_socket, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, threads_per_core, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, ntasks_per_node, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, ntasks_per_socket, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, ntasks_per_core, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, pn_min_cpus, uint16_t, FALSE);
	FETCH_FIELD(hv, job_desc, pn_min_memory, uint64_t, FALSE);
	FETCH_FIELD(hv, job_desc, pn_min_tmp_disk, uint32_t, FALSE);
	FETCH_FIELD(hv, job_desc, reboot, uint16_t, FALSE);
	FETCH_PTR_FIELD(hv, job_desc, select_jobinfo, "Slurm::dynamic_plugin_data_t",  FALSE);

	FETCH_FIELD(hv, job_desc, std_err, charp, FALSE);
	FETCH_FIELD(hv, job_desc, std_in, charp, FALSE);
	FETCH_FIELD(hv, job_desc, std_out, charp, FALSE);
	FETCH_FIELD(hv, job_desc, wckey, charp, FALSE);
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
	if (msg->argv)
		Safefree (msg->argv);
	_free_environment(msg->environment);
	_free_environment(msg->spank_job_env);
}

/*
 * convert resource_allocation_resource_msg_t to perl HV
 */
int
resource_allocation_response_msg_to_hv(resource_allocation_response_msg_t *resp_msg, HV *hv)
{
	AV *av;
	int i;

	STORE_FIELD(hv, resp_msg, job_id, uint32_t);
	if(resp_msg->node_list)
		STORE_FIELD(hv, resp_msg, node_list, charp);
	STORE_FIELD(hv, resp_msg, num_cpu_groups, uint16_t);
	if(resp_msg->num_cpu_groups) {
		av = newAV();
		for(i = 0; i < resp_msg->num_cpu_groups; i ++) {
			av_store_uint16_t(av, i, resp_msg->cpus_per_node[i]);
		}
		hv_store_sv(hv, "cpus_per_node", newRV_noinc((SV*)av));

		av = newAV();
		for(i = 0; i < resp_msg->num_cpu_groups; i ++) {
			av_store_uint32_t(av, i, resp_msg->cpu_count_reps[i]);
		}
		hv_store_sv(hv, "cpu_count_reps", newRV_noinc((SV*)av));
	}
	STORE_FIELD(hv, resp_msg, node_cnt, uint32_t);
	STORE_FIELD(hv, resp_msg, error_code, uint32_t);
	STORE_PTR_FIELD(hv, resp_msg, select_jobinfo, "Slurm::dynamic_plugin_data_t");

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

/* 
 * convert job_sbcast_cred_msg_t to perl HV
 */
int
job_sbcast_cred_msg_to_hv(job_sbcast_cred_msg_t *msg, HV *hv)
{
	AV *av;
	int i;

	STORE_FIELD(hv, msg, job_id, uint32_t);
	STORE_FIELD(hv, msg, node_cnt, uint32_t);
	if(msg->node_cnt) {
		av = newAV();
		for(i = 0; i < msg->node_cnt; i ++) {
			/* XXX: This is a packed inet address */
			av_store(av, i, newSVpvn((char*)(msg->node_addr + i), sizeof(slurm_addr_t)));
		}
		hv_store_sv(hv, "node_addr", newRV_noinc((SV*)av));
	}
	if (msg->node_list)
		STORE_FIELD(hv, msg, node_list, charp);
	STORE_PTR_FIELD(hv, msg, sbcast_cred, "Slurm::sbcast_cred_t");
	return 0;
}

int
srun_job_complete_msg_to_hv(srun_job_complete_msg_t *msg, HV *hv)
{
	STORE_FIELD(hv, msg, job_id, uint32_t);
	STORE_FIELD(hv, msg, step_id, uint32_t);
	return 0;
}

int
srun_timeout_msg_to_hv(srun_timeout_msg_t *msg, HV *hv)
{
	STORE_FIELD(hv, msg, job_id, uint32_t);
	STORE_FIELD(hv, msg, step_id, uint32_t);
	STORE_FIELD(hv, msg, timeout, time_t);
	return 0;
}



/********** pending_callback for slurm_allocate_resources_blocking() **********/
static SV* sarb_cb_sv = NULL;

void
set_sarb_cb(SV *callback)
{
	if (callback == NULL) {
		if (sarb_cb_sv != NULL)
			sv_setsv(sarb_cb_sv, &PL_sv_undef);
	} else {
		if (sarb_cb_sv == NULL)
			sarb_cb_sv = newSVsv(callback);
		else
			sv_setsv(sarb_cb_sv, callback);
	}
}

void
sarb_cb(uint32_t job_id)
{
	dSP;

	if (sarb_cb_sv == NULL ||
	    sarb_cb_sv == &PL_sv_undef)
	    	return;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newSVuv(job_id)));
	PUTBACK;

	call_sv(sarb_cb_sv, G_VOID | G_DISCARD);

	FREETMPS;
	LEAVE;
}

/********** convert functions for callbacks **********/

static int
srun_ping_msg_to_hv(srun_ping_msg_t *msg, HV *hv)
{
	STORE_FIELD(hv, msg, job_id, uint32_t);
	STORE_FIELD(hv, msg, step_id, uint32_t);
	return 0;
}

static int
srun_user_msg_to_hv(srun_user_msg_t *msg, HV *hv)
{
	STORE_FIELD(hv, msg, job_id, uint32_t);
	STORE_FIELD(hv, msg, msg, charp);
	return 0;
}

static int
srun_node_fail_msg_to_hv(srun_node_fail_msg_t *msg, HV *hv)
{
	STORE_FIELD(hv, msg, job_id, uint32_t);
	STORE_FIELD(hv, msg, nodelist, charp);
	STORE_FIELD(hv, msg, step_id, uint32_t);
	return 0;
}

/*********** callbacks for slurm_allocation_msg_thr_create() **********/
static SV *ping_cb_sv = NULL;
static SV *job_complete_cb_sv = NULL;
static SV *timeout_cb_sv = NULL;
static SV *user_msg_cb_sv = NULL;
static SV *node_fail_cb_sv = NULL;

void
set_sacb(HV *callbacks)
{
	SV **svp, *cb;

	if (callbacks == NULL) {
		if (ping_cb_sv != NULL)
			sv_setsv(ping_cb_sv, &PL_sv_undef);
		if (job_complete_cb_sv != NULL)
			sv_setsv(job_complete_cb_sv, &PL_sv_undef);
		if (timeout_cb_sv != NULL)
			sv_setsv(timeout_cb_sv, &PL_sv_undef);
		if (user_msg_cb_sv != NULL)
			sv_setsv(user_msg_cb_sv, &PL_sv_undef);
		if (node_fail_cb_sv != NULL)
			sv_setsv(node_fail_cb_sv, &PL_sv_undef);
		return;
	}

	svp = hv_fetch(callbacks, "ping", 4, FALSE);
	cb = svp ? *svp : &PL_sv_undef;
	if (ping_cb_sv == NULL) {
		ping_cb_sv = newSVsv(cb);
	} else {
		sv_setsv(ping_cb_sv, cb);
	}

	svp = hv_fetch(callbacks, "job_complete", 4, FALSE);
	cb = svp ? *svp : &PL_sv_undef;
	if (job_complete_cb_sv == NULL) {
		job_complete_cb_sv = newSVsv(cb);
	} else {
		sv_setsv(job_complete_cb_sv, cb);
	}

	svp = hv_fetch(callbacks, "timeout", 4, FALSE);
	cb = svp ? *svp : &PL_sv_undef;
	if (timeout_cb_sv == NULL) {
		timeout_cb_sv = newSVsv(cb);
	} else {
		sv_setsv(timeout_cb_sv, cb);
	}

	svp = hv_fetch(callbacks, "user_msg", 4, FALSE);
	cb = svp ? *svp : &PL_sv_undef;
	if (user_msg_cb_sv == NULL) {
		user_msg_cb_sv = newSVsv(cb);
	} else {
		sv_setsv(user_msg_cb_sv, cb);
	}

	svp = hv_fetch(callbacks, "node_fail", 4, FALSE);
	cb = svp ? *svp : &PL_sv_undef;
	if (node_fail_cb_sv == NULL) {
		node_fail_cb_sv = newSVsv(cb);
	} else {
		sv_setsv(node_fail_cb_sv, cb);
	}
}

static void
ping_cb(srun_ping_msg_t *msg)
{
	HV *hv;
	dSP;

	if (ping_cb_sv == NULL ||
	    ping_cb_sv == &PL_sv_undef) {
		return;
	}
	hv = newHV();
	if (srun_ping_msg_to_hv(msg, hv) < 0) {
		Perl_warn( aTHX_ "failed to convert surn_ping_msg_t to perl HV");
		SvREFCNT_dec(hv);
		return;
	}

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_noinc((SV*)hv)));
	PUTBACK;

	call_sv(ping_cb_sv, G_VOID);

	FREETMPS;
	LEAVE;
}

static void
job_complete_cb(srun_job_complete_msg_t *msg)
{
	HV *hv;
	dSP;

	if (job_complete_cb_sv == NULL ||
	    job_complete_cb_sv == &PL_sv_undef) {
		return;
	}
	hv = newHV();
	if (srun_job_complete_msg_to_hv(msg, hv) < 0) {
		Perl_warn( aTHX_ "failed to convert surn_job_complete_msg_t to perl HV");
		SvREFCNT_dec(hv);
		return;
	}

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_noinc((SV*)hv)));
	PUTBACK;

	call_sv(job_complete_cb_sv, G_VOID);

	FREETMPS;
	LEAVE;
}

static void
timeout_cb(srun_timeout_msg_t *msg)
{
	HV *hv;
	dSP;

	if (timeout_cb_sv == NULL ||
	    timeout_cb_sv == &PL_sv_undef) {
		return;
	}
	hv = newHV();
	if (srun_timeout_msg_to_hv(msg, hv) < 0) {
		Perl_warn( aTHX_ "failed to convert surn_timeout_msg_t to perl HV");
		SvREFCNT_dec(hv);
		return;
	}

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_noinc((SV*)hv)));
	PUTBACK;

	call_sv(timeout_cb_sv, G_VOID);

	FREETMPS;
	LEAVE;
}

static void
user_msg_cb(srun_user_msg_t *msg)
{
	HV *hv;
	dSP;

	if (user_msg_cb_sv == NULL ||
	    user_msg_cb_sv == &PL_sv_undef) {
		return;
	}
	hv = newHV();
	if (srun_user_msg_to_hv(msg, hv) < 0) {
		Perl_warn( aTHX_ "failed to convert surn_user_msg_msg_t to perl HV");
		SvREFCNT_dec(hv);
		return;
	}

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_noinc((SV*)hv)));
	PUTBACK;

	call_sv(user_msg_cb_sv, G_VOID);

	FREETMPS;
	LEAVE;
}

static void
node_fail_cb(srun_node_fail_msg_t *msg)
{
	HV *hv;
	dSP;

	if (node_fail_cb_sv == NULL ||
	    node_fail_cb_sv == &PL_sv_undef) {
		return;
	}
	hv = newHV();
	if (srun_node_fail_msg_to_hv(msg, hv) < 0) {
		Perl_warn( aTHX_ "failed to convert surn_node_fail_msg_t to perl HV");
		SvREFCNT_dec(hv);
		return;
	}

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_noinc((SV*)hv)));
	PUTBACK;

	call_sv(node_fail_cb_sv, G_VOID);

	FREETMPS;
	LEAVE;
}

slurm_allocation_callbacks_t sacb = {
	ping_cb,
	job_complete_cb,
	timeout_cb,
	user_msg_cb,
	node_fail_cb
};
