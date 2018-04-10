/*****************************************************************************\
 *  launch_poe.c - Define job launch using IBM's poe.
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
 *****************************************************************************
 * POE DEBUGING NOTES:
 *
 * MP_INFOLEVEL=4	Verbose POE logging
 * MP_PMDLOG=yes	Write log files to /tmp/mplog.*
 * SCI_DEBUG_FANOUT=#   Fanout of pmdv12 in launching tasks
\*****************************************************************************/

#include <ctype.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "src/common/slurm_xlator.h"
#include "src/srun/libsrun/launch.h"
#include "src/common/env.h"
#include "src/common/parse_time.h"

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
const char plugin_name[]        = "launch poe plugin";
const char plugin_type[]        = "launch/poe";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static char *cmd_fname = NULL;
static char *poe_cmd_line = NULL;
static pid_t poe_pid = 0;

static void _build_work_dir(char *dname, int dname_size)
{
	char *work_dir;

	/* NOTE: Directory must be shared between nodes for cmd_file to work */
	if (!(work_dir = getenv("HOME"))) {
		work_dir = xmalloc(512);
		if (!getcwd(work_dir, 512))
			fatal("getcwd(): %m");
		snprintf(dname, dname_size, "%s/.slurm", work_dir);
		xfree(work_dir);
	} else {
		snprintf(dname, dname_size, "%s/.slurm", work_dir);
	}
	mkdir(dname, 0700);
}

/* Given a program name, return its communication protocol */
static char *_get_cmd_protocol(char *cmd)
{
	int stdout_pipe[2] = {-1, -1}, stderr_pipe[2] = {-1, -1};
	int read_size, buf_rem = 16 * 1024, offset = 0, status;
	pid_t pid;
	char *buf, *protocol = "mpi";

	if ((pipe(stdout_pipe) == -1) || (pipe(stderr_pipe) == -1)) {
		error("pipe: %m");
		return "mpi";
	}

	pid = fork();
	if (pid < 0) {
		error("fork: %m");
		return "mpi";
	} else if (pid == 0) {
		if ((dup2(stdout_pipe[1], 1) == -1) ||
		    (dup2(stderr_pipe[1], 2) == -1)) {
			error("dup2: %m");
			return NULL;
		}
		(void) close(0);	/* stdin */
		(void) close(stdout_pipe[0]);
		(void) close(stdout_pipe[1]);
		(void) close(stderr_pipe[0]);
		(void) close(stderr_pipe[1]);

		execlp("/usr/bin/ldd", "ldd", cmd, NULL);
		error("execv(ldd) error: %m");
		return NULL;
	}

	(void) close(stdout_pipe[1]);
	(void) close(stderr_pipe[1]);
	buf = xmalloc(buf_rem);
	while ((read_size = read(stdout_pipe[0], &buf[offset], buf_rem))) {
		if (read_size > 0) {
			buf_rem -= read_size;
			offset  += read_size;
			if (buf_rem == 0)
				break;
		} else if ((errno != EAGAIN) || (errno != EINTR)) {
			error("read(pipe): %m");
			break;
		}
	}

	if (strstr(buf, "libmpi"))
		protocol = "mpi";
	else if (strstr(buf, "libshmem.so"))
		protocol = "shmem";
	else if (strstr(buf, "libxlpgas.so"))
		protocol = "pgas";
	else if (strstr(buf, "libpami.so"))
		protocol = "pami";
	else if (strstr(buf, "liblapi.so"))
		protocol = "lapi";
	xfree(buf);
	while ((waitpid(pid, &status, 0) == -1) && (errno == EINTR))
		;
	(void) close(stdout_pipe[0]);
	(void) close(stderr_pipe[0]);

	return protocol;
}

static void _setenv(const char *name, const char *value, int overwrite,
		    int pack_offset)
{
	char *key = NULL;

	if (pack_offset == NO_VAL) {
		setenv(name, value, overwrite);
	} else {
		xstrfmtcat(key, "%s_PACK_GROUP_%d", name, pack_offset);
		setenv(key, value, overwrite);
		xfree(key);
	}
}

/* Propagate srun options for use by POE by setting environment
 * variables, which are subsequently processed the libsrun/opt.c logic
 * as called from launch/slurm (by POE). */
static void _propagate_srun_opts(uint32_t nnodes, uint32_t ntasks,
				 slurm_opt_t *opt_local, int pack_offset)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	char value[32];
	xassert(srun_opt);

	if (opt_local->account)
		_setenv("SLURM_ACCOUNT", opt_local->account, 1, pack_offset);
	if (opt_local->acctg_freq) {
		snprintf(value, sizeof(value), "%s", opt_local->acctg_freq);
		_setenv("SLURM_ACCTG_FREQ", value, 1, pack_offset);
	}
	if (srun_opt->ckpt_dir)
		_setenv("SLURM_CHECKPOINT_DIR", srun_opt->ckpt_dir, 1,
			pack_offset);
	if (srun_opt->ckpt_interval) {
		snprintf(value, sizeof(value), "%d", srun_opt->ckpt_interval);
		_setenv("SLURM_CHECKPOINT", value, 1, pack_offset);
	}
	if (opt_local->cpus_per_task) {
		snprintf(value, sizeof(value), "%d", opt_local->cpus_per_task);
		_setenv("SLURM_CPUS_PER_TASK", value, 1, pack_offset);
	}
	if (opt_local->dependency)
		_setenv("SLURM_DEPENDENCY", opt_local->dependency, 1,
			pack_offset);
	if (opt_local->distribution != SLURM_DIST_UNKNOWN) {
		snprintf(value, sizeof(value), "%u", opt_local->distribution);
		_setenv("SLURM_DISTRIBUTION", value, 1, pack_offset);
	}
	if (opt_local->exc_nodes)
		_setenv("SRUN_EXC_NODES", opt_local->exc_nodes, 1, pack_offset);
	if (srun_opt->exclusive)
		_setenv("SLURM_EXCLUSIVE", "1", 1, pack_offset);
	if (opt_local->gres)
		_setenv("SLURM_GRES", opt_local->gres, 1, pack_offset);
	if (opt_local->immediate)
		_setenv("SLURM_IMMEDIATE", "1", 1, pack_offset);
	if (opt_local->job_name)
		_setenv("SLURM_JOB_NAME", opt_local->job_name, 1, pack_offset);
	if (opt_local->mem_per_cpu > 0) {
		snprintf(value, sizeof(value), "%"PRIu64,
			 opt_local->mem_per_cpu);
		_setenv("SLURM_MEM_PER_CPU", value, 1, pack_offset);
	}
	if (opt_local->pn_min_memory > 0) {
		snprintf(value, sizeof(value), "%"PRIu64,
			 opt_local->pn_min_memory);
		_setenv("SLURM_MEM_PER_NODE", value, 1, pack_offset);
	}
	if (opt_local->network)
		_setenv("SLURM_NETWORK", opt_local->network, 1, pack_offset);
	if (nnodes) {
		snprintf(value, sizeof(value), "%u", nnodes);
		_setenv("SLURM_JOB_NUM_NODES", value, 1, pack_offset);
		if (!srun_opt->preserve_env)
			_setenv("SLURM_NNODES", value, 1, pack_offset);
	}
	if (srun_opt->alloc_nodelist) {
		_setenv("SLURM_JOB_NODELIST", srun_opt->alloc_nodelist, 1,
			pack_offset);
		if (!srun_opt->preserve_env)
			_setenv("SLURM_NODELIST", srun_opt->alloc_nodelist, 1,
				pack_offset);
	}
	if (!srun_opt->preserve_env && ntasks) {
		snprintf(value, sizeof(value), "%u", ntasks);
		_setenv("SLURM_NTASKS", value, 1, pack_offset);
	}
	if (opt_local->overcommit)
		_setenv("SLURM_OVERCOMMIT", "1", 1, pack_offset);
	if (opt_local->nodelist)
		_setenv("SRUN_WITH_NODES", opt_local->nodelist, 1, pack_offset);
	if (opt_local->partition)
		_setenv("SLURM_PARTITION", opt_local->partition, 1,
			pack_offset);
	if (opt_local->qos)
		_setenv("SLURM_QOS", opt_local->qos, 1, pack_offset);
	if (srun_opt->relative_set) {
		snprintf(value, sizeof(value), "%u", srun_opt->relative);
		_setenv("SRUN_RELATIVE", value, 1, pack_offset);
	}
	if (srun_opt->resv_port_cnt >= 0) {
		snprintf(value, sizeof(value), "%d", srun_opt->resv_port_cnt);
		_setenv("SLURM_RESV_PORTS", value, 1, pack_offset);
	}
	if (opt_local->time_limit_str)
		_setenv("SLURM_TIMELIMIT", opt_local->time_limit_str, 1,
			pack_offset);
	if (opt_local->wckey)
		_setenv("SLURM_WCKEY", opt_local->wckey, 1, pack_offset);
	if (opt_local->cwd)
		_setenv("SLURM_WORKING_DIR", opt_local->cwd, 1, pack_offset);
	if (srun_opt->preserve_env) {
		snprintf(value, sizeof(value), "%d", srun_opt->preserve_env);
		_setenv("SLURM_PRESERVE_ENV", value, 1, pack_offset);
	}
}

static void _unblock_signals (void)
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
	xassert(srun_opt);

	if (srun_opt->test_only) {
		error("--test-only not supported with poe");
		exit (1);
	} else if (srun_opt->no_alloc) {
		error("--no-allocate not supported with poe");
		exit (1);
	}
	if (srun_opt->slurmd_debug != LOG_LEVEL_QUIET) {
		error("--slurmd-debug not supported with poe");
		srun_opt->slurmd_debug = LOG_LEVEL_QUIET;
	}

	srun_opt->argc++;

	/* We need to do +2 here just in case multi-prog is needed (we
	 * add an extra argv on so just make space for it).
	 */
	srun_opt->argv = xmalloc((srun_opt->argc + 2) * sizeof(char *));

	srun_opt->argv[0] = xstrdup("poe");
	/* Set default job name to the executable name rather than
	 * "poe" */
	if (!srun_opt->job_name_set_cmd && (1 < srun_opt->argc)) {
		srun_opt->job_name_set_cmd = true;
		opt_local->job_name = xstrdup(rest[0]);
	}

	return 1;
}

extern int launch_p_handle_multi_prog_verify(int command_pos,
					     slurm_opt_t *opt_local)
{
	return 0;
}


extern int launch_p_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job,
				    slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	char dname[512], value[32];
	char *protocol = "mpi";
	uint32_t ntasks = opt_local->ntasks;
	uint32_t nnodes = opt_local->min_nodes;
	xassert(srun_opt);

	if (srun_opt->launch_cmd) {
		int i;

		xstrfmtcat(poe_cmd_line, "%s", srun_opt->argv[0]);
		for (i = 1; i < srun_opt->argc; i++)
			xstrfmtcat(poe_cmd_line, " %s", srun_opt->argv[i]);
	}

	if (job) {
		/* poe can't accept ranges so give the actual number
		   here so it doesn't get confused if srun gives the
		   max instead of the min.
		*/
		ntasks = job->ntasks;
		nnodes = job->nhosts;
	}

	/*
	 * In order to support MPMD or job steps smaller than the LoadLeveler
	 * job allocation size, specify a command file using the poe option
	 * -cmdfile or MP_CMDFILE env var. See page 43 here:
	 * http://publib.boulder.ibm.com/epubs/pdf/c2367811.pdf
	 * The command file should contain one more more lines of the following
	 * form:
	 * <cmd>@<step_id>%<total_tasks>%<protocol>:<num_tasks> <args>
	 * IBM is working to eliminate the need to specify protocol, but until
	 * then it might be determined as follows:
	 *
	 * We are currently looking at 'ldd <program>' and checking the name of
	 * the MPI and PAMI libraries and on x86, also checking to see if Intel
	 * MPI library is used.
	 * This is done at runtime in PMD and depending on '-mpilib' option and
	 * '-config' option used, change the LD_LIBRARY_PATH to properly
	 * support the different PE Runtime levels the customer have installed
	 * on their cluster.
	 *
	 * There is precedence order that would be important if multiple
	 * libraries are listed in the 'ldd output' as long as you know it is
	 * not a mixed protocol (i.e. Openshmem + MPI, UPC + MPI, etc)
	 * application.
	 * 1) If MPI library is found (libmpi*.so) -> use 'mpi'
	 * 2) if Openshmem library is found (libshmem.so) -> use 'shmem'
	 * 3) if UPC runtime library is found (libxlpgas.so) -> use 'pgas'
	 * 4) if only PAMI library is found (libpami.so) -> use 'pami'
	 * 5) if only LAPI library is found (liblapi.so) -> use 'lapi'
	 */
	if (srun_opt->multi_prog) {
		protocol = "multi";
	} else {
		protocol = _get_cmd_protocol(srun_opt->argv[1]);
	}
	debug("cmd:%s protocol:%s", srun_opt->argv[1], protocol);

	if (srun_opt->multi_prog) {
		int fd, k;

		if (srun_opt->launch_cmd) {
			error("--launch_cmd not available "
			      "when using a cmdfile");
			return SLURM_ERROR;
		}
		xassert(job);
		/* NOTE: The command file needs to be in a directory that can
		 * be read from the compute node(s), so /tmp does not work.
		 * We use the user's home directory (based upon "HOME"
		 * environment variable) otherwise use current working
		 * directory. The file is only created here, it is written
		 * in launch_poe.c. */
		_build_work_dir(dname, sizeof(dname));
		xstrfmtcat(cmd_fname, "%s/slurm_cmdfile.%u",
			   dname, (uint32_t) getpid());
		while ((fd = creat(cmd_fname, 0600)) < 0) {
			if (errno == EINTR)
				continue;
			fatal("creat(%s): %m", cmd_fname);
		}
		(void) close(fd);

		/* Set command file name via MP_CMDFILE and remove it from
		 * the execute line. */
		setenv("MP_NEWJOB", "parallel", 1);
		setenv("MP_CMDFILE", cmd_fname, 1);
		setenv("SLURM_CMDFILE", srun_opt->argv[1], 1);
		if (srun_opt->argc) {
			xfree(srun_opt->argv[1]);
			for (k = 1; k < srun_opt->argc; k++)
				srun_opt->argv[k] = srun_opt->argv[k + 1];
			srun_opt->argc--;
		}
	}

	if (opt_local->shared != NO_VAL16) {
		char *shared_cpu_use = "multiple";

		if (opt_local->shared)
			shared_cpu_use = "unique";

		setenv("MP_CPU_USE", shared_cpu_use, 1);

		if (srun_opt->launch_cmd)
			xstrfmtcat(poe_cmd_line, " -cpu_use %s",
				   shared_cpu_use);
	}
	if (opt_local->network) {
		bool cau_set = false;
		bool dev_type_set = false;
		bool protocol_set = false;
		char *type_ptr = NULL;
		char *save_ptr = NULL, *token;
		char *network_str = xstrdup(opt_local->network);
		char *adapter_use = NULL;

		if (strstr(opt_local->network, "dedicated"))
			adapter_use = "dedicated";
		else if (strstr(opt_local->network, "shared"))
			adapter_use = "shared";

		if (adapter_use) {
			setenv("MP_ADAPTER_USE", adapter_use, 1);
			if (srun_opt->launch_cmd)
				xstrfmtcat(poe_cmd_line, " -adapter_use %s",
					   adapter_use);
		}

		token = strtok_r(network_str, ",", &save_ptr);
		while (token) {
			/* bulk_xfer options */
			if (!xstrncasecmp(token, "bulk_xfer", 9)) {
				setenv("MP_USE_BULK_XFER", "yes", 1);
				if (srun_opt->launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -use_bulk_xfer yes");
			/* device name options */
			} else if (!xstrncasecmp(token, "devname=", 8)) {
				/* Ignored by POE */

			/* device type options */
			} else if (!xstrncasecmp(token, "devtype=", 8)) {
				type_ptr = token + 8;
				if (!xstrcasecmp(type_ptr, "ib")) {
					setenv("MP_DEVTYPE", type_ptr, 1);
					if (srun_opt->launch_cmd)
						xstrfmtcat(poe_cmd_line,
							   " -devtype %s",
							   type_ptr);
				} else if (!xstrcasecmp(type_ptr, "hfi")) {
					setenv("MP_DEVTYPE", type_ptr, 1);
					if (srun_opt->launch_cmd)
						xstrfmtcat(poe_cmd_line,
							   " -devtype %s",
							   type_ptr);
				}
				dev_type_set = true;
				/* POE ignores other options */

			/* instances options */
			} else if (!xstrncasecmp(token, "instances=", 10)) {
				type_ptr = token + 10;
				setenv("MP_INSTANCES", type_ptr, 1);
				if (srun_opt->launch_cmd) {
					xstrfmtcat(poe_cmd_line,
						   " -instances %s",
						   type_ptr);
				}

			/* network options */
			} else if (!xstrcasecmp(token, "ip")   ||
				  !xstrcasecmp(token, "ipv4")  ||
				  !xstrcasecmp(token, "ipv6")) {
				setenv("MP_EUILIB", "ip", 1);
				if (srun_opt->launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -euilib ip");
			} else if (!xstrcasecmp(token, "us")) {
				setenv("MP_EUILIB", "us", 1);
				if (srun_opt->launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -euilib us");
			/* protocol options
			 *
			 *  NOTE: Slurm supports the values listed below, and
			 *        does not support poe user_defined_parallelAPI
			 *        values. If you add to this list please add to
			 *        the list in
			 *        src/plugins/switch/net/switch_nrt.c
			 */
			} else if ((!xstrncasecmp(token, "lapi", 4)) ||
				   (!xstrncasecmp(token, "mpi",  3)) ||
				   (!xstrncasecmp(token, "pami", 4)) ||
				   (!xstrncasecmp(token, "pgas", 4)) ||
				   (!xstrncasecmp(token, "shmem",5)) ||
				   (!xstrncasecmp(token, "test", 4)) ||
				   (!xstrncasecmp(token, "upc",  3))) {
				if (!protocol_set) {
					protocol_set = true;
					protocol = NULL;
				}
				if (protocol)
					xstrcat(protocol, ",");
				xstrcat(protocol, token);
				setenv("MP_MSG_API", protocol, 1);
			/* adapter options */
			} else if (!xstrcasecmp(token, "sn_all")) {
				setenv("MP_EUIDEVICE", "sn_all", 1);
				if (srun_opt->launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -euidevice sn_all");
			} else if (!xstrcasecmp(token, "sn_single")) {
				setenv("MP_EUIDEVICE", "sn_single", 1);
				if (srun_opt->launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -euidevice sn_single");
			/* Collective Acceleration Units (CAU) */
			} else if (!xstrncasecmp(token, "cau=", 4)) {
				setenv("MP_COLLECTIVE_GROUPS", token + 4, 1);
				if (srun_opt->launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -collective_groups %s",
						   token + 4);
				if (atoi(token + 4))
					cau_set = true;
			/* Immediate Send Slots Per Window */
			} else if (!xstrncasecmp(token, "immed=", 6)) {
				setenv("MP_IMM_SEND_BUFFERS", token + 6, 1);
				if (srun_opt->launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -imm_send_buffers %s",
						   token + 6);
			/* other */
			} else {
				info("switch/nrt: invalid option: %s", token);
			}
			token = strtok_r(NULL, ",", &save_ptr);
		}
		if (cau_set && !dev_type_set) {
			/* If POE is executed directly (not spawned by srun)
			 * it will generate an error if -collective_groups is
			 * non-zero and devtype is not set. Since we do not
			 * know what devices are available at this point, set
			 * the default type to hfi in hopes of avoiding an
			 * error. User can always specify a devtype in the
			 * --network option to avoid possible invalid value */
			setenv("MP_DEVTYPE", type_ptr, 1);
			if (srun_opt->launch_cmd)
				xstrcat(poe_cmd_line, " -devtype hfi");
		}

		if (srun_opt->launch_cmd)
			xstrfmtcat(poe_cmd_line, " -msg_api %s", protocol);
		if (protocol_set)
			xfree(protocol);
		else
			setenv("MP_MSG_API", protocol, 0);
	} else {
		if (xstrcmp(protocol, "multi")) {
			setenv("MP_MSG_API", protocol, 0);
			if (srun_opt->launch_cmd)
				xstrfmtcat(poe_cmd_line,
					   " -msg_api %s", protocol);
		}
	}

	if (opt_local->nodelist &&
	    ((opt_local->distribution & SLURM_DIST_STATE_BASE) ==
	     SLURM_DIST_ARBITRARY)) {
		bool destroy_hostfile = 0;
		if (!srun_opt->hostfile) {
			char *host_name, *host_line;
			pid_t pid = getpid();
			hostlist_t hl;
			int fd, len, offset, wrote;

			destroy_hostfile = 1;

			hl = hostlist_create(opt_local->nodelist);
			if (!hl)
				fatal("Invalid nodelist: %s",
				      opt_local->nodelist);
			xstrfmtcat(srun_opt->hostfile, "slurm_hostlist.%u",
				   (uint32_t) pid);
			if ((fd = creat(srun_opt->hostfile, 0600)) < 0)
				fatal("creat(%s): %m", srun_opt->hostfile);
			host_line = NULL;
			while ((host_name = hostlist_shift(hl))) {
				if (host_line)
					xstrcat(host_line, "\n");
				xstrcat(host_line, host_name);
				free(host_name);
			}
			hostlist_destroy(hl);
			len = strlen(host_line) + 1;
			offset = 0;
			while (len > offset) {
				wrote = write(fd, host_line + offset,
					      len - offset);
				if (wrote < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					fatal("write(%s): %m",
					      srun_opt->hostfile);
				}
				offset += wrote;
			}
			xfree(host_line);
			close(fd);
		}
		debug2("using hostfile %s", srun_opt->hostfile);
		setenv("MP_HOSTFILE", srun_opt->hostfile, 1);
		if (srun_opt->launch_cmd) {
			xstrfmtcat(poe_cmd_line, " -hfile %s",
				   srun_opt->hostfile);
			if (destroy_hostfile)
				info("WARNING: hostlist file %s was created.  "
				     "User is responsible to remove it when "
				     "done.", srun_opt->hostfile);
		} else if (destroy_hostfile)
			setenv("SRUN_DESTROY_HOSTFILE", srun_opt->hostfile, 1);

		/* RESD has to be set to yes or for some reason poe
		   thinks things are already set up and then we are
		   screwed.
		*/
		setenv("MP_RESD", "yes", 1);
		if (srun_opt->launch_cmd)
			xstrcat(poe_cmd_line, " -resd yes");
		/* FIXME: This next line is here just for debug
		 * purpose.  It makes it so each task has a separate
		 * line. */
		setenv("MP_STDOUTMODE", "unordered", 1);
		/* Just in case we didn't specify a file in srun. */
		setenv("SLURM_ARBITRARY_NODELIST", opt_local->nodelist, 1);
	} else {
		/* Since poe doesn't need to know about the partition and it
		   really needs to have RMPOOL set just set it to something.
		   This only needs to happen if we don't specify the
		   hostlist like above.
		*/
		setenv("MP_RMPOOL", "SLURM", 1);
		if (srun_opt->launch_cmd)
			xstrfmtcat(poe_cmd_line, " -rmpool slurm");
	}

	if (srun_opt->msg_timeout) {
		snprintf(value, sizeof(value), "%d", srun_opt->msg_timeout);
		setenv("MP_TIMEOUT", value, 1);
		/* There is no equivelent cmd line option */
	}
	if (opt_local->immediate) {
		setenv("MP_RETRY", "0", 1);
		if (srun_opt->launch_cmd)
			xstrfmtcat(poe_cmd_line, " -retry 0");
	}
	if (srun_opt->labelio) {
		setenv("MP_LABELIO", "yes", 0);
		if (srun_opt->launch_cmd)
			xstrfmtcat(poe_cmd_line, " -labelio yes");
	}
	if (nnodes) {
		snprintf(value, sizeof(value), "%u", nnodes);
		setenv("MP_NODES", value, 1);
		if (srun_opt->launch_cmd)
			xstrfmtcat(poe_cmd_line, " -nodes %s", value);
	}
	if (ntasks) {
		snprintf(value, sizeof(value), "%u", ntasks);
		setenv("MP_PROCS", value, 1);
		if (srun_opt->launch_cmd)
			xstrfmtcat(poe_cmd_line, " -procs %s", value);
	}
	if (srun_opt->cpu_bind_type) {
		/* POE supports a limited subset of CPU binding options */
		srun_opt->cpu_bind_type &= (CPU_BIND_TO_THREADS |
				      CPU_BIND_TO_CORES   |
				      CPU_BIND_RANK);
	}
	if (srun_opt->cpu_bind_type) {
		char *units;
		int count = 1;

		if (srun_opt->cpu_bind_type & CPU_BIND_TO_CORES)
			units = "core";
		else
			units = "cpu";

		if (opt_local->cpus_per_task)
			count = MAX(opt_local->cpus_per_task, 1);
		snprintf(value, sizeof(value), "%s:%d", units, count);
		setenv("MP_TASK_AFFINITY", value, 1);
		setenv("MP_BINDPROC", "yes", 1);
		if (srun_opt->launch_cmd) {
			xstrfmtcat(poe_cmd_line, " -task_affinity %s", value);
			xstrfmtcat(poe_cmd_line, " -bindproc yes");
		}
	}
	if (opt_local->ntasks_per_node != NO_VAL) {
		snprintf(value, sizeof(value), "%u", opt_local->ntasks_per_node);
		setenv("MP_TASKS_PER_NODE", value, 1);
		if (srun_opt->launch_cmd)
			xstrfmtcat(poe_cmd_line, " -tasks_per_node %s", value);
	}
	if (srun_opt->unbuffered) {
		setenv("MP_STDOUTMODE", "unordered", 1);
		if (srun_opt->launch_cmd)
			xstrfmtcat(poe_cmd_line,
				   " -stdoutmode unordered");
	}

	_propagate_srun_opts(nnodes, ntasks, opt_local, job->pack_offset);
	setenv("SLURM_STARTED_STEP", "YES", 1);
	//disable_status = opt_local->disable_status;
	//quit_on_intr = opt_local->quit_on_intr;
	//srun_jobid = xstrdup(opt_local->jobid);

	if (srun_opt->launch_cmd) {
		printf("%s\n", poe_cmd_line);
		xfree(poe_cmd_line);

		exit(0);
	}
 	return SLURM_SUCCESS;
}

static void _build_user_env(slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	char *tmp_env, *tok, *save_ptr = NULL, *eq_ptr, *value;
	xassert(srun_opt);

	tmp_env = xstrdup(srun_opt->export_env);
	tok = strtok_r(tmp_env, ",", &save_ptr);
	while (tok) {
		if (!xstrcasecmp(tok, "NONE"))
			break;
		eq_ptr = strchr(tok, '=');
		if (eq_ptr) {
			eq_ptr[0] = '\0';
			value = eq_ptr + 1;
			setenv(tok, value, 1);
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_env);
}

extern int launch_p_step_launch(srun_job_t *job, slurm_step_io_fds_t *cio_fds,
				uint32_t *global_rc,
				slurm_step_launch_callbacks_t *step_callbacks,
				slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	int rc = 0;
	xassert(srun_opt);

	if (srun_opt->export_env)
		_build_user_env(opt_local);

	poe_pid = fork();
	if (poe_pid < 0) {
		error("fork: %m");
		return 1;
	} else if (poe_pid > 0) {
		if (waitpid(poe_pid, &rc, 0) < 0)
			error("Unable to reap poe child process");
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
		error("execv(poe) error: %m");
		return 1;
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
	if (poe_pid)
		kill(poe_pid, signal);
}
