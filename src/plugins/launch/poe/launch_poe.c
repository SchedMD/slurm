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

#include <sys/file.h>
#include <stdlib.h>

#include "src/srun/launch.h"
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
	if (strstr(buf, "libmpi.so"))
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

/* Build a POE command line based upon srun options (using global variables) */
static char *_build_poe_command(uint32_t job_id)
{
	int i, step_id;
	char *cmd_line = NULL, *tmp_str;
	char dname[512], value[32];
	bool need_cmdfile = false;
	char *protocol = "mpi";

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
	 * 1) If MPI library is found (libmpi.so) -> use 'mpi'
	 * 2) if Openshmem library is found (libshmem.so) -> use 'shmem'
	 * 3) if UPC runtime library is found (libxlpgas.so) -> use 'pgas'
	 * 4) if only PAMI library is found (libpami.so) -> use 'pami'
	 * 5) if only LAPI library is found (liblapi.so) -> use 'lapi'
	 */
	if (opt.multi_prog)
		need_cmdfile = true;
	if (opt.ntasks_set && !need_cmdfile) {
		tmp_str = getenv("SLURM_NPROCS");
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

	step_id = _get_next_stepid(job_id, dname, sizeof(dname));

	if (need_cmdfile) {
		char *buf;
		int fd, i, j, k;

		/* NOTE: The command file needs to be in a directory that can
		 * be read from the compute node(s), so /tmp does not work.
		 * We use the user's home directory (based upon "HOME"
		 * environment variable) otherwise use current working
		 * directory. The file name contains the job ID and step ID. */
		xstrfmtcat(cmd_fname,
			   "%s/slurm_cmdfile_%u.%d", dname, job_id, step_id);
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
		setenv("MP_NEWJOB", "parallel", 1);
		setenv("MP_CMDFILE", cmd_fname, 1);
	} else {
		xstrfmtcat(cmd_line, " %s", opt.argv[1]);
		/* Each token gets double quotes around it in case any
		 * arguments contain spaces */
		for (i = 2; i < opt.argc; i++) {
			xstrfmtcat(cmd_line, " \"%s\"", opt.argv[i]);
		}
	}

	if (opt.network) {
		if (strstr(opt.network, "dedicated"))
			setenv("MP_ADAPTER_USE", "dedicated", 1);
		else if (strstr(opt.network, "shared"))
			setenv("MP_ADAPTER_USE", "shared", 1);
	}
	if (opt.cpu_bind_type) {
		if ((opt.cpu_bind_type & CPU_BIND_TO_THREADS) ||
		    (opt.cpu_bind_type & CPU_BIND_TO_CORES)) {
			setenv("MP_BINDPROC", "yes", 1);
		}
	}
	if (opt.shared != (uint16_t) NO_VAL) {
		if (opt.shared)
			setenv("MP_CPU_USE", "unique", 1);
		else
			setenv("MP_CPU_USE", "multiple", 1);
	}
	if (opt.network) {
		if (strstr(opt.network, "hfi"))
			setenv("MP_DEVTYPE", "hfi", 1);
		else if  (strstr(opt.network, "ib"))
			setenv("MP_DEVTYPE", "ib", 1);
	}
	if (opt.network) {
		if (strstr(opt.network, "sn_all"))
			setenv("MP_EUIDEVICE", "sn_all", 1);
		else if (strstr(opt.network, "sn_single"))
			setenv("MP_EUIDEVICE", "sn_single", 1);
		else if ((tmp_str = strstr(opt.network, "eth"))) {
			char buf[5];
			strncpy(buf, tmp_str, 5);
			buf[4] = '\0';
			setenv("MP_EUIDEVICE", buf, 1);
		}
	}
	if (opt.network) {
		if (strstr(opt.network, "ip") || strstr(opt.network, "ip"))
			setenv("MP_EUILIB", "IP", 1);
		else if (strstr(opt.network, "us") ||
			 strstr(opt.network, "US"))
			setenv("MP_EUILIB", "US", 1);
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
	}
	if (opt.immediate)
		setenv("MP_RETRY", "0", 1);
	if (_verbose) {
		int info_level = MIN((_verbose + 1), 6);
		snprintf(value, sizeof(value), "%d", info_level);
		setenv("MP_INFOLEVEL", value, 1);
	}
	if (opt.labelio)
		setenv("MP_LABELIO", "yes", 0);
	if (!strcmp(protocol, "multi"))
		setenv("MP_MSG_API", "mpi", 0);
	else if (!strcmp(protocol, "mpi"))
		setenv("MP_MSG_API", "mpi", 0);
	else if (!strcmp(protocol, "lapi"))
		setenv("MP_MSG_API", "lapi", 0);
	else if (!strcmp(protocol, "pami"))
		setenv("MP_MSG_API", "pami", 0);
	else if (!strcmp(protocol, "upc"))
		setenv("MP_MSG_API", "upc", 0);
	else if (!strcmp(protocol, "shmem")) {
		setenv("MP_MSG_API", "shmem,xmi", 0);
		setenv("MP_USE_BULK_XFER", "no", 0);
	}
	if (opt.min_nodes != NO_VAL) {
		snprintf(value, sizeof(value), "%u", opt.min_nodes);
		setenv("MP_NODES", value, 1);
	}
	if (opt.ntasks) {
		snprintf(value, sizeof(value), "%u", opt.ntasks);
		setenv("MP_PROCS", value, 1);
	}
	if (opt.cpu_bind_type) {
		if (opt.cpu_bind_type & CPU_BIND_TO_THREADS)
			setenv("MP_TASK_AFFINITY", "cpu", 1);
		else if (opt.cpu_bind_type & CPU_BIND_TO_CORES)
			setenv("MP_TASK_AFFINITY", "core", 1);
		else if (opt.cpus_per_task) {
			snprintf(value, sizeof(value), "cpu:%d",
				 opt.cpus_per_task);
			setenv("MP_TASK_AFFINITY", value, 1);
		}
	}
	if (opt.ntasks_per_node != NO_VAL) {
		snprintf(value, sizeof(value), "%u", opt.ntasks_per_node);
		setenv("MP_TASKS_PER_NODE", value, 1);
	}
	if (opt.unbuffered) {
		setenv("MP_STDERRMODE", "unordered", 1);
		setenv("MP_STDOUTMODE", "unordered", 1);
	}

	/* Since poe doesn't need to know about the partition and it
	   really needs to have RMPOOL set just set it to something.
	*/
	setenv("MP_RMPOOL", "SLURM", 1);
	//disable_status = opt.disable_status;
	//quit_on_intr = opt.quit_on_intr;
	//srun_jobid = xstrdup(opt.jobid);

#if _DEBUG_SRUN
	info("cmd_line:%s", cmd_line);
#endif
	xfree(cmd_line);
	return NULL;
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
		error("--no-allocate not supported with LoadLeveler");
		exit (1);
	}

	opt.argc++;

	opt.argv = (char **) xmalloc((opt.argc + 1) * sizeof(char *));

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
	info("partition = %s", opt.partition);
	poe_cmd_line = _build_poe_command(job->jobid);
	info("command built");
 	return SLURM_SUCCESS;
}

extern int launch_p_step_launch(
	srun_job_t *job, slurm_step_io_fds_t *cio_fds,
	uint32_t *global_rc, bool got_alloc, bool *srun_shutdown)
{
	int rc = 0;
	pid_t pid;
	int stderr_pipe[2] = {-1, -1};
	int stdin_pipe[2] = {-1, -1}, stdout_pipe[2] = {-1, -1};

	if ((pipe(stdin_pipe) == -1)
	    || (pipe(stdout_pipe) == -1)
	    || (pipe(stderr_pipe) == -1)) {
		error("pipe: %m");
		return 1;
	}
	info("calling %s", opt.argv[0]);
	pid = fork();
	if (pid < 0) {
		/* (void) close(stdin_pipe[0]); */
		/* (void) close(stdin_pipe[1]); */
		/* (void) close(stdout_pipe[0]); */
		/* (void) close(stdout_pipe[1]); */
		/* (void) close(stderr_pipe[0]); */
		/* (void) close(stderr_pipe[1]); */
		error("fork: %m");
		return 1;
	} else if (pid > 0) {
		if (waitpid(pid, NULL, 0) < 0)
			error("Unable to reap slurmd child process");
	} else {
		/* if ((dup2(stdin_pipe[0],  0) == -1) || */
		/*     (dup2(stdout_pipe[1], 1) == -1) || */
		/*     (dup2(stderr_pipe[1], 2) == -1)) { */
		/* 	error("dup2: %m"); */
		/* 	return 1; */
		/* } */

		/* (void) close(stderr_pipe[0]); */
		/* (void) close(stderr_pipe[1]); */
		/* (void) close(stdin_pipe[0]); */
		/* (void) close(stdin_pipe[1]); */
		/* (void) close(stdout_pipe[0]); */
		/* (void) close(stdout_pipe[1]); */

		execvp(opt.argv[0], opt.argv);
		error("execv(poe) error: %m");
		return 1;
	}

	(void) close(stdin_pipe[0]);
	(void) close(stdout_pipe[1]);
	(void) close(stderr_pipe[1]);

	info("partition = %s", opt.partition);
	/* NOTE: dummy_pipe is only used to wake the select() function in the
	 * loop below when the spawned process terminates */
	info("done with exec");
	return rc;
}

extern int launch_p_step_terminate(void)
{
	if (cmd_fname)
		(void) unlink(cmd_fname);
	if (stepid_fname)
		(void) unlink(stepid_fname);
	info("finishing");
	return SLURM_SUCCESS;
}


extern void launch_p_print_status(void)
{

}

extern void launch_p_fwd_signal(int signal)
{

}
