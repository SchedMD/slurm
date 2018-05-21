/*****************************************************************************\
 *  launch_aprun.c - Define job launch using Cray's aprun.
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <ctype.h>
#include <stdlib.h>

#include "src/common/slurm_xlator.h"
#include "src/common/parse_time.h"
#include "src/srun/libsrun/launch.h"
#include "src/srun/libsrun/multi_prog.h"

#include "src/api/step_ctx.h"
#include "src/api/step_launch.h"


/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
resource_allocation_response_msg_t *global_resp __attribute__((weak_import)) =
	NULL;
#else
resource_allocation_response_msg_t *global_resp = NULL;
#endif

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "launch aprun plugin";
const char plugin_type[]        = "launch/aprun";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static pid_t aprun_pid = 0;

extern void launch_p_fwd_signal(int signal);

/* Convert a Slurm hostlist expression into the equivalent node index
 * value expression.
 */
static char *_get_nids(slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	hostlist_t hl;
	char *nids = NULL;
	int node_cnt;
	xassert(srun_opt);

	if (!opt_local->nodelist)
		return NULL;
	hl = hostlist_create(opt_local->nodelist);
	if (!hl) {
		error("Invalid hostlist: %s", opt_local->nodelist);
		return NULL;
	}
	//info("input hostlist: %s", nodelist);
	hostlist_uniq(hl);

	/* aprun needs the hostlist to be the exact size requested.
	   So if it doesn't set it.
	*/
	node_cnt = hostlist_count(hl);
	if (srun_opt->nodes_set_opt && (node_cnt != opt_local->min_nodes)) {
		error("You requested %d nodes and %d hosts.  These numbers "
		      "must be the same, so setting number of nodes to %d",
		      opt_local->min_nodes, node_cnt, node_cnt);
	}
	opt_local->min_nodes = node_cnt;
	opt_local->nodes_set = 1;

	nids = cray_nodelist2nids(hl, NULL);

	hostlist_destroy(hl);
	//info("output node IDs: %s", nids);

	return nids;
}

/*
 * Parse a multi-prog input file line
 * line IN - line to parse
 * command_pos IN/OUT - where in srun_opt->argv we are
 * count IN - which command we are on
 * return 0 if empty line, 1 if added
 */
static int _parse_prog_line(char *in_line, int *command_pos, int count,
			    slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	int i, cmd_inx;
	int first_task_inx, last_task_inx;
	hostset_t hs = NULL;
	char *tmp_str = NULL;
	xassert(srun_opt);

	xassert(opt_local->ntasks);

	/* Get the task ID string */
	for (i = 0; in_line[i]; i++)
		if (!isspace(in_line[i]))
			break;

	if (!in_line[i]) /* empty line */
		return 0;
	else if (in_line[i] == '#')
		return 0;
	else if (!isdigit(in_line[i]))
		goto bad_line;
	first_task_inx = i;
	for (i++; in_line[i]; i++) {
		if (isspace(in_line[i]))
			break;
	}
	if (!isspace(in_line[i]))
		goto bad_line;
	last_task_inx = i;


	/* Now transfer data to the function arguments */
	in_line[last_task_inx] = '\0';
	xstrfmtcat(tmp_str, "[%s]", in_line + first_task_inx);
	hs = hostset_create(tmp_str);
	xfree(tmp_str);
	in_line[last_task_inx] = ' ';
	if (!hs)
		goto bad_line;


	if (count) {
		srun_opt->argc += 1;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[(*command_pos)++] = xstrdup(":");
	}
	srun_opt->argc += 2;
	xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
	srun_opt->argv[(*command_pos)++] = xstrdup("-n");
	srun_opt->argv[(*command_pos)++] = xstrdup_printf("%d",
							   hostset_count(hs));
	hostset_destroy(hs);

	/* Get the command */
	for (i++; in_line[i]; i++) {
		if (!isspace(in_line[i]))
			break;
	}
	if (in_line[i] == '\0')
		goto bad_line;

	cmd_inx = i;
	for ( ; in_line[i]; i++) {
		if (isspace(in_line[i])) {
			if (i > cmd_inx) {
				int diff = i - cmd_inx + 1;
				char tmp_char[diff + 1];
				snprintf(tmp_char, diff, "%s",
					 in_line + cmd_inx);
				srun_opt->argc += 1;
				xrealloc(srun_opt->argv,
					 srun_opt->argc * sizeof(char *));
				srun_opt->argv[(*command_pos)++] =
					xstrdup(tmp_char);
			}
			cmd_inx = i + 1;
		} else if (in_line[i] == '\n')
			break;
	}

	return 1;

bad_line:
	error("invalid input line: %s", in_line);

	return 0;
}

static void _handle_multi_prog(char *in_file, int *command_pos,
			       slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	char in_line[512];
	FILE *fp;
	int count = 0;
	xassert(srun_opt);

	if (verify_multi_name(in_file, opt_local))
		exit(error_exit);

	fp = fopen(in_file, "r");
	if (!fp) {
		fatal("fopen(%s): %m", in_file);
		return;
	}

	while (fgets(in_line, sizeof(in_line), fp)) {
		if (_parse_prog_line(in_line, command_pos, count, opt_local))
			count++;
	}

	return;
}

static void _unblock_signals(void)
{
	sigset_t set;
	int i;

	for (i = 0; sig_array[i]; i++) {
		/* eliminate pending signals, then set to default */
		xsignal(sig_array[i], SIG_IGN);
		xsignal(sig_array[i], SIG_DFL);
	}
	sigemptyset(&set);
	xsignal_set_mask (&set);
}

static void _send_step_complete_rpc(srun_job_t *srun_job, int step_rc)
{
	slurm_msg_t req;
	step_complete_msg_t msg;
	int rc;

	memset(&msg, 0, sizeof(step_complete_msg_t));
	msg.job_id = srun_job->jobid;
	msg.job_step_id = srun_job->stepid;
	msg.range_first = 0;
	msg.range_last = 0;
	msg.step_rc = step_rc;
	msg.jobacct = jobacctinfo_create(NULL);

	slurm_msg_t_init(&req);
	req.msg_type = REQUEST_STEP_COMPLETE;
	req.data = &msg;
/*	req.address = step_complete.parent_addr; */

	debug3("Sending step complete RPC to slurmctld");
	if (slurm_send_recv_controller_rc_msg(&req, &rc, working_cluster_rec)
	    < 0)
		error("Error sending step complete RPC to slurmctld");
	jobacctinfo_destroy(msg.jobacct);
}

static void _handle_step_complete(srun_job_complete_msg_t *comp_msg)
{
	launch_p_fwd_signal(SIGKILL);
	return;
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

static void _handle_msg(slurm_msg_t *msg)
{
	static uint32_t slurm_uid = NO_VAL;
	char *auth_info = slurm_get_auth_info();
	uid_t req_uid;
	uid_t uid = getuid();
	job_step_kill_msg_t *ss;
	srun_user_msg_t *um;

	req_uid = g_slurm_auth_get_uid(msg->auth_cred, auth_info);
	xfree(auth_info);

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
		break;
	case SRUN_JOB_COMPLETE:
		debug("received job step complete message");
		_handle_step_complete(msg->data);
		break;
	case SRUN_USER_MSG:
		um = msg->data;
		info("%s", um->msg);
		break;
	case SRUN_TIMEOUT:
		debug2("received job step timeout message");
		_handle_timeout(msg->data);
		break;
	case SRUN_STEP_SIGNAL:
		ss = msg->data;
		debug("received step signal %u RPC", ss->signal);
		if (ss->signal)
			launch_p_fwd_signal(ss->signal);
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
	int newsockfd;
	slurm_msg_t msg;
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
		slurm_msg_t_init(&msg);
		if (slurm_receive_msg(newsockfd, &msg, 0) != 0) {
			error("slurm_receive_msg: %m");
			/* close the new socket */
			close(newsockfd);
			continue;
		}
		_handle_msg(&msg);
		slurm_free_msg_members(&msg);
		close(newsockfd);
	}
	return NULL;
}

static pthread_t _spawn_msg_handler(srun_job_t *job)
{
	pthread_attr_t attr;
	pthread_t msg_thread;
	static int slurmctld_fd;

	slurmctld_fd = job->step_ctx->launch_state->slurmctld_socket_fd;
	if (slurmctld_fd < 0)
		return (pthread_t) 0;
	job->step_ctx->launch_state->slurmctld_socket_fd = -1;

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

extern int launch_p_setup_srun_opt(char **rest, slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	int command_pos = 0;
	xassert(srun_opt);

	if (srun_opt->test_only) {
		error("--test-only not supported with aprun");
		exit (1);
	} else if (srun_opt->no_alloc) {
		error("--no-allocate not supported with aprun");
		exit (1);
	}
	if (srun_opt->slurmd_debug != LOG_LEVEL_QUIET) {
		error("--slurmd-debug not supported with aprun");
		srun_opt->slurmd_debug = LOG_LEVEL_QUIET;
	}

	srun_opt->argc += 2;

	srun_opt->argv = (char **) xmalloc(srun_opt->argc * sizeof(char *));

	srun_opt->argv[command_pos++] = xstrdup("aprun");
	/* Set default job name to the executable name rather than
	 * "aprun" */
	if (!srun_opt->job_name_set_cmd && (1 < srun_opt->argc)) {
		srun_opt->job_name_set_cmd = true;
		opt_local->job_name = xstrdup(rest[0]);
	}

	if (opt_local->cpus_per_task) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-d");
		srun_opt->argv[command_pos++] = xstrdup_printf(
			"%u", opt_local->cpus_per_task);
	}

	if (srun_opt->exclusive) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-F");
		srun_opt->argv[command_pos++] = xstrdup("exclusive");
	} else if (opt_local->shared == 1) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-F");
		srun_opt->argv[command_pos++] = xstrdup("share");
	}

	if (srun_opt->cpu_bind_type & CPU_BIND_ONE_THREAD_PER_CORE) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-j");
		srun_opt->argv[command_pos++] = xstrdup("1");
	}

	if (opt_local->nodelist) {
		char *nids = _get_nids(opt_local);
		if (nids) {
			srun_opt->argc += 2;
			xrealloc(srun_opt->argv,
				 srun_opt->argc * sizeof(char *));
			srun_opt->argv[command_pos++] = xstrdup("-L");
			srun_opt->argv[command_pos++] = xstrdup(nids);
			xfree(nids);
		}
	}

	if (opt_local->mem_per_cpu != NO_VAL64) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-m");
		srun_opt->argv[command_pos++] = xstrdup_printf("%"PRIu64"",
							opt_local->mem_per_cpu);
	}

	if (opt_local->ntasks_per_node != NO_VAL) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-N");
		srun_opt->argv[command_pos++] = xstrdup_printf(
			"%u", opt_local->ntasks_per_node);
		if (!opt_local->ntasks && opt_local->min_nodes)
			opt_local->ntasks = opt_local->ntasks_per_node *
					    opt_local->min_nodes;
	} else if (opt_local->nodes_set && opt_local->min_nodes) {
		uint32_t tasks_per_node;
		opt_local->ntasks = MAX(opt_local->ntasks,
					opt_local->min_nodes);
		tasks_per_node = (opt_local->ntasks + opt_local->min_nodes - 1) /
			opt_local->min_nodes;
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-N");
		srun_opt->argv[command_pos++] = xstrdup_printf("%u",
								tasks_per_node);
	}

	if (opt_local->ntasks && !srun_opt->multi_prog) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-n");
		srun_opt->argv[command_pos++] = xstrdup_printf("%u",
							opt_local->ntasks);
	}

	if ((_verbose < 3) || opt_local->quiet) {
		srun_opt->argc += 1;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-q");
	}

	if (opt_local->ntasks_per_socket != NO_VAL) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-S");
		srun_opt->argv[command_pos++] = xstrdup_printf(
			"%u", opt_local->ntasks_per_socket);
	}

	if (opt_local->sockets_per_node != NO_VAL) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-sn");
		srun_opt->argv[command_pos++] = xstrdup_printf(
			"%u", opt_local->sockets_per_node);
	}

	if (opt_local->mem_bind_type & MEM_BIND_LOCAL) {
		srun_opt->argc += 1;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-ss");
	}

	if (opt_local->time_limit_str) {
		srun_opt->argc += 2;
		xrealloc(srun_opt->argv, srun_opt->argc * sizeof(char *));
		srun_opt->argv[command_pos++] = xstrdup("-t");
		srun_opt->argv[command_pos++] = xstrdup_printf(
			"%d", time_str2secs(opt_local->time_limit_str));
	}

	if (srun_opt->launcher_opts) {
		char *save_ptr = NULL, *tok;
		char *tmp = xstrdup(srun_opt->launcher_opts);
		tok = strtok_r(tmp, " ", &save_ptr);
		while (tok) {
			srun_opt->argc++;
			xrealloc(srun_opt->argv,
				 srun_opt->argc * sizeof(char *));
			srun_opt->argv[command_pos++]  = xstrdup(tok);
			tok = strtok_r(NULL, " ", &save_ptr);
		}
		xfree(tmp);
	}


	/* These are srun options that are not supported by aprun, but
	   here just in case in the future they add them.

	   if (opt_local->disable_status) {
	   xstrcat(cmd_line, " --disable-status");
	   }

	   if (opt_local->epilog) {
	   xstrfmtcat(cmd_line, " --epilog=", opt_local->epilog);
	   }

	   if (kill_on_bad_exit) {
	   xstrcat(cmd_line, " --kill-on-bad-exit");
	   }

	   if (label) {
	   xstrcat(cmd_line, " --label");
	   }

	   if (opt_local->mpi_type) {
	   xstrfmtcat(cmd_line, " --mpi=", opt_local->mpi_type);
	   }

	   if (opt_local->msg_timeout) {
	   xstrfmtcat(cmd_line, " --msg-timeout=", opt_local->msg_timeout);
	   }

	   if (no_allocate) {
	   xstrcat(cmd_line, " --no-allocate");
	   }

	   if (opt_local->open_mode) {
	   xstrcat(cmd_line, " --open-mode=", opt_local->open_mode);
	   }

	   if (preserve_env) {
	   xstrcat(cmd_line, " --preserve_env");
	   }


	   if (opt_local->prolog) {
	   xstrcat(cmd_line, " --prolog=", opt_local->prolog );
	   }


	   if (opt_local->propagate) {
	   xstrcat(cmd_line, " --propagate", opt_local->propagate );
	   }

	   if (pty) {
	   xstrcat(cmd_line, " --pty");
	   }

	   if (quit_on_interrupt) {
	   xstrcat(cmd_line, " --quit-on-interrupt");
	   }


	   if (opt_local->relative) {
	   xstrfmtcat(cmd_line, " --relative=", opt_local->relative);
	   }

	   if (restart_dir) {
	   xstrfmtcat(cmd_line, " --restart-dir=", opt_local->restart_dir);
	   }


	   if (resv_port) {
	   xstrcat(cmd_line, "--resv-port");
	   }

	   if (opt_local->slurm_debug) {
	   xstrfmtcat(cmd_line, " --slurmd-debug=", opt_local->slurm_debug);
	   }

	   if (opttask_epilog) {
	   xstrfmtcat(cmd_line, " --task-epilog=", opt_local->task_epilog);
	   }

	   if (opt_local->task_prolog) {
	   xstrfmtcat(cmd_line, " --task-prolog", opt_local->task_prolog);
	   }

	   if (test_only) {
	   xstrcat(cmd_line, " --test-only");
	   }

	   if (unbuffered) {
	   xstrcat(cmd_line, " --unbuffered");
	   }

	*/

	if (srun_opt->multi_prog) {
		_handle_multi_prog(rest[0], &command_pos, opt_local);
		/* just so we don't tack on the script to the aprun line */
		command_pos = srun_opt->argc;
	}

	return command_pos;
}

extern int launch_p_handle_multi_prog_verify(int command_pos,
					     slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);

	if (srun_opt->multi_prog)
		return 1;
	return 0;
}

extern int launch_p_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job,
				    slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);

	if (srun_opt->launch_cmd) {
		int i = 0;
		char *cmd_line = NULL;

		while (srun_opt->argv[i])
			xstrfmtcat(cmd_line, "%s ", srun_opt->argv[i++]);
		printf("%s\n", cmd_line);
		xfree(cmd_line);
		exit(0);
	}

	/* You can only run 1 job per node on a cray so make the
	   request exclusive every time. */
	srun_opt->exclusive = true;
	opt_local->shared = 0;

	return launch_common_create_job_step(job, use_all_cpus,
					     signal_function,
					     destroy_job, opt_local);
}

extern int launch_p_step_launch(srun_job_t *job, slurm_step_io_fds_t *cio_fds,
				uint32_t *global_rc,
				slurm_step_launch_callbacks_t *step_callbacks,
				slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	int rc = 0;
	xassert(srun_opt);

	pthread_t msg_thread = _spawn_msg_handler(job);

	aprun_pid = fork();
	if (aprun_pid < 0) {
		error("fork: %m");
		return 1;
	} else if (aprun_pid > 0) {
		if (waitpid(aprun_pid, &rc, 0) < 0)
			error("Unable to reap aprun child process");
		*global_rc = rc;
		/* Just because waitpid returns something doesn't mean
		   this function failed so always set it back to 0.
		*/
		rc = 0;
	} else {
		setpgid(0, 0);
		_unblock_signals();
		/* dup stdio onto our open fds */
		if ((dup2(cio_fds->input.fd, 0) == -1) ||
		    (dup2(cio_fds->out.fd, 1) == -1) ||
		    (dup2(cio_fds->err.fd, 2) == -1)) {
			error("dup2: %m");
			return 1;
		}
		execvp(srun_opt->argv[0], srun_opt->argv);
		error("execv(aprun) error: %m");
		return 1;
	}

	_send_step_complete_rpc(job, *global_rc);
	if (msg_thread) {
		srun_shutdown = true;
		pthread_cancel(msg_thread);
		pthread_join(msg_thread, NULL);
	}

	return rc;
}

extern int launch_p_step_wait(srun_job_t *job, bool got_alloc,
			      slurm_opt_t *opt_local)
{
	return SLURM_SUCCESS;
}

extern int launch_p_step_terminate(void)
{
	return SLURM_SUCCESS;
}


extern void launch_p_print_status(void)
{

}

extern void launch_p_fwd_signal(int signal)
{
	if (aprun_pid)
		kill(aprun_pid, signal);
}
