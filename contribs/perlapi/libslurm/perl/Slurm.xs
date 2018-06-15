#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#define NEED_newRV_noinc_GLOBAL
#include "ppport.h"

#include <slurm/slurm.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "slurm-perl.h"
#include "bitstr.h"

extern void slurm_conf_reinit(char *pathname);

/* Custom typemap that free's memory after copying to perl stack. */
typedef char char_xfree;
typedef char char_free;

struct slurm {
};
typedef struct slurm * slurm_t;

/*
 * default slurm object, for backward compatibility with "Slurm->method()".
 */
static struct slurm default_slurm_object;

static slurm_t
new_slurm(void)
{
	int size = sizeof(struct slurm);
	if (size == 0) {
		/* Avoid returning NULL, which causes the perl APIs to fail */
		size = 1;
	}
	return xmalloc(size);
}

static void
free_slurm(slurm_t self)
{
	xfree(self);
}



/********************************************************************/

MODULE = Slurm		PACKAGE = Slurm		PREFIX=slurm_
PROTOTYPES: ENABLE

######################################################################
# 	CONSTRUCTOR/DESTRUCTOR FUNCTIONS
######################################################################

#
# $slurm = Slurm::new($conf_file);
#
slurm_t
slurm_new(char *conf_file=NULL)
	CODE:
		if(conf_file) {
			slurm_conf_reinit(conf_file);
		}
		RETVAL = new_slurm();
		if (RETVAL == NULL) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

void
slurm_DESTROY(slurm_t self)
	CODE:
		if (self != &default_slurm_object) {
			free_slurm(self);
		}

######################################################################
# 	ERROR INFORMATION FUNCTIONS
######################################################################

int
slurm_get_errno(slurm_t self)
	C_ARGS:
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */

char *
slurm_strerror(slurm_t self, int errnum=0)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (errnum == 0)
			errnum = slurm_get_errno();
		RETVAL = slurm_strerror(errnum);
	OUTPUT:
		RETVAL


######################################################################
# 	ENTITY STATE/REASON/FLAG/TYPE STRING FUNCTIONS
######################################################################
#
# These functions are made object method instead of class method.

char *
slurm_preempt_mode_string(slurm_t self, uint16_t preempt_mode);
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		RETVAL = slurm_preempt_mode_string(preempt_mode);
	OUTPUT:
		RETVAL

uint16_t
slurm_preempt_mode_num(slurm_t self, char *preempt_mode)
	C_ARGS:
		preempt_mode
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */

char *
slurm_job_reason_string(slurm_t self, uint32_t inx)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		RETVAL = slurm_job_reason_string(inx);
	OUTPUT:
		RETVAL

char *
slurm_job_state_string(slurm_t self, uint32_t inx)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		RETVAL = slurm_job_state_string(inx);
	OUTPUT:
		RETVAL

char *
slurm_job_state_string_compact(slurm_t self, uint32_t inx)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		RETVAL = slurm_job_state_string_compact(inx);
	OUTPUT:
		RETVAL

int
slurm_job_state_num(slurm_t self, char *state_name)
	C_ARGS:
		state_name
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */

char_xfree *
slurm_reservation_flags_string(slurm_t self, uint16_t flags)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		RETVAL = slurm_reservation_flags_string(flags);
	OUTPUT:
		RETVAL

char *
slurm_node_state_string(slurm_t self, uint32_t inx)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		RETVAL = slurm_node_state_string(inx);
	OUTPUT:
		RETVAL

char *
slurm_node_state_string_compact(slurm_t self, uint32_t inx)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		RETVAL = slurm_node_state_string_compact(inx);
	OUTPUT:
		RETVAL

char *
slurm_private_data_string(slurm_t self, uint16_t private_data)
	PREINIT:
		char tmp_str[128];
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		slurm_private_data_string(private_data, tmp_str, sizeof(tmp_str));
		RETVAL = tmp_str;
	OUTPUT:
		RETVAL

char *
slurm_accounting_enforce_string(slurm_t self, uint16_t enforce)
	PREINIT:
		char tmp_str[128];
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		slurm_accounting_enforce_string(enforce, tmp_str, sizeof(tmp_str));
		RETVAL = tmp_str;
	OUTPUT:
		RETVAL

char *
slurm_node_use_string(slurm_t self, uint16_t node_use)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		RETVAL = slurm_node_use_string((enum node_use_type)node_use);
	OUTPUT:
		RETVAL

######################################################################
# 	RESOURCE ALLOCATION FUNCTIONS
######################################################################

#
# $resp = $slurm->allocate_resources($desc);
HV *
slurm_allocate_resources(slurm_t self, HV *job_desc)
	PREINIT:
		job_desc_msg_t jd_msg;
		resource_allocation_response_msg_t* resp_msg = NULL;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (hv_to_job_desc_msg(job_desc, &jd_msg) < 0) {
			XSRETURN_UNDEF;
		}
		rc = slurm_allocate_resources(&jd_msg, &resp_msg);
		free_job_desc_msg_memory(&jd_msg);
		if (resp_msg == NULL) {
			XSRETURN_UNDEF;
		}
		if(rc != SLURM_SUCCESS) {
			slurm_free_resource_allocation_response_msg(resp_msg);
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
		rc = resource_allocation_response_msg_to_hv(resp_msg, RETVAL);
		slurm_free_resource_allocation_response_msg(resp_msg);
		if (rc < 0) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

HV *
slurm_allocate_resources_blocking(slurm_t self, HV *user_req, time_t timeout=0, SV *pending_callback=NULL)
	PREINIT:
		job_desc_msg_t jd_msg;
		resource_allocation_response_msg_t *resp_msg = NULL;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (hv_to_job_desc_msg(user_req, &jd_msg) < 0) {
			XSRETURN_UNDEF;
		}
		set_sarb_cb(pending_callback);
		resp_msg = slurm_allocate_resources_blocking(&jd_msg, timeout,
				pending_callback == NULL ? NULL : sarb_cb);
		free_job_desc_msg_memory(&jd_msg);
		if (resp_msg != NULL) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			resource_allocation_response_msg_to_hv(resp_msg, RETVAL);
			slurm_free_resource_allocation_response_msg(resp_msg);
		}
		else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL


HV *
slurm_allocation_lookup(slurm_t self, uint32_t job_id)
	PREINIT:
		resource_allocation_response_msg_t *resp_msg = NULL;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_allocation_lookup(job_id, &resp_msg);
		if(rc != SLURM_SUCCESS) {
			slurm_free_resource_allocation_response_msg(resp_msg);
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
		rc = resource_allocation_response_msg_to_hv(resp_msg, RETVAL);
		slurm_free_resource_allocation_response_msg(resp_msg);
		if (rc < 0) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

char_free *
slurm_read_hostfile(slurm_t self, char *filename, int n)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		RETVAL = slurm_read_hostfile(filename, n);
		if(RETVAL == NULL) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

allocation_msg_thread_t *
slurm_allocation_msg_thr_create(slurm_t self, OUT uint16_t port, HV *callbacks)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		set_sacb(callbacks);
	C_ARGS:
		&port, &sacb

void
slurm_allocation_msg_thr_destroy(slurm_t self, allocation_msg_thread_t * msg_thr)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */

	C_ARGS:
		msg_thr

HV *
slurm_submit_batch_job(slurm_t self, HV *job_desc)
	PREINIT:
		job_desc_msg_t jd_msg;
		submit_response_msg_t *resp_msg = NULL;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_job_desc_msg(job_desc, &jd_msg) < 0) {
			XSRETURN_UNDEF;
		}
		rc = slurm_submit_batch_job(&jd_msg, &resp_msg);
		free_job_desc_msg_memory(&jd_msg);
		if(rc != SLURM_SUCCESS) {
			slurm_free_submit_response_response_msg(resp_msg);
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
		rc = submit_response_msg_to_hv(resp_msg, RETVAL);
		slurm_free_submit_response_response_msg(resp_msg);
		if (rc < 0) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

int
slurm_job_will_run(slurm_t self, HV *job_desc)
	PREINIT:
		job_desc_msg_t jd_msg;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (hv_to_job_desc_msg(job_desc, &jd_msg) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_job_will_run(&jd_msg);
		free_job_desc_msg_memory(&jd_msg);
	OUTPUT:
		RETVAL

HV *
slurm_sbcast_lookup(slurm_t self, uint32_t job_id, uint32_t step_id)
	PREINIT:
		job_sbcast_cred_msg_t *info;
		int rc;
		uint32_t pack_job_offset = NO_VAL;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_sbcast_lookup(job_id, pack_job_offset, step_id,
					 &info);
		if (rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = job_sbcast_cred_msg_to_hv(info, RETVAL);
			slurm_free_sbcast_cred_msg(info);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL


######################################################################
#	JOB/STEP SIGNALING FUNCTIONS
######################################################################
int
slurm_kill_job(slurm_t self, uint32_t job_id, uint16_t signal, uint16_t batch_flag=0)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, signal, batch_flag

int
slurm_kill_job_step(slurm_t self, uint32_t job_id, uint32_t step_id, uint16_t signal)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id, signal

int
slurm_signal_job(slurm_t self, uint32_t job_id, uint16_t signal)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, signal

int
slurm_signal_job_step(slurm_t self, uint32_t job_id, uint32_t step_id, uint16_t signal)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id, signal


######################################################################
#	JOB/STEP COMPLETION FUNCTIONS
######################################################################
int
slurm_complete_job(slurm_t self, uint32_t job_id, uint32_t job_rc=0)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, job_rc

int
slurm_terminate_job_step(slurm_t self, uint32_t job_id, uint32_t step_id)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id


######################################################################
#       SLURM TASK SPAWNING FUNCTIONS
######################################################################
MODULE=Slurm PACKAGE=Slurm PREFIX=slurm_

# $ctx = $slurm->step_ctx_create($params);
slurm_step_ctx_t *
slurm_step_ctx_create(slurm_t self, HV *step_params)
	PREINIT:
		slurm_step_ctx_params_t sp;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (hv_to_slurm_step_ctx_params(step_params, &sp) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_step_ctx_create(&sp);
		if (RETVAL == NULL) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

slurm_step_ctx_t *
slurm_step_ctx_create_no_alloc(slurm_t self, HV *step_params, uint32_t step_id)
	PREINIT:
		slurm_step_ctx_params_t sp;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (hv_to_slurm_step_ctx_params(step_params, &sp) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_step_ctx_create_no_alloc(&sp, step_id);
		if (RETVAL == NULL) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

######################################################################
MODULE=Slurm PACKAGE=Slurm::Stepctx PREFIX=slurm_step_ctx_
int
slurm_step_ctx_get(slurm_step_ctx_t *ctx, int ctx_key, INOUT ...)
	PREINIT:
		uint32_t tmp_32, *tmp_32_ptr;
		uint16_t tmp_16, *tmp_16_ptr;
#if 0
		/* TODO: job_step_create_response_msg_t not exported in slurm.h */
		job_step_create_response_msg_t *resp_msg;
#endif
		slurm_cred_t *cred;
		dynamic_plugin_data_t *switch_info;
		char *tmp_str;
		int i, tmp_int, *tmp_int_ptr;
	CODE:
		switch(ctx_key) {
		case SLURM_STEP_CTX_JOBID: /* uint32_t* */
		case SLURM_STEP_CTX_STEPID: /* uint32_t* */
		case SLURM_STEP_CTX_NUM_HOSTS: /* uint32_t* */
			if (items != 3) {
				Perl_warn( aTHX_ "error number of parameters");
				errno = EINVAL;
				RETVAL = SLURM_ERROR;
				break;
			}
			RETVAL = slurm_step_ctx_get(ctx, ctx_key, &tmp_32);
			if (RETVAL == SLURM_SUCCESS) {
				sv_setuv(ST(2), (UV)tmp_32);
			}
			break;
		case SLURM_STEP_CTX_TASKS: /* uint16_t** */
			if (items != 3) {
				Perl_warn( aTHX_ "error number of parameters");
				errno = EINVAL;
				RETVAL = SLURM_ERROR;
				break;
			}
			RETVAL = slurm_step_ctx_get(ctx, SLURM_STEP_CTX_NUM_HOSTS, &tmp_32);
			if (RETVAL != SLURM_SUCCESS)
				break;
			RETVAL = slurm_step_ctx_get(ctx, ctx_key, &tmp_16_ptr);
			if (RETVAL == SLURM_SUCCESS) {
				AV* av = newAV();
				for(i = 0; i < tmp_32; i ++) {
					av_store_uint16_t(av, i, tmp_16_ptr[i]);
				}
				sv_setsv(ST(2), newRV_noinc((SV*)av));
			}
			break;
		case SLURM_STEP_CTX_TID: /* uint32_t, uint32_t** */
			if (items != 4) {
				Perl_warn( aTHX_ "error number of parameters");
				errno = EINVAL;
				RETVAL = SLURM_ERROR;
				break;
			}
			tmp_32 = (uint32_t)SvUV(ST(2));
			RETVAL = slurm_step_ctx_get(ctx, SLURM_STEP_CTX_TASKS, &tmp_16_ptr);
			if (RETVAL != SLURM_SUCCESS)
				break;
			tmp_16 = tmp_16_ptr[tmp_32];
			RETVAL = slurm_step_ctx_get(ctx, ctx_key, tmp_32, &tmp_32_ptr);
			if (RETVAL == SLURM_SUCCESS) {
				AV* av = newAV();
				for(i = 0; i < tmp_16; i ++) {
					av_store_uint32_t(av, i, tmp_32_ptr[i]);
				}
				sv_setsv(ST(3), newRV_noinc((SV*)av));
			}
			break;
#if 0
		case SLURM_STEP_CTX_RESP: /* job_step_create_response_msg_t** */
			if (items != 3) {
				Perl_warn( aTHX_ "error number of parameters");
				errno = EINVAL;
				RETVAL = SLURM_ERROR;
				break;
			}
			RETVAL = slurm_step_ctx_get(ctx, ctx_key, &resp_msg);
			if (RETVAL == SLURM_SUCCESS) {
				HV *hv = newHV();
				if (job_step_create_response_msg_to_hv(resp_msg, hv) < 0) {
					SVdev_REFCNT((SV*)hv);
					RETVAL = SLURM_ERROR;
					break;
				}
				sv_setsv(ST(2), newRV_noinc((SV*)hv));
			}
			break;
#endif
		case SLURM_STEP_CTX_CRED: /* slurm_cred_t** */
			if (items != 3) {
				Perl_warn( aTHX_ "error number of parameters");
				errno = EINVAL;
				RETVAL = SLURM_ERROR;
				break;
			}
			RETVAL = slurm_step_ctx_get(ctx, ctx_key, &cred);
			if (RETVAL == SLURM_SUCCESS && cred) {
				sv_setref_pv(ST(2), "Slurm::slurm_cred_t", (void*)cred);
			} else if (RETVAL == SLURM_SUCCESS) {
				/* the returned cred is NULL */
				sv_setsv(ST(2), &PL_sv_undef);
			}
			break;
		case SLURM_STEP_CTX_SWITCH_JOB: /* switch_jobinfo_t** */
			if (items != 3) {
				Perl_warn( aTHX_ "error number of parameters");
				errno = EINVAL;
				RETVAL = SLURM_ERROR;
				break;
			}
			RETVAL = slurm_step_ctx_get(ctx, ctx_key, &switch_info);
			if (RETVAL == SLURM_SUCCESS && switch_info) {
				sv_setref_pv(ST(2), "Slurm::dynamic_plugin_data_t", (void*)switch_info);
			} else if (RETVAL == SLURM_SUCCESS) {
				/* the returned switch_info is NULL */
				sv_setsv(ST(2), &PL_sv_undef);
			}
			break;
		case SLURM_STEP_CTX_HOST: /* uint32_t, char** */
			if (items != 4) {
				Perl_warn( aTHX_ "error number of parameters");
				errno = EINVAL;
				RETVAL = SLURM_ERROR;
				break;
			}
			tmp_32 = (uint32_t)SvUV(ST(2));
			RETVAL = slurm_step_ctx_get(ctx, ctx_key, tmp_32, &tmp_str);
			if (RETVAL == SLURM_SUCCESS) {
				sv_setpv(ST(3), tmp_str);
			}
			break;
		case SLURM_STEP_CTX_USER_MANAGED_SOCKETS: /* int*, int** */
			if (items != 4) {
				Perl_warn( aTHX_ "error number of parameters");
				errno = EINVAL;
				RETVAL = SLURM_ERROR;
				break;
			}
			RETVAL = slurm_step_ctx_get(ctx, ctx_key, &tmp_int, &tmp_int_ptr);
			if (RETVAL == SLURM_SUCCESS) {
				AV *av = newAV();
				for (i = 0; i < tmp_int; i ++) {
					av_store_int(av, i, tmp_int_ptr[i]);
				}
				sv_setiv(ST(2), tmp_int);
				sv_setsv(ST(3), newRV_noinc((SV*)av));
			} else { /* returned val: 0, NULL */
				sv_setiv(ST(2), tmp_int);
				sv_setsv(ST(3), &PL_sv_undef);
			}
			break;
		default:
			RETVAL = slurm_step_ctx_get(ctx, ctx_key);
		}
	OUTPUT:
		RETVAL

# TODO: data_type not exported in slurm.h
#int
#slurm_job_info_ctx_get(switch_jobinfo_t *jobinfo, int data_type, void *data)

void
slurm_step_ctx_DESTROY(slurm_step_ctx_t *ctx)
	CODE:
		slurm_step_ctx_destroy(ctx);

int
slurm_step_ctx_daemon_per_node_hack(slurm_step_ctx_t *ctx, char *node_list, uint32_t node_cnt, void *curr_task_num)
        PREINIT:
	        uint32_t *tmp32;
        CODE:
	        tmp32 = (uint32_t *)curr_task_num;

                RETVAL = slurm_step_ctx_daemon_per_node_hack(ctx, node_list, node_cnt, tmp32);
        OUTPUT:
	        RETVAL

#####################################################################
MODULE=Slurm PACKAGE=Slurm::Stepctx PREFIX=slurm_step_

int
slurm_step_launch(slurm_step_ctx_t *ctx, HV *params, HV *callbacks=NULL)
	PREINIT:
		slurm_step_launch_params_t lp;
		slurm_step_launch_callbacks_t *cb = NULL;
	CODE:
		if (hv_to_slurm_step_launch_params(params, &lp) < 0) {
			Perl_warn( aTHX_ "failed to convert slurm_step_launch_params_t");
			RETVAL = SLURM_ERROR;
		} else {
			if (callbacks) {
				set_slcb(callbacks);
				cb = &slcb;
			}
			RETVAL = slurm_step_launch(ctx, &lp, cb, -1);
			free_slurm_step_launch_params_memory(&lp);
		}
	OUTPUT:
		RETVAL


int
slurm_step_launch_wait_start(slurm_step_ctx_t *ctx)

void
slurm_step_launch_wait_finish(slurm_step_ctx_t *ctx)

void
slurm_step_launch_abort(slurm_step_ctx_t *ctx)

void
slurm_step_launch_fwd_signal(slurm_step_ctx_t *ctx, uint16_t signo)

# TODO: this function is not implemented in libslurm
#void
#slurm_step_launch_fwd_wake(slurm_step_ctx_t *ctx)


######################################################################
#	SLURM CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################
MODULE = Slurm		PACKAGE = Slurm		PREFIX=slurm_
#
# ($major, $minor, $micro) = $slurm->api_version();
#
void
slurm_api_version(slurm_t self, OUTLIST int major, OUTLIST int minor, OUTLIST int micro)
	PREINIT:
		long version;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		version = slurm_api_version();
		major = SLURM_VERSION_MAJOR(version);
		minor = SLURM_VERSION_MINOR(version);
		micro = SLURM_VERSION_MICRO(version);

HV *
slurm_load_ctl_conf(slurm_t self, time_t update_time=0)
	PREINIT:
		slurm_ctl_conf_t *ctl_conf;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_load_ctl_conf(update_time, &ctl_conf);
		if(rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = slurm_ctl_conf_to_hv(ctl_conf, RETVAL);
			slurm_free_ctl_conf(ctl_conf);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

void
slurm_print_ctl_conf(slurm_t self, FILE *out, HV *conf)
	PREINIT:
		slurm_ctl_conf_t cc;
	INIT:
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if (hv_to_slurm_ctl_conf(conf, &cc) < 0) {
			XSRETURN_UNDEF;
		}
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		out, &cc

#
# $key_pairs = $slurm->ctl_conf_2_key_pairs($conf);
# XXX: config_key_pair_t not exported
#
List
slurm_ctl_conf_2_key_pairs(slurm_t self, HV *conf)
	PREINIT:
		slurm_ctl_conf_t cc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (hv_to_slurm_ctl_conf(conf, &cc) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = (List)slurm_ctl_conf_2_key_pairs(&cc);
		if(RETVAL == NULL) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL


#
# $status = $slurm->load_slurmd_status();
#
HV *
slurm_load_slurmd_status(slurm_t self)
	PREINIT:
		slurmd_status_t *status;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_load_slurmd_status(&status);
		if (rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = slurmd_status_to_hv(status, RETVAL);
			slurm_free_slurmd_status(status);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

void
slurm_print_slurmd_status(slurm_t self, FILE *out, HV *slurmd_status)
	PREINIT:
		slurmd_status_t st;
	INIT:
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if (hv_to_slurmd_status(slurmd_status, &st) < 0) {
			XSRETURN_UNDEF;
		}
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		out, &st

void
slurm_print_key_pairs(slurm_t self, FILE *out, List key_pairs, char *title)
	INIT:
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		out, key_pairs, title

int
slurm_update_step(slurm_t self, HV *step_msg)
	PREINIT:
		step_update_request_msg_t su_msg;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (hv_to_step_update_request_msg(step_msg, &su_msg) < 0) {
			RETVAL = SLURM_ERROR;
		} else {
			RETVAL = slurm_update_step(&su_msg);
		}
	OUTPUT:
		RETVAL


######################################################################
#	SLURM JOB RESOURCES READ/PRINT FUNCTIONS
######################################################################
int
slurm_job_cpus_allocated_on_node_id(slurm_t self, SV *job_res, int node_id)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(job_res) {
			RETVAL = slurm_job_cpus_allocated_on_node_id(
				(job_resources_t *)SV2ptr(job_res), node_id);
		} else {
			RETVAL = 0;
		}
	OUTPUT:
		RETVAL

int
slurm_job_cpus_allocated_on_node(slurm_t self, SV *job_res, char *node_name)
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(job_res) {
			RETVAL = slurm_job_cpus_allocated_on_node(
				(job_resources_t *)SV2ptr(job_res), node_name);
		} else {
			RETVAL = 0;
		}
	OUTPUT:
		RETVAL


######################################################################
#	SLURM JOB CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################
MODULE = Slurm		PACKAGE = Slurm::job_info_msg_t		PREFIX=job_info_msg_t_

void
job_info_msg_t_DESTROY(job_info_msg_t *ji_msg)
	CODE:
		slurm_free_job_info_msg(ji_msg);


######################################################################
MODULE = Slurm		PACKAGE = Slurm		PREFIX=slurm_

# $time = $slurm->get_end_time($job_id);
time_t
slurm_get_end_time(slurm_t self, uint32_t job_id)
	PREINIT:
		time_t tmp_time;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_get_end_time(job_id, &tmp_time);
		if (rc == SLURM_SUCCESS) {
			RETVAL = tmp_time;
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

long
slurm_get_rem_time(slurm_t self, uint32_t job_id)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id

int
slurm_job_node_ready(slurm_t self, uint32_t job_id)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id

#
# $resp = $slurm->load_job($job_id, $show_flags);
#
HV *
slurm_load_job(slurm_t self, uint32_t job_id, uint16_t show_flags=0)
	PREINIT:
		job_info_msg_t *ji_msg;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_load_job(&ji_msg, job_id, show_flags);
		if (rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = job_info_msg_to_hv(ji_msg, RETVAL);
			/* cannot free ji_msg because RETVAL holds data in it */
			if (rc >= 0) {
				hv_store_ptr(RETVAL, "job_info_msg", ji_msg, "Slurm::job_info_msg_t");
			}
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

#
# $resp = $slurm->load_jobs($update_time, $show_flags);
#
HV *
slurm_load_jobs(slurm_t self, time_t update_time=0, uint16_t show_flags=0)
	PREINIT:
		job_info_msg_t *ji_msg;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_load_jobs(update_time, &ji_msg, show_flags);
		if (rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = job_info_msg_to_hv(ji_msg, RETVAL);
			/* cannot free ji_msg because RETVAL holds data in it */
			if (rc >= 0) {
				hv_store_ptr(RETVAL, "job_info_msg", ji_msg, "Slurm::job_info_msg_t");
			}
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

int
slurm_notify_job(slurm_t self, uint32_t job_id, char *message)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, message

#
# $job_id = $slurm->pid2jobid($job_pid);
#
uint32_t
slurm_pid2jobid(slurm_t self, pid_t job_pid)
	PREINIT:
		uint32_t tmp_pid;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_pid2jobid(job_pid, &tmp_pid);
		if (rc == SLURM_SUCCESS) {
			RETVAL = tmp_pid;
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

void
slurm_print_job_info(slurm_t self, FILE* out, HV *job_info, int one_liner=0)
	PREINIT:
		job_info_t ji;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if (hv_to_job_info(job_info, &ji) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &ji, one_liner
	CLEANUP:
		xfree(ji.exc_node_inx);
		xfree(ji.node_inx);
		xfree(ji.req_node_inx);

void
slurm_print_job_info_msg(slurm_t self, FILE *out, HV *job_info_msg, int one_liner=0)
	PREINIT:
		job_info_msg_t ji_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if (hv_to_job_info_msg(job_info_msg, &ji_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &ji_msg, one_liner
	CLEANUP:
		xfree(ji_msg.job_array);

char_xfree *
slurm_sprint_job_info(slurm_t self, HV *job_info, int one_liner=0)
	PREINIT:
		job_info_t ji;
		char *tmp_str = NULL;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_job_info(job_info, &ji) < 0) {
			XSRETURN_UNDEF;
		}
		tmp_str = slurm_sprint_job_info(&ji, one_liner);
		xfree(ji.exc_node_inx);
		xfree(ji.node_inx);
		xfree(ji.req_node_inx);
		RETVAL = tmp_str;
	OUTPUT:
		RETVAL

int
slurm_update_job(slurm_t self, HV *job_info)
	PREINIT:
		job_desc_msg_t update_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_job_desc_msg(job_info, &update_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&update_msg
	CLEANUP:
		free_job_desc_msg_memory(&update_msg);



######################################################################
#	SLURM JOB STEP CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################

HV *
slurm_get_job_steps(slurm_t self, time_t update_time=0, uint32_t job_id=NO_VAL, uint32_t step_id=NO_VAL, uint16_t show_flags=0)
	PREINIT:
		int rc;
		job_step_info_response_msg_t *resp_msg;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_get_job_steps(update_time, job_id, step_id, &resp_msg, show_flags);
		if(rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = job_step_info_response_msg_to_hv(resp_msg, RETVAL);
			slurm_free_job_step_info_response_msg(resp_msg);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

void
slurm_print_job_step_info_msg(slurm_t self, FILE *out, HV *step_info_msg, int one_liner=0)
	PREINIT:
		job_step_info_response_msg_t si_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_job_step_info_response_msg(step_info_msg, &si_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &si_msg, one_liner
	CLEANUP:
		xfree(si_msg.job_steps);

void
slurm_print_job_step_info(slurm_t self, FILE *out, HV *step_info, int one_liner=0)
	PREINIT:
		job_step_info_t si;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_job_step_info(step_info, &si) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &si, one_liner
	CLEANUP:
		xfree(si.node_inx);

char_xfree *
slurm_sprint_job_step_info(slurm_t self, HV *step_info, int one_liner=0)
	PREINIT:
		job_step_info_t si;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_job_step_info(step_info, &si) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_sprint_job_step_info(&si, one_liner);
		xfree(si.node_inx);
	OUTPUT:
		RETVAL

HV *
slurm_job_step_layout_get(slurm_t self, uint32_t job_id, uint32_t step_id)
	PREINIT:
		int rc;
		slurm_step_layout_t *layout;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		layout = slurm_job_step_layout_get(job_id, step_id);
		if(layout == NULL) {
			XSRETURN_UNDEF;
		} else {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = slurm_step_layout_to_hv(layout, RETVAL);
			slurm_job_step_layout_free(layout);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		}
	OUTPUT:
		RETVAL

HV *
slurm_job_step_stat(slurm_t self, uint32_t job_id, uint32_t step_id, char *nodelist=NULL, uint16_t protocol_version)
	PREINIT:
		int rc;
		job_step_stat_response_msg_t *resp_msg;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
                rc = slurm_job_step_stat(job_id, step_id, nodelist,
					 protocol_version, &resp_msg);
		if (rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = job_step_stat_response_msg_to_hv(resp_msg, RETVAL);
			slurm_job_step_stat_response_msg_free(resp_msg);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			errno = rc;
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

HV *
slurm_job_step_get_pids(slurm_t self, uint32_t job_id, uint32_t step_id, char *nodelist=NULL)
	PREINIT:
		int rc;
		job_step_pids_response_msg_t *resp_msg = NULL;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_job_step_get_pids(job_id, step_id, nodelist, &resp_msg);
		if (rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = job_step_pids_response_msg_to_hv(resp_msg, RETVAL);
			slurm_job_step_pids_response_msg_free(resp_msg);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			errno = rc;
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

######################################################################
#	SLURM NODE CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################
MODULE = Slurm		PACKAGE = Slurm::node_info_msg_t	PREFIX=node_info_msg_t_

void
node_info_msg_t_DESTROY(node_info_msg_t *ni_msg)
	CODE:
		slurm_free_node_info_msg(ni_msg);


######################################################################
MODULE = Slurm		PACKAGE = Slurm		PREFIX=slurm_

HV *
slurm_load_node(slurm_t self, time_t update_time=0, uint16_t show_flags=0)
	PREINIT:
		node_info_msg_t *ni_msg = NULL;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_load_node(update_time, &ni_msg,
				     show_flags | SHOW_MIXED);
		if (rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			/* RETVAL holds ni_msg->select_nodeinfo, so delay free-ing the msg */
			rc = node_info_msg_to_hv(ni_msg, RETVAL);
			if (rc >= 0) {
				rc = hv_store_ptr(RETVAL, "node_info_msg", ni_msg, "Slurm::node_info_msg_t");
			}
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

HV *
slurm_load_single_node(slurm_t self, char *node_name, uint16_t show_flags=0)
	PREINIT:
		node_info_msg_t *ni_msg = NULL;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_load_node_single(&ni_msg, node_name, show_flags | SHOW_MIXED);

		if (rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			/* RETVAL holds ni_msg->select_nodeinfo, so delay free-ing the msg */
			rc = node_info_msg_to_hv(ni_msg, RETVAL);
			if (rc >= 0) {
				rc = hv_store_ptr(RETVAL, "node_info_msg", ni_msg, "Slurm::node_info_msg_t");
			}
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

void
slurm_print_node_info_msg(slurm_t self, FILE *out, HV *node_info_msg, int one_liner=0)
	PREINIT:
		node_info_msg_t ni_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_node_info_msg(node_info_msg, &ni_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &ni_msg, one_liner
	CLEANUP:
		xfree(ni_msg.node_array);

void
slurm_print_node_table(slurm_t self, FILE *out, HV *node_info, int one_liner=0)
	PREINIT:
		node_info_t ni;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_node_info(node_info, &ni) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &ni, one_liner

char_xfree *
slurm_sprint_node_table(slurm_t self, HV *node_info, int one_liner=0)
	PREINIT:
		node_info_t ni;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_node_info(node_info, &ni) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_sprint_node_table(&ni, one_liner);
	OUTPUT:
		RETVAL

int
slurm_update_node(slurm_t self, HV *update_req)
	PREINIT:
		update_node_msg_t node_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_update_node_msg(update_req, &node_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&node_msg


######################################################################
#	SLURM SWITCH TOPOLOGY CONFIGURATION READ/PRINT FUNCTIONS
######################################################################

HV *
slurm_load_topo(slurm_t self)
	PREINIT:
		topo_info_response_msg_t *topo_info_msg = NULL;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_load_topo( &topo_info_msg);
		if(rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = topo_info_response_msg_to_hv(topo_info_msg, RETVAL);
			slurm_free_topo_info_msg(topo_info_msg);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

void
slurm_print_topo_info_msg(slurm_t self, FILE *out, HV *topo_info_msg, int one_liner=0)
	PREINIT:
		topo_info_response_msg_t ti_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_topo_info_response_msg(topo_info_msg, &ti_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &ti_msg, one_liner
	CLEANUP:
		xfree(ti_msg.topo_array);

void
slurm_print_topo_record(slurm_t self, FILE *out, HV *topo_info, int one_liner=0)
	PREINIT:
		topo_info_t ti;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_topo_info(topo_info, &ti) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &ti, one_liner


######################################################################
#	SLURM SELECT READ/PRINT/UPDATE FUNCTIONS
######################################################################
#
# $rc = $slurm->get_select_nodeinfo($nodeinfo, $data_type, $state, $data);
#
int
slurm_get_select_nodeinfo(slurm_t self, dynamic_plugin_data_t *nodeinfo, uint32_t data_type, uint32_t state, SV *data)
	PREINIT:
		uint16_t tmp_16;
		char *tmp_str;
		bitstr_t *tmp_bitmap;
		select_nodeinfo_t *tmp_ptr;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		switch(data_type) {
		case SELECT_NODEDATA_BITMAP_SIZE: /* data-> uint16_t */
		case SELECT_NODEDATA_SUBGRP_SIZE: /* data-> uint16_t */
		case SELECT_NODEDATA_SUBCNT:      /* data-> uint16_t */
			RETVAL = slurm_get_select_nodeinfo(nodeinfo, data_type, state, &tmp_16);
			if (RETVAL == 0) {
				sv_setuv(data, (UV)tmp_16);
			}
			break;
		case SELECT_NODEDATA_BITMAP:      /* data-> bitstr_t * needs to be
						   * freed with FREE_NULL_BITMAP */
			RETVAL = slurm_get_select_nodeinfo(nodeinfo, data_type, state, &tmp_bitmap);
			if (RETVAL == 0) {
				sv_setref_pv(data, "Slurm::Bitstr", tmp_bitmap);
			}
			break;
		case SELECT_NODEDATA_STR:         /* data-> char *  needs to be freed with xfree */
			RETVAL = slurm_get_select_nodeinfo(nodeinfo, data_type, state, &tmp_str);
			if (RETVAL == 0) {
				char *str;
				int len = strlen(tmp_str) + 1;
				New(0, str, len, char);
				Copy(tmp_str, str, len, char);
				xfree(tmp_str);
				sv_setpvn(data, str, len);
			}
			break;
		case SELECT_NODEDATA_PTR:         /* data-> select_nodeinfo_t *nodeinfo */
			RETVAL = slurm_get_select_nodeinfo(nodeinfo, data_type, state, &tmp_ptr);
			if (RETVAL == 0) {
				sv_setref_pv(data, "Slurm::select_nodeinfo_t", (void*)tmp_ptr);
			}
			break;
		default:
			RETVAL = slurm_get_select_nodeinfo(nodeinfo, data_type, state, NULL);
		}
	OUTPUT:
		RETVAL

######################################################################
#	SLURM PARTITION CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################

HV *
slurm_load_partitions(slurm_t self, time_t update_time=0, uint16_t show_flags=0)
	PREINIT:
		partition_info_msg_t *part_info_msg;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_load_partitions(update_time, &part_info_msg,
					   show_flags);
		if (rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = partition_info_msg_to_hv(part_info_msg, RETVAL);
			slurm_free_partition_info_msg(part_info_msg);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

void
slurm_print_partition_info_msg(slurm_t self, FILE *out, HV *part_info_msg, int one_liner=0)
	PREINIT:
		partition_info_msg_t pi_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_partition_info_msg(part_info_msg, &pi_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &pi_msg, one_liner
	CLEANUP:
		xfree(pi_msg.partition_array);

void
slurm_print_partition_info(slurm_t self, FILE *out, HV *part_info, int one_liner=0)
	PREINIT:
		partition_info_t pi;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_partition_info(part_info, &pi) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &pi, one_liner
	CLEANUP:
		xfree(pi.node_inx);

char_xfree *
slurm_sprint_partition_info(slurm_t self, HV *part_info, int one_liner=0)
	PREINIT:
		partition_info_t pi;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_partition_info(part_info, &pi) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_sprint_partition_info(&pi, one_liner);
		xfree(pi.node_inx);
	OUTPUT:
		RETVAL

int
slurm_create_partition(slurm_t self, HV *part_info)
	PREINIT:
		update_part_msg_t update_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_update_part_msg(part_info, &update_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&update_msg

int
slurm_update_partition(slurm_t self, HV *part_info)
	PREINIT:
		update_part_msg_t update_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_update_part_msg(part_info, &update_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&update_msg

int
slurm_delete_partition(slurm_t self, HV *delete_part_msg)
	PREINIT:
		delete_part_msg_t dp_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_delete_part_msg(delete_part_msg, &dp_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&dp_msg


######################################################################
#	SLURM RESERVATION CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################

HV *
slurm_load_reservations(slurm_t self, time_t update_time=0)
	PREINIT:
		reserve_info_msg_t *resv_info_msg = NULL;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_load_reservations(update_time, &resv_info_msg);
		if(rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = reserve_info_msg_to_hv(resv_info_msg, RETVAL);
			slurm_free_reservation_info_msg(resv_info_msg);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

char_free *
slurm_create_reservation(slurm_t self, HV *res_info)
	PREINIT:
		resv_desc_msg_t resv_msg;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_update_reservation_msg(res_info, &resv_msg) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_create_reservation(&resv_msg);
		if (RETVAL == NULL) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

int
slurm_update_reservation(slurm_t self, HV *res_info)
	PREINIT:
		resv_desc_msg_t resv_msg;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_update_reservation_msg(res_info, &resv_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&resv_msg

int
slurm_delete_reservation(slurm_t self, HV *res_info)
	PREINIT:
		reservation_name_msg_t resv_name;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_delete_reservation_msg(res_info, &resv_name) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&resv_name

void
slurm_print_reservation_info_msg(slurm_t self, FILE *out, HV *resv_info_msg, int one_liner=0)
	PREINIT:
		reserve_info_msg_t ri_msg;
		int i;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_reserve_info_msg(resv_info_msg, &ri_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &ri_msg, one_liner
	CLEANUP:
		for (i = 0; i < ri_msg.record_count; i ++)
			xfree(ri_msg.reservation_array[i]);
		xfree(ri_msg.reservation_array);

void
slurm_print_reservation_info(slurm_t self, FILE *out, HV *resv_info, int one_liner=0)
	PREINIT:
		reserve_info_t ri;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if (out == NULL) {
			Perl_croak (aTHX_ "Invalid output stream specified: FILE not found");
		}
		if(hv_to_reserve_info(resv_info, &ri) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		out, &ri, one_liner
	CLEANUP:
		xfree(ri.node_inx);

char_xfree *
slurm_sprint_reservation_info(slurm_t self, HV *resv_info, int one_liner=0)
	PREINIT:
		reserve_info_t ri;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_reserve_info(resv_info, &ri) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_sprint_reservation_info(&ri, one_liner);
		xfree(ri.node_inx);
	OUTPUT:
		RETVAL


######################################################################
#	SLURM PING/RECONFIGURE/SHUTDOWN FUNCTIONS
######################################################################

int
slurm_ping(slurm_t self, uint16_t primary=1)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		primary

int
slurm_reconfigure(slurm_t self)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:

int
slurm_shutdown(slurm_t self, uint16_t options=0)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		options

int
slurm_takeover(slurm_t self, int backup_inx=1)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		backup_inx

int
slurm_set_debug_level(slurm_t self, uint32_t debug_level)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		debug_level

int
slurm_set_schedlog_level(slurm_t self, uint32_t schedlog_level)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		schedlog_level


######################################################################
#	SLURM JOB SUSPEND FUNCTIONS
######################################################################

int
slurm_suspend(slurm_t self, uint32_t job_id)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id

int
slurm_resume(slurm_t self, uint32_t job_id)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id

int
slurm_requeue(slurm_t self, uint32_t job_id, uint32_t state)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, state


######################################################################
#	SLURM JOB CHECKPOINT FUNCTIONS
######################################################################

int
slurm_checkpoint_able(slurm_t self, uint32_t job_id, uint32_t step_id, OUT time_t start_time)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id, &start_time

int
slurm_checkpoint_disable(slurm_t self, uint32_t job_id, uint32_t step_id)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id

int
slurm_checkpoint_enable(slurm_t self, uint32_t job_id, uint32_t step_id)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id

int
slurm_checkpoint_create(slurm_t self, uint32_t job_id, uint32_t step_id, uint16_t max_wait, char *image_dir)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id, max_wait, image_dir

int
slurm_checkpoint_requeue(slurm_t self, uint32_t job_id, uint16_t max_wait, char *image_dir)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, max_wait, image_dir

int
slurm_checkpoint_vacate(slurm_t self, uint32_t job_id, uint32_t step_id, uint16_t max_wait, char *image_dir)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id, max_wait, image_dir

int
slurm_checkpoint_restart(slurm_t self, uint32_t job_id, uint32_t step_id, uint16_t stick, char *image_dir)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id, stick, image_dir

int
slurm_checkpoint_complete(slurm_t self, uint32_t job_id, uint32_t step_id, time_t begin_time, uint32_t error_code, char *error_msg)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id, begin_time, error_code, error_msg

int
slurm_checkpoint_error(slurm_t self, uint32_t job_id, uint32_t step_id, OUT uint32_t error_code, OUT char *error_msg)
	PREINIT:
		char* err_msg = NULL;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		error_code = SLURM_SUCCESS;
		RETVAL = slurm_checkpoint_error(job_id, step_id, (uint32_t *)&error_code, &err_msg);
		Newz(0, error_msg, strlen(err_msg), char);
		Copy(err_msg, error_msg, strlen(err_msg), char);
		xfree(err_msg);
	OUTPUT:
		RETVAL

int
slurm_checkpoint_tasks(slurm_t self, uint32_t job_id, uint16_t step_id, time_t begin_time, char *image_dir, uint16_t max_wait, char *nodelist)
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	C_ARGS:
		job_id, step_id, begin_time, image_dir, max_wait, nodelist


######################################################################
#	SLURM TRIGGER FUNCTIONS
######################################################################

int slurm_set_trigger(slurm_t self, HV *trigger_info)
	PREINIT:
		trigger_info_t ti;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
	       if(hv_to_trigger_info(trigger_info, &ti) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&ti

int slurm_clear_trigger(slurm_t self, HV *trigger_info)
	PREINIT:
		trigger_info_t ti;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_trigger_info(trigger_info, &ti) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&ti

HV *
slurm_get_triggers(slurm_t self)
	PREINIT:
		trigger_info_msg_t *ti_msg;
		int rc;
	CODE:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		rc = slurm_get_triggers(&ti_msg);
		if(rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			rc = trigger_info_msg_to_hv(ti_msg, RETVAL);
			slurm_free_trigger_msg(ti_msg);
			if (rc < 0) {
				XSRETURN_UNDEF;
			}
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

int
slurm_pull_trigger(slurm_t self, HV *trigger_info)
	PREINIT:
		trigger_info_t ti;
	INIT:
		if (self); /* this is needed to avoid a warning about
			      unused variables.  But if we take slurm_t self
			      out of the mix Slurm-> doesn't work,
			      only Slurm::
			    */
		if(hv_to_trigger_info(trigger_info, &ti) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&ti

######################################################################
#	SLURM HOSTLIST FUNCTIONS
######################################################################
MODULE=Slurm PACKAGE=Slurm::Hostlist PREFIX=slurm_hostlist_

hostlist_t
slurm_hostlist_create(char* hostlist)

int
slurm_hostlist_count(hostlist_t hl)

int
slurm_hostlist_find(hostlist_t hl, char* hostname)

int
slurm_hostlist_push(hostlist_t hl, char* hosts)

int
slurm_hostlist_push_host(hostlist_t hl, char* host)

char_xfree *
slurm_hostlist_ranged_string(hostlist_t hl)
	CODE:
		RETVAL = slurm_hostlist_ranged_string_xmalloc(hl);
		if (RETVAL == NULL) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

char_free *
slurm_hostlist_shift(hostlist_t hl = NULL)
	CODE:
		RETVAL = slurm_hostlist_shift(hl);
		if (RETVAL == NULL) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

void
slurm_hostlist_uniq(hostlist_t hl)

void
slurm_hostlist_DESTROY(hostlist_t hl)
	CODE:
		slurm_hostlist_destroy(hl);


# TODO: add some non-exported functions

######################################################################
#	LIST FUNCTIONS
######################################################################
MODULE = Slurm		PACKAGE = Slurm::List		PREFIX=slurm_list_

#void
#slurm_list_append(List l, void *x)

int
slurm_list_count(List l)

int
slurm_list_is_empty(List l)

#List
#slurm_list_create(ListDelF f)

#void
#slurm_list_sort(List l, ListCmpF f)

void
slurm_list_DESTROY(List l)
	CODE:
		slurm_list_destroy(l);

##################################################################################
MODULE = Slurm		PACKAGE = Slurm::ListIterator	PREFIX=slurm_list_iterator_

#void *
#slurm_list_iterator_find(ListIterator i, ListFindF f, void *key)
#	CODE:
#		RETVAL = slurm_list_find(i, f, key)
#	OUTPUT:
#		RETVAL

ListIterator
slurm_list_iterator_create(List l)

void
slurm_list_iterator_reset(ListIterator i)

#void *
#slurm_list_iterator_next(ListIterator i)
#	CODE:
#		RETVAL = slurm_list_next(i)
#	OUTPUT:
#		RETVAL

void
slurm_list_iterator_DESTROY(ListIterator i)
	CODE:
		slurm_list_iterator_destroy(i);


######################################################################
#	BITSTRING FUNCTIONS
######################################################################
MODULE = Slurm		PACKAGE = Slurm::Bitstr	PREFIX=slurm_bit_
#
# $bitmap = Slurm::Bitstr::alloc($nbits);
bitstr_t *
slurm_bit_alloc(bitoff_t nbits)
	POSTCALL:
		if(RETVAL == NULL) {
			XSRETURN_UNDEF;
		}

bitstr_t *
slurm_bit_copy(bitstr_t *b)
	POSTCALL:
		if(RETVAL == NULL) {
			XSRETURN_UNDEF;
		}

int
slurm_bit_test(bitstr_t *b, bitoff_t bit)

void
slurm_bit_set(bitstr_t *b, bitoff_t bit)

void
slurm_bit_clear(bitstr_t *b, bitoff_t bit)

void
slurm_bit_nset(bitstr_t *b, bitoff_t start, bitoff_t stop)

void
slurm_bit_nclear(bitstr_t *b, bitoff_t start, bitoff_t stop)

bitoff_t
slurm_bit_ffc(bitstr_t *b)

bitoff_t
slurm_bit_ffs(bitstr_t *b)

bitoff_t
slurm_bit_fls(bitstr_t *b)

bitoff_t
slurm_bit_nffc(bitstr_t *b, int n)

bitoff_t
slurm_bit_nffs(bitstr_t *b, int n)

bitoff_t
slurm_bit_noc(bitstr_t *b, int n, int seed)

bitoff_t
slurm_bit_size(bitstr_t *b)

void
slurm_bit_and(bitstr_t *b1, bitstr_t *b2)

void
slurm_bit_not(bitstr_t *b)

void
slurm_bit_or(bitstr_t *b1, bitstr_t *b2)

void
slurm_bit_copybits(bitstr_t *b1, bitstr_t *b2)

int
slurm_bit_set_count(bitstr_t *b)

int
slurm_bit_set_count_range(bitstr_t *b, int start, int end)

int
slurm_bit_clear_count(bitstr_t *b)

int
slurm_bit_nset_max_count(bitstr_t *b)

bitstr_t *
slurm_bit_rotate_copy(bitstr_t *b, int n, bitoff_t nbits)
	POSTCALL:
		if(RETVAL == NULL) {
			XSRETURN_UNDEF;
		}

void
slurm_bit_rotate(bitstr_t *b, int n)


# $str = $bitmap->fmt();
char *
slurm_bit_fmt(bitstr_t *b)
	PREINIT:
		int len = 1, bits;
		char *tmp_str;
	CODE:
		bits = slurm_bit_size(b);
		while(bits > 0) {
			bits /= 10;
			len ++;
		}
		bits = slurm_bit_size(b);
		len *= bits;
		New(0, tmp_str, len, char);
		slurm_bit_fmt(tmp_str, len, b);
		len = strlen(tmp_str) + 1;
		New(0, RETVAL, len, char);
		Copy(tmp_str, RETVAL, len, char);
		Safefree(tmp_str);
	OUTPUT:
		RETVAL

int
slurm_bit_unfmt(bitstr_t *b, char *str)


# $array = Slurm::Bitstr::fmt2int($str);
AV *
slurm_bit_fmt2int(char *str)
	PREINIT:
		int i = 0, *array;
	CODE:
		array = slurm_bitfmt2int(str);
		RETVAL = newAV();
		while (array[i] != -1) {
			av_store_int(RETVAL, i, array[i]);
			i ++;
		}
		xfree(array);
	OUTPUT:
		RETVAL


char *
slurm_bit_fmt_hexmask(bitstr_t *b)
	PREINIT:
		char *tmp_str;
		int len;
	CODE:
		tmp_str = slurm_bit_fmt_hexmask(b);
		len = strlen(tmp_str) + 1;
		New(0, RETVAL, len, char);
		Copy(tmp_str, RETVAL, len, char);
		xfree(tmp_str);
	OUTPUT:
		RETVAL

# XXX: only bits set in "str" are copied to "b".
#      bits set originally in "b" stay set after unfmt.
#      maybe this is a bug
int
slurm_bit_unfmt_hexmask(bitstr_t *b, char *str)

char *
slurm_bit_fmt_binmask(bitstr_t *b)
	PREINIT:
		char *tmp_str;
		int len;
	CODE:
		tmp_str = slurm_bit_fmt_binmask(b);
		len = strlen(tmp_str) + 1;
		New(0, RETVAL, len, char);
		Copy(tmp_str, RETVAL, len, char);
		xfree(tmp_str);
	OUTPUT:
		RETVAL

# ditto
int
slurm_bit_unfmt_binmask(bitstr_t *b, char *str)

void
slurm_bit_fill_gaps(bitstr_t *b)

int
slurm_bit_super_set(bitstr_t *b1, bitstr_t *b2)

int
slurm_bit_overlap(bitstr_t *b1, bitstr_t *b2)

int
slurm_bit_equal(bitstr_t *b1, bitstr_t *b2)

bitstr_t *
slurm_bit_pick_cnt(bitstr_t *b, bitoff_t nbits)
	POSTCALL:
		if(RETVAL == NULL) {
			XSRETURN_UNDEF;
		}

bitoff_t
slurm_bit_get_bit_num(bitstr_t *b, int pos)

int
slurm_bit_get_pos_num(bitstr_t *b, bitoff_t pos)

void
slurm_bit_DESTROY(bitstr_t *b)
	CODE:
		FREE_NULL_BITMAP(b);
