/*
 * step_ctx.c - convert data between step context related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "ppport.h"

#include <slurm/slurm.h>
#include "slurm-perl.h"

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
	HV *step_id_hv = (HV*)sv_2mortal((SV*)newHV());

	step_id_to_hv(&exit_msg->step_id, step_id_hv);
	hv_store_sv(hv, "step_id", newRV((SV*)step_id_hv));

	STORE_FIELD(hv, exit_msg, num_tasks, uint32_t);
	if (exit_msg->num_tasks > 0) {
		av = newAV();
		for (i = 0; i < exit_msg->num_tasks; i ++) {
			av_store_uint32_t(av, i, exit_msg->task_id_list[i]);
		}
		hv_store_sv(hv, "task_id_list", newRV_noinc((SV*)av));
	}
	STORE_FIELD(hv, exit_msg, return_code, uint32_t);

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
