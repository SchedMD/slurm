/*****************************************************************************\
 *  sbatch.c - Submit a Slurm batch script.$
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2017 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
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

#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>               /* MAXPATHLEN */
#include <sys/resource.h> /* for RLIMIT_NOFILE */

#include "slurm/slurm.h"

#include "src/interfaces/cli_filter.h"
#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/interfaces/gres.h"
#include "src/common/pack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/run_command.h"
#include "src/interfaces/select.h"
#include "src/interfaces/auth.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/spank.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/sbatch/opt.h"

#define MAX_RETRIES 15
#define MAX_WAIT_SLEEP_TIME 32

static int   _fill_job_desc_from_opts(job_desc_msg_t *desc);
static void *_get_script_buffer(const char *filename, int *size);
static int   _job_wait(uint32_t job_id);
static char *_script_wrap(char *command_string);
static void  _set_exit_code(void);
static void  _set_prio_process_env(void);
static int   _set_rlimit_env(void);
static void  _set_spank_env(void);
static void  _set_submit_dir_env(void);
static int   _set_umask_env(void);

int main(int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	job_desc_msg_t *desc = NULL, *first_desc = NULL;
	submit_response_msg_t *resp = NULL;
	char *script_name;
	char *script_body;
	char **het_job_argv;
	int script_size = 0, het_job_argc, het_job_argc_off = 0, het_job_inx;
	int i, rc = SLURM_SUCCESS, retries = 0, het_job_limit = 0;
	bool het_job_fini = false;
	List job_env_list = NULL, job_req_list = NULL;
	sbatch_env_t *local_env = NULL;
	bool quiet = false;

	/* force line-buffered output on non-tty outputs */
	if (!isatty(STDOUT_FILENO))
		setvbuf(stdout, NULL, _IOLBF, 0);
	if (!isatty(STDERR_FILENO))
		setvbuf(stderr, NULL, _IOLBF, 0);

	slurm_init(NULL);
	log_init(xbasename(argv[0]), logopt, 0, NULL);

	_set_exit_code();
	if (spank_init_allocator() < 0) {
		error("Failed to initialize plugin stack");
		exit(error_exit);
	}

	/* Be sure to call spank_fini when sbatch exits
	 */
	if (atexit((void (*) (void)) spank_fini) < 0)
		error("Failed to register atexit handler for plugins: %m");

	script_name = process_options_first_pass(argc, argv);

	/* Preserve quiet request which is lost in second pass */
	quiet = opt.quiet;

	/* reinit log with new verbosity (if changed by command line) */
	if (opt.verbose || opt.quiet) {
		logopt.stderr_level += opt.verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	if (sbopt.wrap != NULL) {
		script_body = _script_wrap(sbopt.wrap);
	} else {
		script_body = _get_script_buffer(script_name, &script_size);
	}
	if (script_body == NULL)
		exit(error_exit);

	het_job_argc = argc - opt.argc;
	het_job_argv = argv;
	for (het_job_inx = 0; !het_job_fini; het_job_inx++) {
		bool more_het_comps = false;
		init_envs(&het_job_env);
		process_options_second_pass(het_job_argc, het_job_argv,
					    &het_job_argc_off, het_job_inx,
					    &more_het_comps, script_name ?
					    xbasename (script_name) : "stdin",
					    script_body, script_size);
		if ((het_job_argc_off >= 0) &&
		    (het_job_argc_off < het_job_argc) &&
		    !xstrcmp(het_job_argv[het_job_argc_off], ":")) {
			/* het_job_argv[0] moves from "salloc" to ":" */
			het_job_argc -= het_job_argc_off;
			het_job_argv += het_job_argc_off;
		} else if (!more_het_comps) {
			het_job_fini = true;
		}

		/*
		 * Note that this handling here is different than in
		 * salloc/srun. Instead of sending the file contents as the
		 * burst_buffer field in job_desc_msg_t, it will be spliced
		 * in to the job script.
		 */
		if (opt.burst_buffer_file) {
			buf_t *buf = create_mmap_buf(opt.burst_buffer_file);
			if (!buf) {
				error("Invalid --bbf specification");
				exit(error_exit);
			}
			run_command_add_to_script(&script_body,
						  get_buf_data(buf));
			FREE_NULL_BUFFER(buf);
		}

		if (spank_init_post_opt() < 0) {
			error("Plugin stack post-option processing failed");
			exit(error_exit);
		}

		if (opt.get_user_env_time < 0) {
			/* Moab doesn't propagate the user's resource limits, so
			 * slurmd determines the values at the same time that it
			 * gets the user's default environment variables. */
			(void) _set_rlimit_env();
		}

		/*
		 * if the environment is coming from a file, the
		 * environment at execution startup, must be unset.
		 */
		if (sbopt.export_file != NULL)
			env_unset_environment();

		_set_prio_process_env();
		_set_spank_env();
		_set_submit_dir_env();
		_set_umask_env();
		if (local_env && !job_env_list) {
			job_env_list = list_create(NULL);
			list_append(job_env_list, local_env);
			job_req_list = list_create(NULL);
			list_append(job_req_list, desc);
		}
		local_env = xmalloc(sizeof(sbatch_env_t));
		memcpy(local_env, &het_job_env, sizeof(sbatch_env_t));

		desc = slurm_opt_create_job_desc(&opt, true);
		if (_fill_job_desc_from_opts(desc) == -1)
			exit(error_exit);
		if (!first_desc)
			first_desc = desc;
		if (het_job_inx || !het_job_fini) {
			set_env_from_opts(&opt, &first_desc->environment,
					  het_job_inx);
		} else
			set_env_from_opts(&opt, &first_desc->environment, -1);
		if (!job_req_list) {
			desc->script = (char *) script_body;
		} else {
			list_append(job_env_list, local_env);
			list_append(job_req_list, desc);
		}
	}
	het_job_limit = het_job_inx;
	if (!desc) {	/* For CLANG false positive */
		error("Internal parsing error");
		exit(1);
	}

	if (job_env_list) {
		ListIterator desc_iter, env_iter;
		i = 0;
		desc_iter = list_iterator_create(job_req_list);
		env_iter  = list_iterator_create(job_env_list);
		desc      = list_next(desc_iter);
		while (desc && (local_env = list_next(env_iter))) {
			set_envs(&desc->environment, local_env, i++);
			desc->env_size = envcount(desc->environment);
		}
		list_iterator_destroy(env_iter);
		list_iterator_destroy(desc_iter);

	} else {
		set_envs(&desc->environment, &het_job_env, -1);
		desc->env_size = envcount(desc->environment);
	}
	if (!desc) {	/* For CLANG false positive */
		error("Internal parsing error");
		exit(1);
	}

	/*
	 * If can run on multiple clusters find the earliest run time
	 * and run it there
	 */
	if (opt.clusters) {
		if (job_req_list) {
			rc = slurmdb_get_first_het_job_cluster(job_req_list,
					opt.clusters, &working_cluster_rec);
		} else {
			rc = slurmdb_get_first_avail_cluster(desc,
					opt.clusters, &working_cluster_rec);
		}
		if (rc != SLURM_SUCCESS) {
			print_db_notok(opt.clusters, 0);
			exit(error_exit);
		}
	}

	if (sbopt.test_only) {
		if (job_req_list)
			rc = slurm_het_job_will_run(job_req_list);
		else
			rc = slurm_job_will_run(desc);

		if (rc != SLURM_SUCCESS) {
			slurm_perror("allocation failure");
			exit(1);
		}
		exit(0);
	}

	while (true) {
		static char *msg;
		if (job_req_list)
			rc = slurm_submit_batch_het_job(job_req_list, &resp);
		else
			rc = slurm_submit_batch_job(desc, &resp);
		if (rc >= 0)
			break;
		if (errno == ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) {
			msg = "Slurm job queue full, sleeping and retrying";
		} else if (errno == ESLURM_NODES_BUSY) {
			msg = "Job creation temporarily disabled, retrying";
		} else if (errno == EAGAIN) {
			msg = "Slurm temporarily unable to accept job, "
			      "sleeping and retrying";
		} else
			msg = NULL;
		if ((msg == NULL) || (retries >= MAX_RETRIES)) {
			error("Batch job submission failed: %m");
			exit(error_exit);
		}

		if (retries)
			debug("%s", msg);
		else if (errno == ESLURM_NODES_BUSY)
			info("%s", msg); /* Not an error, powering up nodes */
		else
			error("%s", msg);
		slurm_free_submit_response_response_msg(resp);
		sleep(++retries);
	}

	if (!resp) {
		error("Batch job submission failed: %m");
		exit(error_exit);
	}

	print_multi_line_string(resp->job_submit_user_msg, -1, LOG_LEVEL_INFO);

	/* run cli_filter post_submit */
	for (i = 0; i < het_job_limit; i++)
		cli_filter_g_post_submit(i, resp->job_id, NO_VAL);

	if (!quiet) {
		if (!sbopt.parsable) {
			printf("Submitted batch job %u", resp->job_id);
			if (working_cluster_rec)
				printf(" on cluster %s",
				       working_cluster_rec->name);
			printf("\n");
		} else {
			printf("%u", resp->job_id);
			if (working_cluster_rec)
				printf(";%s", working_cluster_rec->name);
			printf("\n");
		}
	}

	if (sbopt.wait)
		rc = _job_wait(resp->job_id);

#ifdef MEMORY_LEAK_DEBUG
	select_g_fini();
	slurm_reset_all_options(&opt, false);
	slurm_auth_fini();
	slurm_conf_destroy();
	log_fini();
#endif /* MEMORY_LEAK_DEBUG */
	xfree(script_body);

	return rc;
}

/* Wait for specified job ID to terminate, return it's exit code */
static int _job_wait(uint32_t job_id)
{
	slurm_job_info_t *job_ptr;
	job_info_msg_t *resp = NULL;
	int ec = 0, ec2, i, rc;
	int sleep_time = 2;
	bool complete = false;

	while (!complete) {
		complete = true;
		sleep(sleep_time);
		/*
		 * min_job_age is factored into this to ensure the job can't
		 * run, complete quickly, and be purged from slurmctld before
		 * we've woken up and queried the job again.
		 */
		if ((sleep_time < (slurm_conf.min_job_age / 2)) &&
		    (sleep_time < MAX_WAIT_SLEEP_TIME))
			sleep_time *= 4;

		rc = slurm_load_job(&resp, job_id, SHOW_ALL);
		if (rc == SLURM_SUCCESS) {
			for (i = 0, job_ptr = resp->job_array;
			     (i < resp->record_count) && complete;
			     i++, job_ptr++) {
				if (IS_JOB_FINISHED(job_ptr)) {
					if (WIFEXITED(job_ptr->exit_code)) {
						ec2 = WEXITSTATUS(job_ptr->
								  exit_code);
					} else
						ec2 = 1;
					ec = MAX(ec, ec2);
				} else {
					complete = false;
				}
			}
			slurm_free_job_info_msg(resp);
		} else if (rc == ESLURM_INVALID_JOB_ID) {
			error("Job %u no longer found and exit code not found",
			      job_id);
		} else {
			complete = false;
			error("Currently unable to load job state information, retrying: %m");
		}
	}

	return ec;
}

/*
 * Returns 0 on success, -1 on failure
 * A copy of this exists in src/plugins/openapi/v0.0.xx/jobs.c. If this changes
 * please do the same there.
 */
static int _fill_job_desc_from_opts(job_desc_msg_t *desc)
{
	extern char **environ;

	if (!desc)
		return -1;

	if (!opt.job_name)
		desc->name = xstrdup("sbatch");

	desc->array_inx = sbopt.array_inx;
	desc->batch_features = sbopt.batch_features;
	desc->container = xstrdup(opt.container);
	xfree(desc->container_id);
	desc->container_id = xstrdup(opt.container_id);

	desc->wait_all_nodes = sbopt.wait_all_nodes;

	desc->environment = NULL;
	if (sbopt.export_file) {
		desc->environment = env_array_from_file(sbopt.export_file);
		if (desc->environment == NULL)
			exit(1);
	}
	if (opt.export_env == NULL) {
		env_array_merge(&desc->environment, (const char **) environ);
	} else if (!xstrcasecmp(opt.export_env, "ALL")) {
		env_array_merge(&desc->environment, (const char **) environ);
	} else if (!xstrcasecmp(opt.export_env, "NONE")) {
		desc->environment = env_array_create();
		env_array_merge_slurm(&desc->environment,
				      (const char **)environ);
		opt.get_user_env_time = 0;
	} else {
		env_merge_filter(&opt, desc);
		opt.get_user_env_time = 0;
	}
	if (opt.get_user_env_time >= 0) {
		env_array_overwrite(&desc->environment,
				    "SLURM_GET_USER_ENV", "1");
	}

	if ((opt.distribution & SLURM_DIST_STATE_BASE) == SLURM_DIST_ARBITRARY){
		env_array_overwrite_fmt(&desc->environment,
					"SLURM_ARBITRARY_NODELIST",
					"%s", desc->req_nodes);
	}

	desc->env_size = envcount(desc->environment);

	desc->argc     = opt.argc;
	desc->argv     = opt.argv;
	desc->std_err  = xstrdup(opt.efname);
	desc->std_in   = xstrdup(opt.ifname);
	desc->std_out  = xstrdup(opt.ofname);

	if (sbopt.requeue != NO_VAL)
		desc->requeue = sbopt.requeue;

	return 0;
}

static void _set_exit_code(void)
{
	int i;
	char *val = getenv("SLURM_EXIT_ERROR");

	if (val) {
		i = atoi(val);
		if (i == 0)
			error("SLURM_EXIT_ERROR has zero value");
		else
			error_exit = i;
	}
}

/* Propagate SPANK environment via SLURM_SPANK_ environment variables */
static void _set_spank_env(void)
{
	int i;

	for (i = 0; i < opt.spank_job_env_size; i++) {
		if (setenvfs("SLURM_SPANK_%s", opt.spank_job_env[i]) < 0) {
			error("unable to set %s in environment",
			      opt.spank_job_env[i]);
		}
	}
}

/* Set SLURM_SUBMIT_DIR and SLURM_SUBMIT_HOST environment variables within
 * current state */
static void _set_submit_dir_env(void)
{
	char buf[MAXPATHLEN + 1], host[256];

	if ((getcwd(buf, MAXPATHLEN)) == NULL)
		error("getcwd failed: %m");
	else if (setenvf(NULL, "SLURM_SUBMIT_DIR", "%s", buf) < 0)
		error("unable to set SLURM_SUBMIT_DIR in environment");

	if ((gethostname(host, sizeof(host))))
		error("gethostname_short failed: %m");
	else if (setenvf(NULL, "SLURM_SUBMIT_HOST", "%s", host) < 0)
		error("unable to set SLURM_SUBMIT_HOST in environment");
}

/* Set SLURM_UMASK environment variable with current state */
static int _set_umask_env(void)
{
	char mask_char[5];
	mode_t mask;

	if (getenv("SLURM_UMASK"))	/* use this value */
		return SLURM_SUCCESS;

	if (sbopt.umask >= 0) {
		mask = sbopt.umask;
	} else {
		mask = (int)umask(0);
		umask(mask);
	}

	sprintf(mask_char, "0%d%d%d",
		((mask>>6)&07), ((mask>>3)&07), mask&07);
	if (setenvf(NULL, "SLURM_UMASK", "%s", mask_char) < 0) {
		error ("unable to set SLURM_UMASK in environment");
		return SLURM_ERROR;
	}
	debug ("propagating UMASK=%s", mask_char);
	return SLURM_SUCCESS;
}

/*
 * _set_prio_process_env
 *
 * Set the internal SLURM_PRIO_PROCESS environment variable to support
 * the propagation of the users nice value and the "PropagatePrioProcess"
 * config keyword.
 */
static void  _set_prio_process_env(void)
{
	int retval;

	errno = 0; /* needed to detect a real failure since prio can be -1 */

	if ((retval = getpriority (PRIO_PROCESS, 0)) == -1)  {
		if (errno) {
			error ("getpriority(PRIO_PROCESS): %m");
			return;
		}
	}

	if (setenvf (NULL, "SLURM_PRIO_PROCESS", "%d", retval) < 0) {
		error ("unable to set SLURM_PRIO_PROCESS in environment");
		return;
	}

	debug ("propagating SLURM_PRIO_PROCESS=%d", retval);
}

/*
 * Checks if the buffer starts with a shebang (#!).
 */
static bool has_shebang(const void *buf, int size)
{
	char *str = (char *)buf;

	if (size < 2)
		return false;

	if (str[0] != '#' || str[1] != '!')
		return false;

	return true;
}

/*
 * Checks if the buffer contains a NULL character (\0).
 */
static bool contains_null_char(const void *buf, int size)
{
	char *str = (char *)buf;
	int i;

	for (i = 0; i < size; i++) {
		if (str[i] == '\0')
			return true;
	}

	return false;
}

/*
 * Checks if the buffer contains any DOS linebreak (\r\n).
 */
static bool contains_dos_linebreak(const void *buf, int size)
{
	char *str = (char *)buf;
	char prev_char = '\0';
	int i;

	for (i = 0; i < size; i++) {
		if (prev_char == '\r' && str[i] == '\n')
			return true;
		prev_char = str[i];
	}

	return false;
}

/*
 * If "filename" is NULL, the batch script is read from standard input.
 */
static void *_get_script_buffer(const char *filename, int *size)
{
	int fd;
	char *buf = NULL;
	int buf_size = BUFSIZ;
	int buf_left;
	int script_size = 0;
	char *ptr = NULL;
	int tmp_size;

	/*
	 * First figure out whether we are reading from STDIN_FILENO
	 * or from a file.
	 */
	if (filename == NULL) {
		fd = STDIN_FILENO;
	} else {
		fd = open(filename, O_RDONLY);
		if (fd == -1) {
			error("Unable to open file %s", filename);
			goto fail;
		}
		/* Quick check against absurdly large file sizes */
		if (fd != STDIN_FILENO) {
			struct stat st;
			if (fstat(fd, &st) == -1)
				fatal("Cannot stat %s: %m", filename);
			if (st.st_size > MAX_BATCH_SCRIPT_SIZE)
				fatal("Script file %s is too large",
				      filename);
			buf_size = st.st_size + 1;
		}
	}

	/*
	 * Then read in the script, in chunks of BUFSIZE bytes.
	 */
	buf = ptr = xmalloc(buf_size);
	buf_left = buf_size;
	while((tmp_size = read(fd, ptr, buf_left)) > 0) {
		buf_left -= tmp_size;
		script_size += tmp_size;
		if (buf_left == 0) {
			if (buf_size >= MAX_BATCH_SCRIPT_SIZE) {
				if (filename)
					close(fd);
				xfree(buf);
				if (filename)
					fatal("Script %s is too big, read %d > %d bytes.",
					      filename, script_size,
					      MAX_BATCH_SCRIPT_SIZE);
				else
					fatal("Script from STDIN is too big, read %d > %d bytes.",
					      script_size,
					      MAX_BATCH_SCRIPT_SIZE);
			}

			if (buf_size < (MAX_BATCH_SCRIPT_SIZE - BUFSIZ))
				buf_size += BUFSIZ;
			else
				buf_size = MAX_BATCH_SCRIPT_SIZE;
			xrealloc(buf, buf_size);
		}
		ptr = buf + script_size;
		buf_left = buf_size - script_size;
	}
	if (filename)
		close(fd);

	/*
	 * Finally we perform some sanity tests on the script.
	 */
	if (script_size == 0) {
		error("Batch script is empty!");
		goto fail;
	} else if (xstring_is_whitespace(buf)) {
		error("Batch script contains only whitespace!");
		goto fail;
	} else if (!has_shebang(buf, script_size)) {
		error("This does not look like a batch script.  The first");
		error("line must start with #! followed by the path"
		      " to an interpreter.");
		error("For instance: #!/bin/sh");
		goto fail;
	} else if (contains_null_char(buf, script_size)) {
		error("The Slurm controller does not allow scripts that");
		error("contain a NULL character '\\0'.");
		goto fail;
	} else if (contains_dos_linebreak(buf, script_size)) {
		error("Batch script contains DOS line breaks (\\r\\n)");
		error("instead of expected UNIX line breaks (\\n).");
		goto fail;
	}

	*size = script_size;
	return buf;
fail:
	xfree(buf);
	*size = 0;
	return NULL;
}

/* Wrap a single command string in a simple shell script */
static char *_script_wrap(char *command_string)
{
	char *script = NULL;

	xstrcat(script, "#!/bin/sh\n");
	xstrcat(script, "# This script was created by sbatch --wrap.\n\n");
	xstrcat(script, command_string);
	xstrcat(script, "\n");

	return script;
}

/* Set SLURM_RLIMIT_* environment variables with current resource
 * limit values, reset RLIMIT_NOFILE to maximum possible value */
static int _set_rlimit_env(void)
{
	int                  rc = SLURM_SUCCESS;
	struct rlimit        rlim[1];
	unsigned long        cur;
	char                 name[64], *format;
	slurm_rlimits_info_t *rli;

	/* Load default limits to be propagated from slurm.conf */
	slurm_conf_lock();
	slurm_conf_unlock();

	/* Modify limits with any command-line options */
	if (sbopt.propagate && parse_rlimits( sbopt.propagate, PROPAGATE_RLIMITS)){
		error("--propagate=%s is not valid.", sbopt.propagate);
		exit(error_exit);
	}

	for (rli = get_slurm_rlimits_info(); rli->name != NULL; rli++ ) {

		if (rli->propagate_flag != PROPAGATE_RLIMITS)
			continue;

		if (getrlimit (rli->resource, rlim) < 0) {
			error ("getrlimit (RLIMIT_%s): %m", rli->name);
			rc = SLURM_ERROR;
			continue;
		}

		cur = (unsigned long) rlim->rlim_cur;
		snprintf(name, sizeof(name), "SLURM_RLIMIT_%s", rli->name);
		if (sbopt.propagate && rli->propagate_flag == PROPAGATE_RLIMITS)
			/*
			 * Prepend 'U' to indicate user requested propagate
			 */
			format = "U%lu";
		else
			format = "%lu";

		if (setenvf (NULL, name, format, cur) < 0) {
			error ("unable to set %s in environment", name);
			rc = SLURM_ERROR;
			continue;
		}

		debug ("propagating RLIMIT_%s=%lu", rli->name, cur);
	}

	return rc;
}
