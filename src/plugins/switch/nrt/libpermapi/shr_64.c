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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/slurm_xlator.h"
#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/plugstack.h"

#include "src/srun/libsrun/srun_job.h"
#include "src/srun/libsrun/opt.h"
#include "src/srun/libsrun/allocate.h"
#include "src/srun/libsrun/launch.h"
#include "src/plugins/switch/nrt/nrt_keys.h"

bool srun_max_timer = false;
bool srun_shutdown  = false;

static void *my_handle = NULL;
static srun_job_t *job = NULL;
static int debug_level = 0;
static bool got_alloc = false;
static bool slurm_started = false;
static log_options_t log_opts = LOG_OPTS_INITIALIZER;

int sig_array[] = {
	SIGINT,  SIGQUIT, SIGCONT, SIGTERM, SIGHUP,
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, 0 };

extern char **environ;

/* IBM internal definitions to get information on how and who is
 * calling us.
 */
#define PM_POE  0
#define PM_PMD  1
extern int pm_type;
extern int pmdlog;
extern FILE *pmd_lfp;

typedef struct agent_data {
	uint32_t   fe_auth_key;
	slurm_fd_t fe_comm_socket;
} agent_data_t;

static Buf _pack_srun_job_rec(void)
{
	Buf buffer;

	buffer = init_buf(4096);
	pack32(job->jobid, buffer);
	pack32(job->stepid, buffer);
	pack32(job->cpu_count, buffer);
	pack32(job->nhosts, buffer);
	pack32(job->ntasks, buffer);

	return buffer;
}
static srun_job_t * _unpack_srun_job_rec(Buf buffer)
{
	srun_job_t *job_data;

	job_data = xmalloc(sizeof(srun_job_t));
	safe_unpack32(&job_data->jobid, buffer);
	safe_unpack32(&job_data->stepid, buffer);
	safe_unpack32(&job_data->cpu_count, buffer);
	safe_unpack32(&job_data->nhosts, buffer);
	safe_unpack32(&job_data->ntasks, buffer);

	return job_data;

unpack_error:
	xfree(job_data);
	return NULL;
}

/* Validate a message connection
 * Return: true=valid/authenticated */
static bool _validate_connect(slurm_fd_t socket_conn, uint32_t auth_key)
{
	struct timeval tv;
	fd_set read_fds;
	uint32_t read_key;
	bool valid = false;
	int i, n_fds;

	n_fds = socket_conn;
	while (1) {
		FD_ZERO(&read_fds);
		FD_SET(socket_conn, &read_fds);

		tv.tv_sec = 10;
		tv.tv_usec = 0;
		i = select((n_fds + 1), &read_fds, NULL, NULL, &tv);
		if (i == 0)
			break;
		if (i < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		i = slurm_read_stream(socket_conn, (char *)&read_key,
				      sizeof(read_key));
		if ((i == sizeof(read_key)) && (read_key == auth_key)) {
			valid = true;
		} else {
			error("error validating incoming socket connection");
			sleep(1);	/* Help prevent brute force attack */
		}
		break;
	}

	return valid;
}

/* Process a message from PMD */
static void _agent_proc_connect(slurm_fd_t fe_comm_socket,uint32_t fe_auth_key)
{
	slurm_fd_t fe_comm_conn = -1;
	slurm_addr_t be_addr;
	bool be_connected = false;
	Buf buffer = NULL;
	uint32_t buf_size;
	char *buf_data;
	int i, offset = 0;

	while (1) {
		fe_comm_conn = slurm_accept_stream(fe_comm_socket, &be_addr);
		if (fe_comm_conn != SLURM_SOCKET_ERROR) {
			if (_validate_connect(fe_comm_conn, fe_auth_key))
				be_connected = true;
			break;
		}
		if (errno != EINTR) {
			error("slurm_accept_stream: %m");
			break;
		}
	}

	if (!be_connected)
		goto fini;

	buffer = _pack_srun_job_rec();
	buf_size = get_buf_offset(buffer);
	buf_data = (char *) &buf_size;
	i = slurm_write_stream_timeout(fe_comm_conn, buf_data,
				       sizeof(buf_size), 8000);
	if (i < sizeof(buf_size)) {
		error("_agent_proc_connect write: %m");
		goto fini;
	}

	buf_data = get_buf_data(buffer);
	while (buf_size > offset) {
		i = slurm_write_stream_timeout(fe_comm_conn, buf_data + offset,
					       buf_size - offset, 8000);
		if (i < 0) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				error("_agent_proc_connect write: %m");
				break;
			}
		} else if (i > 0) {
			offset += i;
		} else {
			error("_agent_proc_connect write: timeout");
			break;
		}
	}

fini:	if (fe_comm_conn >= 0)
		slurm_close_accepted_conn(fe_comm_conn);
	if (buffer)
		free_buf(buffer);
}

/* Thread to wait for and process messgaes from PMD (via libpermapi) */
static void *_agent_thread(void *arg)
{
        agent_data_t *agent_data_ptr = (agent_data_t *) arg;
	uint32_t   fe_auth_key    = agent_data_ptr->fe_auth_key;
	slurm_fd_t fe_comm_socket = agent_data_ptr->fe_comm_socket;
	fd_set except_fds, read_fds;
	struct timeval tv;
	int i, n_fds;

	xfree(agent_data_ptr);
	n_fds = fe_comm_socket;
	while (fe_comm_socket >= 0) {
		FD_ZERO(&except_fds);
		FD_SET(fe_comm_socket, &except_fds);
		FD_ZERO(&read_fds);
		FD_SET(fe_comm_socket, &read_fds);

		tv.tv_sec =  0;
		tv.tv_usec = 0;
		i = select((n_fds + 1), &read_fds, NULL, &except_fds, &tv);
		if ((i == 0) ||
		    ((i == -1) && (errno == EINTR))) {
			;
		} else if (i == -1) {
			error("select(): %m");
			break;
		} else {	/* i > 0, ready for I/O */
			_agent_proc_connect(fe_comm_socket, fe_auth_key);;
		}
	}
	slurm_shutdown_msg_engine(fe_comm_socket);

	return NULL;
}

/* Generate and return a pseudo-random 32-bit authentication key */
static uint32_t _gen_auth_key(void)
{
	struct timeval tv;
	uint32_t key;

	gettimeofday(&tv, NULL);
	key  = (tv.tv_sec % 1000) * 1000000;
	key += tv.tv_usec;

	return key;
}

/* Spawn a shell to receive communications from PMD and spawn additional
 * PMD on other nodes using a fanout mechanism other than SLURM. */
static void _spawn_fe_agent(void)
{
	char hostname[256];
	uint32_t   fe_auth_key = 0;
	slurm_fd_t fe_comm_socket = -1;
	slurm_addr_t comm_addr;
	uint16_t comm_port;
	pthread_attr_t agent_attr;
	pthread_t agent_tid;
	agent_data_t *agent_data_ptr;

	/* Open socket for back-end program to communicate with */
	if ((fe_comm_socket = slurm_init_msg_engine_port(0)) < 0) {
		error("init_msg_engine_port: %m");
		return;
	}
	if (slurm_get_stream_addr(fe_comm_socket, &comm_addr) < 0) {
		error("slurm_get_stream_addr: %m");
		return;
	}
	comm_port = ntohs(((struct sockaddr_in) comm_addr).sin_port);
	fe_auth_key = _gen_auth_key();
	if (gethostname_short(hostname, sizeof(hostname)))
		fatal("gethostname_short(): %m");

	/* Set up environment variables for the plugin (as called by PMD)
	 * to load job information */
	setenvfs("SLURM_FE_KEY=%u", fe_auth_key);
	setenvfs("SLURM_FE_SOCKET=%s:%hu", hostname, comm_port);

	agent_data_ptr = xmalloc(sizeof(agent_data_t));
	agent_data_ptr->fe_auth_key = fe_auth_key;
	agent_data_ptr->fe_comm_socket = fe_comm_socket;
	slurm_attr_init(&agent_attr);
	pthread_attr_setdetachstate(&agent_attr, PTHREAD_CREATE_DETACHED);
	while ((pthread_create(&agent_tid, &agent_attr, &_agent_thread,
			       (void *) agent_data_ptr))) {
		if (errno != EAGAIN)
			fatal("pthread_create(): %m");
		sleep(1);
	}
	slurm_attr_destroy(&agent_attr);
}

srun_job_t * _read_job_srun_agent(void)
{
	char *key_str  = getenv("SLURM_FE_KEY");
	char *sock_str = getenv("SLURM_FE_SOCKET");
	char buf[32], *host, *sep;
	slurm_fd_t resp_socket;
	uint16_t resp_port;
	uint32_t resp_auth_key, buf_size;
	srun_job_t *srun_job = NULL;
	slurm_addr_t resp_addr;
	char *job_data;
	Buf buffer;
	int i, offset = 0;

	if (!key_str) {
		error("SLURM_FE_KEY environment variable not set");
		return NULL;
	}
	if (!sock_str) {
		error("SLURM_FE_SOCKET environment variable not set");
		return NULL;
	}
	host = xstrdup(sock_str);
	sep = strchr(host, ':');
	if (!sep) {
		error("_read_job_srun_agent(): SLURM_FE_SOCKET is invalid: %s",
		      sock_str);
		xfree(host);
		return NULL;
	}
	sep[0] = '\0';
	resp_port = atoi(sep + 1);
	slurm_set_addr(&resp_addr, resp_port, host);
	xfree(host);
	resp_socket = slurm_open_stream(&resp_addr);
	if (resp_socket < 0) {
		error("slurm_open_msg_conn(%s): %m", sock_str);
		return NULL;
	}

	resp_auth_key = atoi(key_str);
	memcpy(buf + 0, &resp_auth_key, 4);
	i = slurm_write_stream_timeout(resp_socket, buf, 4, 8000);
	if (i < 4) {
		error("_read_job_srun_agent write: %m");
		return NULL;
	}

	i = slurm_read_stream_timeout(resp_socket, (char *) &buf_size, 4, 8000);
	if (i < 4) {
		error("_read_job_srun_agent read (i=%d): %m", i);
		return NULL;
	}
	job_data = xmalloc(buf_size);
	while (buf_size > offset) {
		i = slurm_read_stream_timeout(resp_socket, job_data + offset,
					      buf_size - offset, 8000);
		if (i < 0) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				error("_read_job_srun_agent read (buf=%d): %m", i);
				break;
			}
		} else if (i > 0) {
			offset += i;
		} else {
			error("_read_job_srun_agent read: timeout");
			break;
		}
	}
	slurm_shutdown_msg_engine(resp_socket);
	buffer = create_buf(job_data, buf_size);
	srun_job = _unpack_srun_job_rec(buffer);
	free_buf(buffer);	/* This does xfree(job_data) */

	return srun_job;
}

/************************************/

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
//	srun_job_t *job = *(srun_job_t **)resource_mgr;
	int my_argc = 1;
	char *my_argv[2] = { connect_param->executable, NULL };
//	char *my_argv[2] = { "/bin/hostname", NULL };
	slurm_step_io_fds_t cio_fds = SLURM_STEP_IO_FDS_INITIALIZER;
	uint32_t global_rc = 0;
	int i, rc, fd_cnt;
	int *ctx_sockfds = NULL;

	if (pm_type == PM_PMD) {
		/* If the PMD calls this and it didn't launch anything we need
		 * to not do anything here or PMD will crap out on it. */
		debug("got pe_rm_connect called from PMD");
		if (create_job_step(job, false) != SLURM_SUCCESS) {
			error("no job step created");
			return -1;
		}


		/* if (environ) { */
		/* 	for (i=0; environ[i]; i++) { */
		/* 		PMD_LOG("%s", environ[i]); */
		/* 	} */
		/* } */
	} else if (pm_type == PM_POE) {
		debug("got pe_rm_connect called");
	} else {
		error("pe_rm_connect: unknown caller");
		return -1;
	}

	xassert(job);

	opt.argc = my_argc;
	opt.argv = my_argv;
	opt.user_managed_io = true;

	launch_common_set_stdio_fds(job, &cio_fds);
	if (slurm_step_ctx_daemon_per_node_hack(job->step_ctx,
						connect_param->machine_name,
						connect_param->machine_count)
	    != SLURM_SUCCESS) {
		*error_msg = xstrdup_printf(
			"pe_rm_connect: problem with hack");
		error("%s", *error_msg);
		return -1;
	}

	if (launch_g_step_launch(job, &cio_fds, &global_rc)) {
		*error_msg = xstrdup_printf(
			"pe_rm_connect: problem with launch");
		error("%s", *error_msg);
		return -1;
	}

	debug("3 got pe_rm_connect called from");
	rc = slurm_step_ctx_get(job->step_ctx,
				SLURM_STEP_CTX_USER_MANAGED_SOCKETS,
				&fd_cnt, &ctx_sockfds);
	if (ctx_sockfds == NULL) {
		*error_msg = xstrdup_printf(
			"pe_rm_connect: Unable to get pmd IO socket array %d",
			rc);
		error("%s", *error_msg);
		return -1;
	}
	if (fd_cnt != connect_param->machine_count) {
		*error_msg = xstrdup_printf(
			"pe_rm_connect: looking for %d sockets but got back %d",
			connect_param->machine_count, fd_cnt);
		error("%s", *error_msg);
		return -1;
	}

	for (i=0; i<fd_cnt; i++)
		rm_sockfds[i] = ctx_sockfds[i];

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
	uint32_t rc = 0;
	//srun_job_t *job = *(srun_job_t **)*resource_mgr;

	if (pm_type == PM_PMD) {
		/* If the PMD calls this and it didn't launch anything we need
		 * to not do anything here or PMD will crap out on it. */
		debug("got pe_rm_free called from PMD, "
		      "we don't handle this yet");
		return;
	} else if (pm_type != PM_POE) {
		error("pe_rm_free: unknown caller");
		return;
	}

	/* If the PMD calls this and it didn't launch anything we need
	 * to not do anything here or PMD will crap out on it. */
	xassert(job);

	/* OK we are now really running something */
	debug("got pe_rm_free called %p %p", job, job->step_ctx);
	/* Since we can't relaunch the step here don't worry about the
	   return code.
	*/
	launch_g_step_wait(job, got_alloc);
	/* We are at the end so don't worry about freeing the
	   srun_job_t pointer */
	fini_srun(job, got_alloc, &rc, slurm_started);

	*resource_mgr = NULL;
	dlclose(my_handle);
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
	if (pm_type == PM_PMD) {
		debug("pe_rm_free_event called");
		return 0;
	} else if (pm_type != PM_POE) {
		error("pe_rm_free_event: unknown caller");
		return -1;
	}

	debug("got pe_rm_free_event called");
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
	if (pm_type == PM_PMD) {
		debug("pe_rm_get_event called");
		return 0;
	} else if (pm_type != PM_POE) {
		error("pe_rm_get_event: unknown caller");
		return -1;
	}

	debug("got pe_rm_get_event called %d %p %p",
	      rm_timeout, job_event, *job_event);

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
	char *mode = "IP";
	host_usage_t *host_ptr;
	int table_cnt;
	nrt_tableinfo_t *tables, *table_ptr;
	nrt_job_key_t job_key;
	job_step_create_response_msg_t *resp;
	int network_id_cnt = 0;
	nrt_network_id_t *network_id_list;

	if (pm_type == PM_PMD) {
		debug("pe_rm_get_job_info called");
		return 0;
	} else if (pm_type != PM_POE) {
		error("pe_rm_get_job_info: unknown caller");
		return -1;
	}

	debug("got pe_rm_get_job_info called %p %p", job_info, *job_info);

	*job_info = ret_info;
	ret_info->job_name = xstrdup(opt.job_name);
	ret_info->rm_id = NULL;
	ret_info->procs = job->ntasks;
	ret_info->max_instances = 0;
	ret_info->check_pointable = 0;
	if (!job || !job->step_ctx)
		return -1;

	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_RESP, &resp);
	if (!resp)
		return -1;
	slurm_jobinfo_ctx_get(resp->switch_job, NRT_JOBINFO_KEY, &job_key);
	ret_info->job_key = job_key;

	if (opt.network) {
		char *network_str = xstrdup(opt.network);
		char *save_ptr = NULL,
			*token = strtok_r(network_str, ",", &save_ptr);
		while (token) {
			/* network options */
			if (!strcasecmp(token, "ip")   ||
			    !strcasecmp(token, "ipv4")  ||
			    !strcasecmp(token, "ipv6")) {
				mode = "IP";
			} else if (!strcasecmp(token, "us")) {
				mode = "US";
			}
			/* Currently ignoring all other options */
			token = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(network_str);
	}

	slurm_jobinfo_ctx_get(
		resp->switch_job, NRT_JOBINFO_TABLESPERTASK, &table_cnt);
	ret_info->protocol = xmalloc(sizeof(char *)*(table_cnt+1));
	ret_info->mode = xmalloc(sizeof(char *)*(table_cnt+1));
	ret_info->devicename = xmalloc(sizeof(char *)*(table_cnt+1));
	ret_info->instance = xmalloc(sizeof(int)*(table_cnt+2));

	slurm_jobinfo_ctx_get(resp->switch_job, NRT_JOBINFO_TABLEINFO, &tables);
	info("got count of %d", table_cnt);
	network_id_list = xmalloc(sizeof(nrt_network_id_t) * table_cnt);
	for (i=0, table_ptr=tables; i<table_cnt; i++, table_ptr++) {
		for (j = 0; j < network_id_cnt; j++) {
			if (table_ptr->network_id == network_id_list[j])
				break;
		}
		if (j >= network_id_cnt) {
			/* add this new network ID to our table */
			network_id_list[network_id_cnt++] =
				table_ptr->network_id;
		}
/* FIXME: Format of these data structure contents not well defined */
		ret_info->protocol[i] = xstrdup(table_ptr->protocol_name);
		ret_info->mode[i] = xstrdup(mode);
		ret_info->devicename[i] = xstrdup(table_ptr->adapter_name);
		ret_info->instance[i] = table_ptr->instance;
		ret_info->max_instances = MAX(ret_info->max_instances,
					      ret_info->instance[i]);
		info("%d: %s %s %s %d", i, ret_info->protocol[i], ret_info->mode[i], ret_info->devicename[i], ret_info->instance[i]);
	}
	xfree(network_id_list);
	ret_info->instance[i] = -1;
	ret_info->num_network = network_id_cnt;
	ret_info->host_count = job->nhosts;

	step_layout = launch_common_get_slurm_step_layout(job);

	ret_info->hosts = xmalloc(sizeof(host_usage_t) * ret_info->host_count);
	host_ptr = ret_info->hosts;
	i=0;
	hl = hostlist_create(step_layout->node_list);
	while ((host = hostlist_shift(hl))) {
		slurm_addr_t addr;
		host_ptr->host_name = host;
/* FIXME: not sure how to handle host_address yet we are guessing the
 * below will do what we need. */
		/* host_ptr->host_address = */
		/* 	xstrdup_printf("10.0.0.5%d", i+1); */
		slurm_conf_get_addr(host, &addr);
		host_ptr->host_address = xstrdup(inet_ntoa(addr.sin_addr));
		host_ptr->task_count = step_layout->tasks[i];
		host_ptr->task_ids =
			xmalloc(sizeof(int) * host_ptr->task_count);
		for (j=0; j<host_ptr->task_count; j++)
			host_ptr->task_ids[j] = task_id++;
		debug3("%s = %s %d tasks",
		       host_ptr->host_name, host_ptr->host_address,
		       host_ptr->task_count);
		i++;
		if (i > ret_info->host_count) {
			error("we have more nodes that we bargined for.");
			break;
		}
		host_ptr++;
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
	char *srun_debug = NULL;

	/* SLURM was originally written against 1300, so we will
	 * return that, no matter what comes in so we always work.
	 */
	*rmapi_version = 1300;
	*resource_mgr = (void *)&job;

#ifdef MYSELF_SO
	/* Since POE opens this lib without
	   RTLD_LAZY | RTLD_GLOBAL | RTLD_DEEPBIND
	   we just open ourself again with those options and bada bing
	   bada boom we are good to go with the symbols we need.
	*/
	my_handle = dlopen(MYSELF_SO, RTLD_LAZY | RTLD_GLOBAL | RTLD_DEEPBIND);
	if (!my_handle) {
		debug("%s", dlerror());
		return 1;
	}
#else
	fatal("I haven't been told where I am.  This should never happen.");
#endif

	if (slurm_select_init(1) != SLURM_SUCCESS )
		fatal( "failed to initialize node selection plugin" );

	slurm_set_launch_type("launch/slurm");

	if ((srun_debug = getenv("SRUN_DEBUG")))
		debug_level = atoi(srun_debug);
	log_opts.stderr_level  = debug_level;
	log_opts.logfile_level = debug_level;
	log_opts.syslog_level  = debug_level;

	if (pm_type == PM_PMD)
		log_alter_with_fp(log_opts, LOG_DAEMON, pmd_lfp);
	else
		log_alter(log_opts, LOG_DAEMON, "/dev/null");

	/* This will be used later in the code to set the
	 * _verbose level. */
	if (debug_level >= LOG_LEVEL_INFO)
		debug_level -= LOG_LEVEL_INFO;

	debug("got pe_rm_init called %s %p", rm_id, info);

	if (pm_type == PM_PMD) {
		uint32_t job_id = -1, step_id = -1;
		resource_allocation_response_msg_t resp;
		char *myargv[3] = { "pmd", "pmd", NULL };
		uint16_t cpn = 1;

		init_srun(2, myargv, &log_opts, debug_level, 1);

		if ((srun_debug = getenv("SLURM_JOB_ID")))
			job_id = atoi(srun_debug);
		if ((srun_debug = getenv("SLURM_STEP_ID")))
			step_id = atoi(srun_debug);
		if (job_id == -1 || step_id == -1) {
			error("SLURM_JOB_ID or SLURM_STEP_ID "
			      "not found %d.%d", job_id, step_id);
			return -1;
		}
		opt.no_alloc = true;

		memset(&resp, 0, sizeof(resp));
		opt.nodelist = resp.node_list = "localhost";
		resp.node_cnt = 1;
		resp.job_id = job_id;
		resp.num_cpu_groups = 1;
		resp.cpus_per_node = &cpn;
		resp.cpu_count_reps = &resp.node_cnt;

		job = _read_job_srun_agent();
		if (!job) {
			error("no job created");
			return -1;
		}
		error("job created %u.%u", job->jobid, job->stepid);
		info("pe_rm_init done");
	} else if (pm_type == PM_POE) {
		/* Don't print standard messages when running under
		   PMD or it will get confused.
		*/
		if ((srun_debug = getenv("SRUN_DEBUG")))
			debug_level = atoi(srun_debug);
		if (debug_level) {
			log_opts.stderr_level  = debug_level;
			log_opts.logfile_level = debug_level;
			log_opts.syslog_level  = debug_level;

			log_alter(log_opts, LOG_DAEMON, "/dev/null");
			/* This will be used later in the code to set the
			 * _verbose level. */
			if (debug_level >= LOG_LEVEL_INFO)
				debug_level -= LOG_LEVEL_INFO;
		}
		debug("got pe_rm_init called %s", rm_id);

		/* Set up slurmctld message handler */
		slurmctld_msg_init();

		/* Create agent thread to forward job credential needed for
		 * PMD to fanout child processes on other nodes */
		_spawn_fe_agent();
	} else {
		error("pe_rm_init: unknown caller");
		return -1;
	}

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
	int rc;

	if (pm_type == PM_PMD) {
		debug("pe_rm_send_event called");
		return 0;
	} else if (pm_type != PM_POE) {
		error("pe_rm_send_event: unknown caller");
		return -1;
	}
	debug("got pe_rm_send_event called with event type %d",
	      job_event->event);

	if ((job_event->event == JOB_CKPT_COMPLETE) && job) {
		struct ckpt_end_data *ckpt_end_ptr = (struct ckpt_end_data *)
						     job_event->event_data;
		rc = slurm_checkpoint_complete(job->jobid, job->stepid,
					       ckpt_end_ptr->ckpt_start_time,
					       ckpt_end_ptr->ckpt_rc,
					       ckpt_end_ptr->ckpt_msg);
		if (rc != SLURM_SUCCESS) {
			error("pe_rm_send_event: Unable to process checkpoint "
			      "complete event for %u.%u",
			      job->jobid, job->stepid);
		} else {
			debug("pe_rm_send_event: Checkpoint complete for %u.%u",
			      job->jobid, job->stepid);
		}
	} else if ((job_event->event == JOB_CKPT_IN_PROGRESS) && job) {
		/* FIXME: This may need to trigger switch/nrt call on each node
		 * to preempt the job. Not sure how this works yet... */
		debug("pe_rm_send_event: Checkpoint in progress for %u.%u",
		      job->jobid, job->stepid);
	}

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
	job_request_t *pe_job_req = NULL;
	char *myargv[3] = { "poe", "poe", NULL };

	if (getenv("SLURM_STARTED_STEP"))
		slurm_started = true;

	if (pm_type == PM_PMD) {
		debug("pe_rm_submit_job called");
		return 0;
	} else if (pm_type != PM_POE) {
		error("pe_rm_submit_job: unknown caller");
		return -1;
	}

	debug("got pe_rm_submit_job called %d", job_cmd.job_format);
	if (job_cmd.job_format != 1) {
		/* We don't handle files */
		error("SLURM doesn't handle files to submit_job");
		return -1;
	}

	init_srun(2, myargv, &log_opts, debug_level, 1);

	pe_job_req = (job_request_t *)job_cmd.job_command;
	debug2("num_nodes\t= %d", pe_job_req->num_nodes);
	debug2("tasks_per_node\t= %d", pe_job_req->tasks_per_node);
	debug2("total_tasks\t= %d", pe_job_req->total_tasks);
	debug2("usage_mode\t= %d", pe_job_req->node_usage);
	debug2("network_usage protocols\t= %s",
	       pe_job_req->network_usage.protocols);
	debug2("network_usage adapter_usage\t= %s",
	       pe_job_req->network_usage.adapter_usage);
	debug2("network_usage adapter_type\t= %s",
	       pe_job_req->network_usage.adapter_type);
	debug2("network_usage mode\t= %s", pe_job_req->network_usage.mode);
	debug2("network_usage instance\t= %s",
	       pe_job_req->network_usage.instances);
	debug2("network_usage dev_type\t= %s",
	       pe_job_req->network_usage.dev_type);
	debug2("check_pointable\t= %d", pe_job_req->check_pointable);
	debug2("check_dir\t= %s", pe_job_req->check_dir);
	debug2("task_affinity\t= %s", pe_job_req->task_affinity);
	debug2("pthreads\t= %d", pe_job_req->parallel_threads);
	debug2("save_job\t= %s", pe_job_req->save_job_file);
	debug2("require\t= %s", pe_job_req->requirements);
	debug2("node_topology\t= %s", pe_job_req->node_topology);
	debug2("pool\t= %s", pe_job_req->pool);

	if (pe_job_req->num_nodes != -1)
		opt.max_nodes = opt.min_nodes = pe_job_req->num_nodes;

	if (pe_job_req->tasks_per_node != -1)
		opt.ntasks_per_node = pe_job_req->tasks_per_node;

	if (pe_job_req->total_tasks != -1) {
		opt.ntasks_set = true;
		opt.ntasks = pe_job_req->total_tasks;
	}

	if (!opt.job_name)
		opt.job_name = xstrdup("poe_ran_job");

	create_srun_job(&job, &got_alloc, slurm_started);

	/* make sure we set up a signal handler */
	if (!slurm_started)
		pre_launch_srun_job(job, 0);

	return 0;
}
