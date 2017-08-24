/*****************************************************************************\
 *  sbatch.c - Submit a SLURM batch script.$
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/plugstack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/sbatch/opt.h"

#define MAX_RETRIES 15

static void  _add_bb_to_script(char **script_body, char *burst_buffer_file);
static void  _env_merge_filter(job_desc_msg_t *desc);
static int   _fill_job_desc_from_opts(job_desc_msg_t *desc);
static int   _check_cluster_specific_settings(job_desc_msg_t *desc);
static void *_get_script_buffer(const char *filename, int *size);
static char *_script_wrap(char *command_string);
static void  _set_exit_code(void);
static void  _set_prio_process_env(void);
static int   _set_rlimit_env(void);
static void  _set_spank_env(void);
static void  _set_submit_dir_env(void);
static int   _set_umask_env(void);
static int   _job_wait(uint32_t job_id);

int main(int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	job_desc_msg_t desc;
	submit_response_msg_t *resp;
	char *script_name;
	char *script_body;
	int script_size = 0;
	int rc = 0, retries = 0;

	slurm_conf_init(NULL);
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
	/* reinit log with new verbosity (if changed by command line) */
	if (opt.verbose || opt.quiet) {
		logopt.stderr_level += opt.verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	if (opt.wrap != NULL) {
		script_body = _script_wrap(opt.wrap);
	} else {
		script_body = _get_script_buffer(script_name, &script_size);
	}
	if (script_body == NULL)
		exit(error_exit);

	if (process_options_second_pass(
				(argc - opt.script_argc),
				argv,
				script_name ? xbasename (script_name) : "stdin",
				script_body, script_size) < 0) {
		error("sbatch parameter parsing");
		exit(error_exit);
	}

	if (opt.burst_buffer_file)
		_add_bb_to_script(&script_body, opt.burst_buffer_file);

	if (spank_init_post_opt() < 0) {
		error("Plugin stack post-option processing failed");
		exit(error_exit);
	}

	if (opt.get_user_env_time < 0) {
		/* Moab does not propage the user's resource limits, so
		 * slurmd determines the values at the same time that it
		 * gets the user's default environment variables. */
		(void) _set_rlimit_env();
	}

	/*
	 * if the environment is coming from a file, the
	 * environment at execution startup, must be unset.
	 */
	if (opt.export_file != NULL)
		env_unset_environment();

	_set_prio_process_env();
	_set_spank_env();
	_set_submit_dir_env();
	_set_umask_env();
	slurm_init_job_desc_msg(&desc);
	if (_fill_job_desc_from_opts(&desc) == -1) {
		exit(error_exit);
	}

	desc.script = (char *)script_body;

	/* If can run on multiple clusters find the earliest run time
	 * and run it there */
	desc.clusters = xstrdup(opt.clusters);
	if (opt.clusters &&
	    slurmdb_get_first_avail_cluster(&desc, opt.clusters,
			&working_cluster_rec) != SLURM_SUCCESS) {
		print_db_notok(opt.clusters, 0);
		exit(error_exit);
	}

	if (_check_cluster_specific_settings(&desc) != SLURM_SUCCESS)
		exit(error_exit);

	if (opt.test_only) {
		if (slurm_job_will_run(&desc) != SLURM_SUCCESS) {
			slurm_perror("allocation failure");
			exit (1);
		}
		exit (0);
	}

	while (slurm_submit_batch_job(&desc, &resp) < 0) {
		static char *msg;

		if (errno == ESLURM_ERROR_ON_DESC_TO_RECORD_COPY)
			msg = "Slurm job queue full, sleeping and retrying.";
		else if (errno == ESLURM_NODES_BUSY) {
			msg = "Job step creation temporarily disabled, "
			      "retrying";
		} else if (errno == EAGAIN) {
			msg = "Slurm temporarily unable to accept job, "
			      "sleeping and retrying.";
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
		sleep (++retries);
        }

	if (!opt.parsable){
		printf("Submitted batch job %u", resp->job_id);
		if (working_cluster_rec)
			printf(" on cluster %s", working_cluster_rec->name);
		printf("\n");
	} else {
		printf("%u", resp->job_id);
		if (working_cluster_rec)
			printf(";%s", working_cluster_rec->name);
		printf("\n");
	}
	if (opt.wait)
		rc = _job_wait(resp->job_id);

	xfree(desc.clusters);
	xfree(desc.name);
	xfree(desc.script);
	env_array_free(desc.environment);
	slurm_free_submit_response_response_msg(resp);
	return rc;
}

/* Insert the contents of "burst_buffer_file" into "script_body" */
static void  _add_bb_to_script(char **script_body, char *burst_buffer_file)
{
	char *orig_script = *script_body;
	char *new_script, *sep, save_char;
	int i;

	if (!burst_buffer_file || (burst_buffer_file[0] == '\0'))
		return;	/* No burst buffer file or empty file */

	if (!orig_script) {
		*script_body = xstrdup(burst_buffer_file);
		return;
	}

	i = strlen(burst_buffer_file) - 1;
	if (burst_buffer_file[i] != '\n')	/* Append new line as needed */
		xstrcat(burst_buffer_file, "\n");

	if (orig_script[0] != '#') {
		/* Prepend burst buffer file */
		new_script = xstrdup(burst_buffer_file);
		xstrcat(new_script, orig_script);
		*script_body = new_script;
		return;
	}

	sep = strchr(orig_script, '\n');
	if (sep) {
		save_char = sep[1];
		sep[1] = '\0';
		new_script = xstrdup(orig_script);
		xstrcat(new_script, burst_buffer_file);
		sep[1] = save_char;
		xstrcat(new_script, sep + 1);
		*script_body = new_script;
		return;
	} else {
		new_script = xstrdup(orig_script);
		xstrcat(new_script, "\n");
		xstrcat(new_script, burst_buffer_file);
		*script_body = new_script;
		return;
	}
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
		sleep_time = MIN(sleep_time + 2, 10);

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
			error("Currently unable to load job state "
			      "information, retrying: %m");
		}
	}

	return ec;
}

static char *_find_quote_token(char *tmp, char *sep, char **last)
{
	char *start, *quote_single = 0, *quote_double = 0;
	int i;

	xassert(last);
	if (*last)
		start = *last;
	else
		start = tmp;
	if (start[0] == '\0')
		return NULL;
	for (i = 0; ; i++) {
		if (start[i] == '\'') {
			if (quote_single)
				quote_single--;
			else
				quote_single++;
		} else if (start[i] == '\"') {
			if (quote_double)
				quote_double--;
			else
				quote_double++;
		} else if (((start[i] == sep[0]) || (start[i] == '\0')) &&
			   (quote_single == 0) && (quote_double == 0)) {
			if (((start[0] == '\'') && (start[i-1] == '\'')) ||
			    ((start[0] == '\"') && (start[i-1] == '\"'))) {
				start++;
				i -= 2;
			}
			if (start[i] == '\0')
				*last = &start[i];
			else
				*last = &start[i] + 1;
			start[i] = '\0';
			return start;
		} else if (start[i] == '\0') {
			error("Improperly formed environment variable (%s)",
			      start);
			*last = &start[i];
			return start;
		}

	}
}

/* Propagate select user environment variables to the job.
 * If ALL is among the specified variables propaagte
 * the entire user environment as well.
 */
static void _env_merge_filter(job_desc_msg_t *desc)
{
	extern char **environ;
	int i, len;
	char *save_env[2] = { NULL, NULL }, *tmp, *tok, *last = NULL;

	tmp = xstrdup(opt.export_env);
	tok = _find_quote_token(tmp, ",", &last);
	while (tok) {

		if (xstrcasecmp(tok, "ALL") == 0) {
			env_array_merge(&desc->environment,
					(const char **)environ);
			tok = _find_quote_token(NULL, ",", &last);
			continue;
		}

		if (strchr(tok, '=')) {
			save_env[0] = tok;
			env_array_merge(&desc->environment,
					(const char **)save_env);
		} else {
			len = strlen(tok);
			for (i = 0; environ[i]; i++) {
				if (xstrncmp(tok, environ[i], len) ||
				    (environ[i][len] != '='))
					continue;
				save_env[0] = environ[i];
				env_array_merge(&desc->environment,
						(const char **)save_env);
				break;
			}
		}
		tok = _find_quote_token(NULL, ",", &last);
	}
	xfree(tmp);

	for (i = 0; environ[i]; i++) {
		if (xstrncmp("SLURM_", environ[i], 6))
			continue;
		save_env[0] = environ[i];
		env_array_merge(&desc->environment,
				(const char **)save_env);
	}
}

/* Returns SLURM_ERROR if settings are invalid for chosen cluster */
static int _check_cluster_specific_settings(job_desc_msg_t *req)
{
	int rc = SLURM_SUCCESS;

	if (is_alps_cray_system()) {
		/*
		 * Fix options and inform user, but do not abort submission.
		 */
		if (req->shared && (req->shared != (uint16_t)NO_VAL)) {
			info("--share is not supported on Cray/ALPS systems.");
			req->shared = (uint16_t)NO_VAL;
		}
		if (req->overcommit && (req->overcommit != (uint8_t)NO_VAL)) {
			info("--overcommit is not supported on Cray/ALPS "
			     "systems.");
			req->overcommit = false;
		}
		if (req->wait_all_nodes &&
		    (req->wait_all_nodes != (uint16_t)NO_VAL)) {
			info("--wait-all-nodes is handled automatically on "
			     "Cray/ALPS systems.");
			req->wait_all_nodes = (uint16_t)NO_VAL;
		}
	}
	return rc;
}

/* Returns 0 on success, -1 on failure */
static int _fill_job_desc_from_opts(job_desc_msg_t *desc)
{
	int i;
	extern char **environ;

	if (opt.jobid_set)
		desc->job_id = opt.jobid;
	desc->contiguous = opt.contiguous ? 1 : 0;
	if (opt.core_spec != (uint16_t) NO_VAL)
		desc->core_spec = opt.core_spec;
	desc->features = opt.constraints;
	desc->immediate = opt.immediate;
	desc->gres = opt.gres;
	if (opt.job_name != NULL)
		desc->name = xstrdup(opt.job_name);
	else
		desc->name = xstrdup("sbatch");
	desc->reservation  = xstrdup(opt.reservation);
	desc->wckey  = xstrdup(opt.wckey);

	desc->req_nodes = opt.nodelist;
	desc->exc_nodes = opt.exc_nodes;
	desc->partition = opt.partition;
	desc->profile = opt.profile;
	if (opt.licenses)
		desc->licenses = xstrdup(opt.licenses);
	if (opt.nodes_set) {
		desc->min_nodes = opt.min_nodes;
		if (opt.max_nodes)
			desc->max_nodes = opt.max_nodes;
	} else if (opt.ntasks_set && (opt.ntasks == 0))
		desc->min_nodes = 0;
	if (opt.ntasks_per_node)
		desc->ntasks_per_node = opt.ntasks_per_node;
	desc->user_id = opt.uid;
	desc->group_id = opt.gid;
	if (opt.dependency)
		desc->dependency = xstrdup(opt.dependency);

	if (opt.array_inx)
		desc->array_inx = xstrdup(opt.array_inx);
	if (opt.mem_bind)
		desc->mem_bind       = opt.mem_bind;
	if (opt.mem_bind_type)
		desc->mem_bind_type  = opt.mem_bind_type;
	if (opt.plane_size != NO_VAL)
		desc->plane_size     = opt.plane_size;
	desc->task_dist  = opt.distribution;

	desc->network = opt.network;
	if (opt.nice != NO_VAL)
		desc->nice = NICE_OFFSET + opt.nice;
	if (opt.priority)
		desc->priority = opt.priority;

	desc->mail_type = opt.mail_type;
	if (opt.mail_user)
		desc->mail_user = xstrdup(opt.mail_user);
	if (opt.begin)
		desc->begin_time = opt.begin;
	if (opt.deadline)
		desc->deadline = opt.deadline;
	if (opt.delay_boot != NO_VAL)
		desc->delay_boot = opt.delay_boot;
	if (opt.account)
		desc->account = xstrdup(opt.account);
	if (opt.comment)
		desc->comment = xstrdup(opt.comment);
	if (opt.qos)
		desc->qos = xstrdup(opt.qos);

	if (opt.hold)
		desc->priority     = 0;

	if (opt.geometry[0] != (uint16_t) NO_VAL) {
		int dims = slurmdb_setup_cluster_dims();

		for (i=0; i<dims; i++)
			desc->geometry[i] = opt.geometry[i];
	}

	memcpy(desc->conn_type, opt.conn_type, sizeof(desc->conn_type));

	if (opt.reboot)
		desc->reboot = 1;
	if (opt.no_rotate)
		desc->rotate = 0;
	if (opt.blrtsimage)
		desc->blrtsimage = xstrdup(opt.blrtsimage);
	if (opt.linuximage)
		desc->linuximage = xstrdup(opt.linuximage);
	if (opt.mloaderimage)
		desc->mloaderimage = xstrdup(opt.mloaderimage);
	if (opt.ramdiskimage)
		desc->ramdiskimage = xstrdup(opt.ramdiskimage);

	/* job constraints */
	if (opt.mincpus > -1)
		desc->pn_min_cpus = opt.mincpus;
	if (opt.realmem > -1)
		desc->pn_min_memory = opt.realmem;
	else if (opt.mem_per_cpu > -1)
		desc->pn_min_memory = opt.mem_per_cpu | MEM_PER_CPU;
	if (opt.tmpdisk > -1)
		desc->pn_min_tmp_disk = opt.tmpdisk;
	if (opt.overcommit) {
		desc->min_cpus = MAX(opt.min_nodes, 1);
		desc->overcommit = opt.overcommit;
	} else if (opt.cpus_set)
		desc->min_cpus = opt.ntasks * opt.cpus_per_task;
	else if (opt.nodes_set && (opt.min_nodes == 0))
		desc->min_cpus = 0;
	else
		desc->min_cpus = opt.ntasks;

	if (opt.ntasks_set)
		desc->num_tasks = opt.ntasks;
	if (opt.cpus_set)
		desc->cpus_per_task = opt.cpus_per_task;
	if (opt.ntasks_per_socket > -1)
		desc->ntasks_per_socket = opt.ntasks_per_socket;
	if (opt.ntasks_per_core > -1)
		desc->ntasks_per_core = opt.ntasks_per_core;

	/* node constraints */
	if (opt.sockets_per_node != NO_VAL)
		desc->sockets_per_node = opt.sockets_per_node;
	if (opt.cores_per_socket != NO_VAL)
		desc->cores_per_socket = opt.cores_per_socket;
	if (opt.threads_per_core != NO_VAL)
		desc->threads_per_core = opt.threads_per_core;

	if (opt.no_kill)
		desc->kill_on_node_fail = 0;
	if (opt.time_limit != NO_VAL)
		desc->time_limit = opt.time_limit;
	if (opt.time_min  != NO_VAL)
		desc->time_min = opt.time_min;
	if (opt.shared != (uint16_t) NO_VAL)
		desc->shared = opt.shared;

	desc->wait_all_nodes = opt.wait_all_nodes;
	if (opt.warn_flags)
		desc->warn_flags = opt.warn_flags;
	if (opt.warn_signal)
		desc->warn_signal = opt.warn_signal;
	if (opt.warn_time)
		desc->warn_time = opt.warn_time;

	desc->environment = NULL;
	if (opt.export_file) {
		desc->environment = env_array_from_file(opt.export_file);
		if (desc->environment == NULL)
			exit(1);
	}
	if (opt.export_env == NULL) {
		env_array_merge(&desc->environment, (const char **)environ);
	} else if (!xstrcasecmp(opt.export_env, "ALL")) {
		env_array_merge(&desc->environment, (const char **)environ);
	} else if (!xstrcasecmp(opt.export_env, "NONE")) {
		desc->environment = env_array_create();
		env_array_merge_slurm(&desc->environment,
				      (const char **)environ);
		opt.get_user_env_time = 0;
	} else {
		_env_merge_filter(desc);
		opt.get_user_env_time = 0;
	}
	if (opt.get_user_env_time >= 0) {
		env_array_overwrite(&desc->environment,
				    "SLURM_GET_USER_ENV", "1");
	}

	if ((opt.distribution & SLURM_DIST_STATE_BASE) == SLURM_DIST_ARBITRARY) {
		env_array_overwrite_fmt(&desc->environment,
					"SLURM_ARBITRARY_NODELIST",
					"%s", desc->req_nodes);
	}

	desc->env_size = envcount(desc->environment);
	desc->argv = opt.script_argv;
	desc->argc = opt.script_argc;
	desc->std_err  = opt.efname;
	desc->std_in   = opt.ifname;
	desc->std_out  = opt.ofname;
	desc->work_dir = opt.cwd;
	if (opt.requeue != NO_VAL)
		desc->requeue = opt.requeue;
	if (opt.open_mode)
		desc->open_mode = opt.open_mode;
	if (opt.acctg_freq)
		desc->acctg_freq = xstrdup(opt.acctg_freq);

	desc->ckpt_dir = opt.ckpt_dir;
	desc->ckpt_interval = (uint16_t)opt.ckpt_interval;

	if (opt.spank_job_env_size) {
		desc->spank_job_env      = opt.spank_job_env;
		desc->spank_job_env_size = opt.spank_job_env_size;
	}

	desc->cpu_freq_min = opt.cpu_freq_min;
	desc->cpu_freq_max = opt.cpu_freq_max;
	desc->cpu_freq_gov = opt.cpu_freq_gov;

	if (opt.req_switch >= 0)
		desc->req_switch = opt.req_switch;
	if (opt.wait4switch >= 0)
		desc->wait4switch = opt.wait4switch;

	if (opt.power_flags)
		desc->power_flags = opt.power_flags;
	if (opt.job_flags)
		desc->bitflags = opt.job_flags;
	if (opt.mcs_label)
		desc->mcs_label = xstrdup(opt.mcs_label);

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

	for (i=0; i<opt.spank_job_env_size; i++) {
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

	if (opt.umask >= 0) {
		mask = opt.umask;
	} else {
		mask = (int)umask(0);
		umask(mask);
	}

	sprintf(mask_char, "0%d%d%d",
		((mask>>6)&07), ((mask>>3)&07), mask&07);
	if (setenvf(NULL, "SLURM_UMASK", "%s", mask_char) < 0) {
		error ("unable to set SLURM_UMASK in environment");
		return SLURM_FAILURE;
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
	}

	/*
	 * Then read in the script.
	 */
	buf = ptr = xmalloc(buf_size);
	buf_left = buf_size;
	while((tmp_size = read(fd, ptr, buf_left)) > 0) {
		buf_left -= tmp_size;
		script_size += tmp_size;
		if (buf_left == 0) {
			buf_size += BUFSIZ;
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
		error("The SLURM controller does not allow scripts that");
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
	if (opt.propagate && parse_rlimits( opt.propagate, PROPAGATE_RLIMITS)){
		error("--propagate=%s is not valid.", opt.propagate);
		exit(error_exit);
	}

	for (rli = get_slurm_rlimits_info(); rli->name != NULL; rli++ ) {

		if (rli->propagate_flag != PROPAGATE_RLIMITS)
			continue;

		if (getrlimit (rli->resource, rlim) < 0) {
			error ("getrlimit (RLIMIT_%s): %m", rli->name);
			rc = SLURM_FAILURE;
			continue;
		}

		cur = (unsigned long) rlim->rlim_cur;
		snprintf(name, sizeof(name), "SLURM_RLIMIT_%s", rli->name);
		if (opt.propagate && rli->propagate_flag == PROPAGATE_RLIMITS)
			/*
			 * Prepend 'U' to indicate user requested propagate
			 */
			format = "U%lu";
		else
			format = "%lu";

		if (setenvf (NULL, name, format, cur) < 0) {
			error ("unable to set %s in environment", name);
			rc = SLURM_FAILURE;
			continue;
		}

		debug ("propagating RLIMIT_%s=%lu", rli->name, cur);
	}

	/*
	 *  Now increase NOFILE to the max available for this srun
	 */
	if (getrlimit (RLIMIT_NOFILE, rlim) < 0)
	 	return (error ("getrlimit (RLIMIT_NOFILE): %m"));

	if (rlim->rlim_cur < rlim->rlim_max) {
		rlim->rlim_cur = rlim->rlim_max;
		if (setrlimit (RLIMIT_NOFILE, rlim) < 0)
			return (error("Unable to increase max no. files: %m"));
	}

	return rc;
}
