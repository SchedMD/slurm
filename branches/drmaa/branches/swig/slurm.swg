%module slurm
/* %include "cpointer.i" */
/* %include "carrays.i" */
/* %include "exception.i" */

%{
/* Includes the header in the wrapper code */
#include <slurm/slurm.h>

/* Header files for perl wrappers */
#ifdef SWIGPERL
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#endif
%}

typedef unsigned long time_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;

/* "in" is a keyword in Python, so we must rename it */
%rename (stdin_file) in;
%rename (stdout_file) out;
%rename (stderr_file) err;

/**********************************************************************
 * Things we ignore.
 **********************************************************************/

/* We hide the slurm_init_job_desc_msg function from the interface, and 
 * instead call it automatically in the job_desc_msg_t constructor.
 */
%ignore slurm_init_job_desc_msg;

/* Why are these even in slurm.h?!? */
%ignore forked_message;
%ignore forked_msg_pipe;

/* other functions and data structures  to ignore. */
%import "slurm_ignore.swg"

/**********************************************************************
 * Load the SLURM API header file to generate wrappers.
 **********************************************************************/

%include <slurm/slurm.h>

/**********************************************************************
 * slurm_allocate_resources
 **********************************************************************/
%extend job_desc_msg_t {
	job_desc_msg_t() {
		job_desc_msg_t *j;
		j = (job_desc_msg_t *)malloc(sizeof(job_desc_msg_t));
		slurm_init_job_desc_msg(j);
		return j;
	}
	~job_desc_msg_t() {
		free(self);
	}
}

%extend resource_allocation_response_msg_t {
	~resource_allocation_response_msg_t() {
		slurm_free_resource_allocation_response_msg(self);
	}
}

/* Add new slurm_allocate_resources function that returns answer
 * instead of returning via an output parameter */
%rename(slurm_allocate_resources) my_alloc;
%inline %{
resource_allocation_response_msg_t *my_alloc(job_desc_msg_t *job_desc_msg)
{
	resource_allocation_response_msg_t *a;
	int rc;

	rc = slurm_allocate_resources(job_desc_msg, &a);
	if (rc == 0) {
		return a;
	} else {
		return NULL;
	}
}
%}

/**********************************************************************
 * slurm_job_step_create
 **********************************************************************/
%extend job_step_create_response_msg_t {
	~job_step_create_response_msg_t() {
		slurm_free_job_step_create_response_msg(self);
	}
}

/* Add new slurm_job_step_create function that returns answer
 * instead of returning via an output parameter */
%rename(slurm_job_step_create) my_job_step_create;
%inline %{
job_step_create_response_msg_t *my_job_step_create(job_step_create_request_msg_t * slurm_step_alloc_req_msg)
{
	job_step_create_response_msg_t *s;
	int rc;

	rc = slurm_job_step_create(slurm_step_alloc_req_msg, &s);
	if (rc == 0) {
		return s;
	} else {
		return NULL;
	}
}
%}

/**********************************************************************
 * slurm_get_job_steps
 **********************************************************************/
%extend job_step_info_response_msg_t {
	~job_step_info_response_msg_t() {
		slurm_free_job_step_info_response_msg(self);
	}
	job_step_info_t *get_step(int i) {
		if (i < 0 || i >= self->job_step_count)
			return NULL;
		else
			return &self->job_steps[i];
	}
}

%rename(slurm_get_job_steps) my_get_job_steps;
%inline %{
job_step_info_response_msg_t *my_get_job_steps(time_t update_time,
		uint32_t job_id, uint32_t step_id, uint16_t show_flags)
{
	job_step_info_response_msg_t *s;
	int rc;

	rc = slurm_get_job_steps(update_time, job_id, step_id, &s, show_flags);
	if (rc == 0) {
		return s;
	} else {
		return NULL;
	}
}
%}

/**********************************************************************
 * slurm_load_jobs
 **********************************************************************/
%extend job_info_msg_t {
	~job_info_msg_t() {
		slurm_free_job_info_msg(self);
	}
	job_info_t *get_job(int i) {
		if (i < 0 || i >= self->record_count)
			return NULL;
		else
			return &self->job_array[i];
	}
}

%rename(slurm_load_jobs) my_load_jobs;
%inline %{
job_info_msg_t *my_load_jobs (time_t update_time, uint16_t show_flags)
{
	job_info_msg_t *j;
	int rc;

	rc = slurm_load_jobs(update_time, &j, show_flags);
	if (rc == 0) {
		return j;
	} else {
		return NULL;
	}
}
%}

/**********************************************************************
 * slurm_load_nodes
 **********************************************************************/
%extend node_info_msg_t {
	~node_info_msg_t() {
		slurm_free_node_info_msg(self);
	}
	node_info_t *get_node(int i) {
		if (i < 0 || i >= self->record_count)
			return NULL;
		else
			return &self->node_array[i];
	}
}

%rename (slurm_load_node) my_load_node;
%inline %{
node_info_msg_t *my_load_node(time_t update_time, uint16_t show_flags)
{
	node_info_msg_t *n;
	int rc;

	rc = slurm_load_node(update_time, &n, show_flags);
	if (rc == 0) {
		return n;
	} else {
		return NULL;
	}
}
%}

/**********************************************************************
 * slurm_load_partitions
 **********************************************************************/
%extend partition_info_msg_t {
	~partition_info_msg_t() {
		slurm_free_partition_info_msg(self);
	}
	partition_info_t *get_partition(int i) {
		if (i < 0 || i >= self->record_count)
			return NULL;
		else
			return &self->partition_array[i];
	}
}

%rename (slurm_load_partitions) my_load_partitions;
%inline %{
partition_info_msg_t *my_load_partitions(time_t update_time, uint16_t show_flags)
{
	partition_info_msg_t *p;
	int rc;

	rc = slurm_load_partitions(update_time, &p, show_flags);
	if (rc == 0) {
		return p;
	} else {
		return NULL;
	}
}
%}
