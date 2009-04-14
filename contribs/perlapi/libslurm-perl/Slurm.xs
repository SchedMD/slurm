#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include <slurm/slurm.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "msg.h"

#include "const-c.inc"
		
/* these declaration are not in slurm.h */
extern void slurm_api_set_conf_file(char *pathname);
extern void slurm_api_clear_config(void);

#define xfree(__p) \
        slurm_xfree((void **)&(__p), __FILE__, __LINE__, "")
extern void slurm_xfree(void **, const char *, int, const char *);

struct slurm {
	node_info_msg_t *node_info_msg;
	partition_info_msg_t *part_info_msg;
	slurm_ctl_conf_t *ctl_conf;
	job_info_msg_t *job_info_msg;
	job_step_info_response_msg_t *job_step_info_msg;
};
typedef struct slurm * slurm_t;
static struct slurm slurm;
#define SINGLETON (&slurm)

static void 
free_slurm(void)
{
	if(slurm.node_info_msg) {
		slurm_free_node_info_msg(slurm.node_info_msg);
		slurm.node_info_msg = NULL;
	}
	if(slurm.part_info_msg) {
		slurm_free_partition_info_msg(slurm.part_info_msg);
		slurm.part_info_msg = NULL;
	}
	if(slurm.ctl_conf) {
		slurm_free_ctl_conf(slurm.ctl_conf);
		slurm.ctl_conf = NULL;
	}
	if(slurm.job_info_msg) {
		slurm_free_job_info_msg(slurm.job_info_msg);
		slurm.job_info_msg = NULL;
	}
	if(slurm.job_step_info_msg) {
		slurm_free_job_step_info_response_msg(slurm.job_step_info_msg);
		slurm.job_step_info_msg = NULL;
	}
}

typedef uint16_t signo_t;
static signo_t
signame_to_no(char* signame)
{
	int i = 0;
	struct { 
		char * name; 
		signo_t no;
	} map [] = {
		{"SIGHUP", SIGHUP}, {"SIGINT", SIGINT}, {"SIGQUIT", SIGQUIT}, {"SIGILL", SIGILL}, {"SIGTRAP", SIGTRAP}, 
		{"SIGABRT", SIGABRT}, {"SIGBUS", SIGBUS}, {"SIGFPE", SIGFPE}, {"SIGKILL", SIGKILL}, {"SIGUSR1", SIGUSR1}, 
		{"SIGSEGV", SIGSEGV}, {"SIGUSR2", SIGUSR2}, {"SIGPIPE", SIGPIPE}, {"SIGALRM", SIGALRM}, {"SIGTERM", SIGTERM}, 
		{"SIGCHLD", SIGCHLD}, {"SIGCONT", SIGCONT}, {"SIGSTOP", SIGSTOP}, {"SIGTSTP", SIGTSTP}, {"SIGTTIN", SIGTTIN}, 
		{"SIGTTOU", SIGTTOU}, {"SIGURG", SIGURG}, {"SIGXCPU", SIGXCPU}, {"SIGXFSZ", SIGXFSZ}, {"SIGVTALRM", SIGVTALRM}, 
		{"SIGPROF", SIGPROF}, {"SIGWINCH", SIGWINCH}, {"SIGIO", SIGIO}, {"SIGPWR", SIGPWR}, {"SIGSYS", SIGSYS}, 
		{NULL, 0}
	};
	
	for( i = 0; map[i].name != NULL; i ++) {
		if (strcasecmp (map[i].name, signame) == 0)
			return map[i].no;
	}
	return 0;
}


static SV* sarb_cb_sv = NULL;
static void
sarb_cb(uint32_t jobid)
{
	int count;
	dSP;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newSVuv(jobid)));
	PUTBACK;

	count = call_sv(sarb_cb_sv, G_VOID);

	SPAGAIN;
	/*
	 * The callback should return nothing, but if there is no return statement
	 * the value of the last expression will be returned, even in context of G_VOID.
	 */
	//if (count != 0) {
	//	fprintf(stderr, "Callback returned %d values\n", count);
	//	Perl_croak (aTHX_ "Callback should not return values");
	//}
	FREETMPS;
	LEAVE;
}

static SV* task_start_cb_sv = NULL;
static SV* task_finish_cb_sv = NULL;
static void
task_start_cb(launch_tasks_response_msg_t *resp)
{
	/* TODO */
}
static void
task_finish_cb(task_exit_msg_t *exit)
{
	/* TODO */
}

/********************************************************************/

MODULE = Slurm		PACKAGE = Slurm		PREFIX=slurm_
INCLUDE: const-xs.inc
PROTOTYPES: ENABLE

######################################################################
# 	MISC FUNCTIONS
######################################################################
slurm_t 
get_slurm(slurm_t self = NULL, char *conf_file=NULL)
		slurm_t	RETVAL = NULL;
	CODE:
		if(conf_file) {
			slurm_api_set_conf_file(conf_file);
			free_slurm();
		}
		RETVAL = self;
	OUTPUT:
		RETVAL

void
DESTROY(slurm_t self)
	CODE:
		free_slurm();

void
slurm_set_config_file(slurm_t self = NO_INIT, char *conf_file=NULL)
	CODE:
		if(conf_file) {
			slurm_api_set_conf_file(conf_file);
			free_slurm();
		}

void
slurm_clear_config(slurm_t self = NO_INIT)
	CODE:
		slurm_api_clear_config();

int
slurm_get_errno(slurm_t self)
	C_ARGS:

char*
slurm_strerror(slurm_t self, int errnum = 0)
	C_ARGS:
		(errnum ? errnum : slurm_get_errno())

######################################################################
# 	RESOURCE ALLOCATION FUNCTIONS
######################################################################
HV*
slurm_allocate_resources(slurm_t self, HV* job_req = NULL)
	PREINIT:
		job_desc_msg_t job_desc_msg;
		resource_allocation_response_msg_t* resp_msg = NULL;
		int rc;
	CODE:
		if (hv_to_job_desc_msg(job_req, &job_desc_msg) < 0) {
			XSRETURN_UNDEF;
		}
		rc = slurm_allocate_resources(&job_desc_msg, &resp_msg);
		free_job_desc_msg_memory(&job_desc_msg);
		if(rc != SLURM_SUCCESS) {
			slurm_free_resource_allocation_response_msg(resp_msg);
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
		resource_allocation_response_msg_to_hv(resp_msg, RETVAL);
		slurm_free_resource_allocation_response_msg(resp_msg);
	OUTPUT:
		RETVAL

HV*
slurm_allocate_resources_blocking(slurm_t self, HV* job_req = NULL, time_t timeout = 0, SV* callback = NULL)
	PREINIT:
		job_desc_msg_t job_desc_msg;
		resource_allocation_response_msg_t *resp_msg = NULL;
	CODE:
		if (hv_to_job_desc_msg(job_req, &job_desc_msg) < 0) {
			XSRETURN_UNDEF;
		}
		sarb_cb_sv = callback;
		resp_msg = slurm_allocate_resources_blocking(&job_desc_msg, timeout, 
				callback == NULL ? NULL : sarb_cb);
		free_job_desc_msg_memory(&job_desc_msg);
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


HV*
slurm_allocation_lookup(slurm_t self, U32 job_id)
	PREINIT:
		job_alloc_info_response_msg_t* resp_msg = NULL;
		int rc;
	CODE:
		rc = slurm_allocation_lookup(job_id, &resp_msg);
		if(rc != SLURM_SUCCESS) {
			slurm_free_job_alloc_info_response_msg(resp_msg);
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
		job_alloc_info_response_msg_to_hv(resp_msg, RETVAL);
		slurm_free_job_alloc_info_response_msg(resp_msg);
	OUTPUT:
		RETVAL

HV*
slurm_allocation_lookup_lite(slurm_t self, U32 job_id)
	PREINIT:
		resource_allocation_response_msg_t* resp_msg = NULL;
		int rc;
	CODE:
		rc = slurm_allocation_lookup_lite(job_id, &resp_msg);
		if(rc != SLURM_SUCCESS) {
			slurm_free_resource_allocation_response_msg(resp_msg);
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
		resource_allocation_response_msg_to_hv(resp_msg, RETVAL);
		slurm_free_resource_allocation_response_msg(resp_msg);
	OUTPUT:
		RETVAL

char*
slurm_read_hostfile(slurm_t self, char* filename, int n)
	PREINIT:
		char* hostlist;
	CODE:
		hostlist = slurm_read_hostfile(filename, n);
		if(hostlist == NULL) {
			XSRETURN_UNDEF;
		} else {
			New(0, RETVAL, strlen(hostlist) + 1, char);
			Copy(hostlist, RETVAL, strlen(hostlist) + 1, char);
			free(hostlist);
		}
	OUTPUT:
		RETVAL

HV*
slurm_submit_batch_job(slurm_t self, HV* job_req = NULL)
	PREINIT:
		job_desc_msg_t job_desc_msg;
		submit_response_msg_t* resp_msg = NULL;
		int rc;
	CODE:
		if(hv_to_job_desc_msg(job_req, &job_desc_msg) < 0) {
			XSRETURN_UNDEF;
		}
		rc = slurm_submit_batch_job(&job_desc_msg, &resp_msg);
		free_job_desc_msg_memory(&job_desc_msg);
		if(rc != SLURM_SUCCESS) {
			slurm_free_submit_response_response_msg(resp_msg);
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
		submit_response_msg_to_hv(resp_msg, RETVAL);
		slurm_free_submit_response_response_msg(resp_msg);
	OUTPUT:
		RETVAL
	
int
slurm_job_will_run(slurm_t self, HV* job_req = NULL)
	PREINIT:
		job_desc_msg_t job_desc_msg;
	CODE:
		if (hv_to_job_desc_msg(job_req, &job_desc_msg) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_job_will_run(&job_desc_msg);
		free_job_desc_msg_memory(&job_desc_msg);
	OUTPUT:
		RETVAL

######################################################################
#	JOB/STEP SIGNALING FUNCTIONS
######################################################################
int
slurm_kill_job(slurm_t self, U32 jobid, signo_t signal = 0, U16 batch_flag = 0)
	C_ARGS:
		jobid, signal, batch_flag

int
slurm_kill_job_step(slurm_t self, U32 jobid, U32 stepid, signo_t signal = 0)
	C_ARGS:
		jobid, stepid, signal

int
slurm_signal_job(slurm_t self, U32 jobid, signo_t signal = 0)
	C_ARGS:
		jobid, signal
		
int
slurm_signal_job_step(slurm_t self, U32 jobid, U32 stepid, signo_t signal = 0)
	C_ARGS:
		jobid, stepid, signal


######################################################################
#	JOB/STEP COMPLETION FUNCTIONS
######################################################################
int
slurm_complete_job(slurm_t self, U32 jobid, U32 job_rc = 0)
	C_ARGS:
		jobid, job_rc

int
slurm_terminate_job(slurm_t self, U32 jobid)
	C_ARGS:
		jobid

int
slurm_terminate_job_step(slurm_t self, U32 jobid, U32 stepid)
	C_ARGS:
		jobid, stepid


######################################################################
#	SLURM CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################
MODULE=Slurm PACKAGE=Slurm PREFIX=slurm_
void
slurm_api_version(slurm_t self, OUTLIST int major, OUTLIST int minor, OUTLIST int micro)
	PREINIT:
		long version;
	CODE:
		version = slurm_api_version();
		major = SLURM_VERSION_MAJOR(version);
		minor = SLURM_VERSION_MINOR(version);
		micro = SLURM_VERSION_MICRO(version);

HV*
slurm_load_ctl_conf(slurm_t self = NULL)
	PREINIT:
		slurm_ctl_conf_t *new_ctl_conf;
		int rc;
	CODE:
		rc = slurm_load_ctl_conf(self->ctl_conf ? self->ctl_conf->last_update : 0, &new_ctl_conf);
		if(rc == SLURM_SUCCESS) {
			slurm_free_ctl_conf(self->ctl_conf);
			self->ctl_conf = new_ctl_conf;
		} else if(slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			/* nothing to do */
		} else {
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
                slurm_ctl_conf_to_hv(self->ctl_conf, RETVAL);
	OUTPUT:
		RETVAL

# To be implemented in perl code
#void
#slurm_print_ctl_conf(slurm_t self, HV* conf)

######################################################################
#	SLURM JOB CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################
HV*
slurm_load_jobs(slurm_t self = NULL, U16 show_flags = 0)
	PREINIT:
		job_info_msg_t* new_job_info_msg = NULL;
		int rc;
	CODE:
		rc = slurm_load_jobs(self->job_info_msg ? self->job_info_msg->last_update : 0, &new_job_info_msg, show_flags);
		if(rc == SLURM_SUCCESS) {
			slurm_free_job_info_msg(self->job_info_msg);
			self->job_info_msg = new_job_info_msg;
		} else if(slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			/* nothing to do */
		} else {
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
                job_info_msg_to_hv(self->job_info_msg, RETVAL);
	OUTPUT:
		RETVAL

# To be implemented in perl code		
#slurm_print_job_info_msg()
#slurm_print_job_info()
#slurm_sprint_job_info()

time_t
slurm_get_end_time(slurm_t self, U32 jobid)
	PREINIT:
		time_t end_time;
		int rc;
	CODE:
		rc = slurm_get_end_time(jobid, &end_time);
		if(rc != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		} else
			RETVAL = end_time;
	OUTPUT:
		RETVAL
	
long
slurm_get_rem_time(slurm_t self, U32 jobid)
	C_ARGS:
		jobid
		
U32
slurm_pid2jobid(slurm_t self, U32 pid)
	PREINIT:
		int rc;
		uint32_t job_id;
	CODE:
		rc = slurm_pid2jobid(pid, &job_id);
		if(rc != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		} else
			RETVAL = job_id;
	OUTPUT:
		RETVAL

int
slurm_update_job(slurm_t self, HV* job_info = NULL)
	PREINIT:
		job_desc_msg_t job_msg;
	CODE:
		if(hv_to_job_desc_msg(job_info, &job_msg) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_update_job(&job_msg);
		free_job_desc_msg_memory(&job_msg);
	OUTPUT:
		RETVAL

#int
#slurm_get_select_jobinfo(slurm_t self, select_jobinfo_t jobinfo, int data_type, void* data)


######################################################################
#	SLURM JOB STEP CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################
HV*
slurm_get_job_steps(slurm_t self = NULL, U16 show_flags = 0, U32 jobid = 0, U32 stepid = 0)
	PREINIT:
		int rc;
		job_step_info_response_msg_t* resp_msg;
	CODE:
		rc = slurm_get_job_steps(self->job_step_info_msg ? self->job_step_info_msg->last_update : 0, jobid, stepid, &resp_msg, show_flags);
		if(rc == SLURM_SUCCESS) {
			slurm_free_job_step_info_response_msg(self->job_step_info_msg);
			self->job_step_info_msg = resp_msg;
		} else if(slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			/* nothing to do */
		} else {
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
		job_step_info_response_msg_to_hv(self->job_step_info_msg, RETVAL);
	OUTPUT:
		RETVAL

# To be implemented in perl code
# slurm_print_job_step_info_response_msg
# slurm_print_job_step_info_msg
# slurm_sprint_job_step_info
		
HV*
slurm_job_step_layout_get(slurm_t self, U32 jobid, U32 stepid)
	PREINIT:
		slurm_step_layout_t *layout;
	CODE:
		layout = slurm_job_step_layout_get(jobid, stepid);
		if(layout == NULL) {
			XSRETURN_UNDEF;
		} else {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
			slurm_step_layout_to_hv(layout, RETVAL);
			slurm_job_step_layout_free(layout);
		}
	OUTPUT:
		RETVAL
	

	
######################################################################
#	SLURM NODE CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################
HV*
slurm_load_node(slurm_t self = NULL, U16 show_flags = 0)
	PREINIT:
		node_info_msg_t* new_node_info_msg = NULL;
		int rc;
	CODE:
		rc = slurm_load_node(self->node_info_msg ? self->node_info_msg->last_update : 0, &new_node_info_msg, show_flags);
		if(rc == SLURM_SUCCESS) {
			slurm_free_node_info_msg(self->node_info_msg);
			self->node_info_msg = new_node_info_msg;
		} else if(slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			/* nothing to do */
		} else {
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
                node_info_msg_to_hv(self->node_info_msg, RETVAL);
	OUTPUT:
		RETVAL

# To be implemented in perl code
# slurm_print_node_info_msg
# slurm_print_node_table
# slurm_sprint_node_table

int
slurm_update_node(slurm_t self, HV* update_req = NULL)
	PREINIT:
		update_node_msg_t node_msg;
	INIT:
		if(hv_to_update_node_msg(update_req, &node_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&node_msg
		
######################################################################
#	SLURM PARTITION CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
######################################################################
HV*
slurm_load_partitions(slurm_t self = NULL, U16 show_flags = 0)
	PREINIT:
		partition_info_msg_t* new_part_info_msg;
		int rc;
	CODE:
		rc = slurm_load_partitions(self->part_info_msg ? self->part_info_msg->last_update : 0, &new_part_info_msg, show_flags);
		if(rc == SLURM_SUCCESS) {
			slurm_free_partition_info_msg(self->part_info_msg);
			self->part_info_msg = new_part_info_msg;
		} else if(slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			/* nothing to do */
		} else {
			XSRETURN_UNDEF;
		}
		RETVAL = newHV();
		sv_2mortal((SV*)RETVAL);
                partition_info_msg_to_hv(self->part_info_msg, RETVAL);
	OUTPUT:
		RETVAL

# To be implemented in perl code
# slurm_print_partition_info_msg
# slurm_print_partition_info
# slurm_sprint_partition_info

int
slurm_update_partition(slurm_t self, HV* part_info = NULL)
	PREINIT:
		update_part_msg_t update_msg;
	INIT:
		if(hv_to_update_part_msg(part_info, &update_msg) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&update_msg
		
int
slurm_delete_partition(slurm_t self, char* part_name)
	PREINIT:
		delete_part_msg_t delete_msg;
	INIT:
		delete_msg.name = part_name;
	C_ARGS:
		&delete_msg



######################################################################
#	SLURM PING/RECONFIGURE/SHUTDOWN FUNCTIONS
######################################################################
int
slurm_ping(slurm_t self, U16 primary = 1)
	C_ARGS:
		primary

int
slurm_reconfigure(slurm_t self)
	C_ARGS:
	
int
slurm_shutdown(slurm_t self, U16 core = 0)
	C_ARGS:
		core



######################################################################
#	SLURM JOB SUSPEND FUNCTIONS
######################################################################
int
slurm_suspend(slurm_t self, U32 jobid)
	C_ARGS:
		jobid

int
slurm_resume(slurm_t self, U32 jobid)
	C_ARGS:
		jobid

int
slurm_requeue(slurm_t self, U32 jobid)
	C_ARGS:
		jobid


######################################################################
#	SLURM JOB CHECKPOINT FUNCTIONS
######################################################################
int
slurm_checkpoint_able(slurm_t self, U32 jobid, U32 stepid, OUT time_t start_time)
	C_ARGS:
		jobid, stepid, &start_time

int
slurm_checkpoint_disable(slurm_t self, U32 jobid, U32 stepid)
	C_ARGS:
		jobid, stepid

int
slurm_checkpoint_enable(slurm_t self, U32 jobid, U32 stepid)
	C_ARGS:
		jobid, stepid

int
slurm_checkpoint_create(slurm_t self, U32 jobid, U32 stepid, U16 max_wait, char* image_dir)
	C_ARGS:
		jobid, stepid, max_wait, image_dir

int
slurm_checkpoint_vacate(slurm_t self, U32 jobid, U32 stepid, U16 max_wait, char* image_dir)
	C_ARGS:
		jobid, stepid, max_wait, image_dir

int
slurm_checkpoint_restart(slurm_t self, U32 jobid, U32 stepid, U16 stick, char *image_dir)
	C_ARGS:
		jobid, stepid, stick, image_dir

int
slurm_checkpoint_complete(slurm_t self, U32 jobid, U32 stepid, time_t begin_time, U32 error_code, char* error_msg)
	C_ARGS:
		jobid, stepid, begin_time, error_code, error_msg

int
slurm_checkpoint_error(slurm_t self, U32 jobid, U32 stepid, OUTLIST U32 error_code, OUTLIST char* error_msg)
	PREINIT:
		char* err_msg = NULL;
	CODE:
		error_code = SLURM_SUCCESS;
		RETVAL = slurm_checkpoint_error(jobid, stepid, (uint32_t *)&error_code, &err_msg);
		Newz(0, error_msg, strlen(err_msg), char);
		Copy(err_msg, error_msg, strlen(err_msg), char);
		xfree(err_msg);
	OUTPUT:
		RETVAL

######################################################################
#	SLURM TRIGGER FUNCTIONS
######################################################################
int slurm_set_trigger(slurm_t self, HV* trigger_info = NULL)
	PREINIT:
		trigger_info_t trigger_set;
	INIT:
		if(hv_to_trigger_info(trigger_info, &trigger_set) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&trigger_set

int slurm_clear_trigger(slurm_t self, HV* trigger_info = NULL)
	PREINIT:
		trigger_info_t trigger_clear;
	INIT:
		if(hv_to_trigger_info(trigger_info, &trigger_clear) < 0) {
			XSRETURN_UNDEF;
		}
	C_ARGS:
		&trigger_clear

HV*
slurm_get_triggers(slurm_t self)
	PREINIT:
		trigger_info_msg_t* trig_info_msg;
		int rc;
	CODE:
		rc = slurm_get_triggers(&trig_info_msg);
		if(rc == SLURM_SUCCESS) {
			RETVAL = newHV();
			sv_2mortal((SV*)RETVAL);
        	        trigger_info_msg_to_hv(trig_info_msg, RETVAL);
			slurm_free_trigger_msg(trig_info_msg);
		} else {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

##################################################################
#               HOSTLIST FUNCTIONS
##################################################################
MODULE=Slurm PACKAGE=Slurm::Hostlist PREFIX=slurm_hostlist_

int
slurm_hostlist_count(hostlist_t hl = NULL)
	OUTPUT:
		RETVAL
	
hostlist_t
slurm_hostlist_create(char* hostlist)

int
slurm_hostlist_find(hostlist_t hl = NULL, char* hostname)
	OUTPUT:
		RETVAL

int
slurm_hostlist_push(hostlist_t hl = NULL, char* hosts)
	OUTPUT:
		RETVAL

int
slurm_hostlist_push_host(hostlist_t hl = NULL, char* host)
	OUTPUT:
		RETVAL

char*
slurm_hostlist_ranged_string(hostlist_t hl = NULL)
	PREINIT:
		size_t size = 1024;
		int rc = 0;
	CODE:
		Newz(0, RETVAL, size, char);
		while((rc = slurm_hostlist_ranged_string(hl, size, RETVAL)) == -1) {
			size *= 2;
			Renew(RETVAL, size, char);
		}
	OUTPUT:
		RETVAL

char*
slurm_hostlist_shift(hostlist_t hl = NULL)		
	PREINIT:
		char *host = NULL;
	CODE:
		host = slurm_hostlist_shift(hl);
		if (host == NULL) {
			XSRETURN_UNDEF;
		}
		Newz(0, RETVAL, strlen(host) + 1, char);
		Copy(host, RETVAL, strlen(host) + 1, char);
		free(host);
	OUTPUT:
		RETVAL

void
slurm_hostlist_uniq(hostlist_t hl = NULL)
        CODE:
		slurm_hostlist_uniq(hl);

void
DESTROY(hl)
		hostlist_t hl=NULL;
	CODE:
		slurm_hostlist_destroy(hl);




##############################################
#	SLURM TASK SPAWNING FUNCTIONS
# I did not try the following functions at all
##############################################
MODULE=Slurm PACKAGE=Slurm::Stepctx PREFIX=slurm_step_ctx_

slurm_step_ctx
slurm_step_ctx_create(slurm_step_ctx ctx = NO_INIT, HV* req = NULL)
	PREINIT:
		slurm_step_ctx_params_t params;
	CODE:
		if (hv_to_slurm_step_ctx_params(req, &params) < 0) {
			XSRETURN_UNDEF;
		}
		RETVAL = slurm_step_ctx_create(&params);
		if (RETVAL == NULL) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

# XXX: slurm_step_ctx_get is divided into the following methods
U32
slurm_step_ctx_get_jobid(slurm_step_ctx ctx = NULL)
	CODE:
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_JOBID, &RETVAL) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

U32
slurm_step_ctx_get_stepid(slurm_step_ctx ctx = NULL)
	CODE:
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_STEPID, &RETVAL) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

U32
slurm_step_ctx_get_num_hosts(slurm_step_ctx ctx = NULL)
	PREINIT:
		uint32_t num_hosts;
	CODE:
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_NUM_HOSTS, &num_hosts) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
		RETVAL = num_hosts;
	OUTPUT:
		RETVAL

AV*
slurm_step_ctx_get_tasks(slurm_step_ctx ctx = NULL)
	PREINIT:
		int i;
		uint32_t num_hosts;
		uint16_t *tasks;
	CODE:
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_NUM_HOSTS, &num_hosts) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_TASKS, &tasks) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
		RETVAL = newAV();
		sv_2mortal((SV*)RETVAL);
		for(i = 0; i < num_hosts; i ++) {
			if(av_store_uint16_t(RETVAL, i, tasks[i]) < 0) {
				XSRETURN_UNDEF;
			}
		}
	OUTPUT:
		RETVAL

AV*
slurm_step_ctx_get_tid(slurm_step_ctx ctx = NULL, U32 index)
	PREINIT:
		int i;
		uint16_t *tasks;
		uint32_t *tids;
	CODE:
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_TASKS, &tasks) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_TID, index, &tids) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
		RETVAL = newAV();
		sv_2mortal((SV*)RETVAL);
		for(i = 0; i < tasks[index]; i ++) {
			if(av_store_uint32_t(RETVAL, i, tids[i]) < 0) {
				XSRETURN_UNDEF;
			}
		}
	OUTPUT:
		RETVAL

# XXX: should we return it as a HV* ?
#slurm_step_ctx_get_resp(slurm_step_ctx ctx)

# XXX: the returned value is no longer valid if ctx goes away
slurm_cred_t
slurm_step_ctx_get_cred(slurm_step_ctx ctx = NULL)
	CODE:
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_CRED, &RETVAL) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

# XXX: the returned value is no longer valid if ctx goes away
switch_jobinfo_t
slurm_step_ctx_get_switch_job(slurm_step_ctx ctx = NULL)
	CODE:
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_SWITCH_JOB, &RETVAL) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

char*
slurm_step_ctx_get_host(slurm_step_ctx ctx = NULL, U32 index)
	CODE:
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_HOST, index, &RETVAL) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
	OUTPUT:
		RETVAL

AV*
slurm_step_ctx_get_user_managed_sockets(slurm_step_ctx ctx = NULL)
	PREINIT:
		int i;
		int tasks_requested;
		int *sockets;
	CODE:
		if(slurm_step_ctx_get(ctx, SLURM_STEP_CTX_USER_MANAGED_SOCKETS, &tasks_requested, &sockets) != SLURM_SUCCESS) {
			XSRETURN_UNDEF;
		}
		RETVAL = newAV();
		sv_2mortal((SV*)RETVAL);
		for(i = 0; i < tasks_requested; i ++) {
			if(av_store_int(RETVAL, i, sockets[i]) < 0) {
				XSRETURN_UNDEF;
			}
		}
	OUTPUT:
		RETVAL


int
slurm_step_ctx_daemon_per_node_hack(slurm_step_ctx ctx = NULL)
	C_ARGS:
		ctx

# TODO
#int
#slurm_jobinfo_ctx_get(slurm_t self, switch_jobinfo_t job_info, int data_type, OUTPUT void* data)
#	C_ARGS:
#		job_info, data_type, data

void
DESTROY(slurm_step_ctx ctx = NULL)
	CODE:
		if(slurm_step_ctx_destroy(ctx) != SLURM_SUCCESS) {
			Perl_croak(aTHX_ "Failed to destory slurm_step_ctx");
		}


MODULE=Slurm PACKAGE=Slurm::Stepctx PREFIX=slurm_step_
int
slurm_step_launch(slurm_step_ctx ctx = NULL, HV* hv = NULL, SV* start_cb = NULL, SV* finish_cb = NULL)
	PREINIT:
		slurm_step_launch_callbacks_t callbacks = {NULL, NULL};
		slurm_step_launch_params_t params;
	CODE:
		if(finish_cb != NULL) {
			task_finish_cb_sv = finish_cb;
			callbacks.task_finish = task_finish_cb;
		}
		if(start_cb != NULL) {
			task_start_cb_sv = start_cb;
			callbacks.task_start = task_start_cb;
		}
		if(hv_to_slurm_step_launch_params(hv, &params) < 0) {
			RETVAL = SLURM_ERROR;
		} else {
			RETVAL = slurm_step_launch(ctx, &params, &callbacks);
		}
	OUTPUT:
		RETVAL


int
slurm_step_launch_wait_start(slurm_step_ctx ctx = NULL)
	C_ARGS:
		ctx


void
slurm_step_launch_wait_finish(slurm_step_ctx ctx = NULL)
	C_ARGS:
		ctx

void
slurm_step_launch_abort(slurm_step_ctx ctx = NULL)
	C_ARGS:
		ctx
