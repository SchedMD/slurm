/*****************************************************************************\
 *  launch_poe.c - Define job launch using IBM's poe.
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "src/common/slurm_xlator.h"
#include "src/srun/libsrun/launch.h"
#include "src/common/env.h"
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
const char plugin_name[]        = "launch poe plugin";
const char plugin_type[]        = "launch/poe";
const uint32_t plugin_version   = 101;

static char *cmd_fname = NULL;
static char *stepid_fname = NULL;
static char *poe_cmd_line = NULL;

/* Return the next available step ID */
static int _get_next_stepid(uint32_t job_id, char *dname, int dname_size)
{
	int fd, i, rc, step_id;
	char *work_dir;
	ssize_t io_size = 0;
	char buf[16];

	/* NOTE: Directory must be shared between nodes for cmd_file to work */
	if (!(work_dir = getenv("HOME"))) {
		work_dir = xmalloc(512);
		if (!getcwd(work_dir, 512))
			fatal("getcwd(): %m");
		snprintf(dname, dname_size, "%s/.slurm_loadl", work_dir);
		xfree(work_dir);
	} else {
		snprintf(dname, dname_size, "%s/.slurm_loadl", work_dir);
	}
	mkdir(dname, 0700);

	/* Create or open our stepid file */
	if (!stepid_fname)
		stepid_fname = xstrdup_printf("%s/slurm_stepid_%u",
					      dname, job_id);
	while (((fd = open(stepid_fname, O_CREAT|O_EXCL|O_RDWR, 0600)) < 0) &&
	       (errno == EINTR))
		;
	if ((fd < 0) && (errno == EEXIST))
		fd = open(stepid_fname, O_RDWR);
	if (fd < 0)
		fatal("open(%s): %m", stepid_fname);

	/* Set exclusive lock on the file */
	for (i = 0; ; i++) {
		rc = flock(fd, LOCK_EX | LOCK_NB);
		if (rc == 0)
			break;
		if (i > 10)
			fatal("flock(%s): %m", stepid_fname);
		usleep(100);
	}

	/* Read latest step ID from the file */
	for (i = 0; ; i++) {
		io_size = read(fd, buf, sizeof(buf));
		if (io_size >= 0)
			break;
		if (i > 10) {
			flock(fd, LOCK_UN);
			fatal("read(%s): %m", stepid_fname);
		}
	}
	if (io_size > 0)
		step_id = atoi(buf) + 1;
	else
		step_id = 1;

	/* Write new step ID value */
	snprintf(buf, sizeof(buf), "%d", step_id);
	for (i = 0; ; i++) {
		lseek(fd, 0, SEEK_SET);
		io_size = write(fd, buf, sizeof(buf));
		if (io_size == sizeof(buf))
			break;
		if (i > 10) {
			flock(fd, LOCK_UN);
			fatal("write(%s): %m", stepid_fname);
		}
	}

	/* Unlock the file */
	for (i = 0; ; i++) {
		rc = flock(fd, LOCK_UN);
		if (rc == 0)
			break;
		if (i > 10)
			fatal("flock(%s): %m", stepid_fname);
	}

	return step_id;
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

/*
 * Parse a multi-prog input file line
 * line IN - line to parse
 * num_task OUT - number of tasks to be started
 * cmd OUT - command to execute, caller must xfree this
 * args OUT - arguments to the command, caller must xfree this
 */
static void _parse_prog_line(char *in_line, int *num_tasks, char **cmd,
			     char **args)
{
	int i;
	int first_arg_inx = 0, last_arg_inx = 0;
	int first_cmd_inx,  last_cmd_inx;
	int first_task_inx, last_task_inx;
	hostset_t hs;

	/* Get the task ID string */
	for (i = 0; in_line[i]; i++) {
		if (!isspace(in_line[i]))
			break;
	}
	if (in_line[i] == '#')
		goto fini;
	if (!isdigit(in_line[i]))
		goto bad_line;
	first_task_inx = i;
	for (i++; in_line[i]; i++) {
		if (isspace(in_line[i]))
			break;
	}
	if (!isspace(in_line[i]))
		goto bad_line;
	last_task_inx = i;

	/* Get the command */
	for (i++; in_line[i]; i++) {
		if (!isspace(in_line[i]))
			break;
	}
	if (in_line[i] == '\0')
		goto bad_line;
	first_cmd_inx = i;
	for (i++; in_line[i]; i++) {
		if (isspace(in_line[i]))
			break;
	}
	if (!isspace(in_line[i]))
		goto bad_line;
	last_cmd_inx = i;

	/* Get the command's arguments */
	for (i++; in_line[i]; i++) {
		if (!isspace(in_line[i]))
			break;
	}
	if (in_line[i])
		first_arg_inx = i;
	for ( ; in_line[i]; i++) {
		if (in_line[i] == '\n') {
			last_arg_inx = i;
			break;
		}
	}

	/* Now transfer data to the function arguments */
	in_line[last_task_inx] = '\0';
	hs = hostset_create(in_line + first_task_inx);
	in_line[last_task_inx] = ' ';
	if (!hs)
		goto bad_line;
	*num_tasks = hostset_count(hs);
	hostset_destroy(hs);

	in_line[last_cmd_inx] = '\0';
	*cmd = xstrdup(in_line + first_cmd_inx);
	in_line[last_cmd_inx] = ' ';

	if (last_arg_inx)
		in_line[last_arg_inx] = '\0';
	if (first_arg_inx)
		*args = xstrdup(in_line + first_arg_inx);
	else
		*args = NULL;
	if (last_arg_inx)
		in_line[last_arg_inx] = '\n';
	return;

bad_line:
	error("invalid input line: %s", in_line);
fini:	*num_tasks = -1;
	return;
}

/*
 * Either get or set a POE command line,
 * line IN/OUT - line to set or get
 * length IN - size of line in bytes
 * step_id IN - -1 if input line, otherwise the step ID to output
 * RET true if more lines to get
 */
static bool _multi_prog_parse(char *line, int length, int step_id)
{
	static int cmd_count = 0, inx = 0, total_tasks = 0;
	static char **args = NULL, **cmd = NULL;
	static int *num_tasks = NULL;
	int i;

	if (step_id < 0) {
		char *tmp_args = NULL, *tmp_cmd = NULL;
		int tmp_tasks = -1;
		_parse_prog_line(line, &tmp_tasks, &tmp_cmd, &tmp_args);

		if (tmp_tasks < 0) {
			if (line[0] != '#')
				error("bad line %s", line);
			return true;
		}

		xrealloc(args, (sizeof(char *) * (cmd_count + 1)));
		xrealloc(cmd,  (sizeof(char *) * (cmd_count + 1)));
		xrealloc(num_tasks, (sizeof(int) * (cmd_count + 1)));
		args[cmd_count] = tmp_args;
		cmd[cmd_count]  = tmp_cmd;
		num_tasks[cmd_count] = tmp_tasks;
		total_tasks += tmp_tasks;
		cmd_count++;
		return true;
	} else if (inx >= cmd_count) {
		for (i = 0; i < cmd_count; i++) {
			xfree(args[i]);
			xfree(cmd[i]);
		}
		xfree(args);
		xfree(cmd);
		xfree(num_tasks);
		cmd_count = 0;
		inx = 0;
		total_tasks = 0;
		return false;
	} else if (args[inx]) {
		/* <cmd>@<step_id>%<total_tasks>%<protocol>:<num_tasks> <args...> */
		snprintf(line, length, "%s@%d%c%d%c%s:%d %s",
			 cmd[inx], step_id, '%', total_tasks, '%',
			 _get_cmd_protocol(cmd[inx]), num_tasks[inx],
			 args[inx]);
		inx++;
		return true;
	} else {
		/* <cmd>@<step_id>%<total_tasks>%<protocol>:<num_tasks> */
		snprintf(line, length, "%s@%d%c%d%c%s:%d",
			 cmd[inx], step_id, '%', total_tasks, '%',
			 _get_cmd_protocol(cmd[inx]), num_tasks[inx]);
		inx++;
		return true;
	}
}

/* Propagate srun options for use by POE by setting environment
 * variables, which are subsequently processed the libsrun/opt.c logic
 * as called from launch/slurm (by POE). */
static void _propagate_srun_opts(uint32_t nnodes, uint32_t ntasks)
{
	char value[32];

	if (opt.account)
		setenv("SLURM_ACCOUNT", opt.account, 1);
	if (opt.acctg_freq >= 0) {
		snprintf(value, sizeof(value), "%d", opt.acctg_freq);
		setenv("SLURM_ACCTG_FREQ", value, 1);
	}
	if (opt.ckpt_dir)
		setenv("SLURM_CHECKPOINT_DIR", opt.ckpt_dir, 1);
	if (opt.ckpt_interval) {
		snprintf(value, sizeof(value), "%d", opt.ckpt_interval);
		setenv("SLURM_CHECKPOINT", value, 1);
	}
	if (opt.cpus_per_task) {
		snprintf(value, sizeof(value), "%d", opt.cpus_per_task);
		setenv("SLURM_CPUS_PER_TASK", value, 1);
	}
	if (opt.dependency)
		setenv("SLURM_DEPENDENCY", opt.dependency, 1);
	if (opt.exclusive)
		setenv("SLURM_EXCLUSIVE", "1", 1);
	if (opt.gres)
		setenv("SLURM_GRES", opt.gres, 1);
	if (opt.immediate)
		setenv("SLURM_IMMEDIATE", "1", 1);
	if (opt.job_name)
		setenv("SLURM_JOB_NAME", opt.job_name, 1);
	if (opt.mem_per_cpu > 0) {
		snprintf(value, sizeof(value), "%d", opt.mem_per_cpu);
		setenv("SLURM_MEM_PER_CPU", value, 1);
	}
	if (opt.pn_min_memory > 0) {
		snprintf(value, sizeof(value), "%d", opt.pn_min_memory);
		setenv("SLURM_MEM_PER_NODE", value, 1);
	}
	if (opt.network)
		setenv("SLURM_NETWORK", opt.network, 1);
	if (nnodes) {
		snprintf(value, sizeof(value), "%u", nnodes);
		setenv("SLURM_JOB_NUM_NODES", value, 1);
		setenv("SLURM_NNODES", value, 1);
	}
	if (opt.alloc_nodelist) {
		setenv("SLURM_JOB_NODELIST", opt.alloc_nodelist, 1);
		setenv("SLURM_NODELIST", opt.alloc_nodelist, 1);
	}
	if (ntasks) {
		snprintf(value, sizeof(value), "%u", ntasks);
		setenv("SLURM_NTASKS", value, 1);
	}
	if (opt.overcommit)
		setenv("SLURM_OVERCOMMIT", "1", 1);
	if (opt.partition)
		setenv("SLURM_PARTITION", opt.partition, 1);
	if (opt.qos)
		setenv("SLURM_QOS", opt.qos, 1);
	if (opt.resv_port_cnt >= 0) {
		snprintf(value, sizeof(value), "%d", opt.resv_port_cnt);
		setenv("SLURM_RESV_PORTS", value, 1);
	}
	if (opt.multi_prog)
		setenv("SLURM_SRUN_MULTI", "1", 1);
	if (opt.time_limit_str)
		setenv("SLURM_TIMELIMIT", opt.time_limit_str, 1);
	if (opt.wckey)
		setenv("SLURM_WCKEY", opt.wckey, 1);
	if (opt.cwd)
		setenv("SLURM_WORKING_DIR", opt.cwd, 1);
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
	if (opt.test_only) {
		error("--test-only not supported with poe");
		exit (1);
	} else if (opt.no_alloc) {
		error("--no-allocate not supported with poe");
		exit (1);
	}
	if (opt.slurmd_debug != LOG_LEVEL_QUIET) {
		error("--slurmd-debug not supported with poe");
		opt.slurmd_debug = LOG_LEVEL_QUIET;
	}

	opt.argc++;

	/* We need to do +2 here just incase multi-prog is needed (we
	   add an extra argv on so just make space for it).
	*/
	opt.argv = (char **) xmalloc((opt.argc + 2) * sizeof(char *));

	opt.argv[0] = xstrdup("poe");
	/* Set default job name to the executable name rather than
	 * "runjob" */
	if (!opt.job_name_set_cmd && (1 < opt.argc)) {
		opt.job_name_set_cmd = true;
		opt.job_name = xstrdup(rest[0]);
	}

	return 1;
}

extern int launch_p_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job)
{
	int step_id;
	char dname[512], value[32];
	bool need_cmdfile = false;
	char *protocol = "mpi";
	uint32_t ntasks = opt.ntasks;
	uint32_t nnodes = opt.min_nodes;

	if (opt.launch_cmd) {
		int i;

		xstrfmtcat(poe_cmd_line, "%s", opt.argv[0]);
		for (i = 1; i < opt.argc; i++)
			xstrfmtcat(poe_cmd_line, " %s", opt.argv[i]);
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
	 * In order to support MPMD or job steps smaller than the job
	 * allocation size, specify a command file using the poe option
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
	 * 1) If MPI library is found (libmpi.so || libmich.so) -> use 'mpi'
	 * 2) if Openshmem library is found (libshmem.so) -> use 'shmem'
	 * 3) if UPC runtime library is found (libxlpgas.so) -> use 'pgas'
	 * 4) if only PAMI library is found (libpami.so) -> use 'pami'
	 * 5) if only LAPI library is found (liblapi.so) -> use 'lapi'
	 */
	if (opt.multi_prog)
		need_cmdfile = true;
	if (opt.ntasks_set && !need_cmdfile) {
		char *tmp_str = getenv("SLURM_NPROCS");
		if (!tmp_str)
			tmp_str = getenv("SLURM_NNODES");
		if (tmp_str && (opt.ntasks != atoi(tmp_str)))
			need_cmdfile = true;
	}

	if (opt.multi_prog) {
		protocol = "multi";
	} else {
		protocol = _get_cmd_protocol(opt.argv[1]);
	}
	debug("cmd:%s protcol:%s", opt.argv[1], protocol);


	if (need_cmdfile) {
		char *buf;
		int fd, i, j, k;

		if (opt.launch_cmd) {
			error("--launch_cmd not available "
			      "when using a cmdfile");
			return SLURM_ERROR;
		}
		xassert(job);
		/* NOTE: The command file needs to be in a directory that can
		 * be read from the compute node(s), so /tmp does not work.
		 * We use the user's home directory (based upon "HOME"
		 * environment variable) otherwise use current working
		 * directory. The file name contains the job ID and step ID. */
		step_id = _get_next_stepid(job->jobid, dname, sizeof(dname));
		xstrfmtcat(cmd_fname,
			   "%s/slurm_cmdfile_%u.%d",
			   dname, job->jobid, step_id);
		while ((fd = creat(cmd_fname, 0600)) < 0) {
			if (errno == EINTR)
				continue;
			fatal("creat(%s): %m", cmd_fname);
		}

		i = strlen(opt.argv[1]) + 128;
		buf = xmalloc(i);
		if (opt.multi_prog) {
			char in_line[512];
			FILE *fp = fopen(opt.argv[1], "r");
			if (!fp)
				fatal("fopen(%s): %m", opt.argv[1]);
			/* Read and parse SLURM MPMD format file here */
			while (fgets(in_line, sizeof(in_line), fp))
				_multi_prog_parse(in_line, 512, -1);
			fclose(fp);
			/* Write LoadLeveler MPMD format file here */
			while (_multi_prog_parse(in_line, 512, step_id))
				j = xstrfmtcat(buf, "%s\n", in_line);
		} else {
			/* <cmd>@<step_id>%<total_tasks>%<protocol>:<num_tasks> <args...>*/
			xstrfmtcat(buf, "%s@%d%c%d%c%s:%d",
				   opt.argv[1], step_id, '%',
				   opt.ntasks, '%', protocol, opt.ntasks);
			for (i = 2; i < opt.argc; i++) /* start at argv[2] */
				xstrfmtcat(buf, " %s", opt.argv[i]);
			xstrfmtcat(buf, "\n");
		}
		i = 0;
		j = strlen(buf);
		while ((k = write(fd, &buf[i], j))) {
			if (k > 0) {
				i += k;
				j -= k;
			} else if ((errno != EAGAIN) && (errno != EINTR)) {
				error("write(cmdfile): %m");
				break;
			}
		}
		(void) close(fd);
		/* FIXME : I get this error whtn MP_NEWJOB IS set ...
		 * ATTENTION: 0031-634 MP_NEWJOB=parallel is ignored
		 * when program name is supplied.
		 */
//		setenv("MP_NEWJOB", "parallel", 1);
		setenv("MP_CMDFILE", cmd_fname, 1);
	}

	if (opt.cpu_bind_type) {
		if ((opt.cpu_bind_type & CPU_BIND_TO_THREADS) ||
		    (opt.cpu_bind_type & CPU_BIND_TO_CORES)) {
			setenv("MP_BINDPROC", "yes", 1);
			if (opt.launch_cmd)
				xstrfmtcat(poe_cmd_line, " -bindproc yes");
		}
	}
	if (opt.shared != (uint16_t) NO_VAL) {
		char *shared_cpu_use = "multiple";

		if (opt.shared)
			shared_cpu_use = "unique";

		setenv("MP_CPU_USE", shared_cpu_use, 1);

		if (opt.launch_cmd)
			xstrfmtcat(poe_cmd_line, " -cpu_use %s",
				   shared_cpu_use);
	}
	if (opt.network) {
		bool cau_set = false;
		bool dev_type_set = false;
		bool protocol_set = false;
		char *type_ptr = NULL;
		char *save_ptr = NULL, *token;
		char *network_str = xstrdup(opt.network);
		char *adapter_use = NULL;

		if (strstr(opt.network, "dedicated"))
			adapter_use = "dedicated";
		else if (strstr(opt.network, "shared"))
			adapter_use = "shared";

		if (adapter_use) {
			setenv("MP_ADAPTER_USE", adapter_use, 1);
			if (opt.launch_cmd)
				xstrfmtcat(poe_cmd_line, " -adapter_use %s",
					   adapter_use);
		}

		token = strtok_r(network_str, ",", &save_ptr);
		while (token) {
			/* bulk_xfer options */
			if (!strncasecmp(token, "bulk_xfer", 9)) {
				setenv("MP_USE_BULK_XFER", "yes", 1);
				if (opt.launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -use_bulk_xfer yes");
			/* device name options */
			} else if (!strncasecmp(token, "devname=", 8)) {
				/* Ignored by POE */

			/* device type options */
			} else if (!strncasecmp(token, "devtype=", 8)) {
				type_ptr = token + 8;
				if (!strcasecmp(type_ptr, "ib")) {
					setenv("MP_DEVTYPE", type_ptr, 1);
					if (opt.launch_cmd)
						xstrfmtcat(poe_cmd_line,
							   " -devtype %s",
							   type_ptr);
				} else if (!strcasecmp(type_ptr, "hfi")) {
					setenv("MP_DEVTYPE", type_ptr, 1);
					if (opt.launch_cmd)
						xstrfmtcat(poe_cmd_line,
							   " -devtype %s",
							   type_ptr);
				}
				dev_type_set = true;
				/* POE ignores other options */

			/* instances options */
			} else if (!strncasecmp(token, "instances=", 10)) {
				/* Ignored */

			/* network options */
			} else if (!strcasecmp(token, "ip")   ||
				  !strcasecmp(token, "ipv4")  ||
				  !strcasecmp(token, "ipv6")) {
				setenv("MP_EUILIB", "ip", 1);
				if (opt.launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -euilib ip");
			} else if (!strcasecmp(token, "us")) {
				setenv("MP_EUILIB", "us", 1);
				if (opt.launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -euilib us");
			/* protocol options */
			} else if ((!strncasecmp(token, "lapi", 4)) ||
				   (!strncasecmp(token, "mpi",  3)) ||
				   (!strncasecmp(token, "pami", 4)) ||
				   (!strncasecmp(token, "upc",  3))) {
				if (!protocol_set) {
					protocol_set = true;
					protocol = NULL;
				}
				if (protocol)
					xstrcat(protocol, ",");
				xstrcat(protocol, token);
				setenv("MP_MSG_API", protocol, 0);
			/* adapter options */
			} else if (!strcasecmp(token, "sn_all")) {
				setenv("MP_EUIDEVICE", "sn_all", 1);
				if (opt.launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -euidevice sn_all");
			} else if (!strcasecmp(token, "sn_single")) {
				setenv("MP_EUIDEVICE", "sn_single", 1);
				if (opt.launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -euidevice sn_single");
			/* Collective Acceleration Units (CAU) */
			} else if (!strncasecmp(token, "cau=", 4)) {
				setenv("MP_COLLECTIVE_GROUPS", token + 4, 1);
				if (opt.launch_cmd)
					xstrfmtcat(poe_cmd_line,
						   " -collective_groups %s",
						   token + 4);
				if (atoi(token + 4))
					cau_set = true;
			/* Immediate Send Slots Per Window */
			} else if (!strncasecmp(token, "immed=", 6)) {
				setenv("MP_IMM_SEND_BUFFERS", token + 6, 1);
				if (opt.launch_cmd)
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
			if (opt.launch_cmd)
				xstrcat(poe_cmd_line, " -devtype hfi");
		}

		if (opt.launch_cmd)
			xstrfmtcat(poe_cmd_line, " -msg_api %s", protocol);
		if (protocol_set)
			xfree(protocol);
		else
			setenv("MP_MSG_API", protocol, 0);
	} else {
		if (!strcmp(protocol, "multi")) {
			setenv("MP_MSG_API", protocol, 0);
			if (opt.launch_cmd)
				xstrfmtcat(poe_cmd_line,
					   " -msg_api %s", protocol);
		}
	}

	if (opt.nodelist) {
		char *fname = NULL, *host_name, *host_line;
		pid_t pid = getpid();
		hostlist_t hl;
		int fd, len, offset, wrote;
		hl = hostlist_create(opt.nodelist);
		if (!hl)
			fatal("Invalid nodelist: %s", opt.nodelist);
		xstrfmtcat(fname, "slurm_hostlist.%u", (uint32_t) pid);
		if ((fd = creat(fname, 0600)) < 0)
			fatal("creat(%s): %m", fname);
		while ((host_name = hostlist_shift(hl))) {
			host_line = NULL;
			xstrfmtcat(host_line, "%s\n", host_name);
			free(host_name);
			len = strlen(host_line) + 1;
			offset = 0;
			while (len > offset) {
				wrote = write(fd, host_line + offset,
					      len - offset);
				if (wrote < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					fatal("write(%s): %m", fname);
				}
				offset += wrote;
			}
			xfree(host_line);
		}
		hostlist_destroy(hl);
		info("wrote hostlist file at %s", fname);
		xfree(fname);
		close(fd);
	}
	if (opt.msg_timeout) {
		snprintf(value, sizeof(value), "%d", opt.msg_timeout);
		setenv("MP_TIMEOUT", value, 1);
		/* There is no equivelent cmd line option */
	}
	if (opt.immediate) {
		setenv("MP_RETRY", "0", 1);
		if (opt.launch_cmd)
			xstrfmtcat(poe_cmd_line, " -retry 0");
	}
	if (opt.labelio) {
		setenv("MP_LABELIO", "yes", 0);
		if (opt.launch_cmd)
			xstrfmtcat(poe_cmd_line, " -labelio yes");
	}
	if (nnodes) {
		snprintf(value, sizeof(value), "%u", nnodes);
		setenv("MP_NODES", value, 1);
		if (opt.launch_cmd)
			xstrfmtcat(poe_cmd_line, " -nodes %s", value);
	}
	if (ntasks) {
		snprintf(value, sizeof(value), "%u", ntasks);
		setenv("MP_PROCS", value, 1);
		if (opt.launch_cmd)
			xstrfmtcat(poe_cmd_line, " -procs %s", value);
	}
	if (opt.cpu_bind_type) {
		char *task_affinity = NULL;

		if (opt.cpu_bind_type & CPU_BIND_TO_THREADS)
			task_affinity = "cpu";
		else if (opt.cpu_bind_type & CPU_BIND_TO_CORES)
			task_affinity = "core";
		else if (opt.cpus_per_task) {
			snprintf(value, sizeof(value), "cpu:%d",
				 opt.cpus_per_task);
			task_affinity = value;
		}

		if (task_affinity) {
			setenv("MP_TASK_AFFINITY", task_affinity, 1);
			if (opt.launch_cmd)
				xstrfmtcat(poe_cmd_line, " -task_affinity %s",
					   task_affinity);

		}
	}
	if (opt.ntasks_per_node != NO_VAL) {
		snprintf(value, sizeof(value), "%u", opt.ntasks_per_node);
		setenv("MP_TASKS_PER_NODE", value, 1);
		if (opt.launch_cmd)
			xstrfmtcat(poe_cmd_line, " -tasks_per_node %s", value);
	}
	if (opt.unbuffered) {
		setenv("MP_STDOUTMODE", "unordered", 1);
		if (opt.launch_cmd)
			xstrfmtcat(poe_cmd_line,
				   " -stdoutmode unordered");
	}

	/* Since poe doesn't need to know about the partition and it
	   really needs to have RMPOOL set just set it to something.
	*/
	setenv("MP_RMPOOL", "SLURM", 1);
	if (opt.launch_cmd)
		xstrfmtcat(poe_cmd_line, " -rmpool slurm");

	_propagate_srun_opts(nnodes, ntasks);
	setenv("SLURM_STARTED_STEP", "YES", 1);
	//disable_status = opt.disable_status;
	//quit_on_intr = opt.quit_on_intr;
	//srun_jobid = xstrdup(opt.jobid);

	if (opt.launch_cmd) {
		printf("%s\n", poe_cmd_line);
		xfree(poe_cmd_line);

		exit(0);
	}
 	return SLURM_SUCCESS;
}

extern int launch_p_step_launch(
	srun_job_t *job, slurm_step_io_fds_t *cio_fds, uint32_t *global_rc)
{
	int rc = 0;
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		error("fork: %m");
		return 1;
	} else if (pid > 0) {
		if (waitpid(pid, &rc, 0) < 0)
			error("Unable to reap poe child process");
		*global_rc = rc;
		/* Just because waitpid returns something doesn't mean
		   this function failed so always set it back to 0.
		*/
		rc = 0;
	} else {
		/* dup stdio onto our open fds */
		if ((dup2(cio_fds->in.fd, 0) == -1) ||
		    (dup2(cio_fds->out.fd, 1) == -1) ||
		    (dup2(cio_fds->err.fd, 2) == -1)) {
			error("dup2: %m");
			return 1;
		}

		execvp(opt.argv[0], opt.argv);
		error("execv(poe) error: %m");
		return 1;
	}

	return rc;
}

extern int launch_p_step_wait(srun_job_t *job, bool got_alloc)
{
	return 0;
}

extern int launch_p_step_terminate(void)
{
	if (cmd_fname)
		(void) unlink(cmd_fname);
	if (stepid_fname)
		(void) unlink(stepid_fname);
	return SLURM_SUCCESS;
}


extern void launch_p_print_status(void)
{

}

extern void launch_p_fwd_signal(int signal)
{

}
