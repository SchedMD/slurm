/*****************************************************************************\
 *  src/slurmd/slurmstepd/slurmstepd.c - Slurm job-step manager.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  and Christopher Morrone <morrone2@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "config.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "src/common/assoc_mgr.h"
#include "src/common/cpu_frequency.h"
#include "src/common/gres.h"
#include "src/common/node_select.h"
#include "src/common/plugstack.h"
#include "src/common/run_command.h"
#include "src/common/setproctitle.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_mpi.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/stepd_api.h"
#include "src/common/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/cgroup.h"

#include "src/slurmd/common/core_spec_plugin.h"
#include "src/slurmd/common/job_container_plugin.h"
#include "src/slurmd/common/slurmstepd_init.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/container.h"
#include "src/slurmd/slurmstepd/mgr.h"
#include "src/slurmd/slurmstepd/req.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

static int _init_from_slurmd(int sock, char **argv,
			     slurm_addr_t **_cli, uid_t *_cli_uid,
			     slurm_addr_t **_self, slurm_msg_t **_msg);

static void _dump_user_env(void);
static void _send_ok_to_slurmd(int sock);
static void _send_fail_to_slurmd(int sock);
static void _got_ack_from_slurmd(int);
static stepd_step_rec_t *_step_setup(slurm_addr_t *cli, uid_t cli_uid,
				     slurm_addr_t *self, slurm_msg_t *msg);
#ifdef MEMORY_LEAK_DEBUG
static void _step_cleanup(stepd_step_rec_t *job, slurm_msg_t *msg, int rc);
#endif
static int _process_cmdline (int argc, char **argv);

static pthread_mutex_t cleanup_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool cleanup = false;

/*
 *  List of signals to block in this process
 */
int slurmstepd_blocked_signals[] = {
	SIGINT,  SIGTERM, SIGTSTP,
	SIGQUIT, SIGPIPE, SIGUSR1,
	SIGUSR2, SIGALRM, SIGHUP, 0
};

/* global variable */
slurmd_conf_t * conf;
extern char  ** environ;

int
main (int argc, char **argv)
{
	log_options_t lopts = LOG_OPTS_INITIALIZER;
	slurm_addr_t *cli;
	uid_t cli_uid;
	slurm_addr_t *self;
	slurm_msg_t *msg;
	stepd_step_rec_t *job;
	int rc = 0;

	if (_process_cmdline (argc, argv) < 0)
		fatal ("Error in slurmstepd command line");

	slurm_conf_init(NULL);
	run_command_init();

	xsignal_block(slurmstepd_blocked_signals);
	conf = xmalloc(sizeof(*conf));
	conf->argv = &argv;
	conf->argc = &argc;
	init_setproctitle(argc, argv);

	log_init(argv[0], lopts, LOG_DAEMON, NULL);

	if (slurm_select_init(1) != SLURM_SUCCESS )
		fatal( "failed to initialize node selection plugin" );
	if (slurm_auth_init(NULL) != SLURM_SUCCESS)
		fatal( "failed to initialize authentication plugin" );

	/* Receive job parameters from the slurmd */
	_init_from_slurmd(STDIN_FILENO, argv, &cli, &cli_uid, &self, &msg);

	/* Create the stepd_step_rec_t, mostly from info in a
	 * launch_tasks_request_msg_t or a batch_job_launch_msg_t */
	if (!(job = _step_setup(cli, cli_uid, self, msg))) {
		_send_fail_to_slurmd(STDOUT_FILENO);
		rc = SLURM_ERROR;
		goto ending;
	}

	/* fork handlers cause mutexes on some global data structures
	 * to be re-initialized after the fork. */
	slurm_conf_install_fork_handlers();

	/* sets job->msg_handle and job->msgid */
	if (msg_thr_create(job) == SLURM_ERROR) {
		_send_fail_to_slurmd(STDOUT_FILENO);
		rc = SLURM_ERROR;
		goto ending;
	}

	if (job->step_id.step_id != SLURM_EXTERN_CONT)
		close_slurmd_conn();

	/* slurmstepd is the only daemon that should survive upgrade. If it
	 * had been swapped out before upgrade happened it could easily lead
	 * to SIGBUS at any time after upgrade. Avoid that by locking it
	 * in-memory. */
	if (xstrstr(slurm_conf.launch_params, "slurmstepd_memlock")) {
#ifdef _POSIX_MEMLOCK
		int flags = MCL_CURRENT;
		if (xstrstr(slurm_conf.launch_params, "slurmstepd_memlock_all"))
			flags |= MCL_FUTURE;
		if (mlockall(flags) < 0)
			info("failed to mlock() slurmstepd pages: %m");
		else
			debug("slurmstepd locked in memory");
#else
		info("mlockall() system call does not appear to be available");
#endif
	}

	acct_gather_energy_g_set_data(ENERGY_DATA_STEP_PTR, job);

	/* This does most of the stdio setup, then launches all the tasks,
	 * and blocks until the step is complete */
	rc = job_manager(job);

	return stepd_cleanup(msg, job, cli, self, rc, 0);
ending:
	return stepd_cleanup(msg, job, cli, self, rc, 1);
}

extern int stepd_cleanup(slurm_msg_t *msg, stepd_step_rec_t *job,
			 slurm_addr_t *cli, slurm_addr_t *self,
			 int rc, bool only_mem)
{
	slurm_mutex_lock(&cleanup_mutex);

	if (cleanup)
		goto done;

	if (!only_mem) {
		if (job->batch)
			batch_finish(job, rc); /* sends batch complete message */

		/* signal the message thread to shutdown, and wait for it */
		eio_signal_shutdown(job->msg_handle);
		pthread_join(job->msgid, NULL);
	}

	mpi_fini();	/* Remove stale PMI2 sockets */

	if (conf->hwloc_xml)
		(void)remove(conf->hwloc_xml);

	if (job->container)
		cleanup_container(job);

	run_command_shutdown();

	if (job->step_id.step_id == SLURM_EXTERN_CONT) {
		uint32_t jobid;
#ifdef HAVE_NATIVE_CRAY
		if (job->het_job_id && (job->het_job_id != NO_VAL))
			jobid = job->het_job_id;
		else
			jobid = job->step_id.job_id;
#else
		jobid = job->step_id.job_id;
#endif
		if (container_g_stepd_delete(jobid))
			error("container_g_stepd_delete(%u): %m", jobid);
	}

#ifdef MEMORY_LEAK_DEBUG
	acct_gather_conf_destroy();
	(void) core_spec_g_fini();
	_step_cleanup(job, msg, rc);

	fini_setproctitle();

	cgroup_conf_destroy();

	xfree(cli);
	xfree(self);
	xfree(conf->block_map);
	xfree(conf->block_map_inv);
	xfree(conf->hostname);
	xfree(conf->hwloc_xml);
	xfree(conf->logfile);
	xfree(conf->node_name);
	xfree(conf->node_topo_addr);
	xfree(conf->node_topo_pattern);
	xfree(conf->spooldir);
	xfree(conf->cpu_spec_list);
	xfree(conf);
#endif
	cleanup = true;
done:
	slurm_mutex_unlock(&cleanup_mutex);
	info("done with job");
	return rc;
}

extern void close_slurmd_conn(void)
{
	_send_ok_to_slurmd(STDOUT_FILENO);
	_got_ack_from_slurmd(STDIN_FILENO);

	/* Fancy way of closing stdin that keeps STDIN_FILENO from being
	 * allocated to any random file.  The slurmd already opened /dev/null
	 * on STDERR_FILENO for us. */
	dup2(STDERR_FILENO, STDIN_FILENO);

	/* Fancy way of closing stdout that keeps STDOUT_FILENO from being
	 * allocated to any random file.  The slurmd already opened /dev/null
	 * on STDERR_FILENO for us. */
	dup2(STDERR_FILENO, STDOUT_FILENO);
}

static slurmd_conf_t *read_slurmd_conf_lite(int fd)
{
	int rc;
	int len;
	buf_t *buffer = NULL;
	slurmd_conf_t *confl, *local_conf = NULL;
	int tmp_int = 0;
	List tmp_list = NULL;
	assoc_mgr_lock_t locks = { .tres = WRITE_LOCK };

	/*  First check to see if we've already initialized the
	 *   global slurmd_conf_t in 'conf'. Allocate memory if not.
	 */
	if (conf) {
		confl = conf;
	} else {
		local_conf = xmalloc(sizeof(slurmd_conf_t));
		confl = local_conf;
	}

	safe_read(fd, &len, sizeof(int));

	buffer = init_buf(len);
	safe_read(fd, buffer->head, len);

	rc = unpack_slurmd_conf_lite_no_alloc(confl, buffer);
	if (rc == SLURM_ERROR)
		fatal("slurmstepd: problem with unpack of slurmd_conf");

	if (slurm_unpack_list(&tmp_list,
			      slurmdb_unpack_tres_rec,
			      slurmdb_destroy_tres_rec,
			      buffer, SLURM_PROTOCOL_VERSION)
	    != SLURM_SUCCESS)
		fatal("slurmstepd: problem with unpack of tres list");

	free_buf(buffer);

	confl->log_opts.prefix_level = 1;
	confl->log_opts.logfile_level = confl->debug_level;

	if (confl->daemonize)
		confl->log_opts.stderr_level = LOG_LEVEL_QUIET;
	else
		confl->log_opts.stderr_level = confl->debug_level;

	if (confl->syslog_debug != LOG_LEVEL_END) {
		confl->log_opts.syslog_level = confl->syslog_debug;
	} else if (!confl->daemonize) {
		confl->log_opts.syslog_level = LOG_LEVEL_QUIET;
	} else if ((confl->debug_level > LOG_LEVEL_QUIET) && !confl->logfile) {
		confl->log_opts.syslog_level = confl->debug_level;
	} else
		confl->log_opts.syslog_level = LOG_LEVEL_FATAL;

	/*
	 * LOGGING BEFORE THIS WILL NOT WORK!  Only afterwards will it show
	 * up in the log.
	 */
	log_alter(confl->log_opts, SYSLOG_FACILITY_DAEMON, confl->logfile);
	log_set_timefmt(slurm_conf.log_fmt);
	debug2("debug level read from slurmd is '%s'.",
		log_num2string(confl->debug_level));

	confl->acct_freq_task = NO_VAL16;
	tmp_int = acct_gather_parse_freq(PROFILE_TASK,
					 slurm_conf.job_acct_gather_freq);
	if (tmp_int != -1)
		confl->acct_freq_task = tmp_int;

	xassert(tmp_list);

	assoc_mgr_lock(&locks);
	assoc_mgr_post_tres_list(tmp_list);
	debug2("%s: slurmd sent %u TRES.", __func__, g_tres_count);
	/* assoc_mgr_post_tres_list destroys tmp_list */
	tmp_list = NULL;
	assoc_mgr_unlock(&locks);

	return (confl);

rwfail:
	FREE_NULL_BUFFER(buffer);
	xfree(local_conf);
	return (NULL);
}

static int _get_jobid_uid_gid_from_env(uint32_t *jobid, uid_t *uid, gid_t *gid)
{
	const char *val;
	char *p;

	if (!(val = getenv("SLURM_JOBID")))
		return error("Unable to get SLURM_JOBID in env!");

	*jobid = (uint32_t) strtoul(val, &p, 10);
	if (*p != '\0')
		return error("Invalid SLURM_JOBID=%s", val);

	if (!(val = getenv("SLURM_UID")))
		return error("Unable to get SLURM_UID in env!");

	*uid = (uid_t) strtoul(val, &p, 10);
	if (*p != '\0')
		return error("Invalid SLURM_UID=%s", val);

	if (!(val = getenv("SLURM_JOB_GID")))
		return error("Unable to get SLURM_JOB_GID in env!");

	*gid = (gid_t) strtoul(val, &p, 10);
	if (*p != '\0')
		return error("Invalid SLURM_JOB_GID=%s", val);

	return SLURM_SUCCESS;
}

static int _handle_spank_mode (int argc, char **argv)
{
	char *prefix = NULL;
	const char *mode = argv[2];
	uid_t uid = (uid_t) -1;
	gid_t gid = (gid_t) -1;
	uint32_t jobid = (uint32_t) -1;
	log_options_t lopts = LOG_OPTS_INITIALIZER;

	/*
	 *  Not necessary to log to syslog
	 */
	lopts.syslog_level = LOG_LEVEL_QUIET;

	/*
	 *  Make our log prefix into spank-prolog: or spank-epilog:
	 */
	xstrfmtcat(prefix, "spank-%s", mode);
	log_init(prefix, lopts, LOG_DAEMON, NULL);
	xfree(prefix);

	slurm_conf_init(NULL);
	/*
	 *  When we are started from slurmd, a lightweight config is
	 *   sent over the stdin fd. If we are able to read this conf
	 *   use it to reinitialize the log.
	 *  It is not a fatal error if we fail to read the conf file.
	 *   This could happen if slurmstepd is run standalone for
	 *   testing.
	 */
	conf = read_slurmd_conf_lite (STDIN_FILENO);
	close (STDIN_FILENO);

	if (_get_jobid_uid_gid_from_env(&jobid, &uid, &gid))
		return error("spank environment invalid");

	debug("Running spank/%s for jobid [%u] uid [%u] gid [%u]",
	      mode, jobid, uid, gid);

	if (xstrcmp (mode, "prolog") == 0) {
		if (spank_job_prolog(jobid, uid, gid) < 0)
			return (-1);
	}
	else if (xstrcmp (mode, "epilog") == 0) {
		if (spank_job_epilog(jobid, uid, gid) < 0)
			return (-1);
	}
	else {
		error ("Invalid mode %s specified!", mode);
		return (-1);
	}
	return (0);
}

/*
 *  Process special "modes" of slurmstepd passed as cmdline arguments.
 */
static int _process_cmdline (int argc, char **argv)
{
	if ((argc == 2) && (xstrcmp(argv[1], "getenv") == 0)) {
		print_rlimits();
		_dump_user_env();
		exit(0);
	}
	if ((argc == 3) && (xstrcmp(argv[1], "spank") == 0)) {
		if (_handle_spank_mode(argc, argv) < 0)
			exit (1);
		exit (0);
	}
	return (0);
}


static void
_send_ok_to_slurmd(int sock)
{
	/* If running under valgrind/memcheck, this pipe doesn't work correctly
	 * so just skip it. */
#if (SLURMSTEPD_MEMCHECK == 0)
	int ok = SLURM_SUCCESS;
	safe_write(sock, &ok, sizeof(int));
	return;
rwfail:
	error("Unable to send \"ok\" to slurmd");
#endif
}

static void
_send_fail_to_slurmd(int sock)
{
	/* If running under valgrind/memcheck, this pipe doesn't work correctly
	 * so just skip it. */
#if (SLURMSTEPD_MEMCHECK == 0)
	int fail = SLURM_ERROR;

	if (errno)
		fail = errno;

	safe_write(sock, &fail, sizeof(int));
	return;
rwfail:
	error("Unable to send \"fail\" to slurmd");
#endif
}

static void
_got_ack_from_slurmd(int sock)
{
	/* If running under valgrind/memcheck, this pipe doesn't work correctly
	 * so just skip it. */
#if (SLURMSTEPD_MEMCHECK == 0)
	int ok;
	safe_read(sock, &ok, sizeof(int));
	return;
rwfail:
	error("Unable to receive \"ok ack\" to slurmd");
#endif
}

static void _set_job_log_prefix(slurm_step_id_t *step_id)
{
	char *buf;
	char tmp_char[64];

	log_build_step_id_str(step_id, tmp_char, sizeof(tmp_char),
			      STEP_ID_FLAG_NO_PREFIX);
	buf = xstrdup_printf("[%s]", tmp_char);

	setproctitle("%s", buf);
	/* note: will claim ownership of buf, do not free */
	xstrcat(buf, " ");
	log_set_fpfx(&buf);
}

/*
 *  This function handles the initialization information from slurmd
 *  sent by _send_slurmstepd_init() in src/slurmd/slurmd/req.c.
 */
static int
_init_from_slurmd(int sock, char **argv,
		  slurm_addr_t **_cli, uid_t *_cli_uid, slurm_addr_t **_self,
		  slurm_msg_t **_msg)
{
	char *incoming_buffer = NULL;
	buf_t *buffer;
	int step_type;
	int len;
	uint16_t proto;
	slurm_addr_t *cli = NULL;
	uid_t cli_uid;
	slurm_addr_t *self = NULL;
	slurm_msg_t *msg = NULL;
	slurm_step_id_t step_id = {
		.job_id = 0,
		.step_id = NO_VAL,
		.step_het_comp = NO_VAL,
	};

	/* receive conf from slurmd */
	if (!(conf = read_slurmd_conf_lite(sock)))
		fatal("Failed to read conf from slurmd");

	/* receive cgroup conf from slurmd */
	if (cgroup_read_conf(sock) != SLURM_SUCCESS)
		fatal("Failed to read cgroup conf from slurmd");

	slurm_conf.slurmd_port = conf->port;
	/* receive acct_gather conf from slurmd */
	if (acct_gather_read_conf(sock) != SLURM_SUCCESS)
		fatal("Failed to read acct_gather conf from slurmd");

	/* receive job type from slurmd */
	safe_read(sock, &step_type, sizeof(int));
	debug3("step_type = %d", step_type);

	/* receive reverse-tree info from slurmd */
	slurm_mutex_lock(&step_complete.lock);
	safe_read(sock, &step_complete.rank, sizeof(int));
	safe_read(sock, &step_complete.parent_rank, sizeof(int));
	safe_read(sock, &step_complete.children, sizeof(int));
	safe_read(sock, &step_complete.depth, sizeof(int));
	safe_read(sock, &step_complete.max_depth, sizeof(int));
	safe_read(sock, &step_complete.parent_addr, sizeof(slurm_addr_t));
	if (step_complete.children)
		step_complete.bits = bit_alloc(step_complete.children);
	step_complete.jobacct = jobacctinfo_create(NULL);
	slurm_mutex_unlock(&step_complete.lock);

	debug3("slurmstepd rank %d, parent = %pA",
	       step_complete.rank, &step_complete.parent_addr);

	/* receive cli from slurmd */
	safe_read(sock, &len, sizeof(int));
	incoming_buffer = xmalloc(len);
	safe_read(sock, incoming_buffer, len);
	buffer = create_buf(incoming_buffer,len);
	cli = xmalloc(sizeof(slurm_addr_t));
	if (slurm_unpack_addr_no_alloc(cli, buffer) == SLURM_ERROR)
		fatal("slurmstepd: problem with unpack of slurmd_conf");
	free_buf(buffer);
	safe_read(sock, &cli_uid, sizeof(uid_t));

	/* receive self from slurmd */
	safe_read(sock, &len, sizeof(int));
	if (len > 0) {
		/* receive packed self from main slurmd */
		incoming_buffer = xmalloc(len);
		safe_read(sock, incoming_buffer, len);
		buffer = create_buf(incoming_buffer,len);
		self = xmalloc(sizeof(slurm_addr_t));
		if (slurm_unpack_addr_no_alloc(self, buffer)
		    == SLURM_ERROR) {
			fatal("slurmstepd: problem with unpack of "
			      "slurmd_conf");
		}
		free_buf(buffer);
	}

	/* Grab the slurmd's spooldir. Has %n expanded. */
	cpu_freq_init(conf);

	/* Receive cpu_frequency info from slurmd */
	cpu_freq_recv_info(sock);

	/* get the protocol version of the srun */
	safe_read(sock, &proto, sizeof(uint16_t));

	/* receive req from slurmd */
	safe_read(sock, &len, sizeof(int));
	incoming_buffer = xmalloc(len);
	safe_read(sock, incoming_buffer, len);
	buffer = create_buf(incoming_buffer,len);

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);
	/* Always unpack as the current version. */
	msg->protocol_version = SLURM_PROTOCOL_VERSION;

	switch (step_type) {
	case LAUNCH_BATCH_JOB:
		msg->msg_type = REQUEST_BATCH_JOB_LAUNCH;
		break;
	case LAUNCH_TASKS:
		msg->msg_type = REQUEST_LAUNCH_TASKS;
		break;
	default:
		fatal("%s: Unrecognized launch RPC (%d)", __func__, step_type);
		break;
	}
	if (unpack_msg(msg, buffer) == SLURM_ERROR)
		fatal("slurmstepd: we didn't unpack the request correctly");
	free_buf(buffer);

	switch (step_type) {
	case LAUNCH_BATCH_JOB:
		step_id.job_id = ((batch_job_launch_msg_t *)msg->data)->job_id;
		step_id.step_id = SLURM_BATCH_SCRIPT;
		step_id.step_het_comp = NO_VAL;
		break;
	case LAUNCH_TASKS:
		memcpy(&step_id,
		       &((launch_tasks_request_msg_t *)msg->data)->step_id,
		       sizeof(step_id));
		break;
	default:
		fatal("%s: Unrecognized launch RPC (%d)", __func__, step_type);
		break;
	}

	/* Receive GRES information from slurmd */
	gres_g_recv_stepd(sock, msg);

	/*
	 * Read slurmd node name - may be dictated by slurmctld due to
	 * Dynamic Future node mapping.
	 */
	safe_read(sock, &len, sizeof(int));
	conf->node_name = xmalloc(len);
	safe_read(sock, conf->node_name, len);

	_set_job_log_prefix(&step_id);

	if (!conf->hwloc_xml) {
		conf->hwloc_xml = xstrdup_printf("%s/hwloc_topo_%u.%u",
						 conf->spooldir,
						 step_id.job_id,
						 step_id.step_id);
		if (step_id.step_het_comp != NO_VAL)
			xstrfmtcat(conf->hwloc_xml, ".%u",
				   step_id.step_het_comp);
		xstrcat(conf->hwloc_xml, ".xml");
	}
	/*
	 * Swap the field to the srun client version, which will eventually
	 * end up stored as protocol_version in srun_info_t. It's a hack to
	 * pass it in-band, while still using the correct version to unpack
	 * the launch request message above.
	 */
	msg->protocol_version = proto;

	*_cli = cli;
	*_cli_uid = cli_uid;
	*_self = self;
	*_msg = msg;

	return 1;

rwfail:
	fatal("Error reading initialization data from slurmd");
	exit(1);
}

static stepd_step_rec_t *
_step_setup(slurm_addr_t *cli, uid_t cli_uid, slurm_addr_t *self,
	    slurm_msg_t *msg)
{
	stepd_step_rec_t *job = NULL;

	switch (msg->msg_type) {
	case REQUEST_BATCH_JOB_LAUNCH:
		debug2("setup for a batch_job");
		job = mgr_launch_batch_job_setup(msg->data, cli);
		break;
	case REQUEST_LAUNCH_TASKS:
		debug2("setup for a launch_task");
		job = mgr_launch_tasks_setup(msg->data, cli, cli_uid, self,
					     msg->protocol_version);
		break;
	default:
		fatal("handle_launch_message: Unrecognized launch RPC");
		break;
	}

	if (!job) {
		error("_step_setup: no job returned");
		return NULL;
	}

	if (job->container)
		setup_container(job);

	job->jmgr_pid = getpid();
	job->jobacct = jobacctinfo_create(NULL);

	/* Establish GRES environment variables */
	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES) {
		gres_job_state_log(job->job_gres_list,
				   job->step_id.job_id);
		gres_step_state_log(job->step_gres_list,
				    job->step_id.job_id,
				    job->step_id.step_id);
	}
	if (job->batch || (job->step_id.step_id == SLURM_INTERACTIVE_STEP)) {
		gres_g_job_set_env(&job->env, job->job_gres_list, 0);
	} else if (msg->msg_type == REQUEST_LAUNCH_TASKS) {
		gres_g_step_set_env(&job->env, job->step_gres_list);
	}

	/*
	 * Add slurmd node topology informations to job env array
	 */
	env_array_overwrite(&job->env,"SLURM_TOPOLOGY_ADDR",
			    conf->node_topo_addr);
	env_array_overwrite(&job->env,"SLURM_TOPOLOGY_ADDR_PATTERN",
			    conf->node_topo_pattern);

	set_msg_node_id(job);

	return job;
}

#ifdef MEMORY_LEAK_DEBUG
static void
_step_cleanup(stepd_step_rec_t *job, slurm_msg_t *msg, int rc)
{
	if (job) {
		jobacctinfo_destroy(job->jobacct);
		if (!job->batch)
			stepd_step_rec_destroy(job);
	}

	if (msg) {
		/*
		 * The message cannot be freed until the jobstep is complete
		 * because the job struct has pointers into the msg, such
		 * as the switch jobinfo pointer.
		 */
		switch(msg->msg_type) {
		case REQUEST_BATCH_JOB_LAUNCH:
			slurm_free_job_launch_msg(msg->data);
			break;
		case REQUEST_LAUNCH_TASKS:
			slurm_free_launch_tasks_request_msg(msg->data);
			break;
		default:
			fatal("handle_launch_message: Unrecognized launch RPC");
			break;
		}
		xfree(msg);
	}
	jobacctinfo_destroy(step_complete.jobacct);
}
#endif

static void _dump_user_env(void)
{
	int i;

	for (i=0; environ[i]; i++)
		printf("%s\n",environ[i]);
}
