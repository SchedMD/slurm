/*****************************************************************************\
 *  launch_runjob.c - Define job launch using IBM's runjob.  Typically
 *                    for use with a BGQ machine.
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>

#include "src/common/slurm_xlator.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_auth.h"

#include "src/api/step_ctx.h"
#include "src/api/step_launch.h"

#include "src/srun/libsrun/launch.h"

#include "src/plugins/launch/runjob/runjob_interface.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "launch runjob plugin";
const char plugin_type[]        = "launch/runjob";
const uint32_t plugin_version   = 101;

static srun_job_t *local_srun_job = NULL;

extern void launch_p_fwd_signal(int signal);

static void _send_step_complete_rpc(int step_rc)
{
	slurm_msg_t req;
	step_complete_msg_t msg;
	int rc;

	memset(&msg, 0, sizeof(step_complete_msg_t));
	msg.job_id = local_srun_job->jobid;
	msg.job_step_id = local_srun_job->stepid;
	msg.range_first = 0;
	msg.range_last = 0;
	msg.step_rc = step_rc;
	msg.jobacct = jobacctinfo_create(NULL);

	slurm_msg_t_init(&req);
	req.msg_type = REQUEST_STEP_COMPLETE;
	req.data = &msg;
/*	req.address = step_complete.parent_addr; */

	debug3("Sending step complete RPC to slurmctld");
	if (slurm_send_recv_controller_rc_msg(&req, &rc) < 0)
		error("Error sending step complete RPC to slurmctld");
	jobacctinfo_destroy(msg.jobacct);
}

static void _handle_timeout(srun_timeout_msg_t *timeout_msg)
{
	time_t now = time(NULL);
	char time_str[24];

	if (now < timeout_msg->timeout) {
		slurm_make_time_str(&timeout_msg->timeout,
				    time_str, sizeof(time_str));
		debug("step %u.%u will timeout at %s",
		      timeout_msg->job_id, timeout_msg->step_id, time_str);
		return;
	}

	slurm_make_time_str(&now, time_str, sizeof(time_str));
	error("*** STEP %u.%u CANCELLED AT %s DUE TO TIME LIMIT ***",
	      timeout_msg->job_id, timeout_msg->step_id, time_str);
	launch_p_fwd_signal(SIGKILL);
	return;
}

static void
_handle_msg(slurm_msg_t *msg)
{
	static uint32_t slurm_uid = NO_VAL;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred, NULL);
	uid_t uid = getuid();
	job_step_kill_msg_t *ss;
	srun_user_msg_t *um;

	if (slurm_uid == NO_VAL)
		slurm_uid = slurm_get_slurm_user_id();
	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u",
		       (unsigned int) req_uid);
 		return;
	}

	switch (msg->msg_type) {
	case SRUN_PING:
		debug3("slurmctld ping received");
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		slurm_free_srun_ping_msg(msg->data);
		break;
	case SRUN_JOB_COMPLETE:
		debug("received job step complete message");
		runjob_signal(SIGKILL);
		slurm_free_srun_job_complete_msg(msg->data);
		break;
	case SRUN_USER_MSG:
		um = msg->data;
		info("%s", um->msg);
		slurm_free_srun_user_msg(msg->data);
		break;
	case SRUN_TIMEOUT:
		debug("received job step timeout message");
		_handle_timeout(msg->data);
		slurm_free_srun_timeout_msg(msg->data);
		break;
	case SRUN_STEP_SIGNAL:
		ss = msg->data;
		debug("received step signal %u RPC", ss->signal);
		if (ss->signal)
			runjob_signal(ss->signal);
		slurm_free_job_step_kill_msg(msg->data);
		break;
	default:
		debug("received spurious message type: %u",
		      msg->msg_type);
		break;
	}
	return;
}

static void *_msg_thr_internal(void *arg)
{
	slurm_addr_t cli_addr;
	slurm_fd_t newsockfd;
	slurm_msg_t *msg;
	int *slurmctld_fd_ptr = (int *)arg;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (!srun_shutdown) {
		newsockfd = slurm_accept_msg_conn(*slurmctld_fd_ptr, &cli_addr);
		if (newsockfd == SLURM_SOCKET_ERROR) {
			if (errno != EINTR)
				error("slurm_accept_msg_conn: %m");
			continue;
		}
		msg = xmalloc(sizeof(slurm_msg_t));
		if (slurm_receive_msg(newsockfd, msg, 0) != 0) {
			error("slurm_receive_msg: %m");
			/* close the new socket */
			slurm_close_accepted_conn(newsockfd);
			continue;
		}
		_handle_msg(msg);
		slurm_free_msg(msg);
		slurm_close_accepted_conn(newsockfd);
	}
	return NULL;
}

static pthread_t
_spawn_msg_handler(void)
{
	pthread_attr_t attr;
	pthread_t msg_thread;
	static int slurmctld_fd;

	slurmctld_fd =
		local_srun_job->step_ctx->launch_state->slurmctld_socket_fd;
	if (slurmctld_fd < 0)
		return (pthread_t) 0;
	local_srun_job->step_ctx->launch_state->slurmctld_socket_fd = -1;

	slurm_attr_init(&attr);
	if (pthread_create(&msg_thread, &attr, _msg_thr_internal,
			   (void *) &slurmctld_fd))
		error("pthread_create of message thread: %m");
	slurm_attr_destroy(&attr);
	return msg_thread;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	return SLURM_SUCCESS;
}

extern int launch_p_setup_srun_opt(char **rest)
{
	int i;
	int command_pos = 0;
	uint32_t taskid = NO_VAL;

	if (opt.reboot) {
		info("WARNING: If your job is smaller than the block "
		     "it is going to run on and other jobs are "
		     "running on it the --reboot option will not be "
		     "honored.  If this is the case, contact your "
		     "admin to reboot the block for you.");
	}

	/* A bit of setup for IBM's runjob.  runjob only has so many
	   options, so it isn't that bad.
	*/
	if (!opt.test_only) {
	 	/* Since we need the opt.argc to allocate the opt.argv array
		 * we need to do this before actually messing with
		 * things. All the extra options added to argv will be
		 * handled after the allocation. */

		/* Default location of the actual command to be ran. We always
		 * have to add 5 options (calling prog, '-p', '--np',
		 * '--env-all' and ':') no matter what. */
		command_pos = 7;

		if (opt.cwd_set)
			command_pos += 2;
		if (opt.labelio)
			command_pos += 2;
		if (_verbose)
			command_pos += 2;
		if (opt.quiet)
			command_pos += 2;
		if (opt.ifname) {
			if (!parse_uint32(opt.ifname, &taskid)
			    && ((int) taskid < opt.ntasks)) {
				command_pos += 2;
			}
		}
		if (opt.launcher_opts) {
			char *save_ptr = NULL, *tok;
			char *tmp = xstrdup(opt.launcher_opts);
			tok = strtok_r(tmp, " ", &save_ptr);
			while (tok) {
				command_pos++;
				tok = strtok_r(NULL, " ", &save_ptr);
			}
			xfree(tmp);
		}

		if (opt.export_env) {
			for (i = 0; opt.export_env[i]; i++) {
				if (opt.export_env[i] == ',')
					command_pos++;
			}
			command_pos += 5;	/* baseline overhead */
		}
	}

	/* We need to do +2 here just incase multi-prog is needed (we
	 * add an extra argv on so just make space for it). */
	opt.argv = (char **) xmalloc((opt.argc + command_pos + 2) *
		   sizeof(char *));

	if (!opt.test_only) {
		i = 0;
		/* First arg has to be something when sending it to the
		   runjob api.  This can be anything, we put runjob
		   here so --launch-cmd looks nice :), but it doesn't matter.
		*/
		opt.argv[i++] = xstrdup("runjob");
		/* srun launches tasks using runjob API. Slurmd is not used */
		/* We are always going to set ntasks_per_node and ntasks */
		// if (opt.ntasks_per_node != NO_VAL) {
		opt.argv[i++]  = xstrdup("-p");
		opt.argv[i++]  = xstrdup_printf("%d", opt.ntasks_per_node);
		// }

		// if (opt.ntasks_set) {
		opt.argv[i++]  = xstrdup("--np");
		opt.argv[i++]  = xstrdup_printf("%d", opt.ntasks);
		// }

		if (opt.cwd_set) {
			opt.argv[i++]  = xstrdup("--cwd");
			opt.argv[i++]  = xstrdup(opt.cwd);
		}

		if (opt.labelio) {
			opt.argv[i++]  = xstrdup("--label");
			opt.argv[i++]  = xstrdup("short");
			/* Since we are getting labels from runjob. and we
			 * don't want 2 sets (slurm's will always be 000)
			 * remove it case. */
			opt.labelio = 0;
		}

		if (opt.quiet) {
			opt.argv[i++]  = xstrdup("--verbose");
			opt.argv[i++]  = xstrdup("OFF");
		}

		if (_verbose) {
			opt.argv[i++]  = xstrdup("--verbose");
			opt.argv[i++]  = xstrdup_printf("%d", _verbose);
		}

		if (taskid != NO_VAL) {
			opt.argv[i++]  = xstrdup("--stdinrank");
			opt.argv[i++]  = xstrdup_printf("%u", taskid);
		}

		if (opt.launcher_opts) {
			char *save_ptr = NULL, *tok;
			char *tmp = xstrdup(opt.launcher_opts);
			tok = strtok_r(tmp, " ", &save_ptr);
			while (tok) {
				opt.argv[i++]  = xstrdup(tok);
				tok = strtok_r(NULL, " ", &save_ptr);
			}
			xfree(tmp);
		}

		if (opt.export_env) {
			char *tmp_env, *tok, *save_ptr = NULL, *eq_ptr;
			bool has_equal = false;
			opt.argv[i++]  = xstrdup("--exp-env");
			opt.argv[i++]  = xstrdup("SLURM_JOB_ID");
			opt.argv[i++]  = xstrdup("SLURM_STEP_ID");
			tmp_env = xstrdup(opt.export_env);
			tok = strtok_r(tmp_env, ",", &save_ptr);
			while (tok) {
				if (!strcasecmp(tok, "NONE"))
					break;
				eq_ptr = strchr(tok, '=');
				if (eq_ptr)
					has_equal = true;
				else
					opt.argv[i++]  = xstrdup(tok);
				tok = strtok_r(NULL, ",", &save_ptr);
			}
			xfree(tmp_env);
			if (has_equal) {
				opt.argv[i++]  = xstrdup("--envs");
				tmp_env = xstrdup(opt.export_env);
				tok = strtok_r(tmp_env, ",", &save_ptr);
				while (tok) {
					eq_ptr = strchr(tok, '=');
					if (eq_ptr)
						opt.argv[i++]  = xstrdup(tok);
					tok = strtok_r(NULL, ",", &save_ptr);
				}
				xfree(tmp_env);
			}
		} else {
			/* Export all the environment so the
			 * runjob_mux will get the correct info about
			 * the job, namely the block. */
			opt.argv[i++] = xstrdup("--env-all");
		}

		/* With runjob anything after a ':' is treated as the actual
		 * job, which in this case is exactly what it is.  So, very
		 * sweet. */
		opt.argv[i++] = xstrdup(":");
		command_pos = i;
		opt.argc += command_pos;

		/* Set default job name to the executable name rather than
		 * "runjob" */
		if (!opt.job_name_set_cmd && (command_pos < opt.argc)) {
			opt.job_name_set_cmd = true;
			opt.job_name = xstrdup(rest[0]);
		}
	}
	return command_pos;
}

extern int launch_p_handle_multi_prog_verify(int command_pos)
{
	return 0;
}

extern int launch_p_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job)
{
	if (opt.launch_cmd) {
		int i = 0;
		char *cmd_line = NULL;

		while (opt.argv[i])
			xstrfmtcat(cmd_line, "%s ", opt.argv[i++]);
		printf("%s\n", cmd_line);
		xfree(cmd_line);
		exit(0);
	}
	return launch_common_create_job_step(job, use_all_cpus,
					     signal_function,
					     destroy_job);
}

extern int launch_p_step_launch(
	srun_job_t *job, slurm_step_io_fds_t *cio_fds, uint32_t *global_rc,
	slurm_step_launch_callbacks_t *step_callbacks)
{
	pthread_t msg_thread;

	local_srun_job = job;

	msg_thread = _spawn_msg_handler();

	*global_rc = runjob_launch(opt.argc, opt.argv,
				   cio_fds->in.fd,
				   cio_fds->out.fd,
				   cio_fds->err.fd);
	_send_step_complete_rpc(*global_rc);
	if (msg_thread) {
		srun_shutdown = true;
		pthread_cancel(msg_thread);
		pthread_join(msg_thread, NULL);
	}

	return 0;
}

extern int launch_p_step_wait(srun_job_t *job, bool got_alloc)
{
	return 0;
}

extern int launch_p_step_terminate(void)
{
	info("Terminating job step %u.%u",
	     local_srun_job->jobid, local_srun_job->stepid);
	runjob_signal(SIGKILL);
	return SLURM_SUCCESS;
}


extern void launch_p_print_status(void)
{

}

extern void launch_p_fwd_signal(int signal)
{
	runjob_signal(signal);
}
