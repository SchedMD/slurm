/*****************************************************************************\
 *  shr_64.c - This plug is used by POE to interact with SLURM.
 *
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com> et. al.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <permapi.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "src/common/slurm_xlator.h"
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"

#include "src/srun/srun_job.h"
#include "src/srun/opt.h"
#include "src/srun/allocate.h"
#include "src/srun/launch.h"
#include "src/plugins/switch/nrt/nrt_keys.h"

void *my_handle = NULL;
srun_job_t *job = NULL;

int sig_array[] = {
	SIGINT,  SIGQUIT, SIGCONT, SIGTERM, SIGHUP,
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, 0 };

extern char **environ;

static int
_is_local_file (fname_t *fname)
{
	if (fname->name == NULL)
		return 1;

	if (fname->taskid != -1)
		return 1;

	return ((fname->type != IO_PER_TASK) && (fname->type != IO_ONE));
}

static slurm_step_layout_t *
_get_slurm_step_layout(srun_job_t *job)
{
	job_step_create_response_msg_t *resp;

	if (!job || !job->step_ctx)
		return (NULL);

	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_RESP, &resp);
	if (!resp)
	    return (NULL);
	return (resp->step_layout);
}

static void
_set_stdio_fds(srun_job_t *job, slurm_step_io_fds_t *cio_fds)
{
	bool err_shares_out = false;
	int file_flags;

	if (opt.open_mode == OPEN_MODE_APPEND)
		file_flags = O_CREAT|O_WRONLY|O_APPEND;
	else if (opt.open_mode == OPEN_MODE_TRUNCATE)
		file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
	else {
		slurm_ctl_conf_t *conf;
		conf = slurm_conf_lock();
		if (conf->job_file_append)
			file_flags = O_CREAT|O_WRONLY|O_APPEND;
		else
			file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
		slurm_conf_unlock();
	}

	/*
	 * create stdin file descriptor
	 */
	if (_is_local_file(job->ifname)) {
		if ((job->ifname->name == NULL) ||
		    (job->ifname->taskid != -1)) {
			cio_fds->in.fd = STDIN_FILENO;
		} else {
			cio_fds->in.fd = open(job->ifname->name, O_RDONLY);
			if (cio_fds->in.fd == -1) {
				error("Could not open stdin file: %m");
				exit(error_exit);
			}
		}
		if (job->ifname->type == IO_ONE) {
			cio_fds->in.taskid = job->ifname->taskid;
			cio_fds->in.nodeid = slurm_step_layout_host_id(
				_get_slurm_step_layout(job),
				job->ifname->taskid);
		}
	}

	/*
	 * create stdout file descriptor
	 */
	if (_is_local_file(job->ofname)) {
		if ((job->ofname->name == NULL) ||
		    (job->ofname->taskid != -1)) {
			cio_fds->out.fd = STDOUT_FILENO;
		} else {
			cio_fds->out.fd = open(job->ofname->name,
					       file_flags, 0644);
			if (cio_fds->out.fd == -1) {
				error("Could not open stdout file: %m");
				exit(error_exit);
			}
		}
		if (job->ofname->name != NULL
		    && job->efname->name != NULL
		    && !strcmp(job->ofname->name, job->efname->name)) {
			err_shares_out = true;
		}
	}

	/*
	 * create seperate stderr file descriptor only if stderr is not sharing
	 * the stdout file descriptor
	 */
	if (err_shares_out) {
		debug3("stdout and stderr sharing a file");
		cio_fds->err.fd = cio_fds->out.fd;
		cio_fds->err.taskid = cio_fds->out.taskid;
	} else if (_is_local_file(job->efname)) {
		if ((job->efname->name == NULL) ||
		    (job->efname->taskid != -1)) {
			cio_fds->err.fd = STDERR_FILENO;
		} else {
			cio_fds->err.fd = open(job->efname->name,
					       file_flags, 0644);
			if (cio_fds->err.fd == -1) {
				error("Could not open stderr file: %m");
				exit(error_exit);
			}
		}
	}
}

static nrt_job_key_t _get_nrt_job_key(srun_job_t *job)
{
	job_step_create_response_msg_t *resp;
	nrt_job_key_t job_key;

	if (!job || !job->step_ctx)
		return NO_VAL;

	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_RESP, &resp);
	if (!resp)
	    return NO_VAL;
	slurm_jobinfo_ctx_get(resp->switch_job, NRT_JOBINFO_KEY, &job_key);
	return job_key;
}

/* The connection communicates information to and from the resource
 * manager, so that the resource manager can start the parallel task
 * manager, and is available for the caller to communicate directly
 * with the parallel task manager.
 * IN resource_mgr - The resource manager handle returned by pe_rm_init.
 * IN connect_param - Input parameter structure (rm_connect_param)
 *        that contains the following:
 *        machine_count: The count of hosts/machines.
 *        machine_name: The array of machine names on which to connect.
 *        executable: The name of the executable to be started.
 * IN rm_timeout - The integer value that defines a connection timeout
 *        value. This value is defined by the MP_RM_TIMEOUT
 *        environment variable. A value less than zero indicates there
 *        is no timeout. A value equal to zero means to immediately
 *        return with no wait or retry. A value greater than zero
 *        means to wait the specified amount of time (in seconds).
 * OUT rm_sockfds - An array of socket file descriptors, that are
 *        allocated by the caller, to be returned as output, of the connection.
 * OUT error_msg - An error message that explains the error.
 * RET 0 - SUCCESS, nonzero on failure.
 */
extern int pe_rm_connect(rmhandle_t resource_mgr,
			 rm_connect_param *connect_param,
			 int *rm_sockfds, int rm_timeout, char **error_msg)
{
	srun_job_t **job_ptr = (srun_job_t **)resource_mgr;
	srun_job_t *job = *job_ptr;
	slurm_step_launch_params_t launch_params;
	int my_argc = 1;
	char **my_argv = xmalloc(sizeof(char *)*my_argc+1);
	my_argv[0] = xstrdup("/etc/pmdv12");

	info("got pe_rm_connect called %p %d", rm_sockfds, rm_sockfds[0]);

	slurm_step_launch_params_t_init(&launch_params);
	launch_params.gid = opt.gid;
	launch_params.alias_list = job->alias_list;
	launch_params.argc = my_argc;
	launch_params.argv = my_argv;
	launch_params.multi_prog = opt.multi_prog ? true : false;
	launch_params.cwd = opt.cwd;
	launch_params.slurmd_debug = opt.slurmd_debug;
	launch_params.buffered_stdio = !opt.unbuffered;
	launch_params.labelio = opt.labelio ? true : false;
	launch_params.remote_output_filename =fname_remote_string(job->ofname);
	launch_params.remote_input_filename = fname_remote_string(job->ifname);
	launch_params.remote_error_filename = fname_remote_string(job->efname);
	launch_params.task_prolog = opt.task_prolog;
	launch_params.task_epilog = opt.task_epilog;
	launch_params.cpu_bind = opt.cpu_bind;
	launch_params.cpu_bind_type = opt.cpu_bind_type;
	launch_params.mem_bind = opt.mem_bind;
	launch_params.mem_bind_type = opt.mem_bind_type;
	launch_params.open_mode = opt.open_mode;
	if (opt.acctg_freq >= 0)
		launch_params.acctg_freq = opt.acctg_freq;
	launch_params.pty = opt.pty;
	launch_params.cpus_per_task	= 1;
	launch_params.task_dist         = opt.distribution;
	launch_params.ckpt_dir		= opt.ckpt_dir;
	launch_params.restart_dir       = opt.restart_dir;
	launch_params.preserve_env      = opt.preserve_env;
	launch_params.spank_job_env     = opt.spank_job_env;
	launch_params.spank_job_env_size = opt.spank_job_env_size;

	_set_stdio_fds(job, &launch_params.local_fds);
	rm_sockfds[0] = launch_params.local_fds.in.fd;
	rm_sockfds[1] = launch_params.local_fds.out.fd;
	rm_sockfds[2] = launch_params.local_fds.err.fd;

	launch_params.parallel_debug = false;

	update_job_state(job, SRUN_JOB_LAUNCHING);
	if (slurm_step_launch(job->step_ctx, &launch_params, NULL) !=
	    SLURM_SUCCESS) {
		error("Application launch failed: %m");
		slurm_step_launch_wait_finish(job->step_ctx);
		goto cleanup;
	}

	update_job_state(job, SRUN_JOB_STARTING);
	if (slurm_step_launch_wait_start(job->step_ctx) == SLURM_SUCCESS) {
		update_job_state(job, SRUN_JOB_RUNNING);
	} else {
		info("Job step %u.%u aborted before step completely launched.",
		     job->jobid, job->stepid);
	}
cleanup:
	xfree(my_argv[0]);
	xfree(my_argv);
	info("done launching");
	return 0;
}

/* Releases the resource manager handle, closes the socket that is
 * created by the pe_rm_init function, and releases memory
 * allocated. When called, pe_rm_free implies the job has completed
 * and resources are freed and available for subsequent jobs.
 * IN/OUT resource_mgr
 */
extern void pe_rm_free(rmhandle_t *resource_mgr)
{
	//srun_job_t *job = (srun_job_t *)*resource_mgr;
       	/* We are at the end so don't worry about freeing the
	   srun_job_t pointer */
	resource_mgr = NULL;

	dlclose(my_handle);
	info("got pe_rm_free called");

}

/* The memory that is allocated to events generated by the resource
 * manager is released. pe_rm_free_event must be called for every
 * event that is received from the resource manager by calling the
 * pe_rm_get_event function.
 * IN resource_mgr
 * IN job_event - The pointer to a job event. The event must have been
 *        built by calling the pe_rm_get_event function.
 * RET 0 - SUCCESS, nonzero on failure.
 */
extern int pe_rm_free_event(rmhandle_t resource_mgr, job_event_t ** job_event)
{
	info("got pe_rm_free_event called");
	if (job_event) {
		xfree(*job_event);
	}
	return 0;
}

/* This resource management interface is called to return job event
 * information. The pe_rm_get_event function is only called in
 * interactive mode.
 *
 * With interactive jobs, this function reads or selects on the listen
 * socket created by the pe_rm_init call. If the listen socket is not
 * ready to read, this function selects and waits. POE processes
 * should monitor this socket at all times for event notification from
 * the resource manager after the job has started running.
 *
 * This function returns a pointer to the event that was updated by
 * the transaction.
 * The valid events are:
 * JOB_ERROR_EVENT
 *        Job error messages occurred. In this case, POE displays the
 *        error and terminates.
 * JOB_STATE_EVENT
 *        A job status change occurred, which results in one of the
 *        following job states. In this case, the caller may need to take
 *        appropriate action.
 *     JOB_STATE_RUNNING
 *        Indicates that the job has started. POE uses the
 *        pe_rm_get_job_info function to return the job
 *        information. When a job state of JOB_STATE_RUNNING has been
 *        returned, the job has started running and POE can obtain the
 *        job information by way of the pe_rm_get_job_info function call.
 *     JOB_STATE_NOTRUN
 *        Indicates that the job was not run, and POE will terminate.
 *     JOB_STATE_PREEMPTED
 *        Indicates that the job was preempted.
 *     JOB_STATE_RESUMED
 *        Indicates that the job has resumed.
 * JOB_TIMER_EVENT
 *        Indicates that no events occurred during the period
 *        specified by pe_rm_timeout.
 *
 * IN resource_mgr
 * OUT job_event - The address of the pointer to the job_event_t
 *        type. If an event is generated successfully by the resource
 *        manager, that event is saved at the location specified, and
 *        pe_rm_get_event returns 0 (or a nonzero value, if the event
 *        is not generated successfully). Based on the event type that is
 *        returned, the appropriate event of the type job_event_t can
 *        be accessed. After the event is processed, it should be
 *        freed by calling pe_rm_free_event.
 * OUT error_msg - The address of a character string at which the
 *        error message that is generated by pe_rm_get_event is
 *        stored. The memory for this error message is allocated by
 *        the malloc API call. After the error message is processed,
 *        the memory allocated should be freed by a calling free function.
 * IN rm_timeout - The integer value that defines a connection timeout
 *        value. This value is defined by the MP_RETRY environment
 *        variable. A value less than zero indicates there is no
 *        timeout. A value equal to zero means to immediately return
 *        with no wait or retry. A value greater than zero means to
 *        wait the specified amount of time (in seconds).
 * RET 0 - SUCCESS, nonzero on failure.
 */
extern int pe_rm_get_event(rmhandle_t resource_mgr, job_event_t **job_event,
			   int rm_timeout, char ** error_msg)
{
	job_event_t *ret_event = NULL;
	int *state;
	info("got pe_rm_get_event called %d %p %p", rm_timeout,
	     job_event, *job_event);

	ret_event = xmalloc(sizeof(job_event_t));
	*job_event = ret_event;
	ret_event->event = JOB_STATE_EVENT;
	state = xmalloc(sizeof(int));
	*state = JOB_STATE_RUNNING;
	ret_event->event_data = (void *)state;

	return 0;
}

/* The pe_rm_get_job_info function is called to return job
 * information, after a job has been started. It can be called in
 * either batch or interactive mode. For interactive jobs, it should
 * be called when pe_rm_get_event returns with the JOB_STATE_EVENT
 * event type, indicating the JOB_STATE_RUNNING
 * state. pe_rm_get_job_info provides the job information data values,
 * as defined by the job_info_t structure. It returns with an error if
 * the job is not in a running state. For batch jobs, POE calls
 * pe_rm_get_job_info immediately because, in batch mode, POE is
 * started only after the job has been started. The pe_rm_get_job_info
 * function must be capable of being called multiple times from the
 * same process or a different process, and the same job data must be
 * returned each time. When called from a different process, the
 * environment of that process is guaranteed to be the same as the
 * environment of the process that originally called the function.
 *
 * IN resource_mgr
 * OUT job_info - The address of the pointer to the job_info_t
 *        type. The job_info_t type contains the job information
 *        returned by the resource manager for the handle that is
 *        specified. The caller itself must free the data areas that
 *        are returned.
 * OUT error_msg - The address of a character string at which the
 *        error message that is generated by pe_rm_get_job_info is
 *        stored. The memory for this error message is allocated by the
 *        malloc API call. After the error message is processed, the memory
 *        allocated should be freed by a calling free function.
 * RET 0 - SUCCESS, nonzero on failure.
 */
extern int pe_rm_get_job_info(rmhandle_t resource_mgr, job_info_t **job_info,
			      char ** error_msg)
{
	job_info_t *ret_info = xmalloc(sizeof(job_info_t));
	int i, j, task_id = 0;
	slurm_step_layout_t *step_layout;
	hostlist_t hl;
	char *host;

	info("got pe_rm_get_job_info called %p %p", job_info, *job_info);

	*job_info = ret_info;

	ret_info->job_name = xstrdup(opt.job_name);
	ret_info->rm_id = NULL;
	ret_info->procs = job->ntasks;
	ret_info->max_instances = 1;
	ret_info->job_key = _get_nrt_job_key(job);
	ret_info->check_pointable = 0;
	ret_info->protocol = xmalloc(sizeof(char *)*2);
	ret_info->protocol[0] = xstrdup(opt.mpi_type);
	ret_info->mode = xmalloc(sizeof(char *)*2);
	ret_info->mode[0] = xstrdup(opt.network);
	ret_info->instance = xmalloc(sizeof(int));
	*ret_info->instance = 1;
/* FIXME: not sure how to handle devicename yet */
	ret_info->devicename = xmalloc(sizeof(char *)*2);
	ret_info->devicename[0] = xstrdup("sn_all");
	ret_info->num_network = 1;
	ret_info->host_count = job->nhosts;

	step_layout = launch_common_get_slurm_step_layout(job);

	ret_info->hosts = xmalloc(sizeof(host_usage_t) * ret_info->host_count);
	i=0;
	hl = hostlist_create(step_layout->node_list);
	while ((host = hostlist_shift(hl))) {
		slurm_addr_t addr;
		ret_info->hosts->host_name = host;
/* FIXME: not sure how to handle host_address yet we are guessing the
 * below will do what we need. */
		/* ret_info->hosts->host_address = */
		/* 	xstrdup_printf("10.0.0.5%d", i+1); */
		slurm_conf_get_addr(host, &addr);
		ret_info->hosts->host_address = inet_ntoa(addr.sin_addr);
		info("%s = %s", ret_info->hosts->host_name, ret_info->hosts->host_address);
		ret_info->hosts->task_count = step_layout->tasks[i];
		ret_info->hosts->task_ids =
			xmalloc(sizeof(int) * ret_info->hosts->task_count);
		for (j=0; j<ret_info->hosts->task_count; j++)
			ret_info->hosts->task_ids[j] = task_id++;
		i++;
		if (i > ret_info->host_count) {
			error("we have more nodes that we bargined for.");
			break;
		}
	}
	hostlist_destroy(hl);

	return 0;
}

/* The handle to the resource manager is returned to the calling
 * function. The calling process needs to use the resource manager
 * handle in subsequent resource manager API calls.
 *
 * A version will be returned as output in the rmapi_version
 * parameter, after POE supplies it as input. The resource manager
 * returns the version value that is installed and running as output.
 *
 * A resource manager ID can be specified that defines a job that is
 * currently running, and for which POE is initializing the resource
 * manager. When the resource manager ID is null, a value for the
 * resource manager ID is included with the job information that is
 * returned by the pe_rm_get_job_info function. When pe_rm_init is
 * called more than once with a null resource manager ID value, it
 * returns the same ID value on the subsequent pe_rm_get_job_info
 * function call.
 *
 * The resource manager can be initialized in either
 * batch or interactive mode. The resource manager must export the
 * environment variable PE_RM_BATCH=yes when in batch mode.
 *
 * By default, the resource manager error messages and any debugging
 * messages that are generated by this function, or any subsequent
 * resource manager API calls, should be written to STDERR. Errors are
 * returned by way of the error message string parameter.
 *
 * When the resource manager is successfully instantiated and
 * initialized, it returns with a file descriptor for a listen socket,
 * which is used by the resource manager daemon to communicate with
 * the calling process. If a resource manager wants to send
 * information to the calling process, it builds an appropriate event
 * that corresponds to the information and sends that event over the
 * socket to the calling process. The calling process could monitor
 * the socket using the select API and read the event when it is ready.
 *
 * IN/OUT rmapi_version - The resource manager API version level. The
 *        value of RM_API_VERSION is defined in permapi.h. Initially,
 *        POE provides this as input, and the resource manager will
 *        return its version level as output.
 * OUT resource_mgr - Pointer to the rmhandle_t handle returned by the
 *        pe_rm_init function. This handle should be used by all other
 *        resource manager API calls.
 * IN rm_id - Pointer to a character string that defines a
 *        resource manager ID, for checkpoint and restart cases. This
 *        pointer can be set to NULL, which means there is no previous
 *        resource manager session or job running. When it is set to a
 *        value, the resource manager uses the specified ID for
 *        returning the proper job information to a subsequent
 *        pe_rm_get_job_info function call.
 * OUT error_msg - The address of a character string at which the
 *        error messages generated by this function are stored. The
 *        memory for this error message is allocated by the malloc API
 *        call. After the error message is processed, the memory
 *        allocated should be freed by a calling free function.
 * RET - Non-negative integer representing a valid file descriptor
 *        number for the socket that will be used by the resource
 *        manager to communicate with the calling process. - SUCCESS
 *        integer less than 0 - FAILURE
 */
extern int pe_rm_init(int *rmapi_version, rmhandle_t *resource_mgr, char *rm_id,
		      char** error_msg)
{
	/* SLURM was originally written against 1300, so we will
	 * return that, no matter what comes in so we always work.
	 */
	*rmapi_version = 1300;
	*resource_mgr = (void *)&job;
#ifdef MYSELF_SO
	/* Since POE opens this lib without RTLD_LAZY | RTLD_GLOBAL we
	   just open ourself again with those options and bada bing
	   bada boom we are good to go with the symbols we need.
	*/
	my_handle = dlopen(MYSELF_SO, RTLD_LAZY | RTLD_GLOBAL);
	if (!my_handle) {
		debug("%s", dlerror());
		return 1;
	}
#else
	fatal("I haven't been told where I am.  This should never happen.");
#endif
	info("got pe_rm_init called %s", rm_id);

	/* Set up slurmctld message handler */
	slurmctld_msg_init();
	slurm_set_launch_type("launch/slurm");
	return 0;
}

/* Used to inform the resource manager that a checkpoint is in
 * progress or has completed. POE calls pe_rm_send_event to provide
 * the resource manager with information about the checkpointed job.
 *
 * IN resource_mgr
 * IN job_event - The address of the pointer to the job_info_t type
 *        that indicates if a checkpoint is in progress (with a type
 *        of JOB_CKPT_IN_PROGRESS) or has completed (with a type of
 *        JOB_CKPT_COMPLETE).
 * OUT error_msg - The address of a character string at which the
 *        error message that is generated by pe_rm_send_event is
 *        stored. The memory for this error message is allocated by the
 *        malloc API call. After the error message is processed, the
 *        memory allocated should be freed by a calling free function.
 * RET 0 - SUCCESS, nonzero on failure.
 */
extern int pe_rm_send_event(rmhandle_t resource_mgr, job_event_t *job_event,
			    char ** error_msg)
{
	info("got pe_rm_send_event called");
	return 0;
}

/* This function is used to submit an interactive job to the resource
 * manager. The job request is either an object or a file (JCL format)
 * that contains information needed by a job to run by way of the
 * resource manager.
 *
 * IN resource_mgr
 * IN job_cmd - The job request (JCL format), either as an object or a file.
 * OUT error_msg - The address of a character string at which the
 *        error messages generated by this function are stored. The
 *        memory for this error message is allocated by the malloc API
 *        call. After the error message is processed, the memory
 *        allocated should be freed by a calling free function.
 * RET 0 - SUCCESS, nonzero on failure.
 */
int pe_rm_submit_job(rmhandle_t resource_mgr, job_command_t job_cmd,
		     char** error_msg)
{
	resource_allocation_response_msg_t *resp;
	job_request_t *pe_job_req = NULL;
	char *myargv[3] = { "poe", "poe", NULL };

	info("got pe_rm_submit_job called %d", job_cmd.job_format);
	if (job_cmd.job_format != 1) {
		/* We don't handle files */
		error("SLURM doesn't handle files to submit_job");
		return 1;
	}

	pe_job_req = (job_request_t *)job_cmd.job_command;

	initialize_and_process_args(2, myargv);
	info("job_type\t= %d", pe_job_req->job_type);

	info("num_nodes\t= %d", pe_job_req->num_nodes);
	if (pe_job_req->num_nodes != -1)
		opt.max_nodes = opt.min_nodes = pe_job_req->num_nodes;

	info("tasks_per_node\t= %d", pe_job_req->tasks_per_node);
	if (pe_job_req->tasks_per_node != -1)
		opt.ntasks_per_node = pe_job_req->tasks_per_node;

	info("total_tasks\t= %d", pe_job_req->total_tasks);
	if (pe_job_req->total_tasks != -1) {
		opt.ntasks_set = true;
		opt.ntasks = pe_job_req->total_tasks;
	}

	info("usage_mode\t= %d", pe_job_req->node_usage);

	info("network_usage protocols\t= %s", pe_job_req->network_usage.protocols);
	opt.mpi_type = xstrdup(pe_job_req->network_usage.protocols);
	info("network_usage adapter_usage\t= %s", pe_job_req->network_usage.adapter_usage);
	info("network_usage adapter_type\t= %s", pe_job_req->network_usage.adapter_type);
	info("network_usage mode\t= %s", pe_job_req->network_usage.mode);
	opt.network = xstrdup(pe_job_req->network_usage.mode);
	info("network_usage instance\t= %s", pe_job_req->network_usage.instances);
	info("network_usage dev_type\t= %s", pe_job_req->network_usage.dev_type);

	info("check_pointable\t= %d", pe_job_req->check_pointable);

	info("check_dir\t= %s", pe_job_req->check_dir);

	info("task_affinity\t= %s", pe_job_req->task_affinity);

	info("pthreads\t= %d", pe_job_req->parallel_threads);

	/* info("pool\t= %s", pe_job_req->pool); */
	/* opt.partition = xstrdup(pe_job_req->pool); */

	info("save_job\t= %s", pe_job_req->save_job_file);

	info("require\t= %s", pe_job_req->requirements);

	info("node_topology\t= %s", pe_job_req->node_topology);
/* 	/\* now global "opt" should be filled in and available, */
/* 	 * create a job from opt */
/* 	 *\/ */
/* 	if (opt.test_only) { */
/* 		int rc = allocate_test(); */
/* 		if (rc) { */
/* 			slurm_perror("allocation failure"); */
/* 			exit (1); */
/* 		} */
/* 		exit (0); */

/* 	} else if (opt.no_alloc) { */
/* 		info("do not allocate resources"); */
/* 		job = job_create_noalloc(); */
/* 		if (create_job_step(job, false) < 0) { */
/* 			exit(error_exit); */
/* 		} */
	if ((resp = existing_allocation())) {
		if (opt.nodes_set_env && !opt.nodes_set_opt &&
		    (opt.min_nodes > resp->node_cnt)) {
			/* This signifies the job used the --no-kill option
			 * and a node went DOWN or it used a node count range
			 * specification, was checkpointed from one size and
			 * restarted at a different size */
			error("SLURM_NNODES environment varariable "
			      "conflicts with allocated node count (%u!=%u).",
			      opt.min_nodes, resp->node_cnt);
			/* Modify options to match resource allocation.
			 * NOTE: Some options are not supported */
			opt.min_nodes = resp->node_cnt;
			xfree(opt.alloc_nodelist);
			if (!opt.ntasks_set)
				opt.ntasks = opt.min_nodes;
		}
		if (opt.alloc_nodelist == NULL)
			opt.alloc_nodelist = xstrdup(resp->node_list);
		/* if (opt.exclusive) */
		/* 	_step_opt_exclusive(); */
		//_set_env_vars(resp);
		//if (_validate_relative(resp))
		//	exit(error_exit);
		job = job_step_create_allocation(resp);
		slurm_free_resource_allocation_response_msg(resp);

		if (opt.begin != 0) {
			error("--begin is ignored because nodes"
			      " are already allocated.");
		}
		if (!job || create_job_step(job, false) < 0)
			exit(error_exit);
	} else {
		/* Combined job allocation and job step launch */
		if (opt.relative_set && opt.relative) {
			fatal("--relative option invalid for job allocation "
			      "request");
		}

		if (!opt.job_name_set_env && opt.job_name_set_cmd)
			setenvfs("SLURM_JOB_NAME=%s", opt.job_name);
		else if (!opt.job_name_set_env && opt.argc)
			setenvfs("SLURM_JOB_NAME=%s", opt.argv[0]);

		if ( !(resp = allocate_nodes()) )
			return error_exit;

		//got_alloc = true;
		//_print_job_information(resp);
		//_set_env_vars(resp);
		/* if (_validate_relative(resp)) { */
		/* 	slurm_complete_job(resp->job_id, 1); */
		/* 	return error_exit; */
		/* } */
		job = job_create_allocation(resp);

		opt.exclusive = false;	/* not applicable for this step */
		opt.time_limit = NO_VAL;/* not applicable for step, only job */
		xfree(opt.constraints);	/* not applicable for this step */
		if (!opt.job_name_set_cmd && opt.job_name_set_env) {
			/* use SLURM_JOB_NAME env var */
			opt.job_name_set_cmd = true;
		}

		/*
		 *  Become --uid user
		 */
		/* if (_become_user () < 0) */
		/* 	info("Warning: Unable to assume uid=%u", opt.uid); */

		if (!job || create_job_step(job, true) < 0) {
			slurm_complete_job(resp->job_id, 1);
			return error_exit;
		}

		slurm_free_resource_allocation_response_msg(resp);
	}
		//*resource_mgr = (void *)job;
	return 0;
}

