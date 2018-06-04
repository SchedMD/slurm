/*
 * step_ctx.c - convert data between step context related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "ppport.h"

#include <slurm/slurm.h>
#include "slurm-perl.h"

/*
 * convert perl HV to slurm_step_ctx_params_t
 */
int
hv_to_slurm_step_ctx_params(HV *hv, slurm_step_ctx_params_t *params)
{
	slurm_step_ctx_params_t_init(params);

	FETCH_FIELD(hv, params, ckpt_dir, charp, FALSE);
	FETCH_FIELD(hv, params, ckpt_interval, uint16_t, FALSE);
	FETCH_FIELD(hv, params, cpu_count, uint32_t, FALSE);
	FETCH_FIELD(hv, params, cpu_freq_min, uint32_t, FALSE);
	FETCH_FIELD(hv, params, cpu_freq_max, uint32_t, FALSE);
	FETCH_FIELD(hv, params, cpu_freq_gov, uint32_t, FALSE);
	FETCH_FIELD(hv, params, exclusive, uint16_t, FALSE);
	FETCH_FIELD(hv, params, features, charp, FALSE);
	FETCH_FIELD(hv, params, immediate, uint16_t, FALSE);
	FETCH_FIELD(hv, params, job_id, uint32_t, FALSE); /* for slurm_step_ctx_create_no_alloc */
	FETCH_FIELD(hv, params, pn_min_memory, uint64_t, FALSE);
	FETCH_FIELD(hv, params, name, charp, FALSE);
	FETCH_FIELD(hv, params, network, charp, FALSE);
	FETCH_FIELD(hv, params, profile, uint32_t, FALSE);
	FETCH_FIELD(hv, params, no_kill, uint8_t, FALSE);
	FETCH_FIELD(hv, params, min_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, params, max_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, params, node_list, charp, FALSE);
	FETCH_FIELD(hv, params, overcommit, bool, FALSE);
	FETCH_FIELD(hv, params, plane_size, uint16_t, FALSE);
	FETCH_FIELD(hv, params, relative, uint16_t, FALSE);
	FETCH_FIELD(hv, params, resv_port_cnt, uint16_t, FALSE);
	FETCH_FIELD(hv, params, task_count, uint32_t, FALSE);
	FETCH_FIELD(hv, params, task_dist, uint16_t, FALSE);
	FETCH_FIELD(hv, params, tres_per_node, charp, FALSE);
	FETCH_FIELD(hv, params, tres_per_step, charp, FALSE);
	FETCH_FIELD(hv, params, tres_per_node, charp, FALSE);
	FETCH_FIELD(hv, params, tres_per_socket, charp, FALSE);
	FETCH_FIELD(hv, params, tres_per_task, charp, FALSE);
	FETCH_FIELD(hv, params, time_limit, uint32_t, FALSE);
	FETCH_FIELD(hv, params, uid, uint32_t, FALSE);
	FETCH_FIELD(hv, params, verbose_level, uint16_t, FALSE);
	return 0;
}

#if 0
/*
 * convert job_step_create_response_msg_t to perl HV
 */
int
job_step_create_response_msg_to_hv(job_step_create_response_msg_t *resp_msg, HV *hv)
{
	HV *hv;

	STORE_FIELD(hv, resp_msg, job_step_id, uint32_t);
	if (resp_msg->resv_ports)
		STORE_FIELD(hv, resp_msg, resv_ports, charp);
	hv = newHV();
	if (slurm_step_layout_to_hv(resp->step_layout, hv) < 0) {
		Perl_warn(aTHX_ "Failed to convert slurm_step_layout_t to hv for job_step_create_response_msg_t");
		SvREFCNT_dec(hv);
		return -1;
	}
	hv_store(hv, "step_layout", 11, newRV_noinc((SV*)hv));
	STORE_PTR_FIELD(hv, resp_msg, cred, "TODO");
	STORE_PTR_FIELD(hv, resp_msg, switch_job, "TODO");
	return 0;
}
#endif

/*
 * convert perl HV to slurm_step_launch_params_t
 */
int
hv_to_slurm_step_launch_params(HV *hv, slurm_step_launch_params_t *params)
{
	int i, num_keys;
	STRLEN vlen;
	I32 klen;
	SV **svp;
	HV *environ_hv, *local_fds_hv, *fd_hv;
	AV *argv_av;
	SV *val;
	char *env_key, *env_val;

	slurm_step_launch_params_t_init(params);

	if((svp = hv_fetch(hv, "argv", 4, FALSE))) {
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
			argv_av = (AV*)SvRV(*svp);
			params->argc = av_len(argv_av) + 1;
			if (params->argc > 0) {
				/* memory of params MUST be free-ed by libslurm-perl */
				Newz(0, params->argv, (int32_t)(params->argc + 1), char*);
				for(i = 0; i < params->argc; i ++) {
					if((svp = av_fetch(argv_av, i, FALSE)))
						*(params->argv + i) = (char*) SvPV_nolen(*svp);
					else {
						Perl_warn(aTHX_ "error fetching `argv' of job descriptor");
						free_slurm_step_launch_params_memory(params);
						return -1;
					}
				}
			}
		} else {
			Perl_warn(aTHX_ "`argv' of step launch params is not an array reference");
			return -1;
		}
	} else {
		Perl_warn(aTHX_ "`argv' missing in step launching params");
		return -1;
	}
	if((svp = hv_fetch(hv, "env", 3, FALSE))) {
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
			environ_hv = (HV*)SvRV(*svp);
			num_keys = HvKEYS(environ_hv);
			params->envc = num_keys;
			Newz(0, params->env, num_keys + 1, char*);
			
			hv_iterinit(environ_hv);
			i = 0;
			while((val = hv_iternextsv(environ_hv, &env_key, &klen))) {
				env_val = SvPV(val, vlen);
				Newz(0, (*(params->env + i)), klen + vlen + 2, char);
				sprintf((*params->env + i), "%s=%s", env_key, env_val);
				i ++;
			}
		}
		else {
			Perl_warn(aTHX_ "`env' of step launch params is not a hash reference, ignored");
		}
	}
	FETCH_FIELD(hv, params, cwd, charp, FALSE);
	FETCH_FIELD(hv, params, user_managed_io, bool, FALSE);
	FETCH_FIELD(hv, params, msg_timeout, uint32_t, FALSE);
	FETCH_FIELD(hv, params, buffered_stdio, bool, FALSE);
	FETCH_FIELD(hv, params, labelio, bool, FALSE);
	FETCH_FIELD(hv, params, profile, uint32_t, FALSE);
	FETCH_FIELD(hv, params, remote_output_filename, charp, FALSE);
	FETCH_FIELD(hv, params, remote_error_filename, charp, FALSE);
	FETCH_FIELD(hv, params, remote_input_filename, charp, FALSE);

	if ((svp = hv_fetch(hv, "local_fds", 9, FALSE))) {
		if (SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
			local_fds_hv = (HV*)SvRV(*svp);
			if ((svp = hv_fetch(local_fds_hv, "in", 2, FALSE))) {
				if (SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
					fd_hv = (HV*)SvRV(*svp);
					FETCH_FIELD(fd_hv, (&params->local_fds.input), fd, int, TRUE);
					FETCH_FIELD(fd_hv, (&params->local_fds.input), taskid, uint32_t, TRUE);
					FETCH_FIELD(fd_hv, (&params->local_fds.input), nodeid, uint32_t, TRUE);
				} else {
					Perl_warn(aTHX_ "`in' of local_fds is not a hash reference, ignored");
				}
			}
			if ((svp = hv_fetch(local_fds_hv, "out", 3, FALSE))) {
				if (SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
					fd_hv = (HV*)SvRV(*svp);
					FETCH_FIELD(fd_hv, (&params->local_fds.out), fd, int, TRUE);
					FETCH_FIELD(fd_hv, (&params->local_fds.out), taskid, uint32_t, TRUE);
					FETCH_FIELD(fd_hv, (&params->local_fds.out), nodeid, uint32_t, TRUE);
				} else {
					Perl_warn(aTHX_ "`out' of local_fds is not a hash reference, ignored");
				}
			}
			if ((svp = hv_fetch(local_fds_hv, "err", 3, FALSE))) {
				if (SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
					fd_hv = (HV*)SvRV(*svp);
					FETCH_FIELD(fd_hv, (&params->local_fds.err), fd, int, TRUE);
					FETCH_FIELD(fd_hv, (&params->local_fds.err), taskid, uint32_t, TRUE);
					FETCH_FIELD(fd_hv, (&params->local_fds.err), nodeid, uint32_t, TRUE);
				} else {
					Perl_warn(aTHX_ "`err' of local_fds is not a hash reference, ignored");
				}
			}
		} else {
			Perl_warn(aTHX_ "`local_fds' of step launch params is not a hash reference, ignored");
		}
	}

	FETCH_FIELD(hv, params, gid, uint32_t, FALSE);
	FETCH_FIELD(hv, params, multi_prog, bool, FALSE);
	FETCH_FIELD(hv, params, slurmd_debug, uint32_t, FALSE);
	FETCH_FIELD(hv, params, parallel_debug, bool, FALSE);
	FETCH_FIELD(hv, params, task_prolog, charp, FALSE);
	FETCH_FIELD(hv, params, task_epilog, charp, FALSE);
	FETCH_FIELD(hv, params, cpu_bind_type, uint16_t, FALSE);
	FETCH_FIELD(hv, params, cpu_bind, charp, FALSE);
	FETCH_FIELD(hv, params, cpu_freq_min, uint32_t, FALSE);
	FETCH_FIELD(hv, params, cpu_freq_max, uint32_t, FALSE);
	FETCH_FIELD(hv, params, cpu_freq_gov, uint32_t, FALSE);
	FETCH_FIELD(hv, params, mem_bind_type, uint16_t, FALSE);
	FETCH_FIELD(hv, params, mem_bind, charp, FALSE);

	FETCH_FIELD(hv, params, max_sockets, uint16_t, FALSE);
	FETCH_FIELD(hv, params, max_cores, uint16_t, FALSE);
	FETCH_FIELD(hv, params, max_threads, uint16_t, FALSE);
	FETCH_FIELD(hv, params, cpus_per_task, uint16_t, FALSE);
	FETCH_FIELD(hv, params, task_dist, uint16_t, FALSE);
	FETCH_FIELD(hv, params, preserve_env, bool, FALSE);

	FETCH_FIELD(hv, params, mpi_plugin_name, charp, FALSE);
	FETCH_FIELD(hv, params, open_mode, uint8_t, FALSE);
	FETCH_FIELD(hv, params, acctg_freq, charp, FALSE);
	FETCH_FIELD(hv, params, pty, bool, FALSE);
	FETCH_FIELD(hv, params, ckpt_dir, charp, FALSE);
	FETCH_FIELD(hv, params, restart_dir, charp, FALSE);
	
	if((svp = hv_fetch(hv, "spank_job_env", 13, FALSE))) {
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
			environ_hv = (HV*)SvRV(*svp);
			num_keys = HvKEYS(environ_hv);
			params->spank_job_env_size = num_keys;
			Newz(0, params->spank_job_env, num_keys + 1, char*);
			
			hv_iterinit(environ_hv);
			i = 0;
			while((val = hv_iternextsv(environ_hv, &env_key, &klen))) {
				env_val = SvPV(val, vlen);
				Newz(0, (*(params->spank_job_env + i)), klen + vlen + 2, char);
				sprintf((*params->spank_job_env + i), "%s=%s", env_key, env_val);
				i ++;
			}
		}
		else {
			Perl_warn(aTHX_ "`spank_job_env' of step launch params is not a hash reference, ignored");
		}
	}

	return 0;
}

/*
 * free allocated environment variable memory for slurm_step_launch_params_t 
 */
static void
_free_env(char** environ)
{
	int i;
	if(! environ)
		return;
	for(i = 0; *(environ + i) ; i ++)
		Safefree(*(environ + i));
	Safefree(environ);
}

/*
 * free allocated memory for slurm_step_launch_params_t
 */
void
free_slurm_step_launch_params_memory(slurm_step_launch_params_t *params)
{
	if (params->argv)
		Safefree (params->argv);
	_free_env(params->env);
	_free_env(params->spank_job_env);
}

/********** conversion functions for callback **********/
static int
launch_tasks_response_msg_to_hv(launch_tasks_response_msg_t *resp_msg, HV *hv)
{
	AV *av, *av2;
	int i;

	STORE_FIELD(hv, resp_msg, return_code, uint32_t);
	if (resp_msg->node_name)
		STORE_FIELD(hv, resp_msg, node_name, charp);
	STORE_FIELD(hv, resp_msg, srun_node_id, uint32_t);
	STORE_FIELD(hv, resp_msg, count_of_pids, uint32_t);
	if (resp_msg->count_of_pids > 0) {
		av = newAV();
		av2 = newAV();
		for (i = 0; i < resp_msg->count_of_pids; i ++) {
			av_store_uint32_t(av, i, resp_msg->local_pids[i]);
			av_store_uint32_t(av2, i, resp_msg->task_ids[i]);
		}
		hv_store_sv(hv, "local_pids", newRV_noinc((SV*)av));
		hv_store_sv(hv, "task_ids", newRV_noinc((SV*)av2));
	}
	return 0;
}

static int
task_exit_msg_to_hv(task_exit_msg_t *exit_msg, HV *hv)
{
	AV *av;
	int i;

	STORE_FIELD(hv, exit_msg, num_tasks, uint32_t);
	if (exit_msg->num_tasks > 0) {
		av = newAV();
		for (i = 0; i < exit_msg->num_tasks; i ++) {
			av_store_uint32_t(av, i, exit_msg->task_id_list[i]);
		}
		hv_store_sv(hv, "task_id_list", newRV_noinc((SV*)av));
	}
	STORE_FIELD(hv, exit_msg, return_code, uint32_t);
	STORE_FIELD(hv, exit_msg, job_id, uint32_t);
	STORE_FIELD(hv, exit_msg, step_id, uint32_t);
	return 0;
}


/********** callback related functions **********/

/* 
 * In the C api, callbacks are associated with step_ctx->launch_state.
 * Since the callback functions have no parameter like "ctx" or "sls",
 * there is no simple way to map Perl callback to C callback.
 *
 * So, only one $step_ctx->launch() call is allowed in Perl, until 
 * $step_ctx->launch_wait_finish().
 */

static SV *task_start_cb_sv = NULL;
static SV *task_finish_cb_sv = NULL;

static PerlInterpreter *main_perl = NULL;
static pthread_key_t cbs_key;

typedef struct thread_callbacks {
	SV *step_complete;
	SV *step_signal;
	SV *step_timeout;
	SV *task_start;
	SV *task_finish;
} thread_callbacks_t;

static void
set_thread_perl(void)
{
	PerlInterpreter *thr_perl = PERL_GET_CONTEXT;
	
	if (thr_perl == NULL) {
		if (main_perl == NULL) { /* should never happen */
			fprintf(stderr, "error: no main perl context\n");
			exit(-1);
		}
		thr_perl = perl_clone(main_perl, CLONEf_COPY_STACKS | CLONEf_KEEP_PTR_TABLE);
		/* seems no need to call PERL_SET_CONTEXT(thr_perl); */

		/* 
		 * seems perl will destroy the interpreter associated with
		 * a thread automatically.
		 */
	}
}

#define GET_THREAD_CALLBACKS ((thread_callbacks_t *)pthread_getspecific(cbs_key))
#define SET_THREAD_CALLBACKS(cbs) (pthread_setspecific(cbs_key, (void *)cbs))

static void
clear_thread_callbacks(void *arg)
{
	thread_callbacks_t *cbs = (thread_callbacks_t *)arg;
	if (cbs->task_start) {
		/* segfault if called. dunno why */
		/* SvREFCNT_dec(cbs->task_start); */
	}
	if (cbs->task_finish) {
		/* SvREFCNT_dec(cbs->task_finish); */
	}
	xfree(cbs);
}

static void
set_thread_callbacks(void)
{
	CLONE_PARAMS params;
	thread_callbacks_t *cbs = GET_THREAD_CALLBACKS;

	if (cbs != NULL) 
		return;

	cbs = xmalloc(sizeof(thread_callbacks_t));
	if (!cbs) {
		fprintf(stderr, "set_thread_callbacks: memory exhausted\n");
		exit(-1);
	}

	params.stashes = NULL;
	params.flags = CLONEf_COPY_STACKS | CLONEf_KEEP_PTR_TABLE;
	params.proto_perl = PERL_GET_CONTEXT;
	
	if (task_start_cb_sv != NULL && task_start_cb_sv != &PL_sv_undef) {
		cbs->task_start = sv_dup(task_start_cb_sv, &params);
	}
	if (task_finish_cb_sv != NULL && task_finish_cb_sv != &PL_sv_undef) {
		cbs->task_finish = sv_dup(task_finish_cb_sv, &params);
	}
	if (SET_THREAD_CALLBACKS(cbs) != 0) {
		fprintf(stderr, "set_thread_callbacks: failed to set thread specific value\n");
		exit(-1);
	}
}

void 
set_slcb(HV *callbacks)
{
	SV **svp, *cb;

	svp = hv_fetch(callbacks, "task_start", 10, FALSE);
	cb = svp ? *svp : &PL_sv_undef;
	if (task_start_cb_sv == NULL) {
		task_start_cb_sv = newSVsv(cb);
	} else {
		sv_setsv(task_start_cb_sv, cb);
	}

	svp = hv_fetch(callbacks, "task_finish", 11, FALSE);
	cb = svp ? *svp : &PL_sv_undef;
	if (task_finish_cb_sv == NULL) {
		task_finish_cb_sv = newSVsv(cb);
	} else {
		sv_setsv(task_finish_cb_sv, cb);
	}

	if (main_perl == NULL) {
		main_perl = PERL_GET_CONTEXT;
		if ( pthread_key_create(&cbs_key, clear_thread_callbacks) != 0) {
			fprintf(stderr, "set_slcb: failed to create cbs_key\n");
			exit(-1);
		}
	}
}

static void
step_complete_cb(srun_job_complete_msg_t *comp_msg)
{
	HV *hv;
	thread_callbacks_t *cbs = NULL;

	set_thread_perl();
	set_thread_callbacks();

	cbs = GET_THREAD_CALLBACKS;
	if (cbs->step_complete == NULL)
		return;

	hv = newHV();
	if (srun_job_complete_msg_to_hv(comp_msg, hv) < 0) {
		Perl_warn( aTHX_ "failed to prepare parameter for step_complete callback");
		SvREFCNT_dec(hv);
		return;
	}

	dSP;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_noinc((SV*)hv)));
	PUTBACK;

	call_sv(cbs->step_complete, G_SCALAR);

	FREETMPS;
	LEAVE;
}

static void
step_signal_cb(int signo)
{
	thread_callbacks_t *cbs = NULL;

	set_thread_perl();
	set_thread_callbacks();

	cbs = GET_THREAD_CALLBACKS;
	if (cbs->step_signal == NULL)
		return;

	dSP;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newSViv(signo)));
	PUTBACK;

	call_sv(cbs->step_signal, G_SCALAR);

	FREETMPS;
	LEAVE;
}

static void
step_timeout_cb(srun_timeout_msg_t *timeout_msg)
{
	HV *hv;
	thread_callbacks_t *cbs = NULL;

	set_thread_perl();
	set_thread_callbacks();

	cbs = GET_THREAD_CALLBACKS;
	if (cbs->step_timeout == NULL)
		return;

	hv = newHV();
	if (srun_timeout_msg_to_hv(timeout_msg, hv) < 0) {
		Perl_warn( aTHX_ "failed to prepare parameter for step_timeout callback");
		SvREFCNT_dec(hv);
		return;
	}

	dSP;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_noinc((SV*)hv)));
	PUTBACK;

	call_sv(cbs->step_timeout, G_SCALAR);

	FREETMPS;
	LEAVE;
}

static void
task_start_cb(launch_tasks_response_msg_t *resp_msg)
{
	HV *hv;
	thread_callbacks_t *cbs = NULL;

	set_thread_perl();
	set_thread_callbacks();

	cbs = GET_THREAD_CALLBACKS;
	if (cbs->task_start == NULL)
		return;

	hv = newHV();
	if (launch_tasks_response_msg_to_hv(resp_msg, hv) < 0) {
		Perl_warn( aTHX_ "failed to prepare parameter for task_start callback");
		SvREFCNT_dec(hv);
		return;
	}

	dSP;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_noinc((SV*)hv)));
	PUTBACK;

	call_sv(cbs->task_start, G_SCALAR);

	FREETMPS;
	LEAVE;
}

static void
task_finish_cb(task_exit_msg_t *exit_msg)
{
	HV *hv;
	thread_callbacks_t *cbs = NULL;

	set_thread_perl();
	set_thread_callbacks();

	cbs = GET_THREAD_CALLBACKS;
	if (cbs->task_finish == NULL)
		return;

	hv = newHV();
	if (task_exit_msg_to_hv(exit_msg, hv) < 0) {
		Perl_warn( aTHX_ "failed to prepare parameter for task_exit callback");
		SvREFCNT_dec(hv);
		return;
	}

	dSP;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_noinc((SV*)hv)));
	PUTBACK;

	call_sv(cbs->task_finish, G_VOID);

	FREETMPS;
	LEAVE;
}

slurm_step_launch_callbacks_t slcb = {
	step_complete_cb,
	step_signal_cb,
	step_timeout_cb,
	task_start_cb,
	task_finish_cb
};
